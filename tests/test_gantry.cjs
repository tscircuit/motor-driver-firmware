const assert=require('node:assert/strict');
const {Jogger}=require('../dist/gantry.js');
let now=100, calls=[];
function axis(name){return {port:{},seen:now,last:{protocol:3,temperature_c:25,motor_enabled:false,capabilities:{acceleration_supported:true}},send:async(cmd,fields)=>{calls.push([name,cmd,fields]);return {ok:true};}};}
function setup(){calls=[];const axes={X:axis('X'),Y:axis('Y')};const j=new Jogger(axes,()=>{},()=>now);return {j,axes};}
(async()=>{
 let {j,axes}=setup();j.key='ArrowRight';await j.tick(20,{});assert.equal(calls.length,0);
 j.armed=true;await j.tick(20,{});assert.equal(calls[1][1],'start');assert.equal(calls[1][2].steps,4);assert.equal(calls[1][2].direction,1);
 await j.tick(20,{});assert.equal(calls.length,2); // no duplicate start before motion observed
 axes.X.last.motor_enabled=true;await j.tick(20,{});axes.X.last.motor_enabled=false;axes.X.last.stop_reason='Step move complete';await j.tick(20,{});await j.tick(20,{});assert.equal(calls.filter(c=>c[1]==='start').length,2);
 await j.release('ArrowRight');assert.equal(j.key,null);assert.deepEqual(calls.slice(-2).map(c=>c[1]),['stop','stop']);
 ({j,axes}=setup());j.armed=true;j.key='ArrowUp';await j.tick(20,{Y:true});assert.equal(calls[1][0],'Y');assert.equal(calls[1][2].direction,-1);
 axes.Y.last.driver_fault_asserted=true;await j.tick(20,{});assert.equal(j.armed,false);assert.equal(j.key,null);
 ({j,axes}=setup());j.armed=true;j.key='ArrowLeft';axes.X.seen=-2000;await j.tick(20,{});assert.equal(j.armed,false);assert.equal(calls.some(c=>c[1]==='start'),false);
 ({j,axes}=setup());j.armed=true;j.key='ArrowRight';let resolve;axes.X.send=(cmd,fields)=>cmd==='heartbeat'?new Promise(r=>resolve=r):Promise.resolve(calls.push(['X',cmd,fields]));const inFlight=j.tick(20,{});await j.release('ArrowRight');resolve();await inFlight;assert.equal(calls.some(c=>c[1]==='start'),false);
 ({j,axes}=setup());j.armed=true;j.key='ArrowRight';await j.tick(20,{});now+=3100;axes.X.seen=now;axes.Y.seen=now;await j.tick(20,{});assert.equal(j.armed,false);
 console.log('Gantry safety and jog tests passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
