const assert=require('node:assert/strict');
const {Jogger,PositionSlots}=require('../dist/gantry.js');
let now=100,calls=[];
function setup(){
 calls=[];const axes={};
 for(const name of ['X','Y'])axes[name]={port:{},seen:now,last:{protocol:3,temperature_c:25,motor_enabled:false,device_id:name,position_session:'boot-1',position_full_steps:0,capabilities:{resolutions:{half:2},fixed_ramp_ms:400,deferred_start_supported:true,max_steps:100000}},
  complete(){this.last.position_full_steps+=this.move.direction*this.move.steps/2;this.move=null;this.last.motor_enabled=false;this.last.motion_mode='stopped';this.last.stop_reason='Step move complete';this.seen=now;},
  async send(cmd,fields){calls.push([name,cmd,fields]);this.seen=now;
   if(cmd==='stop'){this.move=null;this.last.motor_enabled=false;this.last.stop_reason='Stopped by user';}
   if(cmd==='start'){this.last.motor_enabled=true;this.last.motion_mode='prepared';this.move=fields;}
   if(cmd==='run_move')this.last.motion_mode='steps';
   if(cmd==='status'&&this.move&&this.last.motion_mode==='steps')this.complete();
  }};
 const storage={value:null,getItem(){return this.value;},setItem(k,v){this.value=v;}};
 const jog=new Jogger(axes,()=>{},()=>now),positions=new PositionSlots(axes,jog,()=>{},storage,async()=>{});
 return {axes,jog,positions,storage};
}
function target(axes,positions,x,y){axes.X.last.position_full_steps=x;axes.Y.last.position_full_steps=y;assert.equal(positions.save(0),true);axes.X.last.position_full_steps=axes.Y.last.position_full_steps=0;}
(async()=>{
 let {axes,jog,positions,storage}=setup();target(axes,positions,11.5,-5);
 assert.equal(await positions.goto(0,40),true);
 const starts=calls.filter(c=>c[1]==='start');assert.equal(starts.length,2);
 assert.equal(starts[0][2].steps,23);assert.equal(starts[1][2].steps,10);assert.equal(starts[1][2].direction,-1);
 assert.equal(starts[0][2].speed_sps,40);assert.equal(starts[1][2].speed_sps,40*10/23);
 assert.equal(starts[0][2].ramp_ms,400);assert.equal(starts[0][2].defer,true);
 assert.equal(calls.some(c=>c[1]==='stop'),false);assert.equal(calls.filter(c=>c[1]==='status').length,2);
 assert.ok(calls.findIndex(c=>c[1]==='run_move')>calls.findIndex(c=>c[0]==='Y'&&c[1]==='start'));
 assert.equal(axes.X.last.position_full_steps,11.5);assert.equal(axes.Y.last.position_full_steps,-5);assert.equal(jog.returning,false);
 assert.equal(new PositionSlots(axes,jog,()=>{},storage).valid(0),true);
 assert.equal(positions.save(1),true);assert.equal(positions.save(2),true);assert.equal(positions.save(3),false);
 axes.X.last.position_session='boot-2';const before=calls.length;assert.equal(await positions.goto(0,40),false);assert.equal(calls.length,before);
 ({axes,jog,positions}=setup());positions.save(0);axes.Y.last.device_id='other';assert.equal(positions.valid(0),false);
 ({axes,jog,positions}=setup());jog.press('ArrowUp');assert.equal(positions.save(0),false);await jog.stop();axes.X.last.motor_enabled=true;assert.equal(positions.save(0),false);
 // Both preparations finish before either axis runs, even with delayed USB ACKs.
 ({axes,jog,positions}=setup());target(axes,positions,100,200);
 let preparedAck;const original=axes.X.send;axes.X.send=async function(cmd,fields){await original.call(this,cmd,fields);if(cmd==='start')await new Promise(r=>preparedAck=r);};
 const returning=positions.goto(0,100000);await new Promise(r=>setImmediate(r));
 assert.ok(preparedAck);assert.equal(calls.filter(c=>c[1]==='start').length,2);assert.equal(calls.some(c=>c[1]==='run_move'),false);
 assert.equal(calls.find(c=>c[0]==='Y'&&c[1]==='start')[2].speed_sps,100000);
 preparedAck();assert.equal(await returning,true);
 // Wait for spontaneous completion telemetry; do not poll status mid-move.
 ({axes,jog,positions}=setup());target(axes,positions,1,10);
 const ySend=axes.Y.send;axes.Y.send=async function(cmd,fields){if(cmd==='status'){calls.push(['Y',cmd,fields]);return;}await ySend.call(this,cmd,fields);};
 let frames=0;positions.pause=async()=>{assert.equal(positions.busy,true);assert.equal(positions.save(1),false);if(++frames===3)axes.Y.complete();};
 assert.equal(await positions.goto(0,150),true);assert.equal(frames,3);assert.equal(calls.filter(c=>c[1]==='status').length,2);
 axes.Y.last.position_full_steps=0;axes.Y.send=ySend;calls=[];assert.equal(await positions.goto(0,150),true);assert.equal(calls.filter(c=>c[1]==='start').length,1);
 // Stop during prepare cancels both, and never issues run_move.
 ({axes,jog,positions}=setup());target(axes,positions,3,4);const x=axes.X.send;axes.X.send=async function(cmd,fields){await x.call(this,cmd,fields);if(cmd==='start')await jog.stop();};
 assert.equal(await positions.goto(0,40),false);assert.equal(calls.some(c=>c[1]==='run_move'),false);assert.equal(axes.X.last.motor_enabled,false);assert.equal(axes.Y.last.motor_enabled,false);
 ({axes,jog,positions}=setup());target(axes,positions,2,2);const y=axes.Y.send;axes.Y.send=async function(cmd,fields){if(cmd==='start')throw Error('Rejected');await y.call(this,cmd,fields);};
 assert.equal(await positions.goto(0,150),false);assert.equal(axes.X.last.motor_enabled,false);assert.equal(jog.returning,false);
 ({axes,jog,positions}=setup());target(axes,positions,3,4);axes.Y.last.driver_fault_asserted=true;assert.equal(await positions.goto(0,40),false);assert.equal(calls.some(c=>c[1]==='start'),false);
 ({axes,jog,positions}=setup());target(axes,positions,100000,1);assert.equal(await positions.goto(0,40),false);assert.equal(calls.some(c=>c[1]==='start'),false);
 console.log('PASS coordinated speed ratios and preparation barrier, exact positions, no mid-move status polling/stops, delayed completion, cancellation, faults and references');
})().catch(e=>{console.error(e);process.exitCode=1;});
