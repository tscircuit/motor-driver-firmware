'use strict';
const keys={ArrowLeft:['X',-1],ArrowRight:['X',1],ArrowDown:['Y',-1],ArrowUp:['Y',1]};
class Jogger {
 constructor(axes,report,clock=()=>Date.now()){this.axes=axes;this.report=report;this.clock=clock;this.held=new Set();this.states=Object.fromEntries(Object.keys(axes).map(k=>[k,{sent:null,busy:false,braking:false}]));this.stopsPending=0;this.returning=false;this.generation=0;this.update=()=>{};}
 get key(){return this.held.values().next().value||null;}
 get active(){return Object.keys(this.states).find(k=>this.states[k].sent||this.states[k].braking)||null;}
 get phase(){return Object.values(this.states).some(s=>s.sent)?'running':this.active?'braking':'idle';}
 direction(axis){let direction=0;for(const key of this.held)if(keys[key][0]===axis)direction+=keys[key][1];return Math.sign(direction);}
 unavailableReason(){
  for(const [label,a] of Object.entries(this.axes)){
   if(!a.port||!a.last)return `${label}: connect the board first.`;
   if(this.clock()-a.seen>=1000)return `${label}: waiting for fresh telemetry.`;
   if(a.last.protocol!==3||a.last.capabilities?.resolutions?.half!==2||!a.last.capabilities?.jog_update_supported)return `${label}: update the responsive jog firmware.`;
   if(a.last.sensor_error)return `${label}: temperature sensor fault.`;
   if(a.last.driver_fault_asserted)return `${label}: driver fault.`;
   if(a.last.thermal_alert||a.last.alarm_active||!Number.isFinite(a.last.temperature_c)||a.last.temperature_c>=(a.last.capabilities?.start_below_c??60))return `${label}: temperature protection is active.`;
   if(a.last.restarting)return `${label}: board is restarting.`;
  }
  return null;
 }
 ready(){return this.unavailableReason()===null;}
 press(key){
  if(!keys[key]||this.held.has(key)||this.returning)return false;
  const reason=this.unavailableReason();if(reason){this.report(reason);return false;}
  this.held.add(key);this.update();return true;
 }
 async stop(reason=''){
  this.held.clear();this.returning=false;this.generation++;this.stopsPending++;
  for(const state of Object.values(this.states)){state.sent=null;state.braking=false;}
  if(reason)this.report(reason);
  try{await Promise.all(Object.values(this.axes).map(a=>a.port?a.send('stop').then(()=>a.send('status')).catch(e=>this.report('Stop not acknowledged: '+e.message)):null));}
  finally{this.stopsPending--;if(this.held.size)this.update();}
 }
 async release(key){this.held.delete(key);this.update();}
 async tick(speed,invert,acceleration=100,resolution='half'){
  if(this.returning||this.stopsPending)return;
  if(!this.key&&!this.active)return;
  if(!this.ready()){await this.stop(this.unavailableReason());return;}
  if(!Number.isFinite(speed)||speed<=0||!Number.isFinite(acceleration)||acceleration<10||acceleration>1000){await this.stop('Enter a positive speed and supported acceleration.');return;}
  await Promise.all(Object.entries(this.axes).map(async([axis,a])=>{
   const state=this.states[axis];if(state.busy)return;
   const direction=this.direction(axis)*(invert[axis]?-1:1);
   const fields={resolution,direction,speed_sps:speed,acceleration_sps2:acceleration};
   const desired=direction?JSON.stringify(fields):null;
   if(desired===state.sent){if(!desired&&!a.last.motor_enabled)state.braking=false;return;}
   state.busy=true;const generation=this.generation;
   try{
    if(direction){
     await a.send('heartbeat',{},false);
     if(generation!==this.generation)return;
     // Re-read held input after any queued USB writes. Each axis has its own queue.
     if(this.direction(axis)*(invert[axis]?-1:1)!==direction)return;
     await a.send('jog',fields);if(generation===this.generation){state.sent=desired;state.braking=false;}
    }else{
     await a.send('decelerate');await a.send('status');if(generation===this.generation){state.sent=null;state.braking=!!a.last.motor_enabled;}
    }
   }catch(e){if(generation===this.generation)await this.stop(e.message);}
   finally{state.busy=false;}
  }));
 }

}
class PositionSlots {
 constructor(axes,jog,report,storage,pause=ms=>new Promise(r=>setTimeout(r,ms))){
  this.axes=axes;this.jog=jog;this.report=report;this.storage=storage;this.pause=pause;this.busy=false;this.slot=null;this.slots=[null,null,null];
  try{const data=JSON.parse(storage?.getItem('gantry-position-slots-v1')||'null');if(Array.isArray(data)&&data.length===3)this.slots=data;}catch{}
 }
 hasCoordinates(){return this.jog.ready()&&Object.values(this.axes).every(a=>Number.isFinite(a.last.position_full_steps)&&typeof a.last.position_session==='string'&&a.last.position_session.length>0&&typeof a.last.device_id==='string');}
 idle(){return !this.busy&&!this.jog.key&&!this.jog.active&&!this.jog.stopsPending&&Object.values(this.axes).every(a=>!a.last?.motor_enabled);}
 valid(index){const slot=this.slots[index];return !!slot&&this.hasCoordinates()&&Object.entries(this.axes).every(([axis,a])=>slot[axis]?.device_id===a.last.device_id&&slot[axis]?.session===a.last.position_session&&Number.isFinite(slot[axis]?.position));}
 save(index){
  if(!Number.isInteger(index)||index<0||index>2)return false;
  if(!this.hasCoordinates()||!this.idle()){this.report('Stop both axes before saving a position. Updated position firmware is required.');return false;}
  this.slots[index]=Object.fromEntries(Object.entries(this.axes).map(([axis,a])=>[axis,{device_id:a.last.device_id,session:a.last.position_session,position:a.last.position_full_steps}]));
  try{this.storage?.setItem('gantry-position-slots-v1',JSON.stringify(this.slots));}catch{this.report('Position saved for this page; browser storage is unavailable.');return true;}
  this.report(`Saved position ${index+1}.`);return true;
 }
 async goto(index,speed,acceleration){
  if(!this.idle()||!this.valid(index)){this.report('Save a position in this board session and stop both axes before GOTO.');return false;}
  this.busy=true;this.slot=index;
  const preparation=this.jog.stop();const generation=this.jog.generation;this.jog.returning=true;
  try{
   await preparation;if(generation!==this.jog.generation)return false;
   if(!this.valid(index))throw Error('Board coordinates changed. Save the position again.');
   const plan=Object.entries(this.axes).map(([axis,a])=>{
    const c=a.last.capabilities,delta=(this.slots[index][axis].position-a.last.position_full_steps)*2;
    if(!Number.isSafeInteger(delta)||Math.abs(delta)>(c.max_steps??100000))throw Error('Saved position is outside the board’s supported step count.');
    if(!Number.isFinite(speed)||speed<=0||!Number.isFinite(acceleration)||acceleration<(c.min_acceleration_sps2??10)||acceleration>(c.max_acceleration_sps2??1000))throw Error('Choose supported speed and acceleration settings.');
    return {axis,a,delta};
   });
   for(const {axis,a,delta} of plan){
    if(!delta)continue;
    if(generation!==this.jog.generation)return false;
    if(!this.valid(index))throw Error(this.jog.unavailableReason()||'Saved position reference is no longer valid.');
    this.report(`GOTO ${index+1}: moving ${axis}…`);
    await a.send('heartbeat',{},false);
    if(generation!==this.jog.generation)return false;
    await a.send('start',{mode:'steps',resolution:'half',direction:Math.sign(delta),steps:Math.abs(delta),speed_sps:speed,acceleration_sps2:acceleration});
    while(generation===this.jog.generation){
     await a.send('status');
     if(generation!==this.jog.generation)return false;
     if(!this.valid(index))throw Error(this.jog.unavailableReason()||'Saved position reference is no longer valid.');
     if(!a.last.motor_enabled){if(a.last.stop_reason!=='Step move complete')throw Error(`${axis} stopped: ${a.last.stop_reason}`);break;}
     await this.pause(100);
    }
    if(generation!==this.jog.generation)return false;
    if(a.last.position_full_steps!==this.slots[index][axis].position)throw Error(`${axis}: commanded position did not match the saved target.`);
   }
   this.report(`At saved position ${index+1}.`);return true;
  }catch(e){this.report(e.message);if(generation===this.jog.generation)await this.jog.stop();return false;}
  finally{this.busy=false;this.slot=null;this.jog.returning=false;}
 }
}
if(typeof module!=='undefined')module.exports={Jogger,PositionSlots,keys};
if(typeof document!=='undefined'){
 const $=id=>document.getElementById(id),report=t=>{$('message').textContent=t;};
 class Axis {
  constructor(label){this.label=label;this.port=null;this.last=null;this.seen=0;this.pending=new Map();this.nextId=1;this.chain=Promise.resolve();this.closing=false;}
  send(cmd,fields={},ack=true){if(!this.port?.writable)return Promise.reject(Error(this.label+' disconnected'));const target=this.port,id=this.nextId++;let result=Promise.resolve();if(ack)result=new Promise((resolve,reject)=>{const timer=setTimeout(()=>{this.pending.delete(id);reject(Error(this.label+' command timed out'));},1500);this.pending.set(id,{resolve,reject,timer});});this.chain=this.chain.catch(()=>{}).then(async()=>{const w=target.writable.getWriter();try{await w.write(new TextEncoder().encode(JSON.stringify({cmd,id,...fields})+'\n'));}finally{w.releaseLock();}}).catch(e=>{const p=this.pending.get(id);if(p){clearTimeout(p.timer);this.pending.delete(id);p.reject(e);}else report(e.message);});return ack?result:this.chain;}
  receive(line){let d;try{d=JSON.parse(line);}catch{return;}if(d.type==='ack'){const p=this.pending.get(d.id);if(p){clearTimeout(p.timer);this.pending.delete(d.id);d.ok?p.resolve(d):p.reject(Error(d.error||'Rejected'));}}else if(d.type==='telemetry'){this.last=d;this.seen=Date.now();$('status'+this.label).textContent=`${d.device_name} · ${d.device_id}\n${d.temperature_c??'—'} °C · ${d.motion_mode==='braking'?'Slowing down':d.motor_enabled?'Moving':'Stopped'} · ${Number(d.profile_speed_sps||0).toFixed(1)} ${d.resolution||'half'} steps/sec\n${d.sensor_error|| (d.driver_fault_asserted?'Driver fault':d.stop_reason)||''}${Number.isFinite(d.position_full_steps)?'\nPosition: '+(d.position_full_steps*2)+' half steps':''}`;}}
  async connect(){await jog.stop();try{const port=await navigator.serial.requestPort();if(Object.values(axes).some(a=>a.port===port))throw Error('Choose a different board for each axis.');await port.open({baudRate:115200});this.port=port;this.last=null;this.seen=0;await port.setSignals({dataTerminalReady:true,requestToSend:false});this.reader=port.readable.getReader();this.reading=this.read();await this.send('stop');await this.send('status');report('Connected '+this.label+'.');}catch(e){report(e.message);await this.close();}}
  async read(){let buffer='';const decoder=new TextDecoder();try{while(this.port){const {value,done}=await this.reader.read();if(done)break;buffer+=decoder.decode(value,{stream:true});let i;while((i=buffer.indexOf('\n'))>=0){this.receive(buffer.slice(0,i));buffer=buffer.slice(i+1);}if(buffer.length>8192)buffer='';}}catch(e){if(!this.closing)report(e.message);}finally{this.reader.releaseLock();this.reader=null;if(!this.closing){void jog.stop();void this.close();}}}
  async close(){if(this.closing)return;this.closing=true;try{if(this.reader)await this.reader.cancel();if(this.reading)await this.reading;await this.chain.catch(()=>{});if(this.port)await this.port.close();}catch{}finally{this.port=null;this.last=null;this.seen=0;this.closing=false;for(const p of this.pending.values()){clearTimeout(p.timer);p.reject(Error('Disconnected'));}this.pending.clear();$('status'+this.label).textContent='Disconnected';}}
 }
 const axes={X:new Axis('X'),Y:new Axis('Y')};const jog=new Jogger(axes,report);let storage;try{storage=window.localStorage;}catch{}const positions=new PositionSlots(axes,jog,report,storage);
 for(let i=0;i<3;i++){$('savePosition'+i).onclick=()=>positions.save(i);$('gotoPosition'+i).onclick=()=>void positions.goto(i,Number($('jogSpeed').value),Number($('jogAcceleration').value));}
 for(const label of ['X','Y']){
  $('connect'+label).onclick=async()=>{if(axes[label].port){await jog.stop();await axes[label].close();}else await axes[label].connect();};
  $('rename'+label).onclick=async()=>{await jog.stop();const name=$('name'+label).value.trim();if(!/^[\x20-\x7e]{1,32}$/.test(name)){report('Use 1–32 printable ASCII characters.');return;}try{const ack=await axes[label].send('set_device_name',{name});report('Saved '+ack.device_name+'. Reconnect using the new USB name.');await axes[label].close();}catch(e){report(e.message);}};
 }
 $('stopAll').onclick=()=>void jog.stop('Stopped. Press a fresh arrow key to jog again.');
 const editing=e=>['SELECT','TEXTAREA'].includes(e.target.tagName)||(e.target.tagName==='INPUT'&&e.target.type!=='checkbox')||e.target.isContentEditable;
 document.addEventListener('focusin',e=>{if(editing(e)&&(jog.key||jog.active||jog.returning))void jog.stop('Paused while editing settings. Press an arrow after editing to jog.');});
 document.addEventListener('keydown',e=>{if(e.key==='Escape'||(e.code==='Space'&&!editing(e))){e.preventDefault();void jog.stop();return;}if(!keys[e.key]||(['SELECT','TEXTAREA'].includes(e.target.tagName)||(e.target.tagName==='INPUT'&&e.target.type!=='checkbox'))||e.target.isContentEditable)return;e.preventDefault();if(e.repeat)return;jog.press(e.key);});
 document.addEventListener('keyup',e=>{if(keys[e.key]){e.preventDefault();void jog.release(e.key);}});
 window.addEventListener('blur',()=>void jog.stop('Paused because the page lost focus. Press an arrow when you return.'));window.addEventListener('pagehide',()=>void jog.stop());document.addEventListener('visibilitychange',()=>{if(document.hidden)void jog.stop('Paused because the page is hidden. Press an arrow when you return.');});
 jog.update=()=>{void jog.tick(Number($('jogSpeed').value),{X:$('invertX').checked,Y:$('invertY').checked},Number($('jogAcceleration').value),$('jogResolution').value);render();};
 for(const id of ['jogSpeed','jogAcceleration','jogResolution','invertX','invertY'])$(id).onchange=()=>{if(id==='jogResolution')$(id).blur();jog.update();};
 setInterval(jog.update,16);
 setInterval(()=>{for(const a of Object.values(axes))if(a.port&&(jog.key||jog.active||jog.returning)&&!document.hidden&&Date.now()-a.seen<1000)void a.send('heartbeat',{},false);},400);
 for(const button of document.querySelectorAll('[data-arrow]')){button.addEventListener('pointerdown',e=>{e.preventDefault();if(jog.press(button.dataset.arrow))button.setPointerCapture(e.pointerId);});button.addEventListener('pointerup',()=>void jog.release(button.dataset.arrow));button.addEventListener('pointercancel',()=>void jog.stop());button.addEventListener('lostpointercapture',()=>void jog.release(button.dataset.arrow));}
 function render(){for(const button of document.querySelectorAll('[data-arrow]')){button.disabled=!jog.ready()||jog.stopsPending>0||jog.returning;button.classList.toggle('engaged',jog.held.has(button.dataset.arrow));button.classList.toggle('coasting',jog.states[keys[button.dataset.arrow][0]].braking);}for(let i=0;i<3;i++){const slot=positions.slots[i];$('savePosition'+i).disabled=!positions.hasCoordinates()||!positions.idle();$('gotoPosition'+i).disabled=!positions.valid(i)||!positions.idle();$('positionValue'+i).textContent=!slot?'Not saved':positions.hasCoordinates()&&!positions.valid(i)?'Previous board session · save again':`X ${Number(slot.X?.position*2).toFixed(0)} · Y ${Number(slot.Y?.position*2).toFixed(0)} half steps`;}$('jogPhase').textContent=jog.returning?`GOTO position ${positions.slot+1}…`:jog.phase==='braking'?'Slowing down…':jog.active?'Accelerating / jogging':jog.stopsPending?'Stopping…':jog.ready()?'Ready · press an arrow':jog.unavailableReason();$('brakeEstimate').textContent=`Release can add up to ${Math.max(1,Math.ceil(Number($('jogSpeed').value)**2/(2*Number($('jogAcceleration').value))))} ${$('jogResolution').value} steps of deceleration, plus USB/input latency. Space/Escape stops immediately.`;for(const label of ['X','Y']){const a=axes[label];$('connect'+label).textContent=a.port?'Disconnect '+label:'Connect '+label;$('rename'+label).disabled=!a.port||Date.now()-a.seen>1000||!a.last?.usb_name_supported||a.last?.motor_enabled;}}
 render();
 if(!navigator.serial)report('Open in desktop Chrome or Edge with Web Serial support.');
}
