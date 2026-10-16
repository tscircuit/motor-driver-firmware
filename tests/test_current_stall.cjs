const assert=require('node:assert/strict');
const {features,classify,Observer,MODEL}=require('../dist/current-stall.js');
const fixture=require('./fixtures/current-stall.json');
for(const w of fixture.windows){const f=features(w.points,60);assert(f);f.forEach((v,i)=>assert(Math.abs(v-w.features[i])<1e-7,`${w.label} feature ${i}`));assert.equal(classify(f)>=65,w.label==='stalled');}
for(const w of fixture.held_out)assert.equal(classify(w.features)>=65,w.label==='stalled');
assert.equal(features([],60),null);
const gap=fixture.windows[0].points.filter((p,i)=>i<100||i>150);assert.equal(features(gap,60),null);
assert.equal(classify(Array(7).fill(1000)),null);
const o=new Observer();
const active={type:'telemetry',device_id:MODEL.device_id,board_id:MODEL.board_id,driver:'DRV8825',motor_enabled:true,motion_mode:'continuous',step_resolution:'half',speed_sps:120,profile_speed_sps:120,direction:1,pd_voltage_requested:9,temperature_c:30,motor_power_ready:true};
o.receive(active,1000);assert.match(o.evaluate(1000).reason,/fresh current/);
function loadWindow(observer,w,now){
 const offset=observer.points.length?observer.points.at(-1)[0]+1-w.points[0][0]:0;
 for(const p of w.points){const raw=v=>(v+1.65)*4095/3.3;const ra=raw(p[1]),rb=raw(p[2]),wa=p[3]*4095/3.3,wb=p[4]*4095/3.3;observer.receive({type:'current_samples',window_us:1000,current_volts_per_amp:1,first_sample_us:(p[0]+offset)*1000,bins:[[ra-wa/2,ra+wa/2,ra*100,100,rb-wb/2,rb+wb/2,rb*100,100]]},now);}
}
loadWindow(o,fixture.windows[0],2100);o.receive(active,2100);assert(o.evaluate(2100).score<10);
o.receive({...active,direction:-1},2400);assert.match(o.evaluate(2400).reason,/Calibration requires/);
o.receive({...active,step_resolution:'full',speed_sps:60,profile_speed_sps:20},2700);assert.match(o.evaluate(2700).reason,/steady speed/);
o.receive({...active,motor_enabled:false,motion_mode:'stopped'},3000);assert.match(o.evaluate(3000).reason,/Motor stopped/);
o.receive({...active,device_id:'other'},3300);assert.match(o.evaluate(3300).reason,/No calibration/);
loadWindow(o,fixture.windows[0],3500);o.receive({...active,driver_fault_asserted:true},3600);assert.match(o.evaluate(3600).reason,/fault/);
o.receive({...active,driver_fault_asserted:false,temperature_c:45},3900);assert.match(o.evaluate(3900).reason,/Temperature/);
o.receive(active,4200);assert.match(o.evaluate(6200).reason,/fresh current/);
o.reset();assert.equal(o.points.length,0);assert.equal(o.history.length,0);
for(const page of ['single-motor','index','gantry']){const html=require('node:fs').readFileSync(`dist/${page}.html`,'utf8');assert(html.indexOf('src="current-stall.js"')<html.indexOf('src="current-chart.js"'));}
console.log('PASS experimental current stall features, held-out samples, coverage, scope, stopped/startup/fault/stale gates; observer has no motion API');

const native=new Observer();native.receive({...active,capabilities:{current_stall_firmware_supported:true},current_stall_valid:true,current_stall_score:80,current_stall_extrapolated:true},1000);
assert.equal(native.evaluate(1000).score,80);assert.equal(native.result.firmware,true);assert.equal(native.result.extrapolated,true);
native.receive({type:'telemetry',motor_enabled:false},1300);assert.equal(native.evaluate(1300).score,null);
assert.equal(native.evaluate(3000).score,null);
