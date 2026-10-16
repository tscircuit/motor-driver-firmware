const assert=require('node:assert/strict');
const fs=require('node:fs');
const {CurrentHistory}=require('../dist/current-chart.js');
let h=new CurrentHistory();
const bin=[2000,3000,201000,100,1000,2000,199000,100];
h.receive({type:'current_samples',first_sample_us:0,window_us:1000,sample_rate_hz:100000,bins:Array(20).fill(bin)},10000);
assert.equal(h.rate,100000);assert.equal(h.points.length,2);
assert.equal(h.points[0].w[0].count,1000);assert.equal(h.points[0].w[0].max,3000*3.3/4095-1.65);
assert.ok(Math.abs(h.points[0].w[0].sum/1000-(2010*3.3/4095-1.65))<1e-10);
h.receive({type:'telemetry',winding_current_a:[99,99]},10001);assert.equal(h.points.length,2);
h.receive({type:'current_samples',first_sample_us:20000,window_us:1000,sample_rate_hz:100000,bins:[[0,0,0,0,0,0,0,0]]},10020);
assert.deepEqual(h.points.at(-1).w,[null,null]);
for(let i=1;i<1500;i++)h.receive({type:'current_samples',first_sample_us:i*20000+20000,window_us:1000,sample_rate_hz:100000,bins:Array(20).fill(bin)},10020+i*20);
assert.ok(h.points.length<=1100);assert.ok(h.points[0].t>=39990-10000);
h.receive({type:'current_samples',first_sample_us:90000000,window_us:1000,sample_rate_hz:100000,bins:Array(20).fill(bin)},40020);assert.equal(h.points.length,2);assert.ok(h.points.at(-1).t<=40020);
h.reset();h.receive({type:'telemetry',winding_current_a:[-.5,.6]},10000);assert.equal(h.points[0].w[0].sum,-.5);
h.receive({type:'telemetry',winding_current_a:null},10250);assert.deepEqual(h.points.at(-1).w,[null,null]);
for(const file of ['index.html','single-motor.html','gantry.html']){const s=fs.readFileSync('dist/'+file,'utf8');assert.ok(s.includes('data-current-axis'));assert.ok(s.indexOf('src="current-chart.js"')<s.indexOf('src="'+(file==='index.html'?'app':'gantry')+'.js"'));}
h.reset();h.receive({type:'current_samples',first_sample_us:0,window_us:1000,sample_rate_hz:100000,current_volts_per_amp:0.2,bins:Array(20).fill(bin)},10000);assert.ok(Math.abs(h.points[0].w[0].max-5*(3000*3.3/4095-1.65))<1e-10);
console.log('PASS current charts: 100k capture, peak preservation, weighted averages, gaps, bounded history, legacy fallback, all pages');
const {TemperatureHistory}=require('../dist/current-chart.js');
const t=new TemperatureHistory();
t.receive({type:'telemetry',temperature_c:25,threshold_c:55,current_available:false},10000);
t.receive({type:'current_samples',bins:Array(20).fill(bin)},10001);
assert.equal(t.points.length,1);assert.equal(t.threshold,55);
t.receive({type:'telemetry',temperature_c:null,compact:true},10250);
t.receive({type:'telemetry',temperature_c:26,compact:true},10500);
assert.deepEqual(t.points.map(p=>p.value),[25,null,26]);assert.equal(t.threshold,55);
t.receive({type:'telemetry',motor_enabled:false},10750);assert.equal(t.points.length,3);
for(let i=0;i<1500;i++)t.receive({type:'telemetry',temperature_c:20+i/1000},11000+i*10);
assert.ok(t.points.length<=1100);assert.ok(t.points[0].t>=15990);
t.reset();assert.equal(t.points.length,0);assert.equal(t.seen,0);assert.equal(t.threshold,null);
// Exercise the production mounting/dispatch code with two independent boards.
const vm=require('node:vm');
class Node {
 constructor(tag){this.tag=tag;this.children=[];this.hidden=false;this.dataset={};this.attributes={};this.textContent='';this.clientWidth=600;}
 append(...nodes){for(const n of nodes){if(n.parent)n.parent.children=n.parent.children.filter(c=>c!==n);n.parent=this;this.children.push(n);}}
 setAttribute(k,v){this.attributes[k]=v;}
 querySelector(selector){return this.children.flatMap(c=>[c,...c.descendants()]).find(n=>selector==='h3'?n.tag==='h3':selector==='canvas'?n.tag==='canvas':Object.hasOwn(n.attributes,'data-current-summary'));}
 descendants(){return this.children.flatMap(c=>[c,...c.descendants()]);}
 getContext(){return new Proxy({},{get:()=>()=>{}});}
}
function card(axis){const c=new Node('section');c.dataset.currentAxis=axis;const h=new Node('h3');h.textContent=axis+' temperature & current';const summary=new Node('p');summary.setAttribute('data-current-summary','');c.append(h,new Node('canvas'),summary);return c;}
const cards=[card('X'),card('Y')];let tick;
const sandbox={document:{createElement:tag=>new Node(tag),querySelectorAll:()=>cards},requestAnimationFrame:fn=>{tick=fn;},devicePixelRatio:1,Date};
sandbox.window=sandbox;
vm.runInNewContext(fs.readFileSync('dist/current-stall.js','utf8'),sandbox);
vm.runInNewContext(fs.readFileSync('dist/current-chart.js','utf8'),sandbox);
const widget=sandbox.CurrentCharts;
widget.receive('X',{type:'telemetry',temperature_c:25,threshold_c:55,current_available:false,capabilities:{}});
widget.receive('Y',{type:'telemetry',temperature_c:28,current_available:true,winding_current_a:[.3,-.4],capabilities:{current_stream_supported:true}});
tick(100);
const plots=cards.map(c=>c.descendants());
assert.equal(plots[0].filter(n=>n.tag==='canvas').length,4);
assert.ok(plots[0].find(n=>n.className==='windingPlot').hidden);
assert.equal(plots[0].find(n=>n.className==='currentUnavailable').hidden,false);
assert.match(plots[0].find(n=>Object.hasOwn(n.attributes,'data-temperature-summary')).textContent,/25.0 °C.*Live temperature.*55 °C/);
assert.equal(plots[1].find(n=>n.className==='windingPlot').hidden,false);
assert.match(plots[1].find(n=>Object.hasOwn(n.attributes,'data-temperature-summary')).textContent,/28.0 °C/);
widget.receive('Y',{type:'telemetry',compact:true,temperature_c:29});tick(200);
assert.match(plots[1].find(n=>Object.hasOwn(n.attributes,'data-temperature-summary')).textContent,/29.0 °C/);
widget.disconnect('X');tick(300);
assert.match(plots[0].find(n=>Object.hasOwn(n.attributes,'data-temperature-summary')).textContent,/Waiting for live temperature/);
widget.reset('X');tick(400);
assert.match(plots[0].find(n=>Object.hasOwn(n.attributes,'data-temperature-summary')).textContent,/Temperature unavailable/);
assert.equal(plots[0].find(n=>n.className==='windingPlot').hidden,false);
for(const file of ['index.html','single-motor.html','gantry.html'])assert.match(fs.readFileSync('dist/'+file,'utf8'),/temperature &amp; current<\/h3>/);
console.log('PASS shared sensor widget: temperature without current, missing-sample gaps, compact telemetry, independent axes, mounting, disconnect and reconnect');

