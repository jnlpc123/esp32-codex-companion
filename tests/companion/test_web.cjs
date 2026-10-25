const assert=require('node:assert/strict');
const {timingText,uploadSequence,validDirectory}=require('../../web/companion.js');
(async()=>{
 assert.equal(timingText([{delay:20},{delay:200},{delay:0}],30),'33\n200\n100\n');
 assert.equal(validDirectory('v0123abcd'),true);assert.equal(validDirectory('../x'),false);
 const sent=[];const request=async(path,body)=>{sent.push({path,body});return {ok:true,config:{media:['/data/v0123abcd','','']}}};const frames=[{blob:new Blob(['one']),delay:40},{blob:new Blob(['two']),delay:120}];
 await uploadSequence({frames,channel:1,directory:'v0123abcd',request});
 assert.deepEqual(sent.map(s=>s.path),['/api/import','/upload/v0123abcd/0001.jpg','/upload/v0123abcd/0002.jpg','/upload/v0123abcd/_timing.txt','/api/companion','/api/import']);
 assert.equal(JSON.parse(sent[4].body).frames,2);assert.equal(JSON.parse(sent[4].body).op,'activate');
 assert.equal(JSON.parse(sent[0].body).op,'begin');assert.equal(JSON.parse(sent[5].body).op,'commit');
 assert.equal(await sent[3].body.text(),'40\n120\n');
 const failed=[];await assert.rejects(uploadSequence({frames,channel:1,directory:'v0123abcd',request:async(p,b)=>{failed.push(p);throw Error('network');}}));assert.equal(failed.includes('/api/companion'),false);
 const recovered=await uploadSequence({frames,channel:1,directory:'v0123abcd',request:async(p,b)=>{if(p==='/api/companion'&&b)throw Error('lost response');return {ok:true,config:{media:['/data/v0123abcd','','']}}}});assert.equal(recovered.config.media[0],'/data/v0123abcd');
 await assert.rejects(uploadSequence({frames:[],channel:1,directory:'v0123abcd',request}));
 await assert.rejects(uploadSequence({frames,channel:1,directory:'../x',request}));
 console.log('Timing preservation, atomic activation, failed upload and lost-ACK recovery: PASS');
})().catch(e=>{console.error(e);process.exit(1)});
