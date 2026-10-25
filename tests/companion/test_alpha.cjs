const assert=require('node:assert/strict');
const api=require('../../web/companion.js');
assert.equal(typeof api.encodeAlphaFrame,'function');
const rgba=Uint8Array.from([123,45,67,0, 0,0,0,255, 255,0,0,128, 255,255,255,64]);
const encoded=api.encodeAlphaFrame(rgba,2,2);
assert.equal(new TextDecoder().decode(encoded.slice(0,4)),'VAF1');
const decoded=api.decodeAlphaFrame(encoded,0x07e0);
assert.deepEqual([...decoded.pixels],[0x07e0,0,0x83e0,0x47e8]);
assert.equal(api.crc32(new TextEncoder().encode('123456789')),0xcbf43926);
for(const position of [0,4,8,12,16,20,24,encoded.length-1]){
 const bad=encoded.slice();bad[position]^=1;assert.throws(()=>api.decodeAlphaFrame(bad,0));
}
assert.throws(()=>api.decodeAlphaFrame(encoded.slice(0,-1),0));
assert.throws(()=>api.encodeAlphaFrame(new Uint8Array(4),0,1));
for(let trial=0;trial<40;trial++){
 const data=new Uint8Array(128*128*4);
 for(let i=0;i<data.length;i+=4){data[i]=(i*13+trial)%256;data[i+1]=(i*7+31)%256;data[i+2]=(i*3+99)%256;data[i+3]=trial%3===0?0:trial%3===1?255:(i/4)%254+1;}
 const file=api.encodeAlphaFrame(data,128,128);assert.ok(file.length<=65536);
 const bg=0xf800,out=api.decodeAlphaFrame(file,bg).pixels;
 for(let p=0;p<out.length;p++){
  const a=data[p*4+3],r=data[p*4]>>3,g=data[p*4+1]>>2,b=data[p*4+2]>>3;
  const expected=(Math.floor((r*a+31*(255-a)+127)/255)<<11)|(Math.floor((g*a+127)/255)<<5)|Math.floor((b*a+127)/255);
  assert.equal(out[p],expected);
 }
}
(async()=>{
 const sent=[];await api.uploadSequence({frames:[{blob:new Blob([encoded]),format:'a5',delay:40}],preview:new Blob(['PNG']),channel:1,directory:'v0123abcd',request:async(path,body)=>{sent.push(path);return {ok:true};}});
 assert.deepEqual(sent,['/api/import','/upload/v0123abcd/0001.a5','/upload/v0123abcd/_preview.png','/upload/v0123abcd/_timing.txt','/api/companion','/api/import']);
 console.log('Alpha CRC, corruption, black/transparent/partial alpha, bounded random roundtrips and atomic upload: PASS');
})().catch(e=>{console.error(e);process.exit(1)});
