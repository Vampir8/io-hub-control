#pragma once
#include <ArduinoJson.h>
namespace hub {
// Serialize directly from the live document: no second JSON pool and no changes
// to the active configuration. Preserve JSON escaping, including unknown keys.
template<class Buffer> void publicKey(const char* key,Buffer& out) {
  out+='"';
  for(const unsigned char* p=reinterpret_cast<const unsigned char*>(key);*p;++p){
    if(*p=='"'||*p=='\\'){out+='\\';out+=char(*p);}
    else if(*p<32){const char hex[]="0123456789abcdef";out+="\\u00";out+=hex[*p>>4];out+=hex[*p&15];}
    else out+=char(*p);
  }
  out+='"';
}
template<class Buffer> void publicConfig(JsonObjectConst config,Buffer& out) {
  out+='{';bool first=true;
  for(JsonPairConst field:config){
    if(!first)out+=',';first=false;
    publicKey(field.key().c_str(),out);out+=':';
    const bool credentials=strcmp(field.key().c_str(),"wifi")==0||strcmp(field.key().c_str(),"mqtt")==0;
    if(credentials&&field.value().is<JsonObjectConst>()){
      out+='{';bool nestedFirst=true;
      for(JsonPairConst item:field.value().as<JsonObjectConst>()){
        if(!nestedFirst)out+=',';nestedFirst=false;
        publicKey(item.key().c_str(),out);out+=':';
        if(strcmp(item.key().c_str(),"password")==0)out+="null";
        else serializeJson(item.value(),out);
      }
      out+='}';
    }else serializeJson(field.value(),out);
  }
  out+='}';
}
}
