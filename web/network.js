(function(root){
'use strict';
async function provisionWifi({ssid,password,send=request,pause=()=>new Promise(r=>setTimeout(r,700)),timeout=90000}){
 const start=await send('/api/wifi/configure',JSON.stringify({ssid,password}));
 const id=start.wifi?.setup?.job_id;if(!Number.isInteger(id)||id<1)throw Error('设备未返回配网任务，请更新固件并查看网络状态');
 const deadline=Date.now()+timeout;
 while(Date.now()<deadline){
  const status=await send('/api/wifi/status');
  if(status.setup?.job_id!==id)throw Error('设备已重启或配网任务已改变，请查看当前网络状态');
  if(status.setup.state==='failed')throw Error(status.setup.error||'连接失败，请检查网络和密码');
  if(status.setup.state==='connected'&&status.setup.saved&&status.connected&&status.ip)return status;
  await pause();
 }
 throw Error('尚未确认配网结果，请查看网络状态后再操作');
}
let localKey='',localDevice='';
function pinDevice(device){localDevice=device||'';}
async function bootstrap(){
 if(!['127.0.0.1','localhost'].includes(root.location?.hostname))return null;
 const context=await request('/api/local');localKey=context.token;pinDevice(context.device_id);return context;
}
function needsWifiSetup(wifi){return !!(wifi?.ready&&wifi.ap_mode&&!wifi.connected&&!wifi.ignored);}
function managementUrl(ip,hostname){
 const fields=String(ip||'').split('.');
 if(fields.length!==4||fields.some(p=>!/^\d{1,3}$/.test(p)||Number(p)>255))return null;
 return ['127.0.0.1','localhost'].includes(hostname)?'/':'http://'+fields.map(Number).join('.')+'/';
}
async function request(path,body,device=localDevice){
 const controller=new AbortController();const timer=setTimeout(()=>controller.abort(),120000);
 try{
  const headers=typeof body==='string'?{'Content-Type':'application/json'}:{};
  if(localKey){headers['X-Vibe-Key']=localKey;headers['X-Vibe-Device']=device;}
  const response=await fetch(path,{method:body===undefined?'GET':'POST',body,headers,signal:controller.signal,cache:'no-store'});
  const raw=await response.text();
  let result;try{result=JSON.parse(raw);}catch{
   const detail=raw.replace(/<[^>]*>/g,' ').replace(/\s+/g,' ').trim().slice(0,200);
   throw Error('设备请求失败（HTTP '+response.status+'）'+(detail?'：'+detail:'，服务器未返回详情'));
  }
  if(!response.ok||result.ok===false)throw Error(result.errno===28?'设备存储写入失败（空间不足），请先清理内部空间后重试':result.error||result.message||'设备未接受此操作');
  return result;
 }finally{clearTimeout(timer);}
}
const api={request,provisionWifi,bootstrap,pinDevice,needsWifiSetup,managementUrl};
root.VibeNetwork=api;if(typeof module!=='undefined')module.exports=api;
})(typeof window!=='undefined'?window:globalThis);