const {StallHistory}=require('../dist/current-chart.js');const st=new StallHistory();
st.receive({type:'telemetry',stall_detection_valid:true,stall_torque_count:100,stall_threshold:3},10000);
st.receive({type:'telemetry',stall_detection_valid:false,stall_torque_count:0,stall_threshold:3},10250);
assert.deepEqual(st.points.map(p=>p.value),[100,null]);
widget.receive('Y',{type:'telemetry',capabilities:{stall_detection_supported:true},stall_detection_valid:true,stall_torque_count:0,stall_suspected:true,stall_threshold:3});tick(500);
assert.equal(plots[1].find(n=>n.className==='stallPlot').hidden,false);assert.match(plots[1].find(n=>n.className==='stallPlot').children.at(-1).textContent,/Stall suspected/);
widget.receive('Y',{type:'telemetry',hardware_motion_inhibited:true,stall_detection_valid:false});tick(600);assert.match(plots[1].find(n=>n.className==='stallPlot').children.at(-1).textContent,/unknown.*interlock/);
widget.disconnect('Y');tick(700);assert.match(plots[1].find(n=>n.className==='stallPlot').children.at(-1).textContent,/waiting for telemetry/);

widget.reset('Y');
widget.receive('Y',{type:'telemetry',driver:'DRV8825',current_available:true,capabilities:{stall_detection_supported:false,torque_count_supported:false,stall_detection_note:'DRV8825 does not provide torque count or sensorless stall detection.'},stall_detection_valid:false,stall_torque_count:null,stall_suspected:null});
assert.equal(plots[1].find(n=>n.className==='stallPlot').hidden,true);
assert.equal(cards[1].descendants().find(n=>n.className==='stallUnavailable').hidden,false);
assert.match(cards[1].descendants().find(n=>n.className==='stallUnavailable').textContent,/DRV8825.*does not provide torque count/);
assert.equal(plots[1].find(n=>n.className==='windingPlot').hidden,false);
widget.reset('Y');assert.equal(cards[1].descendants().find(n=>n.className==='stallUnavailable').hidden,true);

widget.receive('Y',{type:'telemetry',driver:'DRV8825',device_id:'e66568714f117e21',board_id:'rp2040_drv8825',motor_enabled:false,motion_mode:'stopped',current_available:true,capabilities:{stall_detection_supported:false}});tick(800);
assert.equal(cards[1].descendants().find(n=>n.className==='currentStallPlot').hidden,false);
assert.match(cards[1].descendants().find(n=>n.attributes.role==='status').textContent,/Unknown/);
widget.reset('Y');assert.equal(cards[1].descendants().find(n=>n.className==='currentStallPlot').hidden,true);
