const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const nodes=[];
function element(tag){const e={tag,children:[],value:'',textContent:'',append(...items){this.children.push(...items)},setAttribute(){},addEventListener(name,fn){this[name]=fn}};nodes.push(e);return e;}
const parent=element('div'),calls=[];let now=10000;
const sandbox={Date:{now:()=>now},window:{document:{getElementById:()=>parent,createElement:element},setInterval(){}},console};
vm.runInNewContext(fs.readFileSync('dist/stall-controls.js','utf8'),sandbox);
const api=sandbox.window.StallControls;
const d={type:'telemetry',capabilities:{stall_response_supported:true},motion_mode:'stopped',motor_enabled:false,stall_beeping_on:true,stall_stop_ms:0};
const send=async(cmd,fields)=>calls.push({cmd,fields});
(async()=>{
 api.receive('X',{...d,capabilities:{}},send);assert.equal(parent.children.length,0);
 api.receive('X',d,send);const panel=parent.children[0],inputs=nodes.filter(n=>n.tag==='input'),[beep,delay]=inputs,apply=nodes.find(n=>n.tag==='button'),status=nodes.find(n=>n.attributes?.role==='status')||panel.children[4];
 assert.equal(beep.checked,true);assert.equal(delay.value,'0');assert.equal(apply.disabled,false);
 delay.value='1.25';delay.input();beep.checked=false;beep.change();
 api.receive('X',{type:'telemetry',compact:true,temperature_c:35},send);assert.equal(delay.value,'1.25');assert.equal(beep.checked,false);
 await apply.click();assert.equal(calls[0].cmd,'set_stall_response');assert.equal(calls[0].fields.stop_ms,1250);assert.equal(calls[0].fields.beeping_on,false);assert.equal(calls[1].cmd,'status');
 api.receive('X',{...d,stall_stop_ms:1250,stall_beeping_on:false},send);assert.equal(delay.value,'1.25');
 delay.value='-1';await apply.click();assert.equal(calls.length,2);
 api.receive('X',{...d,motor_enabled:true,motion_mode:'continuous'},send);assert.equal(apply.disabled,true);assert.equal(beep.disabled,true);
 api.receive('X',d,send);api.disconnect('X');assert.equal(apply.disabled,true);
 console.log('PASS stall controls: capability gating, defaults, command values, editing, movement and disconnect');
})().catch(e=>{console.error(e);process.exitCode=1;});
