(function(root){
'use strict';
function controlState(d,seen,now=Date.now()){
 const supported=!!d?.capabilities?.power_arm_required;
 const live=!!seen&&now-seen<1000&&!d?.restarting;
 const stopped=!d?.motor_enabled&&['stopped',undefined].includes(d?.motion_mode);
 // The firmware owns power checks. Keep the action available so failures
 // produce an actionable board response instead of an unexplained disabled button.
 return {supported,live,canSet:live&&stopped,canArm:live&&stopped&&!d?.motor_power_ready};
}
function stallText(d){
 if(d.hardware_motion_inhibited)return 'Stall status unknown · hardware power/temperature interlock active. Commanded steps do not prove shaft movement.';
 if(!d.stall_detection_valid)return 'Stall status unknown · collecting torque data during motion.';
 return `${d.stall_suspected?'Stall suspected':'No stall indication'} · Torque count: ${d.stall_torque_count} · Threshold: ${d.stall_threshold} · Sensorless estimate, uncalibrated`;
}
const devices=new Map();
function element(tag,text){const e=root.document.createElement(tag);if(text)e.textContent=text;return e;}
function mount(axis){
 const parent=root.document.getElementById(axis==='motor'?'hardwarePanel':'hardware'+axis);
 if(!parent)return null;
 const panel=element('section');panel.className='nema23Controls';panel.hidden=true;
 panel.append(element('h3','Motor power and phase current'));
 const label=element('label','Peak winding current (A) '),input=element('input');input.type='number';input.min='.1';input.max='3.965';input.step='.01';input.value='1';label.append(input);panel.append(label);
 const apply=element('button','Apply current limit'),arm=element('button','Enable motor power'),off=element('button','Disconnect motor power');
 for(const b of [apply,arm,off]){b.type='button';panel.append(b);}
 const pgLabel=element('label','Ignore PD power-good '),pg=element('input');pg.type='checkbox';pgLabel.append(pg);panel.append(pgLabel);
 const fuseLabel=element('label','Ignore fuse power fault '),fuse=element('input');fuse.type='checkbox';fuseLabel.append(fuse);panel.append(fuseLabel);
 const ocpLabel=element('label','Ignore overcurrent fault shutdown '),ocp=element('input');ocp.type='checkbox';ocpLabel.append(ocp);panel.append(ocpLabel);
 const windingLabel=element('label','Disable peak winding-current limit '),winding=element('input');winding.type='checkbox';windingLabel.append(winding);panel.append(windingLabel);
 const stopLabel=element('label','Stop on detected stall / electrical fault '),stopFault=element('input');stopFault.type='checkbox';stopLabel.append(stopFault);panel.append(stopLabel);
 const stallLabel=element('label','Stall torque threshold '),stallThreshold=element('input');stallThreshold.type='number';stallThreshold.min='1';stallThreshold.max='4095';stallThreshold.step='1';stallLabel.append(stallThreshold);panel.append(stallLabel);
 const applyStall=element('button','Apply stall threshold');applyStall.type='button';panel.append(applyStall);
 const stallSummary=element('p');stallSummary.setAttribute('role','status');panel.append(stallSummary);
 const status=element('p'),note=element('p');status.setAttribute('role','status');status.className='powerStatus';note.className='hint';panel.append(status,note);parent.append(panel);
 const device={panel,input,apply,arm,off,status,note,last:null,seen:0,enablePending:false,dirty:false,busy:false,error:null,send:null,parent,pg,pgLabel,fuse,fuseLabel,ocp,ocpLabel,winding,windingLabel,stopLabel,stopFault,stallLabel,stallThreshold,applyStall,stallSummary};
 input.addEventListener('input',()=>{device.dirty=true;});
 async function request(cmd,fields={}){
  device.error=null;device.busy=true;render(device);
  try{await device.send(cmd,fields);await device.send('status');device.dirty=false;}catch(e){device.enablePending=false;device.error=e.message;}finally{device.busy=false;tryEnable(device);render(device);}
 }
 apply.addEventListener('click',()=>{const value=Number(input.value);if(!Number.isFinite(value)||value<.1||value>device.last.capabilities.current_limit_max_a){status.textContent='Choose a peak current within the board’s supported range.';return;}void request('set_current_limit',{peak_a:value});});
 stopFault.addEventListener('change',()=>{device.enablePending=false;void request('set_stop_on_detected_fault',{enabled:stopFault.checked});});
 applyStall.addEventListener('click',()=>{const threshold=Number(stallThreshold.value);if(!Number.isInteger(threshold)||threshold<1||threshold>4095){status.textContent='Choose a stall threshold from 1 to 4095.';return;}void request('set_stall_threshold',{threshold});});
 ocp.addEventListener('change',()=>{device.enablePending=false;void request('set_overcurrent_shutdown_override',{enabled:ocp.checked});});
 winding.addEventListener('change',()=>{device.enablePending=false;void request('set_winding_current_limit_override',{enabled:winding.checked});});
 fuse.addEventListener('change',()=>{device.enablePending=false;void request('set_fuse_power_fault_override',{enabled:fuse.checked});});
 pg.addEventListener('change',()=>void request('set_pd_power_good_override',{enabled:pg.checked}));
 arm.addEventListener('click',()=>{
  if(!device.last.pd_request_sent||device.last.pd_voltage_requested<9){
   device.enablePending=true;void request('set_pd_voltage',{voltage:20});
  }else if(/Discharging|Negotiating|Settling|Waiting for protected/.test(device.last.power_state||'')){device.enablePending=true;render(device);}
  else{void request('arm_power');}
 });
 off.addEventListener('click',()=>{device.enablePending=false;void request('disarm_power');});
 return device;
}
function tryEnable(a){
 if(!a.enablePending||a.busy)return;
 const d=a.last;
 if(!d.pd_request_sent||d.pd_voltage_requested<9)return;
 if(d.power_error){a.enablePending=false;return;}
 if(d.power_state!=='Ready to arm motor power'&&d.power_state!=='Motor rail off')return;
 a.enablePending=false;a.busy=true;
 Promise.resolve().then(()=>a.send('arm_power')).then(()=>a.send('status')).catch(e=>{a.error=e.message;}).finally(()=>{a.busy=false;render(a);});
}
function render(a){
 const d=a.last||{},s=controlState(d,a.seen);a.panel.hidden=!s.supported;
 if(!s.supported)return;
 a.apply.disabled=a.input.disabled=!s.canSet||a.busy;a.arm.disabled=!s.canArm||a.busy||a.enablePending;a.arm.textContent=a.enablePending?'Enabling motor power…':'Enable motor power';a.off.disabled=!s.live||a.busy;
 a.pgLabel.hidden=!d.capabilities.pd_power_good_override_supported;a.pg.disabled=!s.live||!s.canSet||a.busy;a.pg.checked=!!d.pd_power_good_override;
 a.fuseLabel.hidden=!d.capabilities.fuse_power_fault_override_supported;a.fuse.disabled=!s.live||!s.canSet||a.busy;a.fuse.checked=!!d.fuse_power_fault_override;
 a.ocpLabel.hidden=!d.capabilities.overcurrent_shutdown_override_supported;a.ocp.disabled=!s.live||!s.canSet||a.busy;a.ocp.checked=!!d.overcurrent_shutdown_override;
 a.windingLabel.hidden=!d.capabilities.winding_current_limit_override_supported;a.winding.disabled=!s.live||!s.canSet||a.busy;a.winding.checked=!!d.winding_current_limit_override;
 a.apply.disabled=a.input.disabled=!s.canSet||a.busy||!!d.winding_current_limit_override;
 a.stopLabel.hidden=!d.capabilities.fault_stop_mode_supported;a.stopFault.disabled=!s.canSet||a.busy;a.stopFault.checked=!!d.stop_on_detected_fault;
 a.stallLabel.hidden=a.applyStall.hidden=a.stallSummary.hidden=!d.capabilities.stall_detection_supported;
 a.stallThreshold.disabled=a.applyStall.disabled=!s.canSet||a.busy;
 if(root.document.activeElement!==a.stallThreshold&&Number.isFinite(d.stall_threshold))a.stallThreshold.value=String(d.stall_threshold);
 a.stallSummary.textContent=s.live?stallText(d):'Stall status unknown · waiting for live board status.';
 a.input.max=String(d.capabilities.current_limit_max_a);if(!a.dirty&&Number.isFinite(d.current_limit_peak_a))a.input.value=String(d.current_limit_peak_a);
 a.note.textContent='Enable requests 20 V if no motor voltage is selected. Current resets to 1 A on reboot. Ignoring fuse power fault resets on reboot; changing overrides disconnects motor power. Peak-limit bypass uses full scale (about 3.965 A peak). Overcurrent override uses auto-retry; the hardware trip remains active. Both current overrides reset on reboot. Stall/fault reporting is on by default; stopping on detections defaults off. Hardware interlocks can still inhibit motion. The overcurrent override applies when stopping on detection is selected. Thermal, sensor, communication and SPI configuration protection remain active.';
 for(const e of a.parent.querySelectorAll('[data-default-power-note]'))e.hidden=true;
 if(!s.live){a.enablePending=false;a.status.textContent='Waiting for live board status.';return;}
 a.status.textContent=`${a.error?a.error+' · ':''}${d.power_error||d.power_state||'Motor rail off'}${d.reported_fault?' · Warning: '+d.reported_fault:''}${d.capabilities.fault_stop_mode_supported?' · Fault mode: '+(d.stop_on_detected_fault?'stop on detection':'report only'):''} · PD power-good: ${d.pd_power_good?'yes':'no'}${d.pd_power_good_override?' (ignored)':''} · Fuse fault: ${d.power_fault_asserted?'asserted':'clear'}${d.fuse_power_fault_override?' (ignored)':''} · PD reported current: ${Number(d.pd_contract_current_a||0).toFixed(2)} A (informational)${d.overcurrent_shutdown_override?' · Overcurrent: auto-retry (software shutdown ignored)':''}${d.winding_current_limit_override?' · Peak limit bypassed: '+Number(d.effective_current_peak_a??d.capabilities.current_limit_max_a).toFixed(3)+' A':''}${d.current_foldback_active?' · current reduced for temperature':''}`;
}
function disconnect(axis){const a=devices.get(axis);if(!a)return;a.enablePending=false;a.last=null;a.seen=0;a.error=null;render(a);}
const api={controlState,stallText,disconnect,receive(axis,d,send){if(d.type!=='telemetry')return;let a=devices.get(axis);if(!a){if(!d.capabilities?.power_arm_required)return;a=mount(axis);if(!a)return;devices.set(axis,a);}a.last=d.compact?{...a.last,...d}:d;a.seen=Date.now();a.send=send;tryEnable(a);render(a);}};
if(typeof module!=='undefined')module.exports=api;
if(root.document){root.BoardControls=api;root.setInterval(()=>{for(const a of devices.values())render(a);},200);}
})(typeof window==='undefined'?globalThis:window);
