const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const network=require('../../web/network.js');
const ready={ready:true,ap_mode:true,connected:false,ignored:false};
assert.equal(network.needsWifiSetup(ready),true);
for(const state of [{...ready,connected:true},{...ready,ignored:true},{...ready,ready:false},{...ready,ap_mode:false},null])assert.equal(network.needsWifiSetup(state),false);
assert.equal(network.managementUrl('192.0.2.8','192.168.4.1'),'http://192.0.2.8/');
assert.equal(network.managementUrl('192.0.2.8','127.0.0.1'),'/');
for(const ip of ['', 'javascript:alert(1)','256.0.0.1','192.0.2.8/path'])assert.equal(network.managementUrl(ip,'192.168.4.1'),null);
const root=path.resolve(__dirname,'../..');
const setup=fs.readFileSync(path.join(root,'web/wifi.html'),'utf8'),home=fs.readFileSync(path.join(root,'web/index.html'),'utf8');
assert.ok(setup.includes('id="wifiForm"'));assert.ok(setup.includes('id="skipWifi"'));
assert.ok(!setup.includes('gifuct'));assert.ok(!home.includes('id="wifiForm"'));
assert.ok(home.includes('href="/wifi"'));assert.ok(setup.includes('src="/network.js"'));
console.log('Dedicated provisioning route, offline/connected gating, safe management links and lightweight entry: PASS');

const vm=require('node:vm');
async function pageFlow(answers){
 const elements={},timers=[];
 const element=id=>elements[id]||(elements[id]={hidden:id==='wifiResult',value:'',type:'password',href:'/',classList:{add(){},remove(){}},addEventListener(){},setAttribute(){},focus(){}});
 element('wifiResult');element('managementLink');
 const context={window:{VibeNetwork:{...network,bootstrap:async()=>null,request:async()=>answers.shift()}},document:{getElementById:element,addEventListener(){},hidden:false},location:{hostname:'192.168.4.1'},TextEncoder,setTimeout:f=>{timers.push(f);return timers.length;},clearTimeout(){}};
 vm.runInNewContext(fs.readFileSync(path.join(root,'web/wifi.js'),'utf8'),context);
 await new Promise(setImmediate);
 if(answers.length){assert.ok(timers.length);await timers.shift()();}
 return elements;
}
(async()=>{
 const connecting={...ready,setup:{job_id:7,state:'connecting'}},done={...ready,connected:true,ip:'192.0.2.8',setup:{job_id:7,state:'connected',saved:true}};
 const resumed=await pageFlow([connecting,done]);
 assert.equal(resumed.wifiResult.hidden,false,'resumed provisioning must show its success receipt');
 assert.equal(resumed.managementLink.href,'http://192.0.2.8/');
 const justFinished=await pageFlow([done]);assert.equal(justFinished.wifiResult.hidden,false,'AP still open after connection must offer the new address');
 const replaced=await pageFlow([connecting,{...done,ap_mode:false,setup:{...done.setup,job_id:8}}]);assert.equal(replaced.wifiResult.hidden,true,'another job cannot complete the watched job');
 console.log('Reloaded provisioning resumes the same job and exposes the new IP before hotspot closure: PASS');
})().catch(e=>{console.error(e);process.exit(1)});
