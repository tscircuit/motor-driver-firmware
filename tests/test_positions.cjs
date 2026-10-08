const assert=require('node:assert/strict');
const {Jogger,PositionSlots}=require('../dist/gantry.js');
let now=100,calls=[];
function setup(){
 calls=[];
 const axes={};
 for(const name of ['X','Y'])axes[name]={port:{},seen:now,last:{protocol:3,temperature_c:25,motor_enabled:false,device_id:name,position_session:'boot-1',position_full_steps:0,capabilities:{resolutions:{half:2},deceleration_supported:true,jog_update_supported:true,fixed_ramp_ms:400,max_steps:100000}},send:async function(cmd,fields){calls.push([name,cmd,fields]);this.seen=now;if(cmd==='stop'){this.last.motor_enabled=false;this.last.stop_reason='Stopped by user';}if(cmd==='start'){this.last.motor_enabled=true;this.move=fields;}if(cmd==='status'&&this.move){this.last.position_full_steps+=this.move.direction*this.move.steps/2;this.move=null;this.last.motor_enabled=false;this.last.stop_reason='Step move complete';}}};
 const storage={value:null,getItem(){return this.value;},setItem(k,v){this.value=v;}};
 const jog=new Jogger(axes,()=>{},()=>now);const positions=new PositionSlots(axes,jog,()=>{},storage,async()=>{});return {axes,jog,positions,storage};
}
(async()=>{
 let {axes,jog,positions,storage}=setup();
 axes.X.last.position_full_steps=10.5;axes.Y.last.position_full_steps=-2;
 assert.equal(positions.save(0),true);assert.equal(positions.valid(0),true);assert.equal(positions.slots.length,3);
 axes.X.last.position_full_steps=-1;axes.Y.last.position_full_steps=3;
 assert.equal(await positions.goto(0,40,100),true);
 const starts=calls.filter(c=>c[1]==='start');assert.equal(starts.length,2);assert.equal(starts[0][0],'X');assert.equal(starts[0][2].steps,23);assert.equal(starts[0][2].direction,1);assert.equal(starts[1][0],'Y');assert.equal(starts[1][2].steps,10);assert.equal(starts[1][2].direction,-1);assert.equal(starts[0][2].speed_sps,40);assert.equal(starts[0][2].ramp_ms,400);assert.ok(calls.findIndex(c=>c[0]==='Y'&&c[1]==='start')<calls.findIndex(c=>c[0]==='X'&&c[1]==='status'&&calls.indexOf(c)>calls.indexOf(starts[0])));assert.equal(axes.X.last.position_full_steps,10.5);assert.equal(axes.Y.last.position_full_steps,-2);assert.equal(jog.returning,false);
 assert.equal(new PositionSlots(axes,jog,()=>{},storage).valid(0),true);
 assert.equal(positions.save(1),true);assert.equal(positions.save(2),true);assert.equal(positions.save(3),false);
 axes.X.last.position_session='boot-2';assert.equal(positions.valid(0),false);const before=calls.length;assert.equal(await positions.goto(0,40,100),false);assert.equal(calls.length,before);
 ({axes,jog,positions}=setup());assert.equal(positions.save(0),true);axes.Y.last.device_id='other-board';assert.equal(positions.valid(0),false);
 ({axes,jog,positions}=setup());jog.press('ArrowUp');assert.equal(positions.save(0),false);await jog.stop();axes.X.last.motor_enabled=true;assert.equal(positions.save(0),false);
 // Stop cancels a simultaneous return and releases both axes.
 ({axes,jog,positions}=setup());axes.X.last.position_full_steps=3;axes.Y.last.position_full_steps=4;positions.save(0);axes.X.last.position_full_steps=0;axes.Y.last.position_full_steps=0;
 const original=axes.X.send;axes.X.send=async function(cmd,fields){await original.call(this,cmd,fields);if(cmd==='start'){assert.equal(jog.press('ArrowUp'),false);await jog.stop();}};
 assert.equal(await positions.goto(0,40,100),false);assert.equal(axes.X.last.motor_enabled,false);assert.equal(axes.Y.last.motor_enabled,false);assert.equal(jog.returning,false);
 // Y must start even while X's start acknowledgement is still pending.
 ({axes,jog,positions}=setup());axes.X.last.position_full_steps=100;axes.Y.last.position_full_steps=200;positions.save(0);axes.X.last.position_full_steps=0;axes.Y.last.position_full_steps=0;
 let startAck;const xSend=axes.X.send;axes.X.send=async function(cmd,fields){await xSend.call(this,cmd,fields);if(cmd==='start')await new Promise(r=>startAck=r);};
 const returning=positions.goto(0,100000);await new Promise(r=>setImmediate(r));
 assert.ok(startAck);assert.equal(calls.filter(c=>c[1]==='start').length,2);assert.equal(axes.Y.last.motor_enabled,true);assert.equal(positions.busy,true);
 assert.equal(calls.find(c=>c[1]==='start')[2].speed_sps,100000);assert.equal(calls.find(c=>c[1]==='start')[2].ramp_ms,400);
 startAck();assert.equal(await returning,true);
 // Keep waiting after the shorter axis finishes, and skip zero-distance axes.
 ({axes,jog,positions}=setup());axes.X.last.position_full_steps=1;axes.Y.last.position_full_steps=10;positions.save(0);axes.X.last.position_full_steps=0;axes.Y.last.position_full_steps=0;
 let polls=0;const ySend=axes.Y.send;axes.Y.send=async function(cmd,fields){if(cmd==='status'&&this.move&&++polls<3)return;await ySend.call(this,cmd,fields);};
 positions.pause=async()=>{assert.equal(axes.X.last.motor_enabled,false);assert.equal(axes.Y.last.motor_enabled,true);assert.equal(positions.busy,true);assert.equal(positions.save(1),false);};
 assert.equal(await positions.goto(0,150),true);assert.equal(polls,3);
 axes.Y.last.position_full_steps=0;calls=[];assert.equal(await positions.goto(0,150),true);assert.equal(calls.filter(c=>c[1]==='start').length,1);assert.equal(calls.find(c=>c[1]==='start')[0],'Y');
 // If one start fails, stop the other axis and cancel the whole return.
 ({axes,jog,positions}=setup());axes.X.last.position_full_steps=2;axes.Y.last.position_full_steps=2;positions.save(0);axes.X.last.position_full_steps=0;axes.Y.last.position_full_steps=0;
 const yOriginal=axes.Y.send;axes.Y.send=async function(cmd,fields){if(cmd==='start')throw Error('Rejected');await yOriginal.call(this,cmd,fields);};
 assert.equal(await positions.goto(0,150),false);assert.equal(axes.X.last.motor_enabled,false);assert.equal(jog.returning,false);
 // No move is sent if either board reports a fault or target counts exceed limits.
 ({axes,jog,positions}=setup());positions.save(0);axes.Y.last.driver_fault_asserted=true;assert.equal(await positions.goto(0,40,100),false);assert.equal(calls.some(c=>c[1]==='start'),false);
 ({axes,jog,positions}=setup());axes.X.last.position_full_steps=100000;positions.save(0);axes.X.last.position_full_steps=0;assert.equal(await positions.goto(0,40,100),false);assert.equal(calls.some(c=>c[1]==='start'),false);
 console.log('PASS three saved slots, exact half-step targets, parallel dispatch, automatic acceleration, unequal move completion, reload persistence, reset/board invalidation, stop cancellation, fault/count gating');
})().catch(e=>{console.error(e);process.exitCode=1;});
