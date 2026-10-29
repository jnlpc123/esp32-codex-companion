const assert=require('node:assert/strict');
const {factoryReset}=require('../../web/companion.js');
(async()=>{
 const sent=[];const send=async(p,b,d)=>{sent.push([p,JSON.parse(b),d]);return {ok:true,reset:{pending:true}};};
 assert.equal(await factoryReset('aabbccddeeff',{prompt:()=>null,send}),null);assert.equal(sent.length,0);
 assert.equal(await factoryReset('aabbccddeeff',{prompt:()=>'',send}),null);assert.equal(sent.length,0);
 assert.equal(await factoryReset('aabbccddeeff',{prompt:()=> 'reset',send}),null);assert.equal(sent.length,0);
 await assert.rejects(factoryReset('',{prompt:()=> 'RESET',send}));assert.equal(sent.length,0);
 const r=await factoryReset('aabbccddeeff',{prompt:t=>{assert.match(t,/Wi-Fi/);assert.match(t,/素材/);return 'RESET';},send});
 assert.equal(r.reset.pending,true);assert.deepEqual(sent.pop(),['/api/companion',{v:1,op:'factory_reset',confirm:'FACTORY_RESET',device_id:'aabbccddeeff'},'aabbccddeeff']);
 await assert.rejects(factoryReset('aabbccddeeff',{prompt:()=> 'RESET',send:async()=>{throw Error('busy');}}),/busy/);
 await assert.rejects(factoryReset('aabbccddeeff',{prompt:()=> 'RESET',send:async()=>({ok:true})}),/确认/);
 console.log('Factory UI: typed confirmation, cancel, pinned device, ACK validation and no implicit retry: PASS');
})().catch(e=>{console.error(e);process.exit(1)});
