'use strict';
const $=id=>document.getElementById(id);
let port=null,reader=null,reading=null,connecting=false,closing=false,last=null,lastSeen=0,nextId=1;
let nameDirty=false,renaming=false,restartNotice=null;
let points=[],pending=new Map(),dirty=false,writeChain=Promise.resolve();
const message=t=>{$('message').textContent=t;};
function controls(){const live=!!port&&lastSeen>0&&Date.now()-lastSeen<2500;$('beep').disabled=!live||renaming;$('deviceName').disabled=!live||!last?.usb_name_supported||!!last?.motor_enabled||renaming;$('rename').disabled=$('deviceName').disabled;$('threshold').disabled=!live;$('apply').disabled=!live||!!last?.motor_enabled||renaming;for(const id of ['mode','resolution','direction','steps','speed','start'])$(id).disabled=!live||last?.protocol!==3||!!last?.motor_enabled||renaming||!!last?.restarting;$('stop').disabled=!port;$('connect').disabled=connecting||renaming||closing;$('connect').textContent=port?'Disconnect':'Connect board ↗';}
function send(cmd,fields={}){
 if(!port||!port.writable)return Promise.reject(Error('Connect the board first.'));
 const id=nextId++;
 return new Promise((resolve,reject)=>{
 const timer=setTimeout(()=>{pending.delete(id);reject(Error('Board did not acknowledge the command.'));},3000);
 pending.set(id,{resolve,reject,timer});
 const target=port;
 writeChain=writeChain.catch(()=>{}).then(async()=>{const w=target.writable.getWriter();try{await w.write(new TextEncoder().encode(JSON.stringify({id,cmd,...fields})+'\n'));}finally{w.releaseLock();}}).catch(e=>{const p=pending.get(id);if(p){clearTimeout(p.timer);pending.delete(id);p.reject(e);}});
 });
}
function receive(line){
 let data;try{data=JSON.parse(line);}catch{return;}
 if(data.type==='ack'){const p=pending.get(data.id);if(p){clearTimeout(p.timer);pending.delete(data.id);data.ok?p.resolve(data):p.reject(Error(data.error||'Command rejected'));}return;}
 if(data.type!=='telemetry'||![1,2,3].includes(data.protocol))return;
 if(!Number.isFinite(data.threshold_c)||data.threshold_c<10||data.threshold_c>74)return;
 const t=data.temperature_c;if(t!==null&&(!Number.isFinite(t)||t< -40||t>125))return;
 const now=Date.now();if(lastSeen&&now-lastSeen>2500)points.push({t:now-1,v:null});
 last=data;lastSeen=now;
 $('deviceIdentity').textContent=`${data.device_name||'MicroPython board'}${data.device_id?' · '+data.device_id:''}`;
 if(!nameDirty)$('deviceName').value=data.device_name||'';
 $('nameHint').textContent=data.usb_name_supported?'Saved on the board. Stop the motor first. After restart, reconnect using the new name.':'Install the updated firmware, including boot.py, to enable USB naming.';
points.push({t:now,v:t});points=points.filter(p=>p.t>=now-300000).slice(-1300);
 $('temperature').textContent=t===null?'—':t.toFixed(1);
 $('tempHint').textContent=data.sensor_error?'Sensor read failed': 'Live · 4 readings per second';
 $('alarm').textContent=data.sensor_error?'Sensor fault':data.alarm_active?'Alarm active':'Normal';
 $('alarm').classList.toggle('danger',!!data.alarm_active);
 $('alarmHint').textContent=`Buzzer threshold ${data.threshold_c.toFixed(1)}°C${data.buzzer_on?' · sounding':''}`;
 $('fault').textContent=data.driver_fault_asserted?'Asserted':'Not asserted';
 $('fault').classList.toggle('danger',!!data.driver_fault_asserted);
 $('state').textContent='Connected';$('connectionHint').textContent=`Live USB connection${data.usb_name?' · '+data.usb_name:''}`;$('motorStatus').textContent=data.motor_enabled?'Running':'Disabled';$('motionState').textContent=data.motor_enabled?(data.motion_mode==='continuous'?'Continuous rotation':'Moving'):(data.stop_reason||'Stopped');$('progress').textContent=data.motor_enabled?`${data.steps_executed} ${data.step_resolution||"full"} steps commanded${data.steps_remaining!=null?' · '+data.steps_remaining+' remaining':''}`:`${data.steps_executed??0} ${data.step_resolution||"full"} steps in last move · ${data.steps_per_revolution||200} per revolution`;if(data.protocol!==3)message('Update the board firmware to enable motor controls.');
 if(!dirty)$('threshold').value=data.threshold_c;
 $('samples').textContent=`${points.filter(p=>p.v!==null).length} samples`;
 controls();draw();
}
function draw(){
 const c=$('chart'),r=c.getBoundingClientRect(),dpr=window.devicePixelRatio||1;
 c.width=r.width*dpr;c.height=r.height*dpr;const g=c.getContext('2d');g.scale(dpr,dpr);
 const w=r.width,h=r.height,L=38,R=10,T=16,B=28,pw=w-L-R,ph=h-T-B;
 const values=points.filter(p=>p.v!==null).map(p=>p.v);const threshold=last?.threshold_c??65;
 const lo=Math.min(20,Math.floor((Math.min(...values,25)-5)/10)*10),hi=Math.max(80,Math.ceil((Math.max(...values,threshold)+5)/10)*10);
 const y=v=>T+(hi-v)/(hi-lo)*ph,now=Date.now(),x=t=>L+(t-(now-300000))/300000*pw;
 g.font='12px system-ui';g.lineWidth=1;
 for(let i=0;i<=4;i++){const v=lo+(hi-lo)*i/4,yy=y(v);g.strokeStyle='#273445';g.beginPath();g.moveTo(L,yy);g.lineTo(w-R,yy);g.stroke();g.fillStyle='#91a3b8';g.textAlign='right';g.fillText(v.toFixed(0),L-9,yy+4);}
 g.textAlign='center';for(let i=0;i<=5;i++){g.fillText(i===5?'now':`−${5-i}m`,L+i/5*pw,h-5);}
 if(last){g.strokeStyle='#ffbd70';g.setLineDash([5,5]);g.beginPath();g.moveTo(L,y(threshold));g.lineTo(w-R,y(threshold));g.stroke();g.setLineDash([]);}
 g.save();g.beginPath();g.rect(L,T,pw,ph);g.clip();g.strokeStyle='#75efd0';g.lineWidth=2;g.beginPath();let started=false,prev=0;
 for(const p of points){if(p.v===null){started=false;continue;}if(!started||p.t-prev>2500)g.moveTo(x(p.t),y(p.v));else g.lineTo(x(p.t),y(p.v));started=true;prev=p.t;}g.stroke();
 const recent=points.at(-1);if(recent?.v!=null){g.fillStyle='#75efd0';g.beginPath();g.arc(x(recent.t),y(recent.v),3,0,Math.PI*2);g.fill();}g.restore();
 $('empty').style.display=values.length?'none':'flex';
}
async function readLoop(){let buffer='';const decoder=new TextDecoder();try{while(port&&reader){const {value,done}=await reader.read();if(done)break;buffer+=decoder.decode(value,{stream:true});let i;while((i=buffer.indexOf('\n'))>=0){receive(buffer.slice(0,i).trim());buffer=buffer.slice(i+1);}if(buffer.length>8192)buffer='';}}catch(e){if(!closing)message(restartNotice||('USB connection lost: '+e.message));}finally{reader?.releaseLock();reader=null;}}
async function disconnect(){if(closing)return;closing=true;try{if(port&&[2,3].includes(last?.protocol)){try{await send('stop');}catch{}}if(reader)await reader.cancel();if(reading)await reading;await writeChain.catch(()=>{});if(port)await port.close();}catch{}finally{port=null;reading=null;lastSeen=0;closing=false;for(const p of pending.values()){clearTimeout(p.timer);p.reject(Error('Disconnected'));}pending.clear();$('state').textContent='Disconnected';$('connectionHint').textContent='Graph retained. Reconnect to resume live readings.';$('tempHint').textContent='Last reading · disconnected';$('motionState').textContent='Disconnected · automatic stop';$('motorStatus').textContent='Disconnected';controls();}}
async function connect(){if(port)return disconnect();connecting=true;controls();try{
 const selected=await navigator.serial.requestPort({filters:[{usbVendorId:0x2e8a,usbProductId:0x0005}]});
 await selected.open({baudRate:115200});port=selected;lastSeen=0;last=null;points=[];dirty=false;nameDirty=false;renaming=false;restartNotice=null;
 await port.setSignals({dataTerminalReady:true,requestToSend:false});reader=port.readable.getReader();reading=readLoop();
 reading.then(()=>{if(!closing&&port)void disconnect();});
 $('state').textContent='Waiting for board';message('Connected. Waiting for telemetry…');
 await send('status');message('Live readings received. Settings and tones are acknowledged by the board.');
 }catch(e){message(e.name==='NotFoundError'?'No board selected.':e.message+' Close any other serial monitor and try again.');if(port)await disconnect();}finally{connecting=false;controls();}}
