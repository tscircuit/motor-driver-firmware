'use strict';
const keys={ArrowLeft:['X',-1],ArrowRight:['X',1],ArrowDown:['Y',-1],ArrowUp:['Y',1]};
class Jogger {
 constructor(axes,report,clock=()=>Date.now(),keyMap=keys){this.keys=keyMap;this.axes=axes;this.report=report;this.clock=clock;this.held=new Set();this.states=Object.fromEntries(Object.keys(axes).map(k=>[k,{sent:null,busy:false,braking:false}]));this.stopsPending=0;this.returning=false;this.generation=0;this.update=()=>{};}
 get key(){return this.held.values().next().value||null;}
 get active(){return Object.keys(this.states).find(k=>this.states[k].sent||this.states[k].braking)||null;}
 get phase(){return Object.values(this.states).some(s=>s.sent)?'running':this.active?'braking':'idle';}
 direction(axis){let direction=0;for(const key of this.held)if(this.keys[key][0]===axis)direction+=this.keys[key][1];return Math.sign(direction);}
 unavailableReason(){
  for(const [label,a] of Object.entries(this.axes)){
   if(!a.port||!a.last)return `${label}: connect the board first.`;
   if(this.clock()-a.seen>=1000)return `${label}: waiting for fresh telemetry.`;
   if(a.last.protocol!==3||!a.last.capabilities?.resolutions?.half||a.last.capabilities?.fixed_ramp_ms!==400)return `${label}: update the fixed 400 ms ramp firmware.`;
   if(a.last.capabilities?.pd_voltages?.length&&!a.last.motor_power_ready)return `${label}: ${a.last.power_error||a.last.power_state||'request motor voltage and wait for settling.'}`;
   if(a.last.sensor_error)return `${label}: temperature sensor fault.`;
   if(a.last.driver_fault_asserted&&!(a.last.capabilities?.fault_stop_mode_supported&&a.last.stop_on_detected_fault===false))return `${label}: driver fault.`;
   if(a.last.thermal_alert||!Number.isFinite(a.last.temperature_c)||a.last.temperature_c>=(a.last.capabilities?.shutdown_c??75))return `${label}: temperature protection is active.`;
   if(a.last.restarting)return `${label}: board is restarting.`;
  }
  return null;
 }
 ready(){return this.unavailableReason()===null;}
 press(key){
  if(!this.keys[key]||this.held.has(key)||this.returning)return false;
  const reason=this.unavailableReason();if(reason){this.report(reason);return false;}
  const axis=this.keys[key][0],a=this.axes[axis];
  if(!a.last.motor_enabled&&a.last.temperature_c>=(a.last.capabilities?.start_below_c??60)){this.report(axis+': waiting for the board to cool before starting.');return false;}
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
 async tick(speed,invert,resolution='half'){
  if(this.returning||this.stopsPending)return;
  if(!this.key&&!this.active)return;
  if(!this.ready()){await this.stop(this.unavailableReason());return;}
  if(!Number.isFinite(speed)||speed<=0){await this.stop('Enter a positive speed.');return;}
  await Promise.all(Object.entries(this.axes).map(async([axis,a])=>{
   const state=this.states[axis];if(state.busy)return;
   const direction=this.direction(axis)*(invert[axis]?-1:1);
   const fields={resolution,direction,speed_sps:speed,ramp_ms:400};
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
 constructor(axes,jog,report,storage,pause=ms=>new Promise(r=>setTimeout(r,ms)),storageKey='gantry-position-slots-v1'){this.storageKey=storageKey;
  this.axes=axes;this.jog=jog;this.report=report;this.storage=storage;this.pause=pause;this.busy=false;this.slot=null;this.slots=[null,null,null];
  try{const data=JSON.parse(storage?.getItem(this.storageKey)||'null');if(Array.isArray(data)&&data.length===3)this.slots=data;}catch{}
 }
 hasCoordinates(){return this.jog.ready()&&Object.values(this.axes).every(a=>Number.isFinite(a.last.position_full_steps)&&typeof a.last.position_session==='string'&&a.last.position_session.length>0&&typeof a.last.device_id==='string');}
 idle(){return !this.busy&&!this.jog.key&&!this.jog.active&&!this.jog.stopsPending&&Object.values(this.axes).every(a=>!a.last?.motor_enabled);}
 valid(index){const slot=this.slots[index];return !!slot&&this.hasCoordinates()&&Object.entries(this.axes).every(([axis,a])=>slot[axis]?.device_id===a.last.device_id&&slot[axis]?.session===a.last.position_session&&Number.isFinite(slot[axis]?.position));}
 save(index){
  if(!Number.isInteger(index)||index<0||index>2)return false;
  if(!this.hasCoordinates()||!this.idle()){this.report('Stop the motor(s) before saving a position. Updated position firmware is required.');return false;}
  this.slots[index]=Object.fromEntries(Object.entries(this.axes).map(([axis,a])=>[axis,{device_id:a.last.device_id,session:a.last.position_session,position:a.last.position_full_steps}]));
  try{this.storage?.setItem(this.storageKey,JSON.stringify(this.slots));}catch{this.report('Position saved for this page; browser storage is unavailable.');return true;}
  this.report(`Saved position ${index+1}.`);return true;
 }
 async goto(index,speed,selectedResolution=null){
  if(!this.idle()||!this.valid(index)){this.report('Save a position in this board session and stop the motor(s) before GOTO.');return false;}
  this.busy=true;this.slot=index;
  const generation=++this.jog.generation;this.jog.returning=true;
  try{
   if(generation!==this.jog.generation)return false;
   if(!this.valid(index))throw Error('Board coordinates changed. Save the position again.');
   const plan=Object.entries(this.axes).map(([axis,a])=>{
    const c=a.last.capabilities,[resolution,factor]=Object.entries(c.resolutions).reduce((best,item)=>item[1]>best[1]?item:best,['half',2]),delta=(this.slots[index][axis].position-a.last.position_full_steps)*factor;
    if(!Number.isSafeInteger(delta)||Math.abs(delta)>(c.max_steps??100000))throw Error('Saved position is outside the board’s supported step count.');
    if(!Number.isFinite(speed)||speed<=0)throw Error('Choose a positive target speed.');
    const selectedFactor=selectedResolution===null?null:c.resolutions[selectedResolution];
    if(selectedResolution!==null&&!(selectedFactor>0))throw Error('Selected step size is unavailable on this board.');
    return {axis,a,delta,resolution,factor,selectedFactor};
   });
   const moving=plan.filter(({delta})=>delta!==0);
   if(moving.some(({a})=>a.last.temperature_c>=(a.last.capabilities.start_below_c??60)))throw Error('Wait for the boards to cool before starting GOTO.');
   if(moving.some(({a})=>!a.last.capabilities.deferred_start_supported))throw Error('Update the board firmware for GOTO.');
   const longest=Math.max(1,...moving.map(({delta})=>Math.abs(delta)));
   if(selectedResolution===null){
    for(const move of moving)move.speed=speed*Math.abs(move.delta)/longest;
   }else{
    const duration=Math.max(...moving.map(move=>Math.abs(move.delta)/move.factor/(speed/move.selectedFactor)));
    for(const move of moving)move.speed=Math.abs(move.delta)/duration;
   }
   this.report(`GOTO ${index+1}: moving…`);
   // Dispatch to independent USB queues together, before waiting for either axis.
   await Promise.all(moving.map(async({a,delta,speed,resolution})=>{
    await a.send('heartbeat',{},false);
    if(generation!==this.jog.generation)return;
    if(!this.valid(index))throw Error(this.jog.unavailableReason()||'Saved position reference is no longer valid.');
    await a.send('start',{mode:'steps',resolution,direction:Math.sign(delta),steps:Math.abs(delta),speed_sps:speed,ramp_ms:400,defer:true});
   }));
   if(generation!==this.jog.generation)return false;
   await Promise.all(moving.map(async({a})=>{
    if(generation!==this.jog.generation)return;
    await a.send('run_move');
   }));
   if(generation!==this.jog.generation)return false;
   // One fresh snapshot establishes that each prepared move has started.
   await Promise.all(moving.map(({a})=>a.send('status')));
   const pending=new Set(moving);
   while(pending.size){
    await Promise.all([...pending].map(async(move)=>{
     const {axis,a}=move;
     if(generation!==this.jog.generation)return;
     if(!this.valid(index))throw Error(this.jog.unavailableReason()||'Saved position reference is no longer valid.');
     if(!a.last.motor_enabled){
      if(a.last.stop_reason!=='Step move complete')throw Error(`${axis} stopped: ${a.last.stop_reason}`);
      if(a.last.position_full_steps!==this.slots[index][axis].position)throw Error(`${axis}: commanded position did not match the saved target.`);
      pending.delete(move);
     }
    }));
    if(generation!==this.jog.generation)return false;
    if(pending.size)await this.pause(50);
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
  receive(line){let d;try{d=JSON.parse(line);}catch{return;}if(typeof FirmwareStatus!=='undefined')FirmwareStatus.receive(this.label,d);if(typeof CurrentCharts!=='undefined')CurrentCharts.receive(this.label,d);if(typeof BoardControls!=='undefined')BoardControls.receive(this.label,d,(cmd,fields)=>this.send(cmd,fields));if(d.type==='current_samples')return;if(d.type==='ack'){const p=this.pending.get(d.id);if(p){clearTimeout(p.timer);this.pending.delete(d.id);d.ok?p.resolve(d):p.reject(Error(d.error||'Rejected'));}}else if(d.type==='telemetry'){if(d.compact)d={...this.last,...d};this.last=d;this.seen=Date.now();$('status'+this.label).textContent=`${d.device_name} · ${d.device_id}\n${d.temperature_c??'—'} °C · ${d.motion_mode==='braking'?'Slowing down':d.hardware_motion_inhibited?'Hardware interlock active (commanded steps only)':d.motor_enabled?'Moving':'Stopped'} · ${Number(d.profile_speed_sps||0).toFixed(1)} ${stepUnit(d.step_resolution||'half')}/sec\n${d.sensor_error||d.reported_fault||(d.stall_suspected?'Stall suspected':d.driver_fault_asserted?'Driver fault':d.stop_reason)||''}${Number.isFinite(d.position_full_steps)?'\n'+(d.capabilities?.stall_detection_supported?'Commanded position: ':'Position: ')+d.position_full_steps+' full steps':''}`;}}
  async connect(){await jog.stop();try{const port=await navigator.serial.requestPort();if(Object.values(axes).some(a=>a.port===port))throw Error('Choose a different board for each axis.');await port.open({baudRate:115200});this.port=port;if(typeof CurrentCharts!=='undefined')CurrentCharts.reset(this.label);this.last=null;this.seen=0;await port.setSignals({dataTerminalReady:true,requestToSend:false});this.reader=port.readable.getReader();this.reading=this.read();await this.send('stop');await this.send('status');report('Connected '+this.label+'.');}catch(e){report(e.message);await this.close();}}
  async read(){let buffer='';const decoder=new TextDecoder();try{while(this.port){const {value,done}=await this.reader.read();if(done)break;buffer+=decoder.decode(value,{stream:true});let i;while((i=buffer.indexOf('\n'))>=0){this.receive(buffer.slice(0,i));buffer=buffer.slice(i+1);}if(buffer.length>8192)buffer='';}}catch(e){if(!this.closing)report(e.message);}finally{this.reader.releaseLock();this.reader=null;if(!this.closing){void jog.stop();void this.close();}}}
  async close(){if(this.closing)return;this.closing=true;try{if(this.reader)await this.reader.cancel();if(this.reading)await this.reading;await this.chain.catch(()=>{});if(this.port)await this.port.close();}catch{}finally{this.port=null;if(typeof BoardControls!=='undefined')BoardControls.disconnect(this.label);if(typeof FirmwareStatus!=='undefined')FirmwareStatus.disconnect(this.label);if(typeof CurrentCharts!=='undefined')CurrentCharts.disconnect(this.label);this.last=null;this.seen=0;this.closing=false;for(const p of this.pending.values()){clearTimeout(p.timer);p.reject(Error('Disconnected'));}this.pending.clear();$('status'+this.label).textContent='Disconnected';}}
 }
 const single=document.body.dataset.singleMotor==='true';
 const inputKeys=single?{ArrowLeft:['X',-1],ArrowRight:['X',1],ArrowDown:['X',-1],ArrowUp:['X',1]}:keys;
 const labels=single?['X']:['X','Y'];
 const axes=Object.fromEntries(labels.map(label=>[label,new Axis(label)]));const jog=new Jogger(axes,report,()=>Date.now(),inputKeys);let storage;try{storage=window.localStorage;}catch{}const positions=new PositionSlots(axes,jog,report,storage,undefined,single?'single-motor-position-slots-v1':'gantry-position-slots-v1');
 for(let i=0;i<3;i++){$('savePosition'+i).onclick=()=>positions.save(i);$('gotoPosition'+i).onclick=()=>void positions.goto(i,Number($('jogSpeed').value),single?$('jogResolution').value:null);}
 for(const label of labels){
  $('setLed'+label).onclick=async()=>{try{await axes[label].send('set_led',{color:$('led'+label).value});await axes[label].send('status');}catch(e){report(e.message);}};
  $('setPd'+label).onclick=async()=>{try{await axes[label].send('set_pd_voltage',{voltage:Number($('pd'+label).value)});await axes[label].send('status');report(axes[label].last?.capabilities?.power_arm_required?'Voltage requested. Use Enable motor power to connect the rail.':'Voltage requested. Wait two seconds for settling.');}catch(e){report(e.message);}};
  $('connect'+label).onclick=async()=>{if(axes[label].port){await jog.stop();await axes[label].close();}else await axes[label].connect();};
  $('beep'+label).onclick=async()=>{try{await axes[label].send('beep',{duration_ms:2000});report(label+': requested a 2-second test tone.');}catch(e){report(e.message);}};
  $('rename'+label).onclick=async()=>{await jog.stop();const name=$('name'+label).value.trim();if(!/^[\x20-\x7e]{1,32}$/.test(name)){report('Use 1–32 printable ASCII characters.');return;}try{const ack=await axes[label].send('set_device_name',{name});report('Saved '+ack.device_name+'. Reconnect using the new USB name.');await axes[label].close();}catch(e){report(e.message);}};
 }
 $('stopAll').onclick=()=>void jog.stop('Stopped. Press a fresh arrow key to jog again.');
 const editing=e=>['SELECT','TEXTAREA'].includes(e.target.tagName)||(e.target.tagName==='INPUT'&&e.target.type!=='checkbox')||e.target.isContentEditable;
 document.addEventListener('focusin',e=>{if(editing(e)&&(jog.key||jog.active||jog.returning))void jog.stop('Paused while editing settings. Press an arrow after editing to jog.');});
 document.addEventListener('keydown',e=>{if(e.key==='Escape'||(e.code==='Space'&&!editing(e))){e.preventDefault();void jog.stop();return;}if(!inputKeys[e.key]||(['SELECT','TEXTAREA'].includes(e.target.tagName)||(e.target.tagName==='INPUT'&&e.target.type!=='checkbox'))||e.target.isContentEditable)return;e.preventDefault();if(e.repeat)return;jog.press(e.key);});
 document.addEventListener('keyup',e=>{if(inputKeys[e.key]){e.preventDefault();void jog.release(e.key);}});
 window.addEventListener('blur',()=>void jog.stop('Paused because the page lost focus. Press an arrow when you return.'));window.addEventListener('pagehide',()=>void jog.stop());document.addEventListener('visibilitychange',()=>{if(document.hidden)void jog.stop('Paused because the page is hidden. Press an arrow when you return.');});
 jog.update=()=>{render();void jog.tick(Number($('jogSpeed').value),Object.fromEntries(labels.map(label=>[label,$('invert'+label).checked])),$('jogResolution').value);};
 for(const id of ['jogSpeed','jogResolution',...labels.map(label=>'invert'+label)])$(id).onchange=()=>{if(id==='jogResolution')$(id).blur();jog.update();};
 setInterval(jog.update,16);
 setInterval(()=>{for(const a of Object.values(axes))if(a.port&&(jog.key||jog.active||jog.returning)&&!document.hidden&&Date.now()-a.seen<1000)void a.send('heartbeat',{},false);},400);
 for(const button of document.querySelectorAll('[data-arrow]')){button.addEventListener('pointerdown',e=>{e.preventDefault();if(jog.press(button.dataset.arrow))button.setPointerCapture(e.pointerId);});button.addEventListener('pointerup',()=>void jog.release(button.dataset.arrow));button.addEventListener('pointercancel',()=>void jog.stop());button.addEventListener('lostpointercapture',()=>void jog.release(button.dataset.arrow));}
 const stepNames={full:'Full step',half:'Half step (1/2)',quarter:'Quarter step (1/4)',eighth:'Microstep (1/8)',sixteenth:'Microstep (1/16)'};
 const stepName=name=>stepNames[name]||name;
 const stepUnit=name=>({full:'full steps',half:'half steps',quarter:'quarter steps',eighth:'1/8 microsteps',sixteenth:'1/16 microsteps'}[name]||name+' steps');
 const stepAngle=(factor,fullSteps=200)=>Number((360/(fullSteps*factor)).toFixed(4));
 let resolutionKey='';
 function renderHardware(label){
  const a=axes[label],c=a.last?.capabilities||{},key=JSON.stringify([c.led_colors,c.pd_voltages]);
  $('hardware'+label).hidden=!c.led_colors?.length&&!c.pd_voltages?.length;
  if(a.hardwareKey!==key){
   const fill=(id,values,text)=>$(id+label).replaceChildren(...values.map(v=>{const o=document.createElement('option');o.value=v;o.textContent=text(v);return o;}));
   fill('led',c.led_colors?.length?['auto',...c.led_colors]:[],v=>v==='auto'?'Automatic status':v[0].toUpperCase()+v.slice(1));
   fill('pd',c.pd_voltages||[],v=>`${v} V${v===5?' · motor disabled':''}`);
   $('pd'+label).value=a.last?.pd_voltage_requested||5;
   $('led'+label).value=a.last?.led_selection||'auto';
   a.hardwareKey=key;
  }
  const live=!!a.port&&Date.now()-a.seen<1000&&!a.last?.restarting;
  $('setLed'+label).disabled=$('led'+label).disabled=!live||!c.led_colors?.length;
  $('setPd'+label).disabled=$('pd'+label).disabled=!live||!!a.last?.motor_enabled||jog.returning||!c.pd_voltages?.length;
  $('hardwareStatus'+label).textContent=!live?'Connect for live board controls.':`LED: ${a.last?.led_color||'—'} · ${c.power_arm_required&&!a.last?.pd_request_sent?'No software voltage request sent':'Requested supply: '+(a.last?.pd_voltage_requested??'—')+' V'} · ${a.last?.motor_power_ready?'ready to request motion':a.last?.pd_voltage_requested===5?'choose motor voltage':c.power_arm_required?'motor power off':'waiting for settling'}`;
 }
 function renderStepSizes(){
  const connected=Object.values(axes).filter(a=>a.last&&a.port),select=$('jogResolution');
  select.disabled=connected.length!==labels.length||connected.some(a=>Date.now()-a.seen>=1000)||jog.returning;
  if(connected.length){
   const fullSteps=connected[0].last.capabilities?.full_steps_per_revolution||200;
   const modes=Object.entries(connected[0].last.capabilities?.resolutions||{}).filter(([mode,factor])=>connected.every(a=>a.last.capabilities?.resolutions?.[mode]===factor)).sort((a,b)=>a[1]-b[1]);
   const key=JSON.stringify([modes,fullSteps]);
   if(key!==resolutionKey){
    const selected=select.value;
    select.replaceChildren(...modes.map(([mode,factor])=>{const o=document.createElement('option');o.value=mode;o.textContent=`${stepName(mode)} · ${stepAngle(factor,fullSteps)}°`;return o;}));
    select.value=modes.some(([mode])=>mode===selected)?selected:modes[0]?.[0]||'';resolutionKey=key;
   }
  }
  if(single){
   const a=axes.X,c=a.last?.capabilities||{},mode=select.value,factor=c.resolutions?.[mode],fullSteps=c.full_steps_per_revolution||200,speed=Number($('jogSpeed').value);
   $('jogSpeedLabel').textContent=`Target ${stepUnit(mode)}/sec`;
   $('jogSpeedEstimate').textContent=factor&&Number.isFinite(speed)&&speed>0?`${stepAngle(factor,fullSteps)}° per selected step · ${(speed/(fullSteps*factor)*60).toFixed(2)} nominal RPM · ${fullSteps*factor} selected steps per revolution`:'Connect to read supported step sizes and motor geometry.';
   $('stepNote').textContent=c.step_note||'Full, half, quarter and microstep options are shown when supported by the connected board.';
   const live=!!a.port&&Date.now()-a.seen<1000,values=a.last?.winding_current_a;
   $('windingCurrent').textContent=!live?'Connect for live current readings':Array.isArray(values)&&values.length===2&&values.every(Number.isFinite)?`A: ${values[0].toFixed(2)} A · B: ${values[1].toFixed(2)} A`:a.last?.current_available&&Number.isFinite(a.last.current_a)?`${a.last.current_a.toFixed(2)} A`:'Current readings unavailable on this board';
   $('currentNote').textContent=c.current_note||'Current availability depends on the connected board.';
   const voltage=a.last?.pd_voltage_measured,measured=live&&Number.isFinite(voltage)&&voltage>=0;
   $('pdVoltageMeasured').textContent=measured?`${voltage.toFixed(2)} V`:'Unavailable';
   $('pdVoltageNote').textContent=measured?'Live measurement reported by the connected board.':'This board does not report measured PD voltage. Use GND → PD with a multimeter; the requested voltage is not a measurement.';
  }
 }
 function render(){
  renderStepSizes();
  for(const button of document.querySelectorAll('[data-arrow]')){button.disabled=!jog.ready()||jog.stopsPending>0||jog.returning;button.classList.toggle('engaged',jog.held.has(button.dataset.arrow));button.classList.toggle('coasting',jog.states[inputKeys[button.dataset.arrow][0]].braking);}
  for(let i=0;i<3;i++){const slot=positions.slots[i];$('savePosition'+i).disabled=!positions.hasCoordinates()||!positions.idle();$('gotoPosition'+i).disabled=!positions.valid(i)||!positions.idle();$('positionValue'+i).textContent=!slot?'Not saved':positions.hasCoordinates()&&!positions.valid(i)?'Previous board session · save again':labels.map(label=>`${single?'Motor':label} ${Number(slot[label]?.position).toFixed(4)}`).join(' · ')+' full steps';}
  $('jogPhase').textContent=jog.returning?`GOTO position ${positions.slot+1}…`:jog.phase==='braking'?'Slowing down…':jog.active?'Accelerating / jogging':jog.stopsPending?'Stopping…':jog.ready()?'Ready · press an arrow':jog.unavailableReason();
  $('brakeEstimate').textContent=`Release can add up to ${Math.max(1,Math.ceil(Number($('jogSpeed').value)*0.2))} ${stepUnit($('jogResolution').value)} of deceleration, plus USB/input latency. Space/Escape stops immediately.`;
  for(const label of labels){renderHardware(label);const a=axes[label];$('connect'+label).textContent=a.port?'Disconnect '+(single?'motor':label):'Connect '+(single?'motor':label);$('beep'+label).disabled=!a.port||Date.now()-a.seen>1000;$('rename'+label).disabled=!a.port||Date.now()-a.seen>1000||!a.last?.usb_name_supported||a.last?.motor_enabled;}
 }
 render();
 if(!navigator.serial)report('Open in desktop Chrome or Edge with Web Serial support.');
}
