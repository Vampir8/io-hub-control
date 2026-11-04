export function servoStateName(template,value){
  const labels={turnout:{s:'straight',t:'diverging'},threeway:{s:'straight',l:'left',r:'right'},wye:{l:'left',r:'right'},barrier:{c:'closed',o:'open'}};
  return labels[template.id]?.[value]||template.states.find(s=>s.value===value)?.name||value;
}
export function servoSummary(template,port,status){
  const cached=status.cache?.find(c=>c.type===port.type&&c.source===port.source);
  const received=cached?(template.states.some(s=>s.value===cached.state)?servoStateName(template,cached.state):`unknown (${cached.state})`):'none';
  const servo=status.servos?.find(s=>s.id===port.id);
  const targets=servo?.known?template.states.filter(s=>Math.abs(s.angle-servo.target)<0.05):[];
  const target=servo?.known?(targets.length===1?`${servoStateName(template,targets[0].value)} · ${servo.target.toFixed(1)}°`:`${servo.target.toFixed(1)}°`):'none';
  let detail='';
  if(servo?.calibration)detail='Calibration — MQTT blocked';
  else if(!port.enabled)detail='Disconnected';
  else if(!status.allowed)detail='Movement blocked';
  else if(!servo?.known)detail='Waiting for a command';
  else if(servo.moving)detail=`Moving · ${servo.commanded.toFixed(1)}°`;
  else detail='Commanded position, no feedback';
  return {received,target,detail};
}