async function applyThreshold(value){if(!Number.isFinite(value)||value<10||value>74)throw Error('Choose a threshold from 10 to 74°C.');const ack=await send('set_threshold',{threshold_c:value});dirty=false;$('threshold').value=ack.threshold_c;message(`Saved on board: buzzer at ${ack.threshold_c}°C.`);return {threshold_c:ack.threshold_c};}
$('connect').addEventListener('click',()=>void connect());
$('threshold').addEventListener('input',()=>{dirty=true;});
$('thresholdForm').addEventListener('submit',async e=>{e.preventDefault();try{await applyThreshold(Number($('threshold').value));}catch(error){message(error.message);}});
$('beep').addEventListener('click',async()=>{try{await send('beep');message('Test tone started: 0.6 seconds.');}catch(e){message(e.message);}});
if(!('serial' in navigator)||!window.isSecureContext){$('connect').disabled=true;message('Web Serial requires desktop Chrome or Edge on HTTPS or localhost. Open this page there.');}
else navigator.serial.addEventListener('disconnect',e=>{if(e.target===port||e.port===port){message(restartNotice||'Board unplugged. Reconnect to continue.');void disconnect();}});
new ResizeObserver(draw).observe($('chart'));
setInterval(()=>{if(port&&lastSeen&&Date.now()-lastSeen>2500){$('state').textContent='Telemetry stale';$('tempHint').textContent='No fresh readings · check USB';$('alarm').textContent='Unknown';controls();}draw();},1000);
const context=document.modelContext;
if(context?.registerTool){try{Promise.resolve(context.registerTool({name:'read_motor_temperature',description:'Read the latest temperature telemetry; does not connect or change the board.',inputSchema:{type:'object',properties:{},additionalProperties:false},annotations:{readOnlyHint:true},execute:()=>({connected:!!port,fresh:!!lastSeen&&Date.now()-lastSeen<2500,telemetry:last})})).catch(()=>{});}catch{}}

