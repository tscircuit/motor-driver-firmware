/* Shared temperature/current widget. Retain 100 kHz winding peaks in 10 ms display buckets. */
(function(root){
'use strict';
const scale=(raw,voltsPerAmp=1)=>(raw*3.3/4095-1.65)/voltsPerAmp;
class CurrentHistory {
 constructor(){this.reset();}
 reset(){this.points=[];this.anchor=null;this.lastBatch=0;this.rate=0;this.dropped=0;this.seen=0;}
 receive(d,now=Date.now()){
  if(d.type==='current_samples'){
   if(!Array.isArray(d.bins)||!Number.isFinite(d.first_sample_us)||d.window_us!==1000)return;
   const end=d.first_sample_us/1000+d.bins.length;
   // A reconnect may deliver queued frames from the previous USB session first.
   if(this.anchor!==null&&this.anchor+end>now+250){this.anchor=null;this.points=[];}
   if(this.anchor===null)this.anchor=now-end;
   this.lastBatch=now;this.seen=now;this.rate=d.sample_rate_hz;this.dropped=d.dropped_samples||0;
   const voltsPerAmp=[0.2,1].includes(d.current_volts_per_amp)?d.current_volts_per_amp:1;
   d.bins.slice(0,32).forEach((bin,i)=>{
    if(!Array.isArray(bin)||bin.length!==8)return;
    const t=this.anchor+(d.first_sample_us+i*d.window_us)/1000;
    const key=Math.floor(t/10)*10;
    let p=this.points.at(-1);
    if(!p||p.t!==key){p={t:key,w:[null,null]};this.points.push(p);}
    for(let w=0;w<2;w++){
     const [min,max,sum,count]=bin.slice(w*4,w*4+4);
     if(!count||count>100||!Number.isFinite(sum)||min<16||max>4079||min>max)continue;
     const s=p.w[w]||(p.w[w]={min:Infinity,max:-Infinity,sum:0,count:0});
     s.min=Math.min(s.min,scale(min,voltsPerAmp));s.max=Math.max(s.max,scale(max,voltsPerAmp));
     s.sum+=scale(sum/count,voltsPerAmp)*count;s.count+=count;
    }
   });
  }else if(d.type==='telemetry'&&now-this.lastBatch>1000){
   this.seen=now;this.rate=0;
   const a=d.winding_current_a;
   this.points.push({t:now,w:[0,1].map(w=>Array.isArray(a)&&Number.isFinite(a[w])?{min:a[w],max:a[w],sum:a[w],count:1}:null)});
  }
  this.points=this.points.filter(p=>p.t>=now-10000).slice(-1100);
 }
}
// Temperature uses the same rolling time window as winding current, independently
// of current-frame frequency and availability. Missing samples remain gaps.
class TemperatureHistory {
 constructor(){this.reset();}
 reset(){this.points=[];this.seen=0;this.threshold=null;}
 receive(d,now=Date.now()){
  if(d.type!=='telemetry'||!Object.hasOwn(d,'temperature_c'))return;
  const value=Number.isFinite(d.temperature_c)?d.temperature_c:null;
  this.points.push({t:now,value});this.seen=now;
  if(Object.hasOwn(d,'threshold_c'))this.threshold=Number.isFinite(d.threshold_c)?d.threshold_c:null;
  this.points=this.points.filter(p=>p.t>=now-10000).slice(-1100);
 }
}
class StallHistory extends TemperatureHistory {
 receive(d,now=Date.now()){
  if(d.type!=='telemetry'||!Object.hasOwn(d,'stall_detection_valid'))return;
  super.receive({type:'telemetry',temperature_c:d.stall_detection_valid&&Number.isFinite(d.stall_torque_count)?d.stall_torque_count:null,threshold_c:d.stall_threshold},now);
  this.suspected=!!d.stall_suspected;this.inhibited=!!d.hardware_motion_inhibited;
 }
}
const charts=new Map();
function drawCurrent(chart,now){
 const {canvas,history:h,summary}=chart,ctx=canvas.getContext('2d');
 const width=canvas.clientWidth||600,height=220,dpr=root.devicePixelRatio||1;
 if(canvas.width!==width*dpr||canvas.height!==height*dpr){canvas.width=width*dpr;canvas.height=height*dpr;}
 ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,width,height);
 const left=49,right=width-12,top=12,bottom=height-30;
 const visible=h.points.filter(p=>p.t>=now-10000);
 let range=.1;for(const p of visible)for(const s of p.w)if(s)range=Math.max(range,Math.abs(s.min),Math.abs(s.max));range=Math.ceil(range*12)/10;
 const x=t=>left+(t-(now-10000))/10000*(right-left),y=v=>top+(range-v)/(2*range)*(bottom-top);
 ctx.font='12px system-ui';ctx.strokeStyle='#334155';ctx.fillStyle='#94a3b8';ctx.lineWidth=1;
 for(let j=-2;j<=2;j++){const v=j*range/2;ctx.beginPath();ctx.moveTo(left,y(v));ctx.lineTo(right,y(v));ctx.stroke();ctx.fillText(v.toFixed(2),3,y(v)+4);}
 for(let j=0;j<=5;j++)ctx.fillText(j===5?'now':`${10-2*j}s`,left+j*(right-left)/5-8,height-8);
 const colors=['#38bdf8','#fbbf24'];
 for(let w=0;w<2;w++){
  ctx.strokeStyle=colors[w];ctx.globalAlpha=.22;ctx.lineWidth=Math.max(1,(right-left)/1000);
  ctx.beginPath();for(const p of visible){const s=p.w[w];if(s){ctx.moveTo(x(p.t),y(s.min));ctx.lineTo(x(p.t),y(s.max));}}ctx.stroke();
  ctx.globalAlpha=1;ctx.lineWidth=1.5;ctx.beginPath();let prev=null;
  for(const p of visible){const s=p.w[w];if(!s){prev=null;continue;}const v=s.sum/s.count;
   if(prev!==null&&p.t-prev<(h.rate?15:600))ctx.lineTo(x(p.t),y(v));else ctx.moveTo(x(p.t),y(v));prev=p.t;
  }ctx.stroke();
 }
 const last=visible.at(-1),values=last?last.w.map(s=>s?`${(s.sum/s.count).toFixed(3)} A`:'—'):['—','—'];
 const live=h.seen&&now-h.seen<1500;
 summary.textContent=`A: ${values[0]} · B: ${values[1]} · ${live?(h.rate?`${h.rate.toLocaleString()} samples/sec per winding`:'Standard telemetry'):'Waiting for live current'}${h.dropped?' · '+h.dropped.toLocaleString()+' samples skipped':''}`;
}
function drawTemperature(chart,now){
 const {temperatureCanvas:canvas,temperatureHistory:h,temperatureSummary:summary}=chart,ctx=canvas.getContext('2d');
 const width=canvas.clientWidth||600,height=220,dpr=root.devicePixelRatio||1;
 if(canvas.width!==width*dpr||canvas.height!==height*dpr){canvas.width=width*dpr;canvas.height=height*dpr;}
 ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,width,height);
 const left=49,right=width-12,top=12,bottom=height-30;
 const visible=h.points.filter(p=>p.t>=now-10000),values=visible.filter(p=>p.value!==null).map(p=>p.value);
 let low=Math.floor((Math.min(...values,20)-5)/5)*5,high=Math.ceil((Math.max(...values,30)+5)/5)*5;
 if(h.threshold!==null){low=Math.min(low,Math.floor(h.threshold/5)*5);high=Math.max(high,Math.ceil(h.threshold/5)*5+5);}
 const x=t=>left+(t-(now-10000))/10000*(right-left),y=v=>top+(high-v)/(high-low)*(bottom-top);
 ctx.font='12px system-ui';ctx.strokeStyle='#334155';ctx.fillStyle='#94a3b8';ctx.lineWidth=1;
 for(let j=0;j<=4;j++){const v=low+(high-low)*j/4;ctx.beginPath();ctx.moveTo(left,y(v));ctx.lineTo(right,y(v));ctx.stroke();ctx.fillText(v.toFixed(1),3,y(v)+4);}
 for(let j=0;j<=5;j++)ctx.fillText(j===5?'now':`${10-2*j}s`,left+j*(right-left)/5-8,height-8);
 if(h.threshold!==null){ctx.strokeStyle='#fb7185';ctx.setLineDash([5,4]);ctx.beginPath();ctx.moveTo(left,y(h.threshold));ctx.lineTo(right,y(h.threshold));ctx.stroke();ctx.setLineDash([]);}
 ctx.strokeStyle='#75efd0';ctx.lineWidth=2;ctx.beginPath();let prev=null;
 for(const p of visible){if(p.value===null){prev=null;continue;}if(prev!==null&&p.t-prev<1000)ctx.lineTo(x(p.t),y(p.value));else ctx.moveTo(x(p.t),y(p.value));prev=p.t;}
 ctx.stroke();
 const last=visible.at(-1),live=h.seen&&now-h.seen<1500;
 summary.textContent=`${last?.value!=null?last.value.toFixed(1)+' °C':'Temperature unavailable'} · ${live?'Live temperature':'Waiting for live temperature'}${h.threshold!==null?' · Alarm at '+h.threshold+' °C':''}`;
}
function drawStall(chart,now){
 const {stallCanvas:canvas,stallHistory:h,stallSummary:summary}=chart,ctx=canvas.getContext('2d');
 const width=canvas.clientWidth||600,height=220,dpr=root.devicePixelRatio||1;
 if(canvas.width!==width*dpr||canvas.height!==height*dpr){canvas.width=width*dpr;canvas.height=height*dpr;}
 ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,width,height);
 const left=49,right=width-12,top=12,bottom=height-30,points=h.points.filter(p=>p.t>=now-10000);
 const max=Math.max(10,...points.map(p=>p.value??0),(h.threshold??3)*2);
 const x=t=>left+(t-now+10000)/10000*(right-left),y=v=>bottom-v/max*(bottom-top);
 ctx.font='12px system-ui';ctx.strokeStyle='#334155';ctx.fillStyle='#94a3b8';ctx.lineWidth=1;
 for(let j=0;j<=4;j++){const v=max*j/4;ctx.beginPath();ctx.moveTo(left,y(v));ctx.lineTo(right,y(v));ctx.stroke();ctx.fillText(v.toFixed(0),3,y(v)+4);}
 for(let j=0;j<=5;j++)ctx.fillText(j===5?'now':`${10-2*j}s`,left+j*(right-left)/5-8,height-8);
 if(h.threshold!==null){ctx.strokeStyle='#fb7185';ctx.setLineDash([5,4]);ctx.beginPath();ctx.moveTo(left,y(h.threshold));ctx.lineTo(right,y(h.threshold));ctx.stroke();ctx.setLineDash([]);}
 ctx.strokeStyle='#c4b5fd';ctx.lineWidth=2;ctx.beginPath();let prev=null;
 for(const p of points){if(p.value===null){prev=null;continue;}if(prev!==null&&p.t-prev<1000)ctx.lineTo(x(p.t),y(p.value));else ctx.moveTo(x(p.t),y(p.value));prev=p.t;}ctx.stroke();
 const live=h.seen&&now-h.seen<1500,last=points.at(-1);
 summary.textContent=!live?'Stall status unknown · waiting for telemetry.':h.inhibited?'Stall status unknown · hardware interlock active.':last?.value==null?'Stall status unknown · collecting torque data.':`${h.suspected?'Stall suspected':'No stall indication'} · Torque count ${last.value} · Threshold ${h.threshold} · Uncalibrated sensorless estimate`;
}
function mount(card){
 const document=root.document,heading=card.querySelector('h3');
 const node=(tag,className,text)=>{const n=document.createElement(tag);if(className)n.className=className;if(text)n.textContent=text;return n;};
 const plots=node('div','sensorPlots'),temperaturePanel=node('section','temperaturePlot');
 const temperatureCanvas=node('canvas'),temperatureSummary=node('p');
 temperatureCanvas.setAttribute('aria-label',`${heading.textContent}: board temperature over the last ten seconds`);temperatureCanvas.setAttribute('role','img');
 temperatureSummary.setAttribute('data-temperature-summary','');
 temperatureSummary.textContent='Waiting for live temperature';
 const temperatureLegend=node('div','temperatureLegend','Temperature');temperatureLegend.append(node('span',null,'Alarm threshold'));
 temperaturePanel.append(node('h4',null,'Board temperature · °C'),temperatureLegend,temperatureCanvas,temperatureSummary);
 const currentPanel=node('section','windingPlot'),canvas=card.querySelector('canvas'),summary=card.querySelector('[data-current-summary]');
 currentPanel.append(node('h4',null,'Coil current · A'));
 for(const child of Array.from(card.children))if(child!==heading)currentPanel.append(child);
 const unavailable=node('p','currentUnavailable','Current measurement unavailable on this board.');unavailable.hidden=true;
 const stallPanel=node('section','stallPlot'),stallCanvas=node('canvas'),stallSummary=node('p');stallPanel.hidden=true;
 stallCanvas.setAttribute('aria-label',`${heading.textContent}: stall torque count over the last ten seconds`);stallCanvas.setAttribute('role','img');
 stallPanel.append(node('h4',null,'Stall detector · torque count'),node('div','stallLegend','Torque count · dashed line: detection threshold'),stallCanvas,stallSummary);
 plots.append(temperaturePanel,currentPanel,stallPanel);card.append(plots,unavailable);
 return {canvas,summary,temperatureCanvas,temperatureSummary,currentPanel,unavailable,history:new CurrentHistory(),temperatureHistory:new TemperatureHistory(),currentSupported:null,stallPanel,stallCanvas,stallSummary,stallHistory:new StallHistory()};
}
function receive(chart,d,now=Date.now()){
 if(!chart)return;
 chart.history.receive(d,now);chart.temperatureHistory.receive(d,now);chart.stallHistory.receive(d,now);
 if(d.capabilities)chart.stallPanel.hidden=!d.capabilities.stall_detection_supported;
 if(d.type==='current_samples')chart.currentSupported=true;
 if(d.type==='telemetry'&&Object.hasOwn(d,'current_available')){
  if(d.current_available||Array.isArray(d.winding_current_a))chart.currentSupported=true;
  else if(d.capabilities)chart.currentSupported=!!d.capabilities.current_stream_supported;
 }
 chart.currentPanel.hidden=chart.currentSupported===false;chart.unavailable.hidden=chart.currentSupported!==false;
}
const api={CurrentHistory,TemperatureHistory,StallHistory,
 receive:(axis,d)=>receive(charts.get(axis),d),
 reset:axis=>{const c=charts.get(axis);if(c){c.history.reset();c.temperatureHistory.reset();c.stallHistory.reset();c.stallPanel.hidden=true;c.currentSupported=null;c.currentPanel.hidden=false;c.unavailable.hidden=true;}},
 disconnect:axis=>{const c=charts.get(axis);if(c){c.history.seen=0;c.temperatureHistory.seen=0;c.stallHistory.seen=0;}}};
if(typeof module!=='undefined')module.exports=api;
if(root.document){
 for(const card of root.document.querySelectorAll('[data-current-axis]'))charts.set(card.dataset.currentAxis,mount(card));
 let last=0;function tick(t){if(t-last>=50){last=t;const now=Date.now();for(const chart of charts.values()){drawTemperature(chart,now);if(!chart.currentPanel.hidden)drawCurrent(chart,now);if(!chart.stallPanel.hidden)drawStall(chart,now);}}root.requestAnimationFrame(tick);}root.requestAnimationFrame(tick);
 root.CurrentCharts=api;
}
})(typeof window==='undefined'?globalThis:window);
