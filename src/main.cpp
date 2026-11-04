#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <AsyncMqttClient.h>
#include <driver/uart.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <freertos/semphr.h>
#include <atomic>
#include <utility>
#include "config.h"
#include "public_config.h"
#include "outputs.h"
#include "store.h"
#include "signal_message.h"

using namespace hub;
DynamicJsonDocument config(JsonCapacity);
// Arduino-ESP32 2.0.17 WebServer::arg() returns a full String copy. Keep the
// request-owned body alive through parsing instead of duplicating up to 24 KB.
class HubWebServer:public WebServer {
public:
  using WebServer::WebServer;
  const String& plainBody()const{
    for(int i=0;i<_postArgsLen;++i)if(_postArgs[i].key=="plain")return _postArgs[i].value;
    for(int i=0;i<_currentArgCount;++i)if(_currentArgs[i].key=="plain")return _currentArgs[i].value;
    static const String empty;return empty;
  }
};
Outputs outputs;Store store;HubWebServer web(80);AsyncMqttClient mqtt;
SemaphoreHandle_t outputMutex=nullptr;
struct OutputGuard {
  OutputGuard(){xSemaphoreTakeRecursive(outputMutex,portMAX_DELAY);}
  ~OutputGuard(){xSemaphoreGiveRecursive(outputMutex);}
};
bool movement=false,operational=false,apActive=false,wasOnline=false,connecting=false,mdnsStarted=false;
bool fsReady=false;
bool networkRestartPending=false;uint32_t networkRestartAt=0;
bool mqttConfigured=false;uint32_t mqttAttempt=0,wifiAttempt=0;std::string stateTopic,modeTopic,apName,hwError;
void logLine(std::string s);
String sessionToken;
// Stable hardware identity is independent of MQTT Client ID and output labels.
std::string nodeId,nodeInfoTopic,nodeStatusTopic,nodeDisplayName;
bool announceNode=false;uint32_t nodeAnnounceAt=0;
void publishNodeInfo() {
  if(!announceNode||!mqtt.connected()||WiFi.status()!=WL_CONNECTED||int32_t(millis()-nodeAnnounceAt)<0)return;
  StaticJsonDocument<1024> info;
  info["client_id"]=config["mqtt"]["clientId"].as<const char*>();
  info["name"]=nodeDisplayName;info["type"]="esp32c3";
  info["model"]="SuperMini / signals and servos";info["firmware"]="nchub-2.2.0";
  const String ip=WiFi.localIP().toString();info["ip"]=ip;info["web_url"]="http://"+ip+"/";
  std::string payload;serializeJson(info,payload);
  const auto metadata=mqtt.publish(nodeInfoTopic.c_str(),1,true,payload.c_str(),payload.size());
  const auto status=mqtt.publish(nodeStatusTopic.c_str(),1,true,"{\"online\":true}");
  announceNode=!(metadata&&status);nodeAnnounceAt=millis()+5000;
  if(!announceNode)logLine("Published node metadata: "+nodeId);
}
struct Event {uint8_t kind=0;uint16_t id=0;uint8_t qos=0;char topic[MaxSignalTopic+1]={};char payload[MaxSignalPayload+1]={};bool retained=false;uint16_t length=0;};
std::set<uint16_t> pendingSubscriptions;bool subscriptionFailed=false;
QueueHandle_t eventQueue;std::atomic<uint32_t> dropped{0};SyncSession syncSession;
struct Cached {Message message;uint32_t received;std::string matched;};
std::vector<Cached> cache;std::vector<std::string> logs;
void logLine(std::string s){if(logs.size()>=24)logs.erase(logs.begin());logs.push_back(std::move(s));}
bool allowed(){OutputGuard guard;return movement&&mqttConfigured&&mqtt.connected()&&WiFi.status()==WL_CONNECTED&&(modeTopic.empty()||operational)&&hwError.empty()&&outputs.error.empty();}
std::string blocked() {
  OutputGuard guard;
  if(!hwError.empty())return hwError;
  if(!outputs.error.empty())return outputs.error;
  if(!mqttConfigured)return "nControl is disconnected in configuration";
  if(!movement)return "MQTT servo movement is disabled";
  if(WiFi.status()!=WL_CONNECTED)return "Wi-Fi offline";
  if(!mqtt.connected())return "MQTT offline";
  if(!modeTopic.empty()&&!operational)return "Custom mode topic: OPERATIONAL not received";
  return "";
}
std::string identityName() {
  uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);char name[40];
  snprintf(name,sizeof(name),"io-hub-control-%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);return name;
}
std::string hostName() {
  uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);char name[32];
  snprintf(name,sizeof(name),"io-hub-control-%02x%02x%02x",mac[3],mac[4],mac[5]);return name;
}
void migrateShortHostname(bool loaded) {
  if(!loaded)return;
  Preferences prefs;if(!prefs.begin("hub-identity",false))return;
  if(!prefs.getBool("dns_short_v1",false)){
    if(std::string(config["device"].as<const char*>())==identityName()){
      config["device"]=hostName();
      if(!store.save(config.as<JsonVariantConst>())){config["device"]=identityName();logLine("Could not save the shortened hostname");prefs.end();return;}
    }
    prefs.putBool("dns_short_v1",true);
  }
  prefs.end();
}
void migrateIdentity(bool loaded) {
  if(!loaded)return;
  Preferences prefs;if(!prefs.begin("hub-identity",false)){logLine("Cannot check identity migration");return;}
  if(!prefs.getBool("names_v1",false)){
    config["device"]=hostName();config["mqtt"]["clientId"]=identityName();
    if(store.save(config.as<JsonVariantConst>())){prefs.putBool("names_v1",true);logLine("Device and MQTT identity: "+identityName());}
    else logLine("Could not save new names; migration will retry after restart");
  }
  prefs.end();
}
void defaults() {
  config.clear();config["version"]=2;
  config["device"]=hostName();config["uart0"]=false;config["externalJtag"]=false;config["allowStrapping"]=false;config["modeTopic"]="";
  config["wifi"]["ssid"]="";config["wifi"]["password"]="";
  auto m=config.createNestedObject("mqtt");m["host"]="";m["port"]=1883;m["clientId"]=identityName();m["user"]="";m["password"]="";m["stateTopic"]="nControl/websocket/rx";
  for(auto k:{"templates","ports"})config.createNestedArray(k);
}
void startAP() {
  if(apActive)return;
  uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);char ssid[32];snprintf(ssid,sizeof(ssid),"IO-Hub-%02x%02x%02x-setup",mac[3],mac[4],mac[5]);apName=ssid;
  WiFi.mode(WIFI_AP_STA);apActive=WiFi.softAP(apName.c_str(),"ncontrol-setup");
  logLine("Setup AP: "+apName+" / 192.168.4.1");
}
void setupMqtt() {
  uint8_t staMac[6];esp_read_mac(staMac,ESP_MAC_WIFI_STA);char display[64];snprintf(display,sizeof(display),"I/O Hub Control - %02x%02x%02x",staMac[3],staMac[4],staMac[5]);nodeDisplayName=display;
  if(nodeId.empty()) {
    const uint64_t mac=ESP.getEfuseMac();char id[32];
    snprintf(id,sizeof(id),"nchub-%012llx",(unsigned long long)(mac&0xffffffffffffULL));nodeId=id;
    nodeInfoTopic="nControl/nodes/"+nodeId+"/info";nodeStatusTopic="nControl/nodes/"+nodeId+"/status";
  }
  mqtt.setWill(nodeStatusTopic.c_str(),1,true,"{\"online\":false}");
  announceNode=false;
  stateTopic=config["mqtt"]["stateTopic"].as<const char*>();modeTopic=config["modeTopic"].as<const char*>();operational=false;syncSession.disconnected();
  mqtt.setServer(config["mqtt"]["host"].as<const char*>(),config["mqtt"]["port"].as<uint16_t>());
  mqtt.setClientId(config["mqtt"]["clientId"].as<const char*>());mqtt.setCleanSession(true);mqtt.setKeepAlive(15);
  // Library keeps pointers: these strings belong to config and live until disconnect.
  auto m=config["mqtt"];mqtt.setCredentials(strlen(m["user"].as<const char*>())?m["user"].as<const char*>():nullptr,strlen(m["user"].as<const char*>())?m["password"].as<const char*>():nullptr);
  pendingSubscriptions.clear();subscriptionFailed=false;
  mqttConfigured=config["mqtt"]["enabled"]|true;connecting=false;mqttAttempt=millis()-5000;
}
JsonVariantConst matching(const Message& msg) {
  for(auto b:config["bindings"].as<JsonArrayConst>())if(b["enabled"].as<bool>()&&msg.type==b["type"].as<const char*>()[0]&&msg.source==b["source"].as<const char*>())
    for(auto s:b["states"].as<JsonArrayConst>())if(msg.state==s["value"].as<const char*>())return s;
  return JsonVariantConst();
}
void applyPorts(const Message& msg,bool servosOnly=false,bool manual=false) {
  for(auto p:config["ports"].as<JsonArrayConst>()){
    auto state=portState(config.as<JsonVariantConst>(),p,msg);if(state.isNull())continue;
    if(!manual&&nativeSignal(p,findTemplate(config.as<JsonVariantConst>(),p["template"].as<const char*>())))continue;
    std::string id=p["id"];if(!servosOnly||outputs.servo(id))outputs.portAction(id,state,allowed());
  }
}
std::string matchedName(const Message& msg) {
  if(config["version"].as<int>()==2){std::string result;for(auto p:config["ports"].as<JsonArrayConst>())if(!portState(config.as<JsonVariantConst>(),p,msg).isNull()){if(!result.empty())result+=", ";result+=p["name"].as<const char*>();}return result.empty()?"No matching port":result;}

  for(auto b:config["bindings"].as<JsonArrayConst>())if(b["enabled"].as<bool>()&&msg.type==b["type"].as<const char*>()[0]&&msg.source==b["source"].as<const char*>())
    for(auto s:b["states"].as<JsonArrayConst>())if(msg.state==s["value"].as<const char*>())return b["name"].as<const char*>();
  return "No matching rule";
}
void receive(const Message& msg,bool execute=true) {
  Cached* found=nullptr;
  for(auto& c:cache)if(c.message.type==msg.type&&c.message.source==msg.source){found=&c;break;}
  bool repeat=found&&sameState(found->message,msg);
  if(!found){if(cache.size()>=64)cache.erase(cache.begin());cache.push_back({});found=&cache.back();}
  found->message=msg;found->received=millis();found->matched=matchedName(msg);
  if(repeat){logLine("Duplicate: "+std::string(1,msg.type)+"|"+msg.source+"|"+msg.state);return;}
  if(!execute)return;
  if(config["version"].as<int>()==2){applyPorts(msg);return;}
  auto rule=matching(msg);if(rule.isNull())return;
  for(auto a:rule["actions"].as<JsonArrayConst>())outputs.action(a,allowed());
}
void applyCached() {OutputGuard guard;for(auto& c:cache){if(config["version"].as<int>()==2){applyPorts(c.message,true);continue;}auto rule=matching(c.message);if(!rule.isNull())for(auto a:rule["actions"].as<JsonArrayConst>())if(std::string(a["kind"].as<const char*>())=="servo")outputs.action(a,allowed());}}
void queueEvent(Event e){if(xQueueSend(eventQueue,&e,0)!=pdTRUE)++dropped;}
void mqttCallbacks() {
  mqtt.onConnect([](bool){Event e;e.kind=1;queueEvent(e);});
  mqtt.onDisconnect([](AsyncMqttClientDisconnectReason){Event e;e.kind=2;queueEvent(e);});
  mqtt.onSubscribe([](uint16_t id,uint8_t qos){Event e;e.kind=3;e.id=id;e.qos=qos;queueEvent(e);});
  mqtt.onMessage([](char* topic,char* payload,AsyncMqttClientMessageProperties properties,size_t len,size_t index,size_t total){
    // Fragment aggregation is confined to the AsyncTCP task; main loop only sees complete copies.
    static Event e;static size_t offset=0;static bool valid=false;
    if(index==0){e=Event{};e.kind=4;offset=0;valid=total>0&&total<=MaxSignalPayload&&strlen(topic)<=MaxSignalTopic;e.retained=properties.retain;if(valid)strcpy(e.topic,topic);}
    if(!valid||index!=offset||index+len>total){valid=false;return;}
    memcpy(e.payload+index,payload,len);offset+=len;
    if(offset==total){e.length=total;queueEvent(e);valid=false;}
  });
}
void processEvents() {
  OutputGuard guard;
  Event e;int budget=8;
  while(budget--&&xQueueReceive(eventQueue,&e,0)==pdTRUE) {
    if(e.kind==1) {
      if(!mqttConfigured){mqtt.disconnect(true);continue;}
      connecting=false;operational=false;announceNode=true;nodeAnnounceAt=millis();
      pendingSubscriptions.clear();subscriptionFailed=false;syncSession.disconnected();
      auto subscribe=[&](const std::string& topic){uint16_t id=mqtt.subscribe(topic.c_str(),1);if(id)pendingSubscriptions.insert(id);else subscriptionFailed=true;};
      subscribe(stateTopic);if(!modeTopic.empty())subscribe(modeTopic);
      std::set<std::string> signals;
      for(auto p:config["ports"].as<JsonArrayConst>())if(p["enabled"].as<bool>()&&nativeSignal(p,findTemplate(config.as<JsonVariantConst>(),p["template"].as<const char*>())))signals.insert(signalTopic(p));
      for(const auto& topic:signals)subscribe(topic);
      if(subscriptionFailed)mqtt.disconnect(true);
      logLine("MQTT connected; subscribing to states");
    }else if(e.kind==2){connecting=false;operational=false;syncSession.disconnected();logLine("MQTT offline");}
    else if(e.kind==3&&pendingSubscriptions.erase(e.id)) {
      if(e.qos==0x80){subscriptionFailed=true;logLine("Broker rejected subscription");mqtt.disconnect(true);}
      else if(pendingSubscriptions.empty()&&!subscriptionFailed){mqtt.publish("nControl/websocket/tx",1,false,"W|ST");logLine("One-time W|ST synchronization after all SUBACKs");}
    }else if(e.kind==4&&mqttConfigured) {
      std::string topic(e.topic),body(e.payload,e.length);
      if(!modeTopic.empty()&&topic==modeTopic) {
        bool before=allowed();operational=body=="OPERATIONAL";
        if(before&&!allowed())outputs.freeze(false);
        logLine("Custom mode: "+body+" (without automatically applying cached states)");
      }else if(topic!=stateTopic&&!e.retained) {
        SignalMessage signal;if(parseSignal(e.payload,e.length,signal)){
          receive(signal.message,false);
          for(auto p:config["ports"].as<JsonArrayConst>()){
            auto t=findTemplate(config.as<JsonVariantConst>(),p["template"].as<const char*>());
            if(!p["enabled"].as<bool>()||!nativeSignal(p,t)||topic!=signalTopic(p)||signal.head!=std::string(t["head"]|"")||signal.message.source!=std::string(p["source"].as<const char*>()))continue;
            auto state=portState(config.as<JsonVariantConst>(),p,signal.message);
            if(!state.isNull()){outputs.portAction(p["id"].as<const char*>(),state,false,signal.period);logLine("Signal JSON: "+signal.message.source+" "+signal.message.state);}
          }
        }else logLine("Rejected signal JSON (schema/mode/lamps)");
      }else if(topic==stateTopic) {
        Message msg;logLine(body);
        if(parse(e.payload,e.length,msg))receive(msg);else logLine("Rejected T/S payload");
      }
    }
  }
}
void sendJson(JsonVariantConst doc,int code=200){String s;serializeJson(doc,s);web.send(code,"application/json; charset=utf-8",s);}
void answer(bool ok,const std::string& text,int code=200){StaticJsonDocument<512> d;d["ok"]=ok;d["message"]=text;sendJson(d.as<JsonVariantConst>(),code);}
bool mutation() {
  if(web.header("X-Hub-Token")!=sessionToken){answer(false,"Reload the panel: missing session token",403);return false;}
  if(web.plainBody().length()>MaxConfigBytes){answer(false,"JSON size limit exceeded",413);return false;}return true;
}
bool body(DynamicJsonDocument& d){
  if(!mutation())return false;
  const String& raw=web.plainBody();
  if(!d.capacity()) {
    const size_t estimate=jsonCapacityFor(reinterpret_cast<const uint8_t*>(raw.c_str()),raw.length(),512);
    const size_t capacity=estimate>JsonCapacity?JsonCapacity:estimate;
    d=DynamicJsonDocument(capacity);
    if(d.capacity()<capacity){answer(false,"Not enough memory to save. Previous configuration preserved.",503);return false;}
  }
  // WebServer releases the request after its handler. Read-only parsing makes
  // the document own its strings before that happens.
  auto err=deserializeJson(d,static_cast<const char*>(raw.c_str()),raw.length());
  if(err){answer(false,err==DeserializationError::NoMemory?"Configuration exceeds the available JSON buffer; previous configuration preserved.":std::string("Cannot parse JSON: ")+err.c_str(),err==DeserializationError::NoMemory?503:400);return false;}
  return true;
}
void redact(DynamicJsonDocument& d){d["wifi"]["password"]=nullptr;d["mqtt"]["password"]=nullptr;}
void sendPublicConfig(JsonObjectConst doc){
  String raw;
  if(!raw.reserve(measureJson(doc))){answer(false,"Not enough memory to read configuration. Try again.",503);return;}
  publicConfig(doc,raw);
  web.send(200,"application/json; charset=utf-8",raw);
}
void restoreSecrets(DynamicJsonDocument& d){if(d["wifi"]["password"].isNull())d["wifi"]["password"]=config["wifi"]["password"];if(d["mqtt"]["password"].isNull())d["mqtt"]["password"]=config["mqtt"]["password"];}
void status() {
  // Report mount health independently of whether the static UI can be served.
  size_t capacity=2048+JSON_ARRAY_SIZE(cache.size())+JSON_ARRAY_SIZE(logs.size());
  for(const auto& c:cache)capacity+=JSON_OBJECT_SIZE(5)+c.message.source.size()+c.message.state.size()+c.matched.size()+5;
  for(const auto& l:logs)capacity+=l.size()+1;
  {OutputGuard guard;capacity+=JSON_ARRAY_SIZE(outputs.servos.size())+outputs.servos.size()*JSON_OBJECT_SIZE(9)+outputs.error.size()+1;for(const auto& servo:outputs.servos)capacity+=servo.id.size()+1;}
  DynamicJsonDocument d(capacity);
  if(d.capacity()<capacity){answer(false,"Not enough memory to read status. Try again.",503);return;}
  d["token"]=sessionToken;d["wifi"]=WiFi.status()==WL_CONNECTED;d["mqtt"]=mqtt.connected();d["ip"]=WiFi.localIP().toString();d["ap"]=apActive;d["apName"]=apName;d["uptime"]=millis()/1000;d["heap"]=ESP.getFreeHeap();d["largestHeapBlock"]=ESP.getMaxAllocHeap();d["configPool"]=config.memoryUsage();d["movement"]=movement;d["allowed"]=allowed();d["blocked"]=blocked();{OutputGuard guard;d["mode"]=operational?"OPERATIONAL":"blocked / unknown";}d["dropped"]=dropped.load();{OutputGuard guard;d["hardwareError"]=outputs.error;}
  d["maxPorts"]=10;d["ledcChannels"]=6;d["extraServoChannels"]=4;d["rmtTxChannels"]=2;
  d["wwwReady"]=fsReady&&LittleFS.exists("/index.html");
  auto ss=d.createNestedArray("servos");{OutputGuard guard;for(auto& s:outputs.servos){auto o=ss.createNestedObject();o["id"]=s.id;o["pin"]=s.pin;o["backend"]=s.channel<6?"LEDC":"timer ISR 50 Hz";o["known"]=s.known;o["commanded"]=s.pos;o["target"]=s.target;o["pwm"]=s.active;o["moving"]=s.moving;o["calibration"]=s.calibration;}}
  auto ca=d.createNestedArray("cache");for(auto& c:cache){auto o=ca.createNestedObject();o["type"]=std::string(1,c.message.type);o["source"]=c.message.source;o["state"]=c.message.state;o["ageMs"]=millis()-c.received;o["matched"]=c.matched;}
  auto la=d.createNestedArray("logs");for(auto& l:logs)la.add(l);if(d.overflowed()){answer(false,"Status exceeds the available buffer; try again.",503);return;}sendJson(d.as<JsonVariantConst>());
}
void preview(bool physical) {
  DynamicJsonDocument req(1024);if(!body(req))return;
  std::string wire=req["payload"]|"";Message msg;
  if(!parse(wire.data(),wire.size(),msg)){answer(false,"Parser rejected the message",400);return;}
  auto rule=matching(msg);DynamicJsonDocument d(4096);d["ok"]=true;d["matched"]=matchedName(msg);d["blocked"]=blocked();d["actions"]=rule["actions"];
  if(config["version"].as<int>()==2){auto actions=d["actions"].to<JsonArray>();for(auto p:config["ports"].as<JsonArrayConst>()){auto state=portState(config.as<JsonVariantConst>(),p,msg);if(state.isNull())continue;auto a=actions.createNestedObject();a["port"]=p["id"];a["gpio"]=p["pin"];a["state"]=state;}}

  if(physical) {
    if(!req["confirm"].as<bool>()){answer(false,"Explicit confirmation is required for a physical test",400);return;}
    // Physical servo tests use the same gate; manual calibration has its own explicit control.
    if(!allowed()){answer(false,blocked(),409);return;}
    {OutputGuard guard;if(config["version"].as<int>()==2)applyPorts(msg,false,true);else if(!rule.isNull())for(auto a:rule["actions"].as<JsonArrayConst>())outputs.action(a,true);}
  }
  sendJson(d.as<JsonVariantConst>());
}
void setupWeb() {
  const char* headers[]={"X-Hub-Token"};web.collectHeaders(headers,1);
  web.on("/api/status",HTTP_GET,status);
  web.on("/api/config",HTTP_GET,[]{sendPublicConfig(config.as<JsonObjectConst>());});
  web.on("/api/config",HTTP_POST,[]{
    DynamicJsonDocument next(0);if(!body(next))return;restoreSecrets(next);
    if(next.overflowed()){answer(false,"Not enough memory to restore stored passwords; previous configuration preserved.",503);return;}
    next.shrinkToFit();
    auto err=validate(next.as<JsonVariantConst>());if(!err.empty()){answer(false,err,400);return;}
    if(next.overflowed()||measureJson(next)>MaxConfigBytes){answer(false,"Configuration is too large",413);return;}
    // Disconnect before replacing JSON-backed MQTT pointers. Callback never references config.
    OutputGuard guard; // A deliberate reconfiguration must not trigger offline safe movement.
    wasOnline=false;
    mqtt.disconnect(true);connecting=false;movement=false;operational=false;
    outputs.stopAll();
    if(!outputs.begin(next.as<JsonVariantConst>())){err=outputs.error;outputs.begin(config.as<JsonVariantConst>());answer(false,err,503);return;}
    if(!store.save(next.as<JsonVariantConst>())){outputs.stopAll();outputs.begin(config.as<JsonVariantConst>());answer(false,"NVS save failed; previous configuration preserved",507);return;}
    outputs.stopAll();config=std::move(next);config.shrinkToFit();
    hwError=outputs.begin(config.as<JsonVariantConst>())?"":outputs.error;
    cache.clear();xQueueReset(eventQueue);setupMqtt();
    if(!config["uart0"].as<bool>()&&uart_is_driver_installed(UART_NUM_0))uart_driver_delete(UART_NUM_0);
    if(mdnsStarted){MDNS.end();mdnsStarted=false;}
    answer(true,"Saved. MQTT movement disabled; saving does not command a servo position.");
    // Allow the HTTP response to reach the browser before reconnecting Wi-Fi.
    networkRestartPending=true;networkRestartAt=millis()+500;
  });
  web.on("/api/preview",HTTP_POST,[]{preview(false);});web.on("/api/physical",HTTP_POST,[]{preview(true);});
  web.on("/api/movement",HTTP_POST,[]{DynamicJsonDocument d(1024);if(!body(d))return;OutputGuard guard;movement=d["enabled"].as<bool>();if(!movement){OutputGuard guard;outputs.freeze(false);}if(d["applyCached"].as<bool>()&&allowed())applyCached();answer(true,blocked().empty()?"Movement enabled":blocked());});
  web.on("/api/calibration",HTTP_POST,[]{
    DynamicJsonDocument d(1024);if(!body(d))return;OutputGuard guard;std::string id=d["id"]|"";auto s=outputs.servo(id);if(!s){answer(false,"Servo not found",404);return;}
    bool enter=d["active"].as<bool>();
    if(enter&&!d["angle"].isNull()) {
      if(!d["confirm"].as<bool>()||!number(d["angle"],s->cfg["minAngle"].as<double>(),s->cfg["maxAngle"].as<double>())){answer(false,"Confirm movement and an angle within limits",400);return;}
    }
    s->calibration=enter;s->target=s->pos;s->moving=false;
    if(enter&&!d["angle"].isNull())outputs.move(*s,d["angle"]);
    answer(true,enter?"Calibration: MQTT control of this servo is blocked":"Calibration finished; cached states not applied");
  });
  web.on("/api/export",HTTP_POST,[]{DynamicJsonDocument req(256);if(!body(req))return;if(req["includeSecrets"].as<bool>())sendJson(config.as<JsonVariantConst>());else sendPublicConfig(config.as<JsonObjectConst>());});
  web.on("/api/backup",HTTP_POST,[]{DynamicJsonDocument req(256);if(!body(req))return;DynamicJsonDocument d(JsonCapacity);if(!store.backup(d)){answer(false,"No previous valid backup",404);return;}redact(d);sendJson(d.as<JsonVariantConst>());});
  web.on("/api/ap",HTTP_POST,[]{if(!mutation())return;startAP();answer(true,"Setup AP enabled");});
  web.on("/api/reset",HTTP_POST,[]{DynamicJsonDocument d(256);if(!body(d))return;if(std::string(d["confirm"]|"")!="RESET"){answer(false,"Type RESET",400);return;}{OutputGuard guard;outputs.stopAll();}store.reset();answer(true,"Reset settings and restart");ESP.restart();});
  web.onNotFound([]{
    if(!fsReady){web.send(503,"text/plain; charset=utf-8","Cannot load the web panel: LittleFS mount failed. Diagnostics: /api/status");return;}
    String path=web.uri();if(path=="/")path="/index.html";
    if(path.indexOf("..")>=0||(!path.endsWith(".html")&&!path.endsWith(".css")&&!path.endsWith(".js"))){web.send(404,"text/plain","Resource not found");return;}
    File f=LittleFS.open(path,"r");if(!f){web.send(404,"text/plain","Upload web files: pio run -t uploadfs");return;}
    const char* mime=path.endsWith(".js")?"text/javascript; charset=utf-8":path.endsWith(".css")?"text/css; charset=utf-8":"text/html; charset=utf-8";
    web.sendHeader("Cache-Control","no-store");web.streamFile(f,mime);f.close();
  });web.begin();
}
void setup() {
  Serial.begin(115200); // USB CDC, never Serial0.begin(): UART0 pins remain free.
  eventQueue=xQueueCreate(20,sizeof(Event));
  outputMutex=xSemaphoreCreateRecursiveMutex();
  if(!eventQueue||!outputMutex){Serial.println("Cannot create queue/mutex");if(eventQueue)vQueueDelete(eventQueue);eventQueue=nullptr;return;}
  bool loaded=store.begin(config);if(!loaded)defaults();migrateIdentity(loaded);migrateShortHostname(loaded);
  if(!config["uart0"].as<bool>()&&uart_is_driver_installed(UART_NUM_0))uart_driver_delete(UART_NUM_0);
  fsReady=LittleFS.begin(false,"/littlefs",10,"littlefs");
  logLine(fsReady?"LittleFS mounted":"LittleFS mount failed");
  sessionToken=String(esp_random(),HEX)+String(esp_random(),HEX);
  WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setHostname(config["device"].as<const char*>());
  // Preserve the SDK Wi-Fi profile when installing onto a board with another firmware.
  // Credentials stay on the device and are redacted by the configuration API.
  if(!loaded){wifi_config_t previous={};if(esp_wifi_get_config(WIFI_IF_STA,&previous)==ESP_OK&&previous.sta.ssid[0]){
    config["wifi"]["ssid"]=std::string((const char*)previous.sta.ssid,strnlen((const char*)previous.sta.ssid,sizeof previous.sta.ssid));
    config["wifi"]["password"]=std::string((const char*)previous.sta.password,strnlen((const char*)previous.sta.password,sizeof previous.sta.password));
    logLine("Restored SDK Wi-Fi profile from previous firmware");
  }}
  WiFi.begin(config["wifi"]["ssid"].as<const char*>(),config["wifi"]["password"].as<const char*>());wifiAttempt=millis();
  config.shrinkToFit(); // Release unused pool space before serving large template edits.
  hwError=outputs.begin(config.as<JsonVariantConst>())?"":outputs.error;
  // Boot safe is an explicit opt-in; default none produces no PWM pulse.
  if(hwError.empty())for(auto& s:outputs.servos)if(std::string(s.cfg["boot"].as<const char*>())=="safe")outputs.move(s,s.cfg["safeAngle"]);
  if(!loaded||std::string(config["wifi"]["ssid"].as<const char*>()).empty())startAP();
  mqttCallbacks();setupMqtt();setupWeb();logLine("Startup: MQTT servo movement disabled");
  if(xTaskCreate([](void*){
    for(;;){
      {
        OutputGuard guard;
        bool online=WiFi.status()==WL_CONNECTED&&mqtt.connected();
        if(wasOnline&&!online){outputs.freeze(true);operational=false;}
        wasOnline=online;
        outputs.tick();
      }
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  },"outputs",4096,nullptr,2,nullptr)!=pdPASS){hwError="Cannot create output task";outputs.stopAll();}
}
void loop() {
  if(!eventQueue)return;
  // USB-only diagnostic query; never executes an output action or reveals credentials.
  if(Serial.available()&&Serial.read()=='?'){
    Serial.printf("WWW mounted=%d index=%d uptime=%lu heap=%u\n",fsReady,LittleFS.exists("/index.html"),(unsigned long)(millis()/1000),ESP.getFreeHeap());
    Serial.printf("NET wifi=%d sta=%s ap=%d apIp=%s apName=%s mqtt=%d\n",int(WiFi.status()),WiFi.localIP().toString().c_str(),apActive,WiFi.softAPIP().toString().c_str(),apName.c_str(),mqtt.connected());
  }
  web.handleClient();processEvents();publishNodeInfo();uint32_t now=millis();
  if(networkRestartPending&&int32_t(now-networkRestartAt)>=0){
    networkRestartPending=false;
    WiFi.disconnect();WiFi.setHostname(config["device"].as<const char*>());
    WiFi.begin(config["wifi"]["ssid"].as<const char*>(),config["wifi"]["password"].as<const char*>());wifiAttempt=now;
  }
  if(WiFi.status()==WL_CONNECTED) {
    if(!mdnsStarted){mdnsStarted=MDNS.begin(config["device"].as<const char*>());if(mdnsStarted)MDNS.addService("http","tcp",80);}
    if(mqttConfigured&&!mqtt.connected()&&!connecting&&now-mqttAttempt>=5000&&strlen(config["mqtt"]["host"])>0){mqttAttempt=now;connecting=true;mqtt.connect();}
    if(connecting&&now-mqttAttempt>15000){mqtt.disconnect(true);connecting=false;mqttAttempt=now;}
  }else {
    if(now-wifiAttempt>=30000){startAP();WiFi.reconnect();wifiAttempt=now;}
  }
}
