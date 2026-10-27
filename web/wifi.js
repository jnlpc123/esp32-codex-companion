(function(){
'use strict';
const {request,bootstrap,provisionWifi,managementUrl}=window.VibeNetwork;
const $=id=>document.getElementById(id);
let state=null,busy=false,timer=null,finished=false,local=null,supported=true,observedJob=null;
function controls(){
 $('wifiConnect').disabled=!supported||busy||!state?.ready||state?.setup?.state==='connecting';
 $('skipWifi').disabled=$('wifiIgnore').disabled=!supported||busy||!state;
 $('ssid').disabled=$('wifiPassword').disabled=$('showPassword').disabled=busy;
 $('wifiConnect').textContent=busy?'正在连接…':'连接并保存';
}
function render(){
 if(!state)return;
 $('wifiIgnore').checked=!!state.ignored;
 $('backToManagement').hidden=!state.connected&&!state.ignored;
 $('networkTitle').textContent=state.connected?'网络设置':state.ap_mode?'为设备连接 Wi-Fi':'连接 Wi-Fi';
 $('wifiStatus').textContent=!state.ready?'网络正在初始化，请稍候…':state.setup?.state==='connecting'?'设备正在连接网络…':state.connected?'已连接 '+state.ssid+' · '+state.ip:state.ap_mode?'配网热点已开启 · 等待连接路由器':'尚未连接路由器';
 $('networkHint').textContent=state.connected?'需要更换网络时，在下方填写新的名称和密码。':'连接一个 2.4 GHz 网络，之后通过设备 IP 进入管理页面。';
 controls();
}
function result(value){
 const url=managementUrl(value.ip,location.hostname);
 if(!url)throw Error('设备未返回有效 IP，请查看网络状态');
 finished=true;clearTimeout(timer);$('wifiResult').hidden=false;
 $('managementLink').href=url;
 const usb=!!local;
 $('resultHint').textContent=usb?'网络已保存。可以继续通过 USB 管理设备，也可以使用设备 IP。':'将手机或电脑切回该 Wi-Fi，再打开下方管理页面。设备热点即将关闭。';
 $('addressLine').textContent='设备地址：http://'+value.ip+'/';
 $('wifiMessage').classList.remove('error');$('wifiMessage').textContent='配网成功，设备正在恢复动画。';
 $('managementLink').focus();
}
async function refresh(){
 clearTimeout(timer);
 try{if(!busy){
  state=await request('/api/wifi/status');render();
  const job=state.setup;
  if(job?.state==='connecting'&&Number.isInteger(job.job_id)&&job.job_id>0)observedJob=job.job_id;
  if(job?.state==='connected'&&job.saved&&state.connected&&state.ip&&
     (job.job_id===observedJob||state.ap_mode&&Number.isInteger(job.job_id)&&job.job_id>0))result(state);
 }}
 catch(e){if(!finished)$('wifiStatus').textContent='暂时无法读取设备：'+e.message;}
 if(!finished&&!document.hidden)timer=setTimeout(refresh,2000);
}
async function ignore(value){
 const d=local?.device_id;
 const response=await request('/api/wifi/ignore',JSON.stringify({ignored:value}),d);
 state=response.wifi;render();
}
$('showPassword').addEventListener('click',()=>{
 const visible=$('wifiPassword').type==='password';$('wifiPassword').type=visible?'text':'password';
 $('showPassword').textContent=visible?'隐藏':'显示';$('showPassword').setAttribute('aria-pressed',String(visible));
});
$('wifiForm').addEventListener('submit',async e=>{
 e.preventDefault();if(busy||!state?.ready)return;
 const ssid=$('ssid').value,password=$('wifiPassword').value,bytes=new TextEncoder();
 const n=bytes.encode(password).length;
 if(!ssid||bytes.encode(ssid).length>32||!(n===0||n>=8&&n<=63||/^[0-9a-fA-F]{64}$/.test(password))){$('wifiMessage').classList.add('error');$('wifiMessage').textContent='网络名称最多32字节；密码需8–63字节、64位十六进制，或留空。';return;}
 busy=true;finished=false;observedJob=null;clearTimeout(timer);controls();
 $('wifiPassword').value='';$('wifiPassword').type='password';$('showPassword').textContent='显示';$('showPassword').setAttribute('aria-pressed','false');
 $('wifiResult').hidden=true;$('wifiMessage').classList.remove('error');$('wifiMessage').textContent='正在连接，请保持设备供电，不要重复提交。';
 const device=local?.device_id;
 try{state=await provisionWifi({ssid,password,send:(path,body)=>request(path,body,device)});render();result(state);}
 catch(e){$('wifiMessage').classList.add('error');$('wifiMessage').textContent=e.message+'。可检查名称和密码后重试；热点已断开时请重新连接热点或通过 USB 查看。';}
 finally{busy=false;controls();if(!finished)refresh();}
});
$('skipWifi').addEventListener('click',async()=>{
 if(busy||!state)return;busy=true;controls();
 try{await ignore(true);location.replace('/');}
 catch(e){$('wifiMessage').classList.add('error');$('wifiMessage').textContent=e.message;}
 finally{busy=false;controls();}
});
$('wifiIgnore').addEventListener('change',async()=>{
 if(busy)return;busy=true;controls();
 try{await ignore($('wifiIgnore').checked);$('wifiMessage').textContent=state.ignored?'已忽略配网提示，可离线使用。':'已恢复配网提示。';}
 catch(e){$('wifiIgnore').checked=!!state?.ignored;$('wifiMessage').textContent=e.message;}
 finally{busy=false;controls();}
});
document.addEventListener('visibilitychange',()=>{if(document.hidden)clearTimeout(timer);else if(!busy&&!finished)refresh();});
(async()=>{
 try{
  local=await bootstrap();
  if(local&&!local.device_id){$('wifiStatus').textContent='请通过 USB 连接设备，再刷新页面。';supported=false;controls();return;}
  if(local){supported=local.capabilities?.includes('wifi_management_v1');$('offlineHint').textContent='USB 可以直接同步 Codex 状态。网络以后再配置。';}
  if(!supported){$('wifiStatus').textContent='当前设备固件不支持 USB 配网，请先升级固件，或通过热点配网。';controls();return;}
  await refresh();
 }catch(e){$('wifiStatus').textContent='连接失败，请刷新页面：'+e.message;}
})();
})();
