const assert=require('node:assert/strict');
const {Jogger}=require('../dist/gantry.js');
let now=100,calls=[];
function setup(){calls=[];const axes={};for(const name of ['X','Y'])axes[name]={port:{},seen:now,last:{protocol:3,temperature_c:25,motor_enabled:false,capabilities:{resolutions:{half:2,full:1},jog_update_supported:true,fixed_ramp_ms:400}},send:async(cmd,fields)=>{calls.push([name,cmd,fields]);if(cmd==='jog')axes[name].last.motor_enabled=true;}};return {j:new Jogger(axes,()=>{},()=>now),axes};}
const moves=()=>calls.filter(c=>c[1]==='jog');
(async()=>{
 let {j,axes}=setup();j.press('ArrowRight');j.press('ArrowUp');await j.tick(2000,{},'full');assert.equal(moves().length,2);assert.deepEqual(moves()[0],['X','jog',{resolution:'full',direction:1,speed_sps:2000,ramp_ms:400}]);assert.equal(moves()[1][0],'Y');
 await j.tick(2000,{},'full');assert.equal(moves().length,2);
 await j.release('ArrowRight');await j.tick(2000,{},'full');assert.equal(calls.at(-1)[0],'X');assert.equal(calls.at(-2)[1],'decelerate');assert.equal(calls.at(-1)[1],'status');assert.equal(j.direction('Y'),1);
 // Resume or reverse without waiting for stopped telemetry.
 assert.equal(j.press('ArrowLeft'),true);await j.tick(2000,{},'half');assert.equal(moves().at(-2)[2].direction,-1);assert.equal(j.states.X.braking,false);
 j.press('ArrowRight');await j.tick(2000,{});assert.equal(j.direction('X'),0);assert.equal(j.states.X.braking,true);await j.release('ArrowLeft');await j.tick(2000,{X:true});assert.equal(moves().at(-1)[2].direction,-1);
 // One slow USB acknowledgement must not block the other axis.
 ({j,axes}=setup());let ack;axes.X.send=cmd=>cmd==='jog'?new Promise(r=>ack=r):Promise.resolve();j.press('ArrowRight');const first=j.tick(50,{});await new Promise(r=>setImmediate(r));j.press('ArrowUp');await j.tick(50,{});assert.equal(moves().at(-1)[0],'Y');await j.release('ArrowRight');ack();await first;await j.tick(50,{});assert.equal(j.states.X.sent,null);
 // A key released before heartbeat write completes must never start motion.
 ({j,axes}=setup());let heartbeat;axes.X.send=(cmd,fields)=>cmd==='heartbeat'?new Promise(r=>heartbeat=r):Promise.resolve(calls.push(['X',cmd,fields]));j.press('ArrowRight');const flight=j.tick(50,{});await j.release('ArrowRight');heartbeat();await flight;assert.equal(moves().length,0);
 ({j,axes}=setup());j.press('ArrowRight');await j.tick(0.5,{});assert.equal(moves()[0][2].speed_sps,0.5);await j.tick(100000,{});assert.equal(moves().at(-1)[2].speed_sps,100000);
 axes.Y.last.driver_fault_asserted=true;await j.tick(100000,{});assert.equal(j.held.size,0);assert.equal(j.active,null);
 ({j,axes}=setup());j.press('ArrowUp');await j.tick(50,{});await j.stop();const count=moves().length;await j.tick(50,{});assert.equal(moves().length,count);assert.equal(j.active,null);
 ({j,axes}=setup());axes.Y.last.capabilities.fixed_ramp_ms=null;assert.equal(j.press('ArrowUp'),false);
 console.log('PASS simultaneous axes, opposite cancellation, live full/half and speed changes, resume/reverse while braking, independent USB queues, release races, no speed ceiling, stops/faults');
})().catch(e=>{console.error(e);process.exitCode=1;});
