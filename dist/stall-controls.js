/* Settings are enforced on the controller, independently of USB telemetry. */
(function(root){
'use strict';
const devices=new Map();
function element(tag,text){const e=root.document.createElement(tag);if(text)e.textContent=text;return e;}
function mount(axis){
 const parent=root.document.getElementById(axis==='motor'?'hardwarePanel':'hardware'+axis);
 if(!parent)return null;
 const panel=element('section');panel.className='stallControls';panel.append(element('h3','Stall response'));
 const beepLabel=element('label','Stall beeping on '),beep=element('input');beep.type='checkbox';beepLabel.append(beep);
 const delayLabel=element('label','Stall time until auto stop (seconds) '),delay=element('input');delay.type='number';delay.min='0';delay.max='60';delay.step='0.1';delay.value='0';delayLabel.append(delay);
 const sensitivityLabel=element('label','Stall sensitivity '),sensitivity=element('input'),sensitivityValue=element('span');sensitivity.type='range';sensitivity.min='0';sensitivity.max='100';sensitivity.step='1';sensitivity.value='35';sensitivityLabel.append(sensitivity,sensitivityValue);
 const apply=element('button','Save stall settings'),status=element('p'),note=element('p','0 seconds = off (default). Auto stop counts only continuous definite detections; free, uncertain or unknown readings reset its timer. Beeping follows a stall-like score even when marked uncertain. Three rapid, lower-pitched beeps repeat while that score stays high. Overheat alarms take priority. Settings are saved on the controller.');
 note.className=status.className='hint';status.setAttribute('role','status');
 panel.append(beepLabel,delayLabel,sensitivityLabel,apply,status,note);parent.append(panel);
 const a={panel,beep,delay,sensitivity,sensitivityLabel,sensitivityValue,apply,status,last:null,seen:0,busy:false,dirty:false,error:null};
 sensitivity.addEventListener('input',()=>{a.dirty=true;sensitivityValue.textContent=sensitivity.value+' / 100 · higher detects more easily';});
 beep.addEventListener('change',()=>{a.dirty=true;});delay.addEventListener('input',()=>{a.dirty=true;});
 apply.addEventListener('click',async()=>{
  const seconds=Number(delay.value),ms=Math.round(seconds*1000);
  if(delay.value.trim()===''||!Number.isFinite(seconds)||seconds<0||seconds>60){a.error='Choose 0–60 seconds; 0 turns auto stop off.';render(a);return;}
  a.busy=true;a.error=null;render(a);
  try{if(a.last.capabilities?.stall_sensitivity_supported)await a.send('set_stall_sensitivity',{sensitivity:Number(sensitivity.value)});await a.send('set_stall_response',{beeping_on:beep.checked,stop_ms:ms});a.dirty=false;await a.send('status');}
  catch(e){a.error=e.message;}
  finally{a.busy=false;render(a);}
 });return a;
}
function render(a){
 const d=a.last||{},supported=!!d.capabilities?.stall_response_supported;
 a.panel.hidden=!supported;if(!supported)return;
 const live=a.seen&&Date.now()-a.seen<1000&&!d.restarting;
 const stopped=!d.motor_enabled&&d.motion_mode==='stopped';
 a.sensitivityLabel.hidden=!d.capabilities?.stall_sensitivity_supported;
 a.sensitivity.disabled=a.beep.disabled=a.delay.disabled=a.apply.disabled=!live||!stopped||a.busy;
 if(!a.dirty){a.beep.checked=!!d.stall_beeping_on;a.delay.value=String((d.stall_stop_ms||0)/1000);a.sensitivity.value=String(d.stall_sensitivity??35);a.sensitivityValue.textContent=a.sensitivity.value+' / 100 · higher detects more easily';}
 a.status.textContent=a.error||(!live?'Waiting for live controller status.':!stopped?'Stop the motor to change these settings.':a.busy?'Saving…':a.dirty?'Unsaved changes.':`Saved · stall beeping ${d.stall_beeping_on?'on':'off'} · auto stop ${d.stall_stop_ms?'after '+d.stall_stop_ms/1000+' seconds':'off'}.`);
 if(live&&d.stall_beep_active)a.status.textContent+=' Stall warning beeping active.';
 if(live&&d.stall_active)a.status.textContent+=` Detected stall: ${(Number(d.stall_elapsed_ms||0)/1000).toFixed(1)} seconds.`;
}
const api={receive(axis,d,send){if(d.type!=='telemetry')return;let a=devices.get(axis);if(!a){if(!d.capabilities?.stall_response_supported)return;a=mount(axis);if(!a)return;devices.set(axis,a);}a.last=d.compact?{...a.last,...d}:d;a.seen=Date.now();a.send=send;render(a);},disconnect(axis){const a=devices.get(axis);if(a){a.seen=0;a.dirty=false;a.error=null;render(a);}}};
if(typeof module!=='undefined')module.exports=api;
if(root.document){root.StallControls=api;root.setInterval(()=>{for(const a of devices.values())render(a);},200);}
})(typeof window==='undefined'?globalThis:window);
