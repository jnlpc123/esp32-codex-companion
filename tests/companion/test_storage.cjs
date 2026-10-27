const assert=require('node:assert/strict');
const {storageRequired}=require('../../web/companion.js');
const frame=size=>({blob:{size},delay:40});
const reserve=131072;
assert.equal(storageRequired([frame(251)]),reserve+512+256+512);
assert.equal(storageRequired([frame(252)]),reserve+768+256+512);
assert.equal(storageRequired([frame(251*104)]),reserve+(104+2)*256+256+512);
assert.equal(storageRequired([frame(251)],{size:252}),reserve+512+768+512);
assert.equal(storageRequired(Array.from({length:30},()=>frame(30000))),1068800);
console.log('SPIFFS page/index/timing/preview allocation and safety reserve: PASS');
const {mediaGroups}=require('../../web/companion.js');
assert.deepEqual(mediaGroups([{name:'one/0001.a5',bytes:10,kind:1},{name:'one/_preview.png',bytes:2,kind:1},{name:'two/0001.jpg',bytes:5,kind:1},{name:'config.json',bytes:20,kind:0}],['/data/one','/data/one','']),[
 {directory:'one',bytes:12,files:2,current:true},{directory:'two',bytes:5,files:1,current:false}
]);
