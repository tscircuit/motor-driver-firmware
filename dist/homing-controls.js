(function(root){
'use strict';
class HomingRunner {
 constructor(axes,jog,report,pause=ms=>new Promise(r=>setTimeout(r,ms)),clock=()=>Date.now()) {Object.assign(this,{axes,jog,report,pause,clock,busy:false});}
 async run(config) {
  if(this.busy)return false;
  const selected=Object.entries(this.axes).filter(([,a])=>a.port);
  if(!selected.length){this.report('Connect at least one controller.');return false;}
  for(const [axis,a] of selected){
   if(!a.last?.capabilities?.stall_homing_supported||a.last.motor_enabled||this.clock()-a.seen>1000||!a.last.motor_power_ready){this.report(axis+': stop the motor and request motor power before homing.');return false;}
  }
  this.busy=true;
  try {
   await this.jog.stop();const generation=this.jog.generation;this.jog.returning=true;
   for(const [axis,a] of selected){
    if(generation!==this.jog.generation)throw Error('Homing cancelled.');
    const port=a.port,id=a.last.device_id;
    this.report('Homing '+axis+' — seeking both end stops.');
    await a.send('heartbeat',{},false);
    await a.send('run_stall_homing',config);await a.send('status');
    const session=a.last.position_session;
    const deadline=this.clock()+Math.min(650000,((config.max_travel_full_steps*3+config.backoff_full_steps*2)/config.speed_full_sps+30)*1000);
    for(;;){
     if(generation!==this.jog.generation)throw Error('Homing cancelled.');
     if(a.port!==port||a.last.device_id!==id||a.last.position_session!==session||this.clock()-a.seen>1500)throw Error(axis+': connection changed during homing.');
     if(a.last.homing_phase===6)throw Error(axis+': '+a.last.homing_message);
     if(!a.last.homing_active&&a.last.homed)break;
     if(this.clock()>deadline)throw Error(axis+': homing timed out.');
     this.report(axis+': '+(a.last.homing_message||'Homing…'));
     await this.pause(100);
    }
   }
   this.report('Homing complete. Saved bounds are enforced on '+selected.map(([axis])=>axis).join(' and ')+'.');return true;
  }catch(e){await this.jog.stop(e.message);return false;}
  finally{this.jog.returning=false;this.busy=false;}
 }
}
function mount(axes,jog,report,parent){
 const doc=root.document,runner=new HomingRunner(axes,jog,report);
 const el=(tag,text)=>{const e=doc.createElement(tag);if(text)e.textContent=text;return e;};
 const panel=el('section');panel.className='jogPanel';panel.append(el('h2','Stall-based homing'));
 const note=el('p','Uses the shared default stall model; no per-controller stall calibration is required. Finds both physical end stops, backs away, saves the usable range, and returns to zero. Use only an axis with hard stops that tolerate brief contact. Bounds use commanded steps, not shaft feedback. Rehome after a restart or any manual shaft movement.');
 const makeInput=(label,value,min,max)=>{const l=el('label',label+' '),i=el('input');i.type='number';i.value=String(value);i.min=String(min);i.max=String(max);l.append(i);panel.append(l);return i;};
 panel.append(note);
 const speed=makeInput('Homing speed (full steps/sec)',60,25,100),travel=makeInput('Maximum search per end (full steps)',1000,100,100000),backoff=makeInput('Backoff from each stop (full steps)',100,20,1000);
 const run=el('button','Run Stall-based Homing'),status=el('p');status.setAttribute('role','status');panel.append(run,status);const anchor=parent.querySelector('.jogPanel');if(anchor)parent.insertBefore(panel,anchor);else parent.append(panel);
 run.addEventListener('click',()=>{
  const config={speed_full_sps:Number(speed.value),max_travel_full_steps:Number(travel.value),backoff_full_steps:Number(backoff.value)};
  if(Object.values(config).some(v=>!Number.isFinite(v)||v<=0)){report('Enter positive homing settings.');return;}
  void runner.run(config);
 });
 root.setInterval(()=>{
  const connected=Object.entries(axes).filter(([,a])=>a.port);
  run.disabled=runner.busy||!connected.length||connected.some(([,a])=>!a.last?.capabilities?.stall_homing_supported||a.last.motor_enabled||!a.last.motor_power_ready||Date.now()-a.seen>1000);
  speed.disabled=travel.disabled=backoff.disabled=runner.busy;
  status.textContent=connected.map(([axis,a])=>axis+': '+(a.last?.homing_active?a.last.homing_message:a.last?.homed?'Homed · bounds 0–'+a.last.bounds_max_full_steps+' full steps':a.last?.bounds_saved?'Bounds saved · homing required':'Not homed')).join(' · ')||'Connect one axis or both X and Y. With two connected controllers, X homes first, then Y.';
 },200);
 return runner;
}
const api={HomingRunner,mount};if(typeof module!=='undefined')module.exports=api;root.HomingControls=api;
})(typeof window==='undefined'?globalThis:window);
