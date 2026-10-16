const assert=require('node:assert/strict');
const {HomingRunner}=require('../dist/homing-controls.js');
const config={speed_full_sps:60,max_travel_full_steps:1000,backoff_full_steps:100};
function fixture(labels){
 let now=10000;const log=[],jog={generation:0,returning:false,async stop(message){this.generation++;this.returning=false;log.push(['stop',message]);}};
 const axes=Object.fromEntries(labels.map(label=>[label,{port:{},seen:now,last:{device_id:label,position_session:'session',capabilities:{stall_homing_supported:true},motor_enabled:false,motor_power_ready:true},async send(cmd){log.push([label,cmd]);if(cmd==='run_stall_homing'){this.last.homing_active=true;this.last.homing_phase=1;this.last.homed=false;}}}]));
 const runner=new HomingRunner(axes,jog,message=>log.push(['report',message]),async()=>{now+=100;for(const a of Object.values(axes)){a.seen=now;if(a.last.homing_active){a.last.homing_active=false;a.last.homed=true;}}},()=>now);
 return {axes,jog,runner,log,get now(){return now;}};
}
(async()=>{
 for(const labels of [['X'],['X','Y']]){const f=fixture(labels);assert(await f.runner.run(config));assert.deepEqual(f.log.filter(x=>x[1]==='run_stall_homing').map(x=>x[0]),labels);assert(!f.jog.returning);}
 const cancel=fixture(['X','Y']);cancel.runner.pause=async()=>{cancel.jog.generation++;};assert.equal(await cancel.runner.run(config),false);assert.equal(cancel.log.filter(x=>x[1]==='run_stall_homing').length,1);
 const fail=fixture(['X','Y']);fail.runner.pause=async()=>{fail.axes.X.last.homing_phase=6;fail.axes.X.last.homing_message='No stop detected';};assert.equal(await fail.runner.run(config),false);assert.equal(fail.log.filter(x=>x[1]==='run_stall_homing').length,1);
 const disconnect=fixture(['X']);disconnect.runner.pause=async()=>{disconnect.axes.X.port=null;};assert.equal(await disconnect.runner.run(config),false);
 const noPower=fixture(['X']);noPower.axes.X.last.motor_power_ready=false;assert.equal(await noPower.runner.run(config),false);assert(!noPower.log.some(x=>x[1]==='run_stall_homing'));
 console.log('PASS single/XY homing, sequential start, cancellation, failed axis, disconnect and power gating');
})().catch(e=>{console.error(e);process.exitCode=1;});
