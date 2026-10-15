/* Hardware captures 100 kHz per winding. Display 10 ms buckets without losing peaks. */
(function(root){
'use strict';
const scale=raw=>raw*3.3/4095-1.65;
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
     s.min=Math.min(s.min,scale(min));s.max=Math.max(s.max,scale(max));
     s.sum+=scale(sum/count)*count;s.count+=count;
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
const charts=new Map();
function draw(chart,now){
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
const api={CurrentHistory,receive:(axis,d)=>charts.get(axis)?.history.receive(d),reset:axis=>charts.get(axis)?.history.reset(),disconnect:axis=>{const h=charts.get(axis)?.history;if(h)h.seen=0;}};
if(typeof module!=='undefined')module.exports=api;
if(root.document){
 for(const card of root.document.querySelectorAll('[data-current-axis]'))charts.set(card.dataset.currentAxis,{canvas:card.querySelector('canvas'),summary:card.querySelector('[data-current-summary]'),history:new CurrentHistory()});
 let last=0;function tick(t){if(t-last>=50){last=t;for(const chart of charts.values())draw(chart,Date.now());}root.requestAnimationFrame(tick);}root.requestAnimationFrame(tick);
 root.CurrentCharts=api;
}
})(typeof window==='undefined'?globalThis:window);
