const esbuild=require('esbuild'),assert=require('assert'),path=require('path'),vm=require('vm');
const code=esbuild.buildSync({entryPoints:[path.join(__dirname,'src/servo-status.js')],bundle:true,write:false,format:'cjs',target:'node14'}).outputFiles[0].text;
const context={exports:{},module:{exports:{}}};vm.runInNewContext(code,context);const {servoStateName,servoSummary}=context.module.exports;
for(const [id,states] of Object.entries({turnout:{s:'straight',t:'diverging'},threeway:{s:'straight',l:'left',r:'right'},wye:{l:'left',r:'right'},barrier:{c:'closed',o:'open'}})){
  for(const [value,name] of Object.entries(states))assert.equal(servoStateName({id,states:[]},value),name);
}
const t={id:'threeway',states:[{value:'s',angle:90},{value:'l',angle:60},{value:'r',angle:120}]};
const p={id:'io-2',type:'T',source:'Z1',enabled:true};
const s={allowed:false,cache:[{type:'T',source:'Z1',state:'l'}],servos:[{id:p.id,known:false,target:90,commanded:90}]};
let view=servoSummary(t,p,s);assert.equal(view.received,'left');assert.equal(view.target,'none');assert.equal(view.detail,'Movement blocked');
Object.assign(s.servos[0],{known:true,target:120,commanded:80,moving:true});s.allowed=true;
view=servoSummary(t,p,s);assert.equal(view.received,'left');assert(view.target.startsWith('right'));assert(view.detail.startsWith('Moving'));
s.servos[0].calibration=true;assert(servoSummary(t,p,s).detail.startsWith('Calibration'));
s.cache[0].state='UNKNOWN';assert.equal(servoSummary(t,p,s).received,'unknown (UNKNOWN)');
s.cache[0].source='z1';assert.equal(servoSummary(t,p,s).received,'none');
assert.equal(servoStateName({id:'custom',states:[{value:'x',name:'Custom position'}]},'x'),'Custom position');
console.log('PASS servo status: turnout, Three Way, Wye, barrier, custom, blocked, unknown, calibration and commanded vs received');
