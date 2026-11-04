"""Exercise the actual one-time identity migrations with simulated NVS and MAC."""
from pathlib import Path
import subprocess, os
root=Path(__file__).resolve().parents[1];power=root.parent/'lego_esp32power';out=root/'test/build/identity';out.mkdir(parents=True,exist_ok=True)
zig=power/'.pio/host-tools/ziglang/zig.exe'
src=(root/'src/main.cpp').read_text(encoding='utf-8');fn=src[src.index('std::string identityName()'):src.index('void defaults()')]
pre=r"""
#include <ArduinoJson.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdint>
using namespace ArduinoJson;
DynamicJsonDocument config(4096);
constexpr int ESP_MAC_WIFI_STA=0;
void esp_read_mac(uint8_t *m,int){const uint8_t value[]={0xaa,0xbb,0xcc,0xdd,0xee,0xff};memcpy(m,value,6);}
bool marker=false,shortMarker=false,fail=false;int saves=0;
struct Preferences{bool begin(const char*,bool){return true;}bool getBool(const char*k,bool){return strcmp(k,"dns_short_v1")==0?shortMarker:marker;}void putBool(const char*k,bool v){if(strcmp(k,"dns_short_v1")==0)shortMarker=v;else marker=v;}void end(){}};
struct Store{bool save(JsonVariantConst){saves++;return !fail;}}store;
void logLine(std::string){}
"""
checks=r"""
int main(){config["device"]="old";config["mqtt"]["clientId"]="old-client";config["wifi"]["ssid"]="network";config["wifi"]["password"]="keep";config["ports"][0]["pin"]=3;
assert(identityName()=="io-hub-control-aabbccddeeff");migrateIdentity(false);assert(saves==0);
fail=true;migrateIdentity(true);assert(!marker&&saves==1);fail=false;migrateIdentity(true);assert(marker&&saves==2);
assert(std::string(config["device"].as<const char*>())==hostName());assert(hostName()=="io-hub-control-ddeeff");assert(std::string(config["mqtt"]["clientId"].as<const char*>())==identityName());assert(config["ports"][0]["pin"]==3);assert(std::string(config["wifi"]["password"].as<const char*>())=="keep");
config["device"]="custom-after-migration";migrateIdentity(true);assert(saves==2&&std::string(config["device"].as<const char*>())=="custom-after-migration");config["device"]=identityName();fail=true;migrateShortHostname(true);assert(!shortMarker&&saves==3);assert(std::string(config["device"].as<const char*>())==identityName());fail=false;migrateShortHostname(true);assert(shortMarker&&saves==4&&std::string(config["device"].as<const char*>())==hostName());assert(std::string(config["mqtt"]["clientId"].as<const char*>())==identityName());migrateShortHostname(true);assert(saves==4);puts("PASS C3 identity migration: STA MAC, fresh boot, retry on failed write, settings preserved, one-time only");}
"""
f=out/'c3.cpp';f.write_text(pre+fn+checks,encoding='utf-8');exe=out/'c3.exe'
subprocess.run([str(zig),'c++','-std=c++17','-DARDUINOJSON_ENABLE_STD_STRING=1','-I'+str(root/'.pio/libdeps/supermini/ArduinoJson/src'),str(f),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
src=(power/'main/config.c').read_text(encoding='utf-8');fn=src[src.index('void config_load('):src.index('bool config_save(')]
pre=r"""
#include "cJSON.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ESP_OK 0
#define ESP_MAC_WIFI_STA 0
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define PROVISION_PANEL_PASSWORD "test-password"
typedef int nvs_handle_t;
typedef struct{char client_id[65],panel_password[65],ap_password[65],ssid[33],broker[128];int outputs[20];}config_t;
static config_t stored;
static bool marker=false,fail=false;static int saves=0;
void config_defaults(config_t*c){memset(c,0,sizeof(*c));}
void esp_read_mac(uint8_t*m,int type){const uint8_t value[]={0x11,0x22,0x33,0x44,0x55,0x66};memcpy(m,value,6);}
int nvs_open(const char*ns,int mode,nvs_handle_t*h){*h=1;return ESP_OK;}
int nvs_get_str(nvs_handle_t h,const char*key,char*s,size_t*n){if(s)strcpy(s,"{}");*n=3;return ESP_OK;}
void nvs_close(nvs_handle_t h){}
int nvs_get_u8(nvs_handle_t h,const char*key,uint8_t*v){*v=strcmp(key,"hub_names_v1")==0?marker:1;return ESP_OK;}
int nvs_set_u8(nvs_handle_t h,const char*key,uint8_t v){assert(strcmp(key,"hub_names_v1")==0);marker=v;return ESP_OK;}
int nvs_commit(nvs_handle_t h){return ESP_OK;}
bool config_from_json(cJSON*j,config_t*c,char*error,size_t n){*c=stored;return true;}
bool config_save(const config_t*c){saves++;if(fail)return false;stored=*c;return true;}
"""
checks=r"""
int main(){config_t actual;memset(&stored,0,sizeof(stored));strcpy(stored.client_id,"old");strcpy(stored.ssid,"network");strcpy(stored.broker,"192.0.2.1");strcpy(stored.ap_password,"keep-password");stored.outputs[19]=42;
fail=true;config_load(&actual);assert(!marker&&saves==1);fail=false;config_load(&actual);assert(marker&&saves==2);
assert(strcmp(actual.client_id,"powered-up-hub-control-112233445566")==0);assert(strcmp(actual.ssid,"network")==0&&strcmp(actual.broker,"192.0.2.1")==0&&strcmp(actual.ap_password,"keep-password")==0&&actual.outputs[19]==42);
strcpy(stored.client_id,"custom-after-migration");config_load(&actual);assert(saves==2&&strcmp(actual.client_id,"custom-after-migration")==0);puts("PASS S3 identity migration: STA MAC, retry on failed write, settings preserved, one-time only");}
"""
f=out/'s3.c';f.write_text(pre+fn+checks,encoding='utf-8');exe=out/'s3.exe';cj=Path(os.environ['USERPROFILE'])/'.platformio/packages/framework-espidf/components/json/cJSON'
subprocess.run([str(zig),'cc','-std=c11','-I'+str(cj),str(f),str(cj/'cJSON.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)

# Verify the actual startup hostname expression against the SDK's 32-byte limit.
network=(power/'main/network.c').read_text(encoding='utf-8')
line=next(line.strip() for line in network.splitlines() if 'snprintf(hostname,' in line)
check='#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n#include <stdint.h>\nint main(void){char hostname[64];const uint8_t identity_mac[]={0x11,0x22,0x33,0x44,0x55,0x66};'+line+'assert(strlen(hostname)<=32);assert(strcmp(hostname,"powered-up-hub-control-445566")==0);puts("PASS S3 actual DHCP/mDNS hostname: within ESP-IDF 32-byte limit");}'
f=out/'hostname.c';f.write_text(check,encoding='utf-8');exe=out/'hostname.exe'
subprocess.run([str(zig),'cc',str(f),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
