(function(root){
'use strict';
const required='1.0.7';
function classify(d){
 const version=d?.firmware_version,match=/^(\d+)\.(\d+)\.(\d+)$/.exec(version||'');
 if(!match||d.firmware!=='native-c-pio')return {upgrade:true,label:version||'Unknown',reason:'Firmware version cannot be verified; install current firmware.'};
 const parts=match.slice(1).map(Number),minimum=required.split('.').map(Number);
 let comparison=0;for(let i=0;i<3&&!comparison;i++)comparison=Math.sign(parts[i]-minimum[i]);
 return {upgrade:comparison<0,label:version,reason:comparison<0?'Firmware '+required+' or newer is required.':'Firmware is up to date.'};
}
const devices=new Map();
function receive(axis,d){
 if(d.type!=='telemetry')return;
 let a=devices.get(axis);
 if(!a){const parent=root.document.getElementById(axis==='motor'?'connect':'connect'+axis)?.parentNode;if(!parent)return;const e=root.document.createElement('a');e.href='firmware.html';e.style.cssText='display:block;font-size:13px;margin:8px 0';parent.append(e);a={e,last:null};devices.set(axis,a);}
 a.last=d.compact?{...a.last,...d}:d;const s=classify(a.last);
 a.e.textContent=`${s.upgrade?'⚠ ':''}Firmware ${s.label}${a.last.firmware_build?' · '+a.last.firmware_build.slice(0,8):''}${s.upgrade?' · Upgrade needed':''}`;
 a.e.title=s.reason;a.e.setAttribute('aria-label',a.e.textContent+'. '+s.reason);a.e.style.color=s.upgrade?'#b45309':'inherit';
}
function disconnect(axis){const a=devices.get(axis);if(a){a.e.textContent='Firmware — disconnected';a.e.title='Connect to read firmware version';a.e.style.color='inherit';a.last=null;}}
const api={classify,receive,disconnect,required};if(typeof module!=='undefined')module.exports=api;if(root.document)root.FirmwareStatus=api;
})(typeof window==='undefined'?globalThis:window);
