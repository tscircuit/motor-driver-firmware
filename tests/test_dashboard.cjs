const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const elements=new Map();
function element(id){if(!elements.has(id))elements.set(id,{id,value:'',disabled:false,textContent:'',style:{},replaceChildren(...children){this.children=children;},classList:{toggle(){}},addEventListener(type,fn){this[type]=fn;},getBoundingClientRect(){return {width:640,height:300};},getContext(){return new Proxy({},{get:()=>()=>{}});}});return elements.get(id);}
const context={document:{getElementById:element,createElement:()=>({}),addEventListener(){},hidden:false},window:{isSecureContext:true,addEventListener(){},devicePixelRatio:1},navigator:{serial:{addEventListener(){}}},ResizeObserver:class{observe(){}},setInterval(){},setTimeout,clearTimeout,TextEncoder,TextDecoder,console};
vm.createContext(context);vm.runInContext(fs.readFileSync(__dirname+'/../dist/app.js','utf8'),context);
const run=code=>vm.runInContext(code,context);
const telemetry={type:'telemetry',protocol:3,threshold_c:45,temperature_c:25,device_name:'<Left motor>',usb_name:'<Left motor>',device_id:'test123',usb_name_supported:true,motor_enabled:false};
run('port={writable:{}}');
context.packet=JSON.stringify(telemetry);run('receive(packet)');
assert.equal(element('deviceIdentity').textContent,'<Left motor> · test123');
assert.equal(element('deviceName').value,'<Left motor>');assert.equal(element('rename').disabled,false);
run('nameDirty=true');element('deviceName').value='Editing';run('receive(packet)');assert.equal(element('deviceName').value,'Editing');
for(const change of [{motor_enabled:true},{usb_name_supported:false},{restarting:true}]){
 context.packet=JSON.stringify({...telemetry,...change});run('receive(packet)');
 if(change.restarting)assert.equal(element('start').disabled,true);else assert.equal(element('rename').disabled,true);
}
run('lastSeen=0;controls()');assert.equal(element('rename').disabled,true);
console.log('PASS name display, edit preservation, motion/compatibility/stale gating, restart interlock');

context.packet=JSON.stringify({...telemetry,board:'Other MCU / driver',current_available:true,current_a:0.25,capabilities:{resolutions:{full:1,quarter:4},full_steps_per_revolution:400,min_speed_sps:10,max_speed_sps:80,shutdown_c:70}});run('receive(packet)');
assert.equal(element('boardLabel').textContent,'Other MCU / driver');
assert.equal(element('current').textContent,'0.25 A');
assert.deepEqual(Array.from(element('resolution').children,x=>x.value),['full','quarter']);
element('resolution').value='quarter';element('steps').value='1600';element('speed').value='40';element('mode').value='steps';run('updateEstimate()');
assert.match(element('moveEstimate').textContent,/1600 quarter steps = 360.0°/);
assert.equal(element('speed').max,80);assert.equal(element('shutdown').textContent,'70°C · fixed');
console.log('PASS alternate board capabilities, quarter-step estimates and measured current');

assert.equal(element('tempHint').textContent,'Live · 4 readings per second');
context.packet=JSON.stringify({...telemetry,capabilities:{telemetry_interval_ms:125}});
run('receive(packet)');
assert.equal(element('tempHint').textContent,'Live · 8 readings per second');
run('points=[]');
for(let i=0;i<2401;i++)run('receive(packet)');
assert.equal(run('points.length'),2401);
assert.equal(element('samples').textContent,'2401 samples');
console.log('PASS telemetry rate compatibility and five-minute buffer at 8 Hz');
