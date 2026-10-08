const assert=require('node:assert/strict');
const {Jogger}=require('../dist/gantry.js');
let now=100,calls=[];
function axis(name){return {port:{},seen:now,last:{protocol:3,temperature_c:25,motor_enabled:false,capabilities:{resolutions:{half:2},deceleration_supported:true,acceleration_supported:true}},send:async(cmd,fields)=>{calls.push([name,cmd,fields]);return {ok:true};}};}
function setup(){calls=[];const axes={X:axis('X'),Y:axis('Y')};return {j:new Jogger(axes,()=>{},()=>now),axes};}
const starts=()=>calls.filter(c=>c[1]==='start');
(async()=>{
 let {j,axes}=setup();assert.equal(j.press('ArrowRight'),true);await j.tick(40,{},100);assert.deepEqual(starts()[0],['X','start',{mode:'continuous',resolution:'half',direction:1,speed_sps:40,acceleration_sps2:100}]);
 await j.tick(40,{});assert.equal(starts().length,1);assert.equal(j.press('ArrowUp'),false);
 axes.X.last.motor_enabled=true;await j.tick(40,{});await j.release('ArrowRight');assert.equal(j.phase,'braking');assert.equal(j.key,null);assert.equal(j.active,'X');assert.equal(calls.at(-2)[1],'decelerate');assert.equal(calls.at(-1)[1],'status');assert.equal(j.press('ArrowLeft'),false);
 axes.X.last.motor_enabled=false;axes.X.last.stop_reason='Jog release complete';await j.tick(40,{});assert.equal(j.active,null);assert.equal(j.press('ArrowUp'),true);await j.tick(40,{Y:true},80);assert.equal(starts().at(-1)[0],'Y');assert.equal(starts().at(-1)[2].direction,-1);assert.equal(starts().at(-1)[2].acceleration_sps2,80);
 axes.Y.last.driver_fault_asserted=true;await j.tick(40,{});assert.equal(j.key,null);assert.equal(j.active,null);assert.equal(j.press('ArrowUp'),false);axes.Y.last.driver_fault_asserted=false;assert.equal(j.press('ArrowUp'),true);
 ({j,axes}=setup());axes.Y.last.capabilities.deceleration_supported=false;assert.equal(j.ready(),false);assert.equal(j.press('ArrowLeft'),false);await j.tick(40,{});assert.equal(starts().length,0);
 ({j,axes}=setup());j.press('ArrowRight');let resolve;axes.X.send=(cmd,fields)=>cmd==='heartbeat'?new Promise(r=>resolve=r):Promise.resolve(calls.push(['X',cmd,fields]));const inFlight=j.tick(40,{});await j.release('ArrowRight');resolve();await inFlight;assert.equal(starts().length,0);assert.equal(j.active,null);
 ({j,axes}=setup());j.press('ArrowRight');await j.tick(40,{});await j.release('ArrowRight');now+=5000;axes.X.seen=now;axes.Y.seen=now;await j.tick(40,{});assert.equal(j.active,'X');assert.equal(j.phase,'braking');axes.X.last.stop_reason='Jog release complete';await j.tick(40,{});assert.equal(j.active,null);assert.equal(j.press('ArrowRight'),true);
 ({j,axes}=setup());j.press('ArrowRight');await j.tick(40,{});axes.X.last.motor_enabled=true;await j.tick(40,{});await j.stop('Focus lost');assert.equal(j.active,null);assert.equal(j.key,null);const count=starts().length;axes.X.last.motor_enabled=false;await j.tick(40,{});assert.equal(starts().length,count);assert.equal(j.press('ArrowRight'),true);await j.tick(40,{});assert.equal(starts().length,count+1);
 ({j,axes}=setup());j.press('ArrowRight');await j.tick(40,{});await j.release('ArrowRight');axes.X.seen=now-1001;await j.tick(40,{});assert.equal(j.key,null);assert.equal(j.active,null);assert.equal(j.press('ArrowRight'),false);axes.X.seen=now;assert.equal(j.press('ArrowRight'),true);
 ({j,axes}=setup());j.press('ArrowRight');await j.tick(40,{});now+=5000;axes.X.seen=now;axes.Y.seen=now;await j.tick(40,{});assert.equal(j.active,'X');assert.equal(j.phase,'starting');assert.equal(j.key,'ArrowRight');await j.stop();
 // A stopped-status acknowledgement is required before a new jog can start.
 ({j,axes}=setup());let stopped;axes.X.send=async cmd=>{if(cmd==='status')await new Promise(r=>stopped=r);};const stopping=j.stop();await new Promise(r=>setImmediate(r));assert.equal(j.press('ArrowRight'),false);stopped();await stopping;assert.equal(j.press('ArrowRight'),true);
 // Old completion telemetry must not clear an in-flight deceleration request.
 ({j,axes}=setup());j.press('ArrowRight');await j.tick(40,{});j.moving=true;let brakeAck;axes.X.send=cmd=>cmd==='decelerate'?new Promise(r=>brakeAck=r):Promise.resolve();axes.X.last.stop_reason='Jog release complete';const releasing=j.release('ArrowRight');await j.tick(40,{});assert.equal(j.active,'X');brakeAck();await releasing;await j.tick(40,{});assert.equal(j.active,null);
 console.log('PASS direct key jog, smooth release, focus/stop recovery without rearming, fault/stale gating, release races, stop interlock');
})().catch(e=>{console.error(e);process.exitCode=1;});
