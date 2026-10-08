const assert=require('node:assert/strict');
const {Jogger}=require('../dist/gantry.js');
let now=100,calls=[];
function axis(name){return {port:{},seen:now,last:{protocol:3,temperature_c:25,motor_enabled:false,capabilities:{resolutions:{half:2},deceleration_supported:true,acceleration_supported:true}},send:async(cmd,fields)=>{calls.push([name,cmd,fields]);return {ok:true};}};}
function setup(){calls=[];const axes={X:axis('X'),Y:axis('Y')};return {j:new Jogger(axes,()=>{},()=>now),axes};}
(async()=>{
 let {j,axes}=setup();assert.equal(j.press('ArrowRight'),false);await j.tick(40,{});assert.equal(calls.length,0);
 j.armed=true;assert.equal(j.press('ArrowRight'),true);await j.tick(40,{},100);assert.deepEqual(calls[1],['X','start',{mode:'continuous',resolution:'half',direction:1,speed_sps:40,acceleration_sps2:100}]);
 await j.tick(40,{});assert.equal(calls.length,2);assert.equal(j.press('ArrowUp'),false);
 axes.X.last.motor_enabled=true;await j.tick(40,{});await j.release('ArrowRight');assert.equal(j.phase,'braking');assert.equal(j.key,null);assert.equal(j.active,'X');assert.equal(calls.at(-1)[1],'decelerate');assert.equal(j.press('ArrowLeft'),false);
 axes.X.last.motor_enabled=false;axes.X.last.stop_reason='Jog release complete';await j.tick(40,{});assert.equal(j.active,null);assert.equal(j.armed,true);assert.equal(j.press('ArrowUp'),true);await j.tick(40,{Y:true},80);assert.equal(calls.at(-1)[0],'Y');assert.equal(calls.at(-1)[2].direction,-1);assert.equal(calls.at(-1)[2].acceleration_sps2,80);
 axes.Y.last.driver_fault_asserted=true;await j.tick(40,{});assert.equal(j.armed,false);assert.deepEqual(calls.slice(-2).map(c=>c[1]),['stop','stop']);
 ({j,axes}=setup());axes.Y.last.capabilities.deceleration_supported=false;assert.equal(j.ready(),false);j.armed=true;j.press('ArrowLeft');await j.tick(40,{});assert.equal(j.armed,false);assert.equal(calls.some(c=>c[1]==='start'),false);
 ({j,axes}=setup());j.armed=true;j.press('ArrowRight');let resolve;axes.X.send=(cmd,fields)=>cmd==='heartbeat'?new Promise(r=>resolve=r):Promise.resolve(calls.push(['X',cmd,fields]));const inFlight=j.tick(40,{});await j.release('ArrowRight');resolve();await inFlight;assert.equal(calls.some(c=>c[1]==='start'),false);assert.equal(j.active,null);
 ({j,axes}=setup());j.armed=true;j.press('ArrowRight');await j.tick(40,{});await j.release('ArrowRight');now+=5000;axes.X.seen=now;axes.Y.seen=now;await j.tick(40,{});assert.equal(j.armed,false);
 ({j,axes}=setup());j.armed=true;j.press('ArrowRight');await j.tick(40,{});axes.X.last.motor_enabled=true;await j.tick(40,{});await j.stop();assert.equal(j.armed,false);assert.equal(j.active,null);assert.equal(j.key,null);assert.deepEqual(calls.slice(-2).map(c=>c[1]),['stop','stop']);
 ({j,axes}=setup());j.armed=true;j.press('ArrowRight');await j.tick(40,{});await j.release('ArrowRight');axes.X.seen=now-1001;await j.tick(40,{});assert.equal(j.armed,false);
 console.log('PASS half-step start, smooth release, reversal interlock, faults, stale telemetry, release race, timeout, emergency stop');
})().catch(e=>{console.error(e);process.exitCode=1;});
