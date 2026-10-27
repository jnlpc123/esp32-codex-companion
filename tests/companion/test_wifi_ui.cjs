const assert=require('node:assert/strict');
const {provisionWifi}=require('../../web/companion.js');
(async()=>{
 const calls=[],answers=[{setup:{job_id:7,state:'connecting'}},{setup:{job_id:7,state:'connected',saved:true},connected:true,ssid:'home',ip:'192.0.2.7'}];
 const send=async(p,b)=>{calls.push([p,b]);return b?{ok:true,wifi:{setup:{job_id:7,state:'connecting'}}}:answers.shift();};
 const result=await provisionWifi({ssid:'home',password:'12345678',send,pause:async()=>{}});
 assert.equal(result.ip,'192.0.2.7');assert.equal(calls.filter(x=>x[1]).length,1);
 assert.deepEqual(JSON.parse(calls[0][1]),{ssid:'home',password:'12345678'});
 const failed=async(p,b)=>b?{wifi:{setup:{job_id:2,state:'connecting'}}}:{setup:{job_id:2,state:'failed',error:'connection failed'}};
 await assert.rejects(provisionWifi({ssid:'home',password:'12345678',send:failed,pause:async()=>{}}),/connection failed/);
 const changed=async(p,b)=>b?{wifi:{setup:{job_id:2,state:'connecting'}}}:{setup:{job_id:3,state:'connected',saved:true},connected:true,ip:'192.0.2.8'};
 await assert.rejects(provisionWifi({ssid:'home',password:'12345678',send:changed,pause:async()=>{}}),/改变|重启/);
 let writes=0;await assert.rejects(provisionWifi({ssid:'home',password:'12345678',send:async()=>{writes++;throw Error('USB disconnected');}}),/USB disconnected/);assert.equal(writes,1);
 console.log('USB provisioning polls actual job outcome, does not retry credentials or accept a replaced job: PASS');
})().catch(e=>{console.error(e);process.exit(1)});
