"""Train the observational NEMA17 model; validate by complete nested trial folds.
Usage: python tools/train_stall_model.py DATA_DIRECTORY [OUTPUT_JSON]
Requires NumPy. Raw private recordings are not bundled in the repository.
"""
import sys,pathlib,json,hashlib,numpy as np,time
FEATURE_NAMES=['radial_mean','radial_cv','ripple_ratio','ripple_cv','raw_ripple_ratio','radial_step']
def features(q,speed):
 a,b,p,v=q[:,1],q[:,2],q[:,3],q[:,4];r=np.hypot(a,b);mu=r.mean();pm=p.mean()
 return np.array([mu,r.std()/(mu+1e-8),pm/(mu+1e-8),p.std()/(pm+1e-8),np.sqrt(np.maximum(v,0).mean())/(mu+1e-8),np.abs(np.diff(r)).mean()/(mu*speed/1000+1e-8)])
def load(folder,width):
 rows=[];trials=[]
 for path in sorted(folder.glob('trial-*.json')):
  d=json.loads(path.read_text())
  if d.get('failure') or d.get('exclude_from_training') or 'User confirmed' not in d.get('label_status',''):continue
  q=np.array(d['bins']);q[:,0]=q[:,0]/1e6+d['clock_alignment']['offset_s']-d['start_host_time'];q=q[q[:,0]<9.8]
  trial={'file':path.name,'label':int(d['config']['initial']=='stalled'),'speed':d['config']['speed'],'config':d['config'],'windows':[]}
  for i in range(0,len(q)-width+1,50):
   w=q[i:i+width]
   if np.max(np.diff(w[:,0]))>.00101:continue
   f=features(w,trial['speed'])
   if not np.all(np.isfinite(f)) or f[0]<.05:continue
   row={'f':f,'label':trial['label'],'trial':path.name,'speed':trial['speed'],'time':float(w[-1,0])};rows.append(row);trial['windows'].append(row)
  if trial['windows']:trials.append(trial)
 return rows,trials
def lda(rows,indices):
 x=np.array([r['f'] for r in rows])[:,indices];y=np.array([r['label'] for r in rows]);scale=np.maximum(x.std(axis=0),np.array([.01,.005,.005,.01,.005,.01])[list(indices)]);mu=[x[y==c].mean(axis=0) for c in (0,1)]
 z=(x-np.array(mu)[y])/scale;cov=z.T@z/max(len(z)-2,1)+np.eye(len(indices))*.5
 return {'scale':scale,'direction':np.linalg.solve(cov,(mu[1]-mu[0])/scale),'mid':(mu[0]+mu[1])/2,'indices':list(indices)}
def raw_predict(m,rows):return np.array([np.dot((r['f'][m['indices']]-m['mid'])/m['scale'],m['direction']) for r in rows])
folder=pathlib.Path(sys.argv[1]);output=pathlib.Path(sys.argv[2]) if len(sys.argv)>2 else folder/'firmware-model.json'
widths=(100,200,300,400,600,800,1200,1600)
sets=((1,),(2,),(3,),(4,),(5,),(2,3,4),(1,2,3,4,5))
# Decision boundaries are biased toward rejecting the free-running envelope,
# rather than an unstable midpoint of the two class means.
alphas=(0,.15,.35,.5)
data={w:load(folder,w) for w in widths}

def fit(rows,indices,alpha):
 m=lda(rows,list(indices));p=raw_predict(m,rows);y=np.array([r['label'] for r in rows]);lo=np.quantile(p[y==0],.975);hi=np.quantile(p[y==1],.1)
 m['boundary']=float(lo+alpha*max(hi-lo,np.std(p[y==0])*.25));m['span']=float(max(np.quantile(p[y==1],.5)-lo,np.std(p[y==0])*3,.1));return m

def pred(m,rows):return raw_predict(m,rows)-m['boundary']
def candidates(speed,excluded):
 out=[]
 for width,(rows,trials) in data.items():
  if width<4000/speed:continue
  selected=[t for t in trials if t['speed']==speed and t['file'] not in excluded]
  allrows=[r for t in selected for r in t['windows']]
  for inds in sets:
   for alpha in alphas:
    errors=[];loss=[]
    for t in selected:
     train=[r for r in allrows if r['trial']!=t['file']]
     if len({r['label'] for r in train})<2:break
     m=fit(train,inds,alpha);p=pred(m,t['windows']);wrong=float(np.mean((p>=0)!=t['label']));errors.append(wrong);loss.append(wrong*(2 if t['label']==0 else 1))
    if len(errors)==len(selected):out.append((max(errors),np.mean(loss),width,len(inds),inds,alpha))
 return min(out)

results={}
for speed in (12.5,25.,60.,100.):
 start=time.monotonic();outer=[]
 for file in [t['file'] for t in data[100][1] if t['speed']==speed]:
  c=candidates(speed,{file});w,inds,alpha=c[2],c[4],c[5];rows,trials=data[w];train=[r for r in rows if r['speed']==speed and r['trial']!=file];t=next(t for t in trials if t['file']==file);m=fit(train,inds,alpha);p=pred(m,t['windows']);outer.append({'trial':file,'label':t['label'],'window_ms':w,'features':inds,'alpha':alpha,'error':float(np.mean((p>=0)!=t['label']))})
 best=candidates(speed,set());w,inds,alpha=best[2],best[4],best[5];rows,_=data[w];m=fit([r for r in rows if r['speed']==speed],inds,alpha)
 results[str(speed)]={'outer':outer,'selected':{'cv_worst_error':best[0],'cv_weighted_error':best[1],'window_ms':w,'indices':inds,'alpha':alpha},'model':{k:np.asarray(v).tolist() for k,v in m.items()}}
 print(speed,'seconds',round(time.monotonic()-start,1),'best',best,'outer errors',[round(x['error'],3) for x in outer],flush=True)
 output.write_text(json.dumps({'dataset_sha256':hashlib.sha256(b''.join(p.read_bytes() for p in sorted(folder.glob('trial-*.json')))).hexdigest(),'recording_count':len(list(folder.glob('trial-*.json'))),'feature_names':FEATURE_NAMES,'validation':'Nested leave-one-complete-trial-out; exploratory retrospective evaluation, not a new blind test','profiles':results},indent=2))
