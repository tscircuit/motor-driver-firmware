'use strict';
const keys={ArrowLeft:['X',-1],ArrowRight:['X',1],ArrowDown:['Y',-1],ArrowUp:['Y',1]};
class Jogger {
 constructor(axes,report,clock=()=>Date.now()){this.axes=axes;this.report=report;this.clock=clock;this.armed=false;this.key=null;this.active=null;this.phase='idle';this.moving=false;this.started=0;this.released=0;this.generation=0;}
 ready(){return Object.values(this.axes).every(a=>a.port&&a.last&&this.clock()-a.seen<1000&&a.last.protocol===3&&a.last.capabilities?.resolutions?.half===2&&a.last.capabilities?.deceleration_supported&&!a.last.sensor_error&&!a.last.driver_fault_asserted&&!a.last.thermal_alert&&!a.last.alarm_active&&!a.last.restarting&&Number.isFinite(a.last.temperature_c)&&a.last.temperature_c<(a.last.capabilities?.start_below_c??60));}
 press(key){if(!keys[key]||!this.armed||this.key||this.active)return false;this.key=key;return true;}
 async stop(){this.armed=false;this.key=null;this.active=null;this.phase='idle';this.moving=false;this.generation++;await Promise.all(Object.values(this.axes).map(a=>a.port?a.send('stop').catch(e=>this.report('Stop not acknowledged: '+e.message)):null));}
 async release(key){
  if(this.key!==key)return;
  this.key=null;this.released=this.clock();
  if(!this.active)return;
  this.phase='braking';
  try{await this.axes[this.active].send('decelerate');}
  catch(e){this.report(e.message);await this.stop();}
 }
 async tick(speed,invert,acceleration=100){
  if(!this.armed)return;
  if(!this.ready()){this.report('Fresh half-step/braking firmware and healthy telemetry are required; jogging disabled.');await this.stop();return;}
  if(this.active){
   const a=this.axes[this.active];
   if(a.last.motor_enabled){this.moving=true;if(this.phase!=='braking')this.phase='running';}
   else if(this.phase==='braking'&&a.last.stop_reason==='Jog release complete'&&a.seen>=this.released){this.active=null;this.phase='idle';this.moving=false;}
   else if(this.moving){this.report('Board stopped: '+a.last.stop_reason);await this.stop();return;}
   if(this.active&&((this.phase==='starting'&&this.clock()-this.started>3000)||(this.phase==='braking'&&this.clock()-this.released>this.brakeTimeout))){this.report('Jog transition timed out.');await this.stop();}
   return;
  }
  if(!this.key)return;
  const key=this.key,[axis,sign]=keys[key],a=this.axes[axis],c=a.last.capabilities;
  if(!Number.isFinite(speed)||speed<Math.max(5,c.min_speed_sps??5)||speed>Math.min(100,c.max_speed_sps??100)||!Number.isFinite(acceleration)||acceleration<(c.min_acceleration_sps2??10)||acceleration>(c.max_acceleration_sps2??1000)){await this.stop();this.report('Use the supported speed and acceleration range.');return;}
  if(Object.values(this.axes).some(a=>a.last.motor_enabled)){await this.stop();this.report('Stop existing motion before jogging.');return;}
  this.active=axis;this.phase='starting';this.moving=false;this.started=this.clock();this.brakeTimeout=3000+speed/acceleration*1000;const generation=this.generation;
  try{
   await a.send('heartbeat',{},false);
   if(generation!==this.generation||this.key!==key||!this.armed){if(generation===this.generation){this.active=null;this.phase='idle';}return;}
   await a.send('start',{mode:'continuous',resolution:'half',direction:sign*(invert[axis]?-1:1),speed_sps:speed,acceleration_sps2:acceleration});
  }catch(e){this.report(e.message);await this.stop();}
 }
}
if(typeof module!=='undefined')module.exports={Jogger,keys};
if(typeof document!=='undefined'){
 const $=id=>document.getElementById(id),report=t=>{$('message').textContent=t;};
 class Axis {
  constructor(label){this.label=label;this.port=null;this.last=null;this.seen=0;this.pending=new Map();this.nextId=1;this.chain=Promise.resolve();this.closing=false;}
  send(cmd,fields={},ack=true){if(!this.port?.writable)return Promise.reject(Error(this.label+' disconnected'));const target=this.port,id=this.nextId++;let result=Promise.resolve();if(ack)result=new Promise((resolve,reject)=>{const timer=setTimeout(()=>{this.pending.delete(id);reject(Error(this.label+' command timed out'));},1500);this.pending.set(id,{resolve,reject,timer});});this.chain=this.chain.catch(()=>{}).then(async()=>{const w=target.writable.getWriter();try{await w.write(new TextEncoder().encode(JSON.stringify({cmd,id,...fields})+'\n'));}finally{w.releaseLock();}}).catch(e=>{const p=this.pending.get(id);if(p){clearTimeout(p.timer);this.pending.delete(id);p.reject(e);}else report(e.message);});return ack?result:this.chain;}
  receive(line){let d;try{d=JSON.parse(line);}catch{return;}if(d.type==='ack'){const p=this.pending.get(d.id);if(p){clearTimeout(p.timer);this.pending.delete(d.id);d.ok?p.resolve(d):p.reject(Error(d.error||'Rejected'));}}else if(d.type==='telemetry'){this.last=d;this.seen=Date.now();$('status'+this.label).textContent=`${d.device_name} · ${d.device_id}\n${d.temperature_c??'—'} °C · ${d.motion_mode==='braking'?'Slowing down':d.motor_enabled?'Moving':'Stopped'} · ${Number(d.profile_speed_sps||0).toFixed(1)} half steps/sec\n${d.sensor_error|| (d.driver_fault_asserted?'Driver fault':d.stop_reason)||''}`;}}
  async connect(){await jog.stop();try{const port=await navigator.serial.requestPort();if(Object.values(axes).some(a=>a.port===port))throw Error('Choose a different board for each axis.');await port.open({baudRate:115200});this.port=port;this.last=null;this.seen=0;await port.setSignals({dataTerminalReady:true,requestToSend:false});this.reader=port.readable.getReader();this.reading=this.read();await this.send('stop');await this.send('status');report('Connected '+this.label+'.');}catch(e){report(e.message);await this.close();}}
  async read(){let buffer='';const decoder=new TextDecoder();try{while(this.port){const {value,done}=await this.reader.read();if(done)break;buffer+=decoder.decode(value,{stream:true});let i;while((i=buffer.indexOf('\n'))>=0){this.receive(buffer.slice(0,i));buffer=buffer.slice(i+1);}if(buffer.length>8192)buffer='';}}catch(e){if(!this.closing)report(e.message);}finally{this.reader.releaseLock();this.reader=null;if(!this.closing){void jog.stop();void this.close();}}}
  async close(){if(this.closing)return;this.closing=true;try{if(this.reader)await this.reader.cancel();if(this.reading)await this.reading;await this.chain.catch(()=>{});if(this.port)await this.port.close();}catch{}finally{this.port=null;this.last=null;this.seen=0;this.closing=false;for(const p of this.pending.values()){clearTimeout(p.timer);p.reject(Error('Disconnected'));}this.pending.clear();$('status'+this.label).textContent='Disconnected';}}
 }
 const axes={X:new Axis('X'),Y:new Axis('Y')};const jog=new Jogger(axes,report);
 for(const label of ['X','Y']){
  $('connect'+label).onclick=async()=>{if(axes[label].port){await jog.stop();await axes[label].close();}else await axes[label].connect();};
  $('rename'+label).onclick=async()=>{await jog.stop();const name=$('name'+label).value.trim();if(!/^[\x20-\x7e]{1,32}$/.test(name)){report('Use 1–32 printable ASCII characters.');return;}try{const ack=await axes[label].send('set_device_name',{name});report('Saved '+ack.device_name+'. Reconnect using the new USB name.');await axes[label].close();}catch(e){report(e.message);}};
 }
 $('armed').onchange=()=>{if($('armed').checked&&jog.ready()&&Object.values(axes).every(a=>!a.last.motor_enabled)){jog.armed=true;report('Half-step jogging enabled. Hold an arrow to accelerate; release to slow down.');}else void jog.stop();};
 $('stopAll').onclick=()=>{void jog.stop();report('Stopped both axes. Jogging disabled.');};
 const editing=e=>['SELECT','TEXTAREA'].includes(e.target.tagName)||(e.target.tagName==='INPUT'&&e.target.type!=='checkbox')||e.target.isContentEditable;
 document.addEventListener('focusin',e=>{if(editing(e))void jog.stop();});
 document.addEventListener('keydown',e=>{if(e.key==='Escape'||(e.code==='Space'&&!editing(e))){e.preventDefault();void jog.stop();return;}if(!keys[e.key]||(['SELECT','TEXTAREA'].includes(e.target.tagName)||(e.target.tagName==='INPUT'&&e.target.type!=='checkbox'))||e.target.isContentEditable)return;e.preventDefault();if(e.repeat)return;jog.press(e.key);});
 document.addEventListener('keyup',e=>{if(keys[e.key]){e.preventDefault();void jog.release(e.key);}});
 window.addEventListener('blur',()=>void jog.stop());window.addEventListener('pagehide',()=>void jog.stop());document.addEventListener('visibilitychange',()=>{if(document.hidden)void jog.stop();});
 for(const id of ['jogSpeed','jogAcceleration','invertX','invertY'])$(id).onchange=()=>void jog.stop();
 let ticking=false;setInterval(async()=>{if(ticking)return;ticking=true;try{await jog.tick(Number($('jogSpeed').value),{X:$('invertX').checked,Y:$('invertY').checked},Number($('jogAcceleration').value));}finally{ticking=false;}},50);
 setInterval(()=>{for(const a of Object.values(axes))if(a.port&&jog.armed&&(jog.key||jog.active)&&!document.hidden&&Date.now()-a.seen<1000)void a.send('heartbeat',{},false);},400);
 for(const button of document.querySelectorAll('[data-arrow]')){button.addEventListener('pointerdown',e=>{e.preventDefault();if(jog.press(button.dataset.arrow))button.setPointerCapture(e.pointerId);});button.addEventListener('pointerup',()=>void jog.release(button.dataset.arrow));button.addEventListener('pointercancel',()=>void jog.stop());button.addEventListener('lostpointercapture',()=>void jog.release(button.dataset.arrow));}
 setInterval(()=>{for(const button of document.querySelectorAll('[data-arrow]')){button.disabled=!jog.armed;button.classList.toggle('engaged',jog.key===button.dataset.arrow);button.classList.toggle('coasting',jog.phase==='braking'&&keys[button.dataset.arrow][0]===jog.active);}$('jogPhase').textContent=jog.phase==='braking'?'Slowing down…':jog.active?'Accelerating / jogging':jog.armed?'Ready · hold an arrow':'Jogging disabled';$('brakeEstimate').textContent=`Release can add up to ${Math.max(1,Math.ceil(Number($('jogSpeed').value)**2/(2*Number($('jogAcceleration').value))))} half steps of deceleration, plus USB/input latency. Space/Escape stops immediately.`;$('armed').disabled=!jog.ready();$('armed').checked=jog.armed;for(const label of ['X','Y']){const a=axes[label];$('connect'+label).textContent=a.port?'Disconnect '+label:'Connect '+label;$('rename'+label).disabled=!a.port||Date.now()-a.seen>1000||!a.last?.usb_name_supported||a.last?.motor_enabled;}},100);
 if(!navigator.serial)report('Open in desktop Chrome or Edge with Web Serial support.');
}
