const assert=require('node:assert/strict');
const {clearMedia,storageSummary}=require('../../web/companion.js');
(async()=>{
 const calls=[];const send=async(p,b)=>{calls.push(JSON.parse(b));return {ok:true};};
 assert.equal(await clearMedia('unused',{confirm:()=>false,send}),null);assert.equal(calls.length,0);
 await clearMedia('unused',{confirm:text=>{assert.match(text,/保留/);return true;},send});
 assert.deepEqual(calls.pop(),{v:1,op:'clear_media',scope:'unused',confirm:'DELETE_UNUSED_MEDIA'});
 await clearMedia('all',{confirm:text=>{assert.match(text,/正在展示/);return true;},send});
 assert.deepEqual(calls.pop(),{v:1,op:'clear_media',scope:'all',confirm:'DELETE_ALL_MEDIA'});
 await assert.rejects(clearMedia('invalid',{confirm:()=>true,send}));
 await assert.rejects(clearMedia('all',{confirm:()=>true,send:async()=>{throw Error('busy');}}),/busy/);
 const summary=storageSummary({spiffs_total:2583041,spiffs_used:1947509,spiffs_free:635532});
 assert.match(summary,/2\.46 MiB/);assert.match(summary,/1902 KiB/);assert.match(summary,/621 KiB/);
 let polling=0;
 const completed=await clearMedia('unused',{confirm:()=>true,send:async(p,b)=>{
  const command=JSON.parse(b);if(command.op==='clear_media')return {ok:true,cleanup:{job_id:7,pending:true}};
  assert.equal(command.op,'cleanup_status');assert.equal(command.job_id,7);polling++;
  return {ok:true,cleanup:{job_id:7,pending:false,files_removed:2}};
 }});
 assert.equal(completed.cleanup.pending,false);assert.equal(polling,1);
 console.log('Cleanup confirmation/cancel, distinct scopes, failure propagation and honest capacity display: PASS');
})().catch(e=>{console.error(e);process.exit(1)});
