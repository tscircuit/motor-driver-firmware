(function(root){
'use strict';
function controlState(d,seen,now=Date.now()){
 const supported=!!d?.capabilities?.power_arm_required;
 const live=!!seen&&now-seen<1000&&!d?.restarting;
 const stopped=!d?.motor_enabled&&['stopped',undefined].includes(d?.motion_mode);
 const pending=/Discharging|Negotiating|Waiting for protected/.test(d?.power_state||'');
 return {supported,live,canSet:live&&stopped&&!pending,canArm:live&&stopped&&!pending&&!d?.motor_power_ready&&d?.pd_request_sent&&d?.pd_voltage_requested>=9&&!!(d?.pd_power_good||d?.pd_power_good_override)&&!d?.power_fault_asserted&&Number.isFinite(d?.temperature_c)&&d.temperature_c<55};
}
const devices=new Map();
function element(tag,text){const e=root.document.createElement(tag);if(text)e.textContent=text;return e;}
function mount(axis){
 const parent=root.document.getElementById(axis==='motor'?'hardwarePanel':'hardware'+axis);
 if(!parent)return null;
 const panel=element('section');panel.className='nema23Controls';panel.hidden=true;
 panel.append(element('h3','Motor power and phase current'));
 const label=element('label','Peak winding current (A) '),input=element('input');input.type='number';input.min='.1';input.max='3.965';input.step='.01';input.value='1';label.append(input);panel.append(label);
 const apply=element('button','Apply current limit'),arm=element('button','Arm motor power'),off=element('button','Disconnect motor power');
 for(const b of [apply,arm,off]){b.type='button';panel.append(b);}
 const pgLabel=element('label','Ignore PD power-good '),pg=element('input');pg.type='checkbox';pgLabel.append(pg);panel.append(pgLabel);
 const status=element('p'),note=element('p');status.setAttribute('role','status');note.className='hint';panel.append(status,note);parent.append(panel);
 const device={panel,input,apply,arm,off,status,note,last:null,seen:0,dirty:false,busy:false,error:null,send:null,parent,pg,pgLabel};
 input.addEventListener('input',()=>{device.dirty=true;});
 async function request(cmd,fields={}){
  device.error=null;device.busy=true;render(device);
  try{await device.send(cmd,fields);await device.send('status');device.dirty=false;}catch(e){device.error=e.message;}finally{device.busy=false;render(device);}
 }
 apply.addEventListener('click',()=>{const value=Number(input.value);if(!Number.isFinite(value)||value<.1||value>device.last.capabilities.current_limit_max_a){status.textContent='Choose a peak current within the board’s supported range.';return;}void request('set_current_limit',{peak_a:value});});
 pg.addEventListener('change',()=>void request('set_pd_power_good_override',{enabled:pg.checked}));
 arm.addEventListener('click',()=>void request('arm_power'));
 off.addEventListener('click',()=>void request('disarm_power'));
 return device;
}
function render(a){
 const d=a.last||{},s=controlState(d,a.seen);a.panel.hidden=!s.supported;
 if(!s.supported)return;
 a.apply.disabled=a.input.disabled=!s.canSet||a.busy;a.arm.disabled=!s.canArm||a.busy;a.off.disabled=!s.live||a.busy;
 a.pgLabel.hidden=!d.capabilities.pd_power_good_override_supported;a.pg.disabled=!s.live||!s.canSet||a.busy;a.pg.checked=!!d.pd_power_good_override;
 a.input.max=String(d.capabilities.current_limit_max_a);if(!a.dirty&&Number.isFinite(d.current_limit_peak_a))a.input.value=String(d.current_limit_peak_a);
 a.note.textContent='Connect motor power, request a voltage, then arm below 55°C. PG is ignored by default; PD communication, temperature, eFuse and driver checks remain active. Voltage changes discharge for 12 seconds. Current limits are peak amps and reset to 1 A on reboot.';
 for(const e of a.parent.querySelectorAll('[data-default-power-note]'))e.hidden=true;
 if(!s.live){a.status.textContent='Waiting for live board status.';return;}
 a.status.textContent=`${a.error?a.error+' · ':''}${d.power_error||d.power_state||'Motor rail off'} · PD power-good: ${d.pd_power_good?'yes':'no'}${d.pd_power_good_override?' (ignored)':''} · Contract: ${Number(d.pd_contract_current_a||0).toFixed(2)} A${d.current_foldback_active?' · current reduced for temperature':''}`;
}
const api={controlState,receive(axis,d,send){if(d.type!=='telemetry')return;let a=devices.get(axis);if(!a){if(!d.capabilities?.power_arm_required)return;a=mount(axis);if(!a)return;devices.set(axis,a);}a.last=d.compact?{...a.last,...d}:d;a.seen=Date.now();a.send=send;render(a);}};
if(typeof module!=='undefined')module.exports=api;
if(root.document){root.BoardControls=api;root.setInterval(()=>{for(const a of devices.values())render(a);},200);}
})(typeof window==='undefined'?globalThis:window);
