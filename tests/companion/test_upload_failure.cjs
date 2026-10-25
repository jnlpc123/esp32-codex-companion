const assert=require('node:assert/strict');
const {request,uploadSequence}=require('../../web/companion.js');
(async()=>{
 const previous=global.fetch;
 try{
  global.fetch=async()=>({ok:false,status:500,text:async()=>'<html>Storage unavailable or full</html>'});
  await assert.rejects(request('/'),/HTTP 500.*Storage unavailable or full/);
  global.fetch=async()=>({ok:false,status:507,text:async()=>JSON.stringify({ok:false,errno:28,error:'write failed'})});
  await assert.rejects(request('/'),/空间不足/);
 }finally{global.fetch=previous;}
 const ops=[];
 await assert.rejects(uploadSequence({frames:[{blob:new Blob(['a']),delay:40}],channel:3,directory:'v1234abcd',request:async(path,body)=>{
  if(path==='/api/import'){ops.push(JSON.parse(body).op);return{ok:true};}
  throw Error('storage full');
 }}),/storage full/);
 assert.deepEqual(ops,['begin','abort']);
 const lostBegin=[];
 await assert.rejects(uploadSequence({frames:[{blob:new Blob(['a']),delay:40}],channel:1,directory:'vabcd1234',request:async(path,body)=>{
  const op=JSON.parse(body).op;lostBegin.push(op);if(op==='begin')throw Error('lost begin ACK');return{ok:true};
 }}),/lost begin ACK/);
 assert.deepEqual(lostBegin,['begin','abort']);
 console.log('Plain/JSON HTTP failures and staged-import rollback: PASS');
})().catch(e=>{console.error(e);process.exit(1)});
