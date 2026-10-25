/* VAF1: bounded RGB565 + full 8-bit alpha runs. Browser import and test oracle. */
(function(root){
'use strict';
const table=new Uint32Array(256);
for(let i=0;i<256;i++){let c=i;for(let j=0;j<8;j++)c=(c>>>1)^((c&1)?0xedb88320:0);table[i]=c>>>0;}
function crc32(bytes){let c=0xffffffff;for(const b of bytes)c=(c>>>8)^table[(c^b)&255];return (c^0xffffffff)>>>0;}
function encodeAlphaFrame(rgba,width,height){
 if(!Number.isInteger(width)||!Number.isInteger(height)||width<1||height<1||width>160||height>160||rgba.length!==width*height*4)throw Error('无效的透明帧尺寸');
 const payload=[],count=width*height;
 const color=i=>((rgba[i*4]>>3)<<11)|((rgba[i*4+1]>>2)<<5)|(rgba[i*4+2]>>3);
 const word=n=>payload.push(n&255,n>>>8);
 const pixel=i=>{const c=color(i);payload.push(c>>>8,c&255);};
 const repeatAt=i=>i+2<count&&rgba[i*4+3]===255&&rgba[(i+1)*4+3]===255&&rgba[(i+2)*4+3]===255&&color(i)===color(i+1)&&color(i)===color(i+2);
 let i=0;
 while(i<count){
  const start=i,a=rgba[i*4+3];
  if(a===0){while(i<count&&i-start<16383&&rgba[i*4+3]===0)i++;word(i-start);}
  else if(a<255){while(i<count&&i-start<16383&&rgba[i*4+3]>0&&rgba[i*4+3]<255)i++;word(0xc000|(i-start));for(let j=start;j<i;j++){pixel(j);payload.push(rgba[j*4+3]);}}
  else if(repeatAt(i)){const c=color(i);while(i<count&&i-start<16383&&rgba[i*4+3]===255&&color(i)===c)i++;word(0x4000|(i-start));pixel(start);}
  else {do{i++;}while(i<count&&i-start<16383&&rgba[i*4+3]===255&&!repeatAt(i));word(0x8000|(i-start));for(let j=start;j<i;j++)pixel(j);}
 }
 if(payload.length+24>65536)throw Error('透明帧过大，请缩小图片');
 const bytes=new Uint8Array(payload.length+24),v=new DataView(bytes.buffer);
 bytes.set([86,65,70,49]);v.setUint16(4,width,true);v.setUint16(6,height,true);v.setUint32(8,payload.length,true);bytes.set(payload,24);
 v.setUint32(12,crc32(bytes.subarray(24)),true);v.setUint32(16,0,true);v.setUint32(20,crc32(bytes.subarray(0,20)),true);
 return bytes;
}
function decodeAlphaFrame(input,background=0){
 const bytes=input instanceof Uint8Array?input:new Uint8Array(input);
 if(bytes.length<26||bytes.length>65536)throw Error('Invalid VAF1 length');
 const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),width=v.getUint16(4,true),height=v.getUint16(6,true);
 if(v.getUint32(0,true)!==0x31464156||!width||!height||width>160||height>160||v.getUint32(16,true)!==0||v.getUint32(8,true)!==bytes.length-24||v.getUint32(20,true)!==crc32(bytes.subarray(0,20))||v.getUint32(12,true)!==crc32(bytes.subarray(24)))throw Error('Invalid VAF1 header/CRC');
 const pixels=new Uint16Array(width*height);let pos=24,used=0;
 const word=()=>{if(pos+2>bytes.length)throw Error('Truncated token');const n=bytes[pos]|(bytes[pos+1]<<8);pos+=2;return n;};
 const color=()=>{if(pos+2>bytes.length)throw Error('Truncated pixel');const c=(bytes[pos]<<8)|bytes[pos+1];pos+=2;return c;};
 while(pos<bytes.length){
  const token=word(),kind=token>>>14,n=token&16383;
  if(!n||used+n>pixels.length)throw Error('Invalid pixel count');
  if(kind===0){pixels.fill(background,used,used+n);used+=n;}
  else if(kind===1){pixels.fill(color(),used,used+n);used+=n;}
  else for(let i=0;i<n;i++){
   const c=color();
   if(kind===2)pixels[used++]=c;
   else {if(pos>=bytes.length)throw Error('Missing alpha');const a=bytes[pos++];if(!a||a===255)throw Error('Invalid alpha token');
    const blend=(s,b)=>Math.floor((s*a+b*(255-a)+127)/255);
    pixels[used++]=(blend(c>>>11,background>>>11)<<11)|(blend((c>>>5)&63,(background>>>5)&63)<<5)|blend(c&31,background&31);
   }
  }
 }
 if(used!==pixels.length)throw Error('Incomplete pixels');return {width,height,pixels};
}
const api={crc32,encodeAlphaFrame,decodeAlphaFrame};root.VibeAlphaCodec=api;if(typeof module!=='undefined')module.exports=api;
})(typeof window!=='undefined'?window:globalThis);
