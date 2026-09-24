const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const elements=new Map();
function element(id){if(!elements.has(id))elements.set(id,{id,value:'',disabled:false,textContent:'',style:{},classList:{toggle(){}},addEventListener(type,fn){this[type]=fn;},getBoundingClientRect(){return {width:640,height:300};},getContext(){return new Proxy({},{get:()=>()=>{}});}});return elements.get(id);}
const context={document:{getElementById:element,addEventListener(){},hidden:false},window:{isSecureContext:true,addEventListener(){},devicePixelRatio:1},navigator:{serial:{addEventListener(){}}},ResizeObserver:class{observe(){}},setInterval(){},setTimeout,clearTimeout,TextEncoder,TextDecoder,console};
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