function heartbeat(){if(!port?.writable||document.hidden||closing||renaming)return;const target=port;writeChain=writeChain.catch(()=>{}).then(async()=>{if(!target.writable)return;const w=target.writable.getWriter();try{await w.write(new TextEncoder().encode('{"cmd":"heartbeat"}\n'));}finally{w.releaseLock();}}).catch(()=>{});}
setInterval(heartbeat,400);
$('mode').addEventListener('change',()=>{$('stepsField').hidden=$('mode').value==='continuous';$('steps').required=$('mode').value==='steps';});
$('motionForm').addEventListener('submit',async e=>{e.preventDefault();const mode=$('mode').value;const speed=Number($('speed').value),steps=Number($('steps').value);if(!Number.isFinite(speed)||speed<5||speed>100||(mode==='steps'&&(!Number.isInteger(steps)||steps<1||steps>100000))){message('Use 5–100 steps/sec and 1–100,000 whole steps.');return;}try{heartbeat();await send('start',{mode,direction:Number($('direction').value),speed_sps:speed,steps,resolution:$('resolution').value});message('Movement started. Press Stop to release the motor.');}catch(error){message(error.message);try{await send('stop');}catch{}}});
$('stop').addEventListener('click',async()=>{try{await send('stop');message('Motor stopped and coils released.');}catch(error){message('Stop not acknowledged. Automatic stop follows if the connection is lost.');}});
document.addEventListener('visibilitychange',()=>{if(document.hidden&&port)void send('stop').catch(()=>{});});
window.addEventListener('pagehide',()=>{if(port)void send('stop').catch(()=>{});});

function updateEstimate(){const half=$('resolution').value==='half',unit=half?'half':'full',perRev=half?400:200,n=Number($('steps').value),speed=Number($('speed').value);$('stepsLabel').textContent=half?'Half steps':'Full steps';$('speedLabel').textContent=half?'Half steps/sec':'Full steps/sec';$('moveEstimate').textContent=$('mode').value==='continuous'?`${perRev} ${unit} steps per revolution · ${(speed/perRev*60).toFixed(1)} nominal RPM`:`${n} ${unit} steps = ${(n/perRev*360).toFixed(1)}° · approximately ${(n/speed).toFixed(1)} seconds at ${speed} ${unit} steps/sec.`;}
for(const id of ['resolution','steps','speed','mode'])$(id).addEventListener('input',updateEstimate);
updateEstimate();

$('deviceName').addEventListener('input',()=>{nameDirty=true;});
$('nameForm').addEventListener('submit',async e=>{
 e.preventDefault();const name=$('deviceName').value.trim();
 if(!/^[\x20-\x7e]{1,32}$/.test(name)){message('Use 1–32 printable ASCII characters for the USB name.');return;}
 if(last?.motor_enabled){message('Stop the motor before renaming it.');return;}
 renaming=true;controls();
 try{
  const ack=await send('set_device_name',{name});nameDirty=false;$('deviceName').value=ack.device_name;
  restartNotice=`Saved “${ack.device_name}”. The board is restarting. Click Connect board and choose its new name.`;
  message(restartNotice);await disconnect();message(restartNotice);
 }catch(error){message(error.message+' If the board restarted, reconnect to check its name.');}
 finally{renaming=false;controls();}
});
