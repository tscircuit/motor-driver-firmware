/* Experimental, observational current-pattern classification. Never controls motion. */
(function(root){
'use strict';
const MODEL={
  "schema": 1,
  "device_id": "e66568714f117e21",
  "board_id": "rp2040_drv8825",
  "voltage": 9,
  "full_steps_per_second": 60,
  "direction": 1,
  "temperature_min_c": 25,
  "temperature_max_c": 40,
  "feature_names": [
    "radial_mean",
    "radial_cv",
    "ripple_ratio",
    "ripple_cv",
    "third_harmonic",
    "fifth_harmonic",
    "quadrature_error"
  ],
  "free": [
    0.8541027778425778,
    0.03647127340649327,
    0.09991558060974433,
    0.2047651324568196,
    0.013109037606722449,
    0.005485620015637049,
    0.007647866823059549
  ],
  "stalled": [
    0.8248254064241087,
    0.14642645928896794,
    0.11638194799857028,
    0.7381990352951989,
    0.39024866475400394,
    1.1910720409498985,
    0.16202984154397695
  ],
  "scale": [
    0.01,
    0.018691230354043915,
    0.005,
    0.03826758679908026,
    0.202334269143461,
    0.5694764015123587,
    0.08654615452651072
  ],
  "threshold": 65,
  "window_ms": 500,
  "training_runs_per_class": 2,
  "held_out_runs_per_class": 1,
  "not_a_probability": true,
  "held_out_window_accuracy": 1.0
};
const avg=a=>a.reduce((s,v)=>s+v,0)/a.length;
const std=a=>{const m=avg(a);return Math.sqrt(avg(a.map(v=>(v-m)**2)));};
function features(points,speed){
 if(points.length<350||points.at(-1)[0]-points[0][0]<450)return null;
 for(let i=1;i<points.length;i++)if(points[i][0]-points[i-1][0]>25)return null;
 const ma=avg(points.map(p=>p[1])),mb=avg(points.map(p=>p[2]));
 const a=points.map(p=>p[1]-ma),b=points.map(p=>p[2]-mb),r=a.map((v,i)=>Math.hypot(v,b[i])),mu=avg(r),rip=points.map(p=>(p[3]+p[4])/2);
 if(mu<.05)return null;
 const coeff=[1,3,5].map(h=>[a,b].map(values=>{
  let re=0,im=0;for(let i=0;i<values.length;i++){const angle=2*Math.PI*speed/4*h*(points[i][0]-points[0][0])/1000;re+=values[i]*Math.cos(angle);im-=values[i]*Math.sin(angle);}
  return [2*re/values.length,2*im/values.length];
 }));
 const magnitude=z=>Math.hypot(...z),amplitudes=coeff.map(c=>c.reduce((s,z)=>s+magnitude(z),0));
 const [ca,cb]=coeff[0];const quad=Math.abs(ca[0]*cb[0]+ca[1]*cb[1])/(magnitude(ca)*magnitude(cb)+1e-12);
 return [mu,std(r)/mu,avg(rip)/mu,std(rip)/(avg(rip)+1e-9),amplitudes[1]/(amplitudes[0]+1e-9),amplitudes[2]/(amplitudes[0]+1e-9),quad];
}
function classify(values,model=MODEL){
 const distance=k=>Math.sqrt(avg(values.map((v,i)=>((v-model[k][i])/model.scale[i])**2)));
 const free=distance('free'),stalled=distance('stalled');
 if(Math.min(free,stalled)>3.5)return null;
 return 100*free/(free+stalled+1e-12);
}
class Observer{
 constructor(model=MODEL){this.model=model;this.reset();}
 reset(){this.points=[];this.history=[];this.state={};this.stableSince=null;this.key=null;this.lastFrame=null;this.lastTelemetry=null;this.lastEval=0;this.result={score:null,reason:'Waiting for live current data.'};}
 receive(d,now=Date.now()){
  if(d.type==='telemetry'){
   Object.assign(this.state,d);this.lastTelemetry=now;
   const s=this.state,factors={full:1,half:2,quarter:4,eighth:8,sixteenth:16},factor=factors[s.step_resolution];
   this.fullSpeed=factor?s.speed_sps/factor:NaN;
   const active=s.motor_enabled&&s.motion_mode!=='stopped'&&Number.isFinite(s.profile_speed_sps)&&Math.abs(s.profile_speed_sps-s.speed_sps)<Math.max(.01,s.speed_sps*.05);
   const key=active?[s.pd_voltage_requested,s.direction,this.fullSpeed].join(':'):null;
   if(!active||key!==this.key)this.stableSince=active?now:null;
   this.key=key;
  }else if(d.type==='current_samples'&&!this.state.capabilities?.current_stall_firmware_supported){
   if(d.window_us!==1000||d.current_volts_per_amp!==1||!Array.isArray(d.bins)||!Number.isFinite(d.first_sample_us))return;
   const start=d.first_sample_us/1000;if(this.points.length&&start<this.points.at(-1)[0]-100){this.points=[];this.stableSince=now;}
   let added=false;
   for(let i=0;i<Math.min(32,d.bins.length);i++){
    const b=d.bins[i],t=start+i;
    if(!Array.isArray(b)||b.length!==8||b.some(v=>!Number.isFinite(v))||b[3]<80||b[7]<80||b[3]>100||b[7]>100||Math.min(b[0],b[4])<16||Math.max(b[1],b[5])>4079||b[0]>b[1]||b[4]>b[5])continue;
    if(this.points.length&&t<=this.points.at(-1)[0])continue;
    added=true;this.points.push([t,b[2]/b[3]*3.3/4095-1.65,b[6]/b[7]*3.3/4095-1.65,(b[1]-b[0])*3.3/4095,(b[5]-b[4])*3.3/4095]);
   }
   if(this.points.length){const end=this.points.at(-1)[0];this.points=this.points.filter(p=>p[0]>end-1500);}
   if(added)this.lastFrame=now;
  }
 }
 evaluate(now=Date.now()){
  if(now-this.lastEval<(this.state.capabilities?.current_stall_firmware_supported?50:250))return this.result;
  this.lastEval=now;const s=this.state,m=this.model;let reason=null,score=null;
  if(s.capabilities?.current_stall_firmware_supported){
   const live=this.lastTelemetry!==null&&now-this.lastTelemetry<1500;
   score=live&&s.motor_enabled&&s.current_stall_valid&&Number.isFinite(s.current_stall_score)?s.current_stall_score:null;
   reason=!live?'Waiting for firmware telemetry.':!s.motor_enabled?'Motor stopped.':s.current_stall_reason||'Collecting current samples.';
   this.result={score,reason,firmware:true,extrapolated:!!s.current_stall_extrapolated,outlier:!!s.current_stall_outlier,state:s.current_stall_state,model:s.current_stall_model};
   this.history.push({t:now,score});this.history=this.history.filter(p=>p.t>now-10000);return this.result;
  }
  if(s.device_id!==m.device_id||s.board_id!==m.board_id)reason='No calibration for this controller.';
  else if(this.lastTelemetry===null||now-this.lastTelemetry>1500||this.lastFrame===null||now-this.lastFrame>1000)reason='Waiting for fresh current data.';
  else if(!s.motor_enabled||s.motion_mode==='stopped')reason='Motor stopped.';
  else if(s.sensor_error||s.driver_fault_asserted||s.thermal_alert||!s.motor_power_ready)reason='Electrical or sensor fault; stall estimate unavailable.';
  else if(s.pd_voltage_requested!==m.voltage||s.direction!==m.direction||!Number.isFinite(this.fullSpeed)||Math.abs(this.fullSpeed-m.full_steps_per_second)>.01)reason='Calibration requires 9 V, forward, 60 full steps/sec (120 half steps/sec).';
  else if(!Number.isFinite(s.temperature_c)||s.temperature_c<m.temperature_min_c||s.temperature_c>m.temperature_max_c)reason='Temperature is outside the calibration range.';
  else if(this.stableSince===null||now-this.stableSince<1000)reason='Waiting for steady speed.';
  else {
   const end=this.points.at(-1)?.[0];const window=this.points.filter(p=>p[0]>end-m.window_ms);
   const vector=features(window,m.full_steps_per_second);
   if(!vector)reason='Insufficient continuous current samples.';
   else {score=classify(vector,m);if(score===null)reason='Current pattern is outside the calibration data.';}
  }
  this.result={score,reason};this.history.push({t:now,score});this.history=this.history.filter(p=>p.t>now-10000);return this.result;
 }
}
function mount(document){
 const panel=document.createElement('section');panel.className='currentStallPlot';panel.hidden=true;
 const h=document.createElement('h4');h.textContent='Experimental stall indicator';
 const note=document.createElement('p');note.textContent='Current-pattern similarity, not a probability or torque count. Calibrated only for this motor at 9 V, forward, 60 full steps/sec (120 half steps/sec). Reports only; never stops the motor.';
 const canvas=document.createElement('canvas');canvas.setAttribute('role','img');canvas.setAttribute('aria-label','Experimental current-based stall pattern similarity over the last ten seconds');
 const summary=document.createElement('p');summary.setAttribute('role','status');
 panel.append(h,note,canvas,summary);const observer=new Observer();
 return {panel,observer,reset(){observer.reset();panel.hidden=true;},receive(d,now){observer.receive(d,now);panel.hidden=observer.state.driver!=='DRV8825';if(observer.state.capabilities?.current_stall_firmware_supported)note.textContent=observer.state.current_stall_model?'Firmware analyzes 100,000 samples/sec per winding using 1,000 Hz current features. Trained from 24 recordings at a 9 V request. Windows adapt to speed; low speeds, interpolated settings and unfamiliar patterns are marked uncertain. Optional stall response settings control beeping and auto stop (off by default). This score is not a probability.':'Firmware analyzes 100,000 samples/sec per winding before USB export. This older firmware uses the earlier calibration; other settings are unvalidated. Reports only, never stops the motor. This score is not a probability.';},draw(now){
  if(panel.hidden)return;const result=observer.evaluate(now);summary.textContent=(result.firmware?'Firmware · ':'')+(result.score===null?'Unknown · '+result.reason:(result.state==='uncertain'?'Uncertain · '+result.reason:result.score>=MODEL.threshold?'Stall-like current pattern':'Free-running-like current pattern')+' · Similarity '+result.score.toFixed(0)+'/100 · '+(result.extrapolated?'Outside validated conditions':'Experimental'));
  const ctx=canvas.getContext('2d'),w=canvas.clientWidth||600,h=180,dpr=root.devicePixelRatio||1;canvas.width=w*dpr;canvas.height=h*dpr;ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);
  const left=35,right=w-12,top=12,bottom=h-25,x=t=>left+(t-now+10000)/10000*(right-left),y=v=>bottom-v/100*(bottom-top);
  ctx.font='12px system-ui';ctx.strokeStyle='#334155';ctx.fillStyle='#94a3b8';for(const v of [0,50,100]){ctx.beginPath();ctx.moveTo(left,y(v));ctx.lineTo(right,y(v));ctx.stroke();ctx.fillText(String(v),0,y(v)+4);}
  ctx.setLineDash([4,4]);ctx.strokeStyle='#fb7185';ctx.beginPath();ctx.moveTo(left,y(MODEL.threshold));ctx.lineTo(right,y(MODEL.threshold));ctx.stroke();ctx.setLineDash([]);
  ctx.strokeStyle='#c4b5fd';ctx.lineWidth=2;ctx.beginPath();let prev=null;for(const p of observer.history){if(p.score===null){prev=null;continue;}if(prev&&p.t-prev.t<600)ctx.lineTo(x(p.t),y(p.score));else ctx.moveTo(x(p.t),y(p.score));prev=p;}ctx.stroke();ctx.fillText('10s',left,h-5);ctx.fillText('now',right-20,h-5);
 }};
}
const api={MODEL,features,classify,Observer,mount};if(typeof module!=='undefined')module.exports=api;root.CurrentStall=api;
})(typeof window!=='undefined'?window:globalThis);
