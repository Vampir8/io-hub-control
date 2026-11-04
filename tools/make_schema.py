"""Generate the version 1 JSON Schema; firmware also checks relational constraints."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def text(n, empty=False): return {"type": "string", "minLength": 0 if empty else 1, "maxLength": n}
def integer(a,b): return {"type":"integer","minimum":a,"maximum":b}
def num(a,b): return {"type":"number","minimum":a,"maximum":b}
def enum(*x): return {"enum":list(x)}
def obj(p): return {"type":"object","properties":p,"required":list(p),"additionalProperties":False}
def array(item, maxn, minn=0): return {"type":"array","items":item,"minItems":minn,"maxItems":maxn}
def ref(n): return {"$ref":"#/$defs/"+n}
named={"id":dict(text(32),pattern="^[A-Za-z0-9_-]+$"),"name":text(64)}
pin=enum(0,1,2,3,4,5,6,7,8,9,10,20,21)
servo=obj(dict(named,pin=pin,hz=integer(40,333),minUs=integer(500,2500),maxUs=integer(500,2500),minAngle=num(0,180),maxAngle=num(0,180),reverse={"type":"boolean"},speed=num(1,360),detachAfter={"type":"boolean"},boot=enum("none","safe"),offline=enum("hold","safe","detach"),safeAngle=num(0,180)))
chain=obj(dict(named,pin=pin,type=enum("WS2812B","SK6812RGB","SK6812RGBW"),order=enum("RGB","GRB","RGBW","GRBW"),count=integer(1,300),brightness=integer(0,255)))
chain["allOf"]=[{"if":{"properties":{"type":{"const":"SK6812RGBW"}}},"then":{"properties":{"order":enum("RGBW","GRBW")}},"else":{"properties":{"order":enum("RGB","GRB")}}}]
group=obj(dict(named,chain=text(32),pixels=dict(array(integer(0,299),300,1),uniqueItems=True),priority={"anyOf":[integer(0,255),{"type":"null"}]}))
servo_action=obj({"kind":{"const":"servo"},"target":text(32),"angle":num(0,180)})
led_action=obj({"kind":{"const":"led"},"target":text(32),"effect":enum("off","solid","blink","alternate"),"color":array(integer(0,255),4,4),"brightness":integer(0,255),"onMs":integer(20,60000),"offMs":integer(20,60000),"other":text(32,True)})
led_action["required"].remove("other")
led_action["allOf"]=[{"if":{"properties":{"effect":{"const":"alternate"}}},"then":{"required":["other"],"properties":{"other":text(32)}}}]
state=obj({"value":text(96),"actions":array({"oneOf":[ref("servoAction"),ref("ledAction")]},256,1)})
binding=obj(dict(named,enabled={"type":"boolean"},type=enum("T","S"),source=dict(text(96),pattern="^[^|]+$"),states=array(ref("state"),128,1)))
binding["allOf"]=[{
    "if": {"properties": {"type": {"const": "S"}}},
    "then": {"properties": {
        "source": {"pattern": "^(0|[1-9][0-9]{0,9})$"},
        "states": {"items": {"properties": {"value": {"pattern": "^-?[0-9]{1,10}$"}}}}
    }}
}]
schema=obj({"version":{"const":1},"device":dict(text(32),pattern="^[a-z0-9](?:[a-z0-9-]*[a-z0-9])?$"),"wifi":obj({"ssid":text(32,True),"password":{"anyOf":[text(64,True),{"type":"null"}]}}),"mqtt":obj({"host":text(128,True),"port":integer(1,65535),"clientId":text(64),"user":text(64,True),"password":{"anyOf":[text(128,True),{"type":"null"}]},"stateTopic":dict(text(128),pattern="^[^+#]+$")}),"modeTopic":dict(text(128,True),pattern="^[^+#]*$"),"uart0":{"type":"boolean"},"externalJtag":{"type":"boolean"},"allowStrapping":{"type":"boolean"},"servos":array(ref("servo"),6),"chains":array(ref("chain"),2),"groups":array(ref("group"),32),"bindings":array(ref("binding"),32)})
schema.update({"$schema":"https://json-schema.org/draft/2020-12/schema","title":"nControl ESP32-C3 — configuration v1","$defs":{"servo":servo,"chain":chain,"group":group,"binding":binding,"state":state,"servoAction":servo_action,"ledAction":led_action},"$comment":"Firmware additionally validates GPIO ownership/reservations, LEDC timers, pulse/angle ranges, ID references, binding owners, group priorities and LED indices. Limits: 128 states and 256 actions. Text limits count UTF-8 bytes."})
schema["properties"]["mqtt"]["properties"]["enabled"]={"type":"boolean"}
servo2=obj({k:v for k,v in servo['properties'].items() if k not in ('id','name','pin')})
pixel=obj({'effect':enum('off','solid','blink'),'color':array(integer(0,255),4,4),'brightness':integer(0,255),'onMs':integer(20,60000),'offMs':integer(20,60000),'phaseMs':integer(0,120000)})
for key in ['brightness','onMs','offMs','phaseMs']:pixel['required'].remove(key)
signal_state=obj({'value':dict(text(96),pattern='^[^|]+$'),'pixels':array(pixel,5,1)})
servo_state=obj({'value':dict(text(96),pattern='^[^|]+$'),'angle':num(0,180)})
signal_template=obj(dict(named,kind={'const':'signal'},count=integer(1,5),states=array(signal_state,128,1)))
for key,value in {'head':text(32),'colors':array(text(32),5,1),'roles':array(text(32),5,1)}.items():signal_template['properties'][key]=value
servo_template=obj(dict(named,kind={'const':'servo'},states=array(servo_state,128,1)))
ws2=obj({'type':enum('WS2812B','SK6812RGB','SK6812RGBW'),'order':enum('RGB','GRB','RGBW','GRBW'),'brightness':integer(0,255)})
ws2['properties'].update({'input':enum('json','states'),'topic':dict(text(128,True),pattern='^[^+#]*$')})
port=obj(dict(named,slot=integer(0,9),pin=pin,enabled={'type':'boolean'},template=text(32),type=enum('T','S'),source=dict(text(96),pattern='^[^|]+$'),servo=servo2,ws=ws2))
common={k:v for k,v in schema['properties'].items() if k not in ('version','servos','chains','groups','bindings')}
v2=obj(dict(common,version={'const':2},ioPins=dict(array(pin,10,10),uniqueItems=True),ports=array(port,10),templates=array({'oneOf':[signal_template,servo_template]},24)))
defs=schema.pop('$defs')
schema={'$schema':'https://json-schema.org/draft/2020-12/schema','title':'nControl ESP32-C3 Hub I/O — v1/v2','$defs':defs,'oneOf':[schema,v2],'$comment':'Firmware additionally validates unique slots/GPIO, hardware reservations, template references and per-servo angle limits; at most 128 states and 320 pixel definitions. Six hardware LEDC channels plus four timer ISR PWM channels at 50 Hz; at most ten devices total.'}
(ROOT/'config.schema.json').write_text(json.dumps(schema,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
