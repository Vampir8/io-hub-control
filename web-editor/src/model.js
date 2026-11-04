import NATIVE_SIGNALS from './signals.generated.json';
export const PINS=[0,1,2,3,4,5,6,7,8,9,10,20,21];
export const DEFAULT_PINS=[0,1,3,4,5,6,7,10,20,21];
export const clone=x=>JSON.parse(JSON.stringify(x));
export const pixel=(effect='off',color=[0,0,0,0])=>({effect,color:[...color],brightness:255,onMs:500,offMs:500,phaseMs:0});
export const servoDefaults=()=>({hz:50,minUs:1000,maxUs:2000,minAngle:0,maxAngle:180,reverse:false,speed:30,detachAfter:false,boot:'none',offline:'hold',safeAngle:90});
export const wsDefaults=()=>({type:'WS2812B',order:'GRB',brightness:64,input:'states'});
export function signalTemplate(count=2,id='signal-'+count){
  const colors=[[0,255,0,0],[255,0,0,0]];
  const state=(value,index,effect)=>({value,pixels:Array.from({length:count},(_,i)=>pixel(i===index?effect:'off',i===index?colors[index]:[0,0,0,0]))});
  return {id,name:count+'-lamp signal',kind:'signal',count,states:[state('g',0,'solid'),state('r',1,'solid'),state('gf',0,'blink'),state('rf',1,'blink'),state('o',-1,'off')]};
}
export function builtins(){return normalizeTemplates(clone(NATIVE_SIGNALS)).concat([
  {id:'turnout',name:'Turnout straight / diverging',kind:'servo',states:[{value:'s',angle:60},{value:'t',angle:120}]},
  {id:'threeway',name:'Three-way turnout',kind:'servo',states:[{value:'s',angle:90},{value:'l',angle:60},{value:'r',angle:120}]},
  {id:'wye',name:'Wye turnout',kind:'servo',states:[{value:'l',angle:60},{value:'r',angle:120}]},
  {id:'barrier',name:'Barrier',kind:'servo',states:[{value:'c',angle:60},{value:'o',angle:120}]}
]);}
export function normalizeTemplates(templates){for(const t of templates)if(t.kind==='signal')for(const s of t.states)for(let i=0;i<s.pixels.length;i++)s.pixels[i]={...pixel(),...s.pixels[i]};return templates;}
export function compactConfig(c){const result=clone(c);result.mqtt.enabled??=true;for(const p of result.ports){p.ws.input='states';delete p.ws.topic;}for(const t of result.templates)if(t.kind==='signal')for(const s of t.states)for(const a of s.pixels)for(const [k,v] of Object.entries({brightness:255,onMs:500,offMs:500,phaseMs:0}))if(a[k]===v)delete a[k];return result;}
export function ensureNative(c){for(const p of c.ports){p.ws.input='states';delete p.ws.topic;}for(const t of builtins())if(!c.templates.some(x=>x.id===t.id)&&c.templates.length<24)c.templates.push(t);normalizeTemplates(c.templates);return c;}
export function uniqueId(list,prefix){let n=1;while(list.some(x=>x.id===prefix+n))n++;return prefix+n;}
export function freePin(c){return DEFAULT_PINS.find(p=>!c.ports.some(x=>x.pin===p)&&(!c.uart0||![20,21].includes(p))&&(!c.externalJtag||p<4||p>7));}
function nextDeviceNumber(c,kind){const prefix=kind==='signal'?'Signal ':'Turnout ';let n=1;while(c.ports.some(p=>p.name===prefix+n))n++;return n;}
export function addPort(c,templateId){
  if(c.ports.length>=10)throw Error('At most 10 devices in total');
  const slot=Array.from({length:10},(_,i)=>i).find(i=>!c.ports.some(p=>p.slot===i)&&(!c.uart0||![20,21].includes(c.ioPins[i]))&&(!c.externalJtag||c.ioPins[i]<4||c.ioPins[i]>7)&&(![2,8,9].includes(c.ioPins[i])||c.allowStrapping));
  if(slot===undefined)throw Error('No free port: check JTAG/UART0 reservations and strapping GPIOs.');
  const pin=c.ioPins[slot];if(pin===undefined)throw Error('No free GPIO.');
  const t=c.templates.find(x=>x.id===templateId);if(!t)throw Error('Select a template');
  const p={id:uniqueId(c.ports,'io-'),name:(t.kind==='signal'?'Signal ':'Turnout ')+nextDeviceNumber(c,t.kind),slot,pin,enabled:true,template:t.id,type:'T',source:t.kind==='servo'?'Switch '+(slot+1):'Light '+(slot+1),ws:wsDefaults(),servo:servoDefaults()};c.ports.push(p);return p;
}
export function resizeTemplate(t,count){t.count=count;for(const s of t.states){while(s.pixels.length<count)s.pixels.push(pixel());s.pixels.length=count;}}
// Read-only migration in the browser. Nothing is applied until explicit Save.
export function migrate(old){
  if(old.version===2){
    const c=clone(old);
    if(!Array.isArray(c.ports)||c.ports.length>10||!Array.isArray(c.templates)||c.templates.length>24||!c.wifi||!c.mqtt)throw Error('Invalid version 2 configuration structure');
    c.mqtt.enabled??=true;
    if(!c.templates.length)c.templates=builtins();c.ioPins??=[...DEFAULT_PINS];
    if(c.ioPins.length!==10||new Set(c.ioPins).size!==10||c.ioPins.some(p=>!PINS.includes(p)))throw Error('Invalid GPIO map');
    for(const t of c.templates){
      if(!['signal','servo'].includes(t.kind)||!Array.isArray(t.states)||!t.states.length)throw Error('Invalid template structure');
      if(t.kind==='signal'){
        if(!Number.isInteger(t.count)||t.count<1||t.count>5)throw Error('Invalid lamp count');
        for(const state of t.states){
          if(!Array.isArray(state.pixels)||state.pixels.length!==t.count)throw Error('Invalid state lamp count');
          for(const a of state.pixels)if(!Array.isArray(a.color)||a.color.length!==4)throw Error('Invalid lamp color');
        }
      }
    }
    for(const p of c.ports)if(!c.templates.some(t=>t.id===p.template)||!Number.isInteger(p.slot)||p.slot<0||p.slot>9||p.pin!==c.ioPins[p.slot]||!p.ws||!p.servo)throw Error('Invalid port structure');
    for(const p of c.ports)if(/^Port \d+$/.test(p.name)){const t=c.templates.find(t=>t.id===p.template);p.name=(t.kind==='signal'?'Signal ':'Turnout ')+nextDeviceNumber(c,t.kind);}
    return ensureNative(c);
  }
  if(old.version!==1)throw Error('Unsupported configuration version');
  const c=clone(old);c.mqtt.enabled??=true;c.version=2;c.templates=builtins();c.ports=[];
  function binding(targets){const found=old.bindings.filter(b=>b.enabled&&b.states.some(s=>s.actions.some(a=>targets.includes(a.target)||targets.includes(a.other))));if(found.length>1)throw Error('Configuration has multiple sources on one chain. Separate the sources before migration; saved settings are unchanged.');return found[0];}
  function append(resource,t,b){const id='migrated-'+resource.id;t.id='template-'+id;c.templates.push(t);c.ports.push({id,name:resource.name,pin:resource.pin,enabled:!!b,template:t.id,type:b?.type||'T',source:b?.source||'unassigned',servo:servoDefaults(),ws:wsDefaults()});return c.ports[c.ports.length-1];}
  for(const s of old.servos){const b=binding([s.id]);const states=b?b.states.filter(st=>st.actions.some(a=>a.target===s.id)).map(st=>({value:st.value,angle:st.actions.find(a=>a.target===s.id).angle})):[{value:'s',angle:s.safeAngle}];const p=append(s,{name:s.name,kind:'servo',states},b);p.servo=clone(s);delete p.servo.id;delete p.servo.name;delete p.servo.pin;}
  for(const ch of old.chains){
    if(ch.count>5)throw Error('Signal port migration supports up to 5 LEDs. Adjust the configuration before importing; saved settings are unchanged.');
    const groups=old.groups.filter(g=>g.chain===ch.id).sort((a,b)=>(a.priority||0)-(b.priority||0)),b=binding(groups.map(g=>g.id));
    const states=(b?.states||[{value:'o',actions:[]}]).map(s=>{
      const pixels=Array.from({length:ch.count},()=>pixel());
      for(const g of groups)for(const a of s.actions){const inverse=a.effect==='alternate'&&a.other===g.id;if(a.target!==g.id&&!inverse)continue;for(const i of g.pixels){const x={...pixel(),...clone(a)};delete x.kind;delete x.target;delete x.other;if(x.effect==='alternate'){x.effect='blink';if(inverse){[x.onMs,x.offMs]=[x.offMs,x.onMs];x.phaseMs=x.onMs;}}pixels[i]=x;}}
      return {value:s.value,pixels};
    });const p=append(ch,{name:ch.name,kind:'signal',count:ch.count,states},b);p.ws={type:ch.type,order:ch.order,brightness:ch.brightness};
  }
  if(c.ports.length>10)throw Error('Migration exceeds 10 ports');
  c.ioPins=c.ports.map(p=>p.pin);for(const pin of PINS.filter(p=>!c.ioPins.includes(p)&&![2,8,9].includes(p)))if(c.ioPins.length<10)c.ioPins.push(pin);
  c.ports.forEach((p,i)=>p.slot=i);
  for(const k of ['servos','chains','groups','bindings'])delete c[k];return c;
}
export function preview(c,payload){const parts=payload.split('|');if(parts.length!==3||!['T','S'].includes(parts[0])||!parts[1]||!parts[2])return [];return c.ports.filter(p=>p.enabled&&p.type===parts[0]&&p.source===parts[1]).flatMap(p=>{const t=c.templates.find(t=>t.id===p.template),state=t?.states.find(s=>s.value===parts[2]);return state?[{port:p.id,gpio:p.pin,template:t.name,state:clone(state)}]:[]});}
