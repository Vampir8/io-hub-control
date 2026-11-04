"""Compile actual metadata publishers with MQTT stubs; feed their JSON into nControl's registry."""
from pathlib import Path
import ast, json, os, subprocess, threading, time, ipaddress
from types import SimpleNamespace
from urllib.parse import urlparse
root=Path(__file__).resolve().parents[1]
power=root.parent/'lego_esp32power'
out=root/'test/build/node-info';out.mkdir(parents=True,exist_ok=True)
zig=power/'.pio/host-tools/ziglang/zig.exe'
compiler=[str(zig),'c++']
source=(root/'src/main.cpp').read_text(encoding='utf8')
function=source[source.index('void publishNodeInfo() {'):source.index('\nstruct Event')]
preamble=r'''
#include <ArduinoJson.h>
#include <cassert>
#include <string>
#include <vector>
#include <iostream>
#include <cstdint>
using String=std::string;
uint32_t tick=100;uint32_t millis(){return tick;}
constexpr int WL_CONNECTED=1;
struct IP{String toString(){return "192.0.2.10";}};
struct Wifi{int state=1;int status(){return state;}IP localIP(){return {};}} WiFi;
struct Packet{std::string topic,body;};
struct Mqtt{bool online=true,fail=false;std::vector<Packet> sent;
bool connected(){return online;}
int publish(const char*t,int qos,bool retain,const char*p,size_t n=0){assert(qos==1&&retain);if(fail)return 0;sent.push_back({t,n?std::string(p,n):std::string(p)});return sent.size();}} mqtt;
StaticJsonDocument<1024> config;
std::string nodeId="nchub-aabbccddeeff",nodeInfoTopic="nControl/nodes/"+nodeId+"/info",nodeStatusTopic="nControl/nodes/"+nodeId+"/status";
std::string nodeDisplayName="I/O Hub Control - ddeeff";
bool announceNode=false;uint32_t nodeAnnounceAt=0;
void logLine(std::string){}
'''
checks=r'''
int main(){config["device"]="Semafory łódź \\\"A\\\"";config["mqtt"]["clientId"]="Client \\\"1\\\"";
publishNodeInfo();assert(mqtt.sent.empty());announceNode=true;mqtt.online=false;publishNodeInfo();assert(mqtt.sent.empty());mqtt.online=true;WiFi.state=0;publishNodeInfo();assert(mqtt.sent.empty());WiFi.state=1;mqtt.fail=true;publishNodeInfo();assert(announceNode);mqtt.fail=false;publishNodeInfo();assert(mqtt.sent.empty());tick+=5000;publishNodeInfo();assert(!announceNode&&mqtt.sent.size()==2);assert(mqtt.sent[1].body=="{\"online\":true}");std::cout<<mqtt.sent[0].body;publishNodeInfo();assert(mqtt.sent.size()==2);
announceNode=true;nodeAnnounceAt=tick;publishNodeInfo();assert(mqtt.sent.size()==4);}
'''
(root/'test/build/node-info/c3.cpp').write_text(preamble+function+checks,encoding='utf8')
subprocess.run(compiler+['-std=c++17','-DARDUINOJSON_ENABLE_STD_STRING=1','-I'+str(root/'.pio/libdeps/supermini/ArduinoJson/src'),str(out/'c3.cpp'),'-o',str(out/'c3.exe')],check=True)
c3=json.loads(subprocess.check_output([str(out/'c3.exe')]))
source=(power/'main/network.c').read_text(encoding='utf8')
function=source[source.index('static int announce_node('):source.index('\nstatic char rx')]
preamble=r'''
#include "cJSON.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *mqtt;
static char node_info_topic[]="nControl/nodes/lego-power-aabbccddeeff/info",node_status_topic[]="nControl/nodes/lego-power-aabbccddeeff/status";
static char node_name[]="Powered Up Hub Control - ddeeff";
static char captured[2048];static int sent,fail;
static int esp_mqtt_client_enqueue(void *client,const char *topic,const char *body,int len,int qos,int retain,bool store){assert(qos==1&&retain&&store);if(fail)return -1;if(strstr(topic,"/info"))snprintf(captured,sizeof(captured),"%s",body);else assert(strcmp(body,"{\"online\":true}")==0);return ++sent;}
'''
checks=r'''
int main(void){fail=1;assert(announce_node("x","192.0.2.11")<0);fail=0;assert(announce_node("Client \\\"2\\\"","192.0.2.11")==0&&sent==2);puts(captured);return 0;}
'''
(out/'s3.c').write_text(preamble+function+checks,encoding='utf8')
cjson=Path(os.environ['USERPROFILE'])/'.platformio/packages/framework-espidf/components/json/cJSON'
subprocess.run([str(zig),'cc','-std=c11','-I'+str(cjson),str(out/'s3.c'),str(cjson/'cJSON.c'),'-o',str(out/'s3.exe')],check=True)
s3=json.loads(subprocess.check_output([str(out/'s3.exe')]))
assert c3['name']=='I/O Hub Control - ddeeff'
assert s3['name']=='Powered Up Hub Control - ddeeff'
registry_file=Path(os.environ.get('NCONTROL_SOURCE', str(root.parent/'ncontrol-source')))/'mqtt_nodes.py'
parsed=ast.parse(registry_file.read_text(encoding='utf8'))
classes=[n for n in parsed.body if isinstance(n,ast.ClassDef) and n.name=='NodeRegistry']
ns=dict(json=json,os=os,time=time,threading=threading,ipaddress=ipaddress)
exec(compile(ast.Module(body=classes,type_ignores=[]),str(registry_file),'exec'),ns)
registry=ns['NodeRegistry'](SimpleNamespace(iniSettings=SimpleNamespace(settingsPath=''),mqttBroker=SimpleNamespace(useInternalBroker=False)))
for node_id,data in [('nchub-aabbccddeeff',c3),('lego-power-aabbccddeeff',s3)]:
 assert set(data)=={'name','client_id','type','model','firmware','ip','web_url'}
 assert urlparse(data['web_url']).hostname==data['ip']
 registry.nodes[data['client_id']]={'id':data['client_id'],'client_id':data['client_id'],'online':True}
 topic='nControl/nodes/'+node_id
 assert registry.ingest(topic+'/info',json.dumps(data,ensure_ascii=False))
 assert data['client_id'] not in registry.nodes,'metadata should merge the broker-only entry'
 assert registry.ingest(topic+'/status','{"online":true}') and registry.nodes[node_id]['online']
 assert registry.ingest(topic+'/status','{"online":false}') and not registry.nodes[node_id]['online']
 assert registry.ingest(topic+'/info',json.dumps(data))
 assert registry.ingest(topic+'/status','{"online":true}') and registry.nodes[node_id]['online']
assert len(registry.nodes)==2
print('PASS actual C3/S3 publishers: UTF-8/JSON escaping, QoS1 retained, retry, no repeated announcements; real nControl registry: identity merge, online/offline, reconnect, two nodes')
