/* Device configuration only. No websocket or browser framebuffer sender. */
(function(root){
'use strict';
const alpha=typeof module!=='undefined'?require('./alpha-codec.js'):root.VibeAlphaCodec;
function validDirectory(name){return /^v[0-9a-f]{8}$/.test(name);}
function timingText(frames,fps=30){
 const minimum=Math.round(1000/Math.max(1,Math.min(30,fps)));
 return frames.map(f=>Math.min(60000,Math.max(minimum,Number.isFinite(f.delay)&&f.delay>0?f.delay:100))).join('\n')+'\n';
}
function storageRequired(frames,preview){
 // SPIFFS: 251 data bytes/page, 103 first-page indexes, 124 thereafter.
 // Keep the GC reserve; account for preview/timing and per-file index pages.
 const allocation=size=>{const pages=Math.ceil(size/251);return (pages+1+Math.max(0,Math.ceil((pages-103)/124)))*256;};
 return frames.reduce((n,f)=>n+allocation(f.blob.size),0)+allocation(preview?.size||0)+allocation(timingText(frames).length)+131072;
}
async function request(path,body){
 const controller=new AbortController();const timer=setTimeout(()=>controller.abort(),120000);
 try{
  const response=await fetch(path,{method:body===undefined?'GET':'POST',body,headers:typeof body==='string'?{'Content-Type':'application/json'}:{},signal:controller.signal,cache:'no-store'});
  const raw=await response.text();
  let result;try{result=JSON.parse(raw);}catch{
   const detail=raw.replace(/<[^>]*>/g,' ').replace(/\s+/g,' ').trim().slice(0,200);
   throw Error('设备请求失败（HTTP '+response.status+'）'+(detail?'：'+detail:'，服务器未返回详情'));
  }
  if(!response.ok||result.ok===false)throw Error(result.errno===28?'设备存储写入失败（空间不足），请先清理内部空间后重试':result.error||result.message||'设备未接受此操作');
  return result;
 }finally{clearTimeout(timer);}
}
async function uploadSequence({frames,preview,channel,directory,request:send=request,onProgress=()=>{}}){
 if(!validDirectory(directory)||!Number.isInteger(channel)||channel<1||channel>3||!frames.length||frames.length>256)throw Error('无效的素材或屏幕');
 if(frames.some(f=>!f.blob||f.blob.size<1||f.blob.size>65536))throw Error('单帧图片大小超限');
 const format=frames[0].format||'jpg';if(!['jpg','a5'].includes(format)||frames.some(f=>(f.format||'jpg')!==format))throw Error('素材帧格式不一致');
 let uploaded=false,started=false;
 const transaction=op=>send('/api/import',JSON.stringify({op,directory}));
 try{
  started=true;await transaction('begin');
  for(let i=0;i<frames.length;i++){
   await send('/upload/'+directory+'/'+String(i+1).padStart(4,'0')+'.'+format,frames[i].blob);
   onProgress(i+1,frames.length+1);
  }
  if(preview)await send('/upload/'+directory+'/_preview.png',preview);
  await send('/upload/'+directory+'/_timing.txt',new Blob([timingText(frames)],{type:'text/plain'}));
  uploaded=true;onProgress(frames.length+1,frames.length+1);
  const result=await send('/api/companion',JSON.stringify({v:1,op:'activate',channel,dir:'/data/'+directory,frames:frames.length}));
  try{await transaction('commit');}catch{} // Boot recovery retains selected directories.
  return result;
 }catch(error){
  // An activation ACK can be lost after the durable commit. Check before retrying.
  if(uploaded){try{const current=await send('/api/companion');if(current.config?.media?.[channel-1]==='/data/'+directory){try{await transaction('commit');}catch{}return current;}}catch{}}
  if(started){try{await transaction('abort');}catch{error.message+='；未完成文件将在设备重启时回收';}}
  throw error;
 }
}
function* gifFrames(buffer,decoder=root.gifuctJs){
 if(!decoder)throw Error('GIF解析器未加载，请刷新页面');
 const gif=decoder.parseGIF(buffer),raw=gif.frames.filter(f=>f.image),w=gif.lsd.width,h=gif.lsd.height;
 if(!w||!h||w>2048||h>2048||!raw.length||raw.length>256)throw Error('动图需在2048×2048、1–256帧以内');
 const rgba=new Uint8ClampedArray(w*h*4),bg=gif.gct?.[gif.lsd.backgroundColorIndex]||[0,0,0];
 let previous=null,restore=null;
 const fill=(dims,transparent)=>{
  for(let y=Math.max(0,dims.top);y<Math.min(h,dims.top+dims.height);y++)for(let x=Math.max(0,dims.left);x<Math.min(w,dims.left+dims.width);x++){
   const p=(y*w+x)*4;rgba[p]=transparent?0:bg[0];rgba[p+1]=transparent?0:bg[1];rgba[p+2]=transparent?0:bg[2];rgba[p+3]=transparent?0:255;
  }
 };
 for(let i=0;i<raw.length;i++){
  const descriptor=raw[i].image.descriptor;
  if(!descriptor.width||!descriptor.height||descriptor.width>2048||descriptor.height>2048)throw Error('GIF帧尺寸超限');
  if(previous?.disposalType===2)fill(previous.dims,previous.transparentIndex!==undefined);
  else if(previous?.disposalType===3&&restore)rgba.set(restore);
  const frame=decoder.decompressFrame(raw[i],gif.gct,true);
  if(i===0&&frame.transparentIndex===undefined)fill({left:0,top:0,width:w,height:h},false);
  restore=frame.disposalType===3?rgba.slice():null;
  for(let y=0;y<frame.dims.height;y++)for(let x=0;x<frame.dims.width;x++){
   const dx=x+frame.dims.left,dy=y+frame.dims.top,p=(y*frame.dims.width+x)*4;
   if(dx<0||dy<0||dx>=w||dy>=h||!frame.patch[p+3])continue;
   const at=(dy*w+dx)*4;rgba[at]=frame.patch[p];rgba[at+1]=frame.patch[p+1];rgba[at+2]=frame.patch[p+2];rgba[at+3]=255;
  }
  // Consumer processes this view before advancing the generator.
  yield {rgba,width:w,height:h,delay:frame.delay||100,index:i,total:raw.length};previous=frame;
 }
}
function dotColor565(background){return (background^65535)&65535;}
function taskDotLayout(count){
 const n=Math.min(5,Math.max(0,Math.floor(Number.isFinite(count)?count:0)));
 return Array.from({length:n},(_,index)=>{const dx=-(n-1)*7+index*14;return {x:80+dx,y:144-Math.floor((dx*dx+74)/148),radius:5,hollow:count>5&&index===4};});
}
const api={request,timingText,storageRequired,validDirectory,uploadSequence,gifFrames,dotColor565,taskDotLayout,...alpha};
root.VibeImport=api;if(typeof module!=='undefined')module.exports=api;
if(typeof document==='undefined')return;

function canvas(width,height){const c=document.createElement('canvas');c.width=width;c.height=height;return c;}
async function alphaFrom(source,makePreview=false){
 if(!alpha)throw Error('透明解码器未加载，请刷新页面');
 const target=canvas(128,128),ctx=target.getContext('2d',{willReadFrequently:true});
 const w=source.naturalWidth||source.width,h=source.naturalHeight||source.height;
 const ratio=Math.min(128/w,128/h);const dw=w*ratio,dh=h*ratio;
 ctx.drawImage(source,(128-dw)/2,(128-dh)/2,dw,dh);
 const bytes=alpha.encodeAlphaFrame(ctx.getImageData(0,0,128,128).data,128,128);
 const result={blob:new Blob([bytes],{type:'application/octet-stream'}),format:'a5'};
 if(makePreview)result.preview=await new Promise((resolve,reject)=>target.toBlob(b=>b?resolve(b):reject(Error('预览生成失败')),'image/png'));
 return result;
}
async function decodeFile(file,progress){
 if(file.size>6*1024*1024)throw Error('文件超过6MB，请先缩小动图');
 if(file.type==='image/gif'||/\.gif$/i.test(file.name)){
  let work=null,ctx=null;const frames=[];
  for(const frame of gifFrames(await file.arrayBuffer())){
   if(!work){work=canvas(frame.width,frame.height);ctx=work.getContext('2d',{willReadFrequently:true});}
   ctx.putImageData(new ImageData(frame.rgba,frame.width,frame.height),0,0);
   frames.push({...await alphaFrom(work,frame.index===0),delay:frame.delay});
   progress(frame.index+1,frame.total);
   if(frame.index%8===0)await new Promise(r=>setTimeout(r,0));
  }
  return frames;
 }
 const url=URL.createObjectURL(file);
 try{
  const image=new Image();image.src=url;await image.decode();
  if(!image.naturalWidth||image.naturalWidth>8192||image.naturalHeight>8192)throw Error('图片尺寸过大');
  return [{...await alphaFrom(image,true),delay:1000}];
 }finally{URL.revokeObjectURL(url);}
}

async function init(){
 const $=id=>document.getElementById(id),cards=[...document.querySelectorAll('.screen')];
 let latest=null,initialized=false,busy=false,pollTimer=null;
 const stateLabels={idle:'等你开始',working:'正在工作',waiting:'需要你回应',done:'工作完成'};
 const colors={idle:'#e1e5db',working:'#22c55e',waiting:'#facc15',done:'#ef4444'};
 function toast(text){const el=$('globalMessage');el.textContent=text;el.hidden=false;setTimeout(()=>{el.hidden=true;},5000);}
 function outputs(){ $('foregroundValue').textContent=$('foregroundSize').value+' px';$('brightnessValue').textContent=$('brightness').value+'%';$('volumeValue').textContent=$('volume').value+'%';}
 $('cleanStorage').addEventListener('click',async()=>{
  const button=$('cleanStorage');if(busy)return;button.disabled=true;
  try{
   const before=await request('/api/sysinfo');
   const result=await request('/api/companion',JSON.stringify({v:1,op:'prune_cache'}));
   const after=await request('/api/sysinfo');
   $('storageMessage').textContent=`已清理 ${result.cache_files_removed} 个缓存，释放 ${Math.max(0,Math.round((after.spiffs_free-before.spiffs_free)/1024))} KB；素材已保留。`;
   await storage();
  }catch(e){$('storageMessage').textContent=e.message;}finally{button.disabled=false;}
 });
 function hex565(v){return '#'+[Math.round((v>>11)*255/31),Math.round(((v>>5)&63)*255/63),Math.round((v&31)*255/31)].map(x=>x.toString(16).padStart(2,'0')).join('');}
 function pack565(v){const n=parseInt(v.slice(1),16);return ((n>>19)<<11)|(((n>>10)&63)<<5)|((n>>3)&31);}
 let catalog={projects:[],sessions:[]},catalogRevision=-1;
 function scopeOptions(target=''){
  const mode=$('scopeMode').value,select=$('scopeTarget');select.replaceChildren();select.disabled=mode==='all';
  if(mode==='all')target='';
  const entries=mode==='all'?[{id:'',label:'全部任务'}]:mode==='project'?catalog.projects:catalog.sessions;
  for(const entry of entries){const option=document.createElement('option');option.value=entry.id;option.textContent=entry.label;select.append(option);}
  if(target&&!entries.some(x=>x.id===target)){const option=document.createElement('option');option.value=target;option.textContent=target;select.append(option);}
  if(target)select.value=target;
 }
 $('scopeMode').addEventListener('change',()=>scopeOptions());
 $('ackDone').addEventListener('click',async()=>{try{show(await request('/api/companion',JSON.stringify({v:1,op:'ack'})));toast('已确认，等待电脑同步');}catch(e){toast(e.message);}});
 function show(status,fill=false){
  latest=status;const state=stateLabels[status.state]?status.state:'idle';document.body.dataset.state=state;
  $('stateLabel').textContent=status.idle_playing?'空闲动画':stateLabels[state];
  const source=status.transport||(status.usb_connected?'usb':'none');
  $('usbBadge').textContent=source==='usb'?'USB 已同步':source==='wifi'?'Wi-Fi 已同步':'等待电脑桥接';
  $('stateDetail').textContent=status.manual?'临时灯光测试':source==='usb'?'USB 同步 · Wi-Fi 备用':source==='wifi'?'Wi-Fi 同步':'等待电脑连接';
  if(status.idle_playing)$('stateDetail').textContent='三屏播放中';
  $('soundHint').textContent=status.sound_available?'':'提示音不可用';
  cards.forEach((card,i)=>{
   const role=status.config.layout==='all'?state:card.dataset.role;
   const count=(status.counts||{})[role]||0,active=status.idle_playing||count>0;
   const lamp=card.querySelector('.lamp'),bg=status.backgrounds?.[i]??0x0841;
   card.classList.toggle('active',active);lamp.classList.toggle('inactive',!active);lamp.style.background=hex565(bg);
   const dots=card.querySelector('.task-dots');dots.replaceChildren();dots.style.color=hex565(dotColor565(bg));
   for(const position of taskDotLayout(status.idle_playing?0:count)){const dot=document.createElement('i');if(position.hollow)dot.className='overflow';dot.style.setProperty('--rise',String(144-position.y));dots.append(dot);}
   let label=card.querySelector('.task-count');if(!label){label=document.createElement('p');label.className='task-count';lamp.after(label);}label.textContent=status.idle_playing?'空闲动画 · 彩色播放':count+' 个任务'+(active?' · 彩色播放':' · 灰度暂停');
   const dir=status.config.media[i];const image=card.querySelector('img');
   if(dir&&image.dataset.dir!==dir){image.dataset.dir=dir;image.src=dir.replace(/^\/data/,'')+'/_preview.png';image.hidden=false;image.onerror=()=>{if(!image.src.endsWith('/0001.jpg'))image.src=dir.replace(/^\/data/,'')+'/0001.jpg';else image.hidden=true;};}
   if(!dir)image.hidden=true;
   if(fill||!initialized)card.querySelector('.existing-dir').value=dir;
  });
  if(fill||!initialized){$('layout').value=status.config.layout;$('foregroundSize').value=status.config.foreground_size;$('brightness').value=status.config.brightness;$('volume').value=status.config.volume;$('muted').checked=status.config.muted;outputs();}
  if(fill||!initialized){
   $('idlePlayback').value=status.config.idle_playback||'off';
   const select=$('idleDoneDelay'),delay=String(status.config.idle_done_delay??10);
   if(!Array.from(select.options).some(option=>option.value===delay)){const option=document.createElement('option');option.value=delay;option.textContent='红灯保留 '+delay+' 秒';select.append(option);}
   select.value=delay;
  }
  const counts=status.counts||{};
  $('taskSummary').textContent=`工作 ${counts.working||0} · 等待 ${counts.waiting||0} · 未确认完成 ${counts.done||0}`;
  $('ackDone').disabled=!counts.done||status.manual;
  if(fill||!initialized){
   ['colorDone','colorWaiting','colorWorking'].forEach((id,i)=>$(id).value=hex565(status.config.colors?.[i]??[0xf800,0xffe0,0x07e0][i]));
   $('scopeMode').value=status.config.scope?.mode||'all';scopeOptions(status.config.scope?.target||'');
  }
  if(status.catalog_revision!==catalogRevision){catalogRevision=status.catalog_revision;request('/api/companion',JSON.stringify({v:1,op:'get_catalog'})).then(r=>{catalog=r.catalog;scopeOptions($('scopeTarget').value);}).catch(()=>{catalogRevision=-1;});}
  initialized=true;
 }
 async function poll(){
  clearTimeout(pollTimer);
  try{show(await request('/api/companion'));}catch{$('usbBadge').textContent='设备网页连接中断';}
  if(!document.hidden)pollTimer=setTimeout(poll,1500);
 }
 for(const id of ['foregroundSize','brightness','volume'])$(id).addEventListener('input',outputs);
 document.addEventListener('visibilitychange',()=>{if(document.hidden)clearTimeout(pollTimer);else poll();});
 cards.forEach((card,index)=>{
  const input=card.querySelector('.file'),message=card.querySelector('.upload-message'),bar=card.querySelector('progress');
  card.querySelector('.import').addEventListener('click',()=>input.click());
  input.addEventListener('change',async()=>{
   const file=input.files[0];if(!file||busy)return;busy=true;
   document.querySelectorAll('.import,.use-existing').forEach(b=>{b.disabled=true;});
   message.classList.remove('error');bar.hidden=false;bar.value=0;
   try{
    message.textContent='正在解析素材…';
    const frames=await decodeFile(file,(n,total)=>{message.textContent=`处理动图 ${n}/${total}`;bar.value=n/total*25;});
   const needed=storageRequired(frames,frames[0].preview);
   let info=await request('/api/sysinfo');
   if(needed>info.spiffs_free){message.textContent='正在释放未使用的播放缓存…';await request('/api/companion',JSON.stringify({v:1,op:'prune_cache'}));info=await request('/api/sysinfo');}
   if(needed>info.spiffs_free)throw Error(`空间不足：本次需约 ${Math.ceil(needed/1024)} KB（含安全余量），剩余 ${Math.floor(info.spiffs_free/1024)} KB。请使用更短的动图。`);
    const random=new Uint32Array(1);crypto.getRandomValues(random);const directory='v'+random[0].toString(16).padStart(8,'0');
    const result=await uploadSequence({frames,preview:frames[0].preview,channel:index+1,directory,onProgress:(n,total)=>{bar.value=25+n/total*75;message.textContent=`保存到设备 ${n}/${total}，请保持页面开启`;}});
    show(result,true);message.textContent=`已保存 ${frames.length} 帧，正在准备播放；下次开机会自动恢复。`;
    await storage();
   }catch(error){message.classList.add('error');message.textContent='导入未完成：'+error.message+'。未确认前不会替换原素材。';}
   finally{bar.hidden=true;input.value='';busy=false;document.querySelectorAll('.import,.use-existing').forEach(b=>{b.disabled=false;});}
  });
  card.querySelector('.use-existing').addEventListener('click',async()=>{
   if(!latest||busy)return;
   try{const media=latest.config.media.slice();media[index]=card.querySelector('.existing-dir').value.trim();show(await request('/api/companion',JSON.stringify({v:1,op:'config',config:{media}})),true);message.textContent='已保存，开机自动载入此目录。';}
   catch(error){message.classList.add('error');message.textContent=error.message;}
  });
 });
 $('settingsForm').addEventListener('submit',async e=>{
  e.preventDefault();if(!initialized||!latest){toast('请等待设备连接');return;}const button=e.submitter;button.disabled=true;$('settingsMessage').classList.remove('error');
  try{show(await request('/api/companion',JSON.stringify({v:1,op:'config',config:{colors:['colorDone','colorWaiting','colorWorking'].map(id=>pack565($(id).value)),scope:{mode:$('scopeMode').value,target:$('scopeTarget').value},layout:$('layout').value,idle_playback:$('idlePlayback').value,idle_done_delay:Number($('idleDoneDelay').value),foreground_size:Number($('foregroundSize').value),brightness:Number($('brightness').value),volume:Number($('volume').value),muted:$('muted').checked}})),true);$('settingsMessage').textContent='已保存';}
  catch(error){$('settingsMessage').classList.add('error');$('settingsMessage').textContent=error.message;}
  finally{button.disabled=false;}
 });
 for(const [id,kind]of [['testDone','done'],['testWaiting','waiting']])$(id).addEventListener('click',async()=>{
  try{const result=await request('/api/companion',JSON.stringify({v:1,op:'tone',kind}));toast(result.tone_queued?'已播放提示音':'当前已静音，或设备音频芯片不可用');}catch(error){toast(error.message);}
 });
 document.querySelectorAll('.state-test').forEach(b=>b.addEventListener('click',async()=>{
  try{show(await request('/api/companion',JSON.stringify({v:1,op:'state',state:b.dataset.state,notify:true})));}catch(error){toast(error.message);}
 }));
 $('wifiForm').addEventListener('submit',async e=>{
  e.preventDefault();$('wifiMessage').textContent='正在连接，请保持设备供电…';
  try{await request('/api/wifi/configure',JSON.stringify({ssid:$('ssid').value,password:$('wifiPassword').value}));$('wifiMessage').textContent='配置已提交。若地址改变，请使用 esp32.local 重新打开页面。';$('wifiPassword').value='';}
  catch(error){$('wifiMessage').textContent=error.message;}
 });
 async function storage(){try{const info=await request('/api/sysinfo');$('storageInfo').textContent='剩余空间 '+Math.round(info.spiffs_free/1024)+' KB';$('deviceInfo').textContent=`ESP32-C3 · ${Math.round(info.cpu_freq_hz/1000000)} MHz\n可用内存 ${Math.round(info.free_heap/1024)} KB · 固件 ${info.app_version}`;}catch{}}
 await poll();storage();
}
if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',init);else init();
})(typeof window!=='undefined'?window:globalThis);
