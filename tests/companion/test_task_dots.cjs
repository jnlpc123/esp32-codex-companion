const assert=require('node:assert/strict');
const {dotColor565,taskDotLayout}=require('../../web/companion.js');
assert.equal(dotColor565(0xf800),0x07ff,'retain cyan dots on red');
assert.equal(dotColor565(0xffe0),0x001f,'retain blue dots on yellow');
assert.equal(dotColor565(0x07e0),0xf81f,'retain magenta dots on green');
assert.equal(dotColor565(0),0xffff);assert.equal(dotColor565(0xffff),0);
for(const count of [0,1,2,3,4,5,6,9999]){
 const dots=taskDotLayout(count);assert.equal(dots.length,Math.min(count,5));
 dots.forEach((dot,index)=>{
  assert.equal(dot.radius,5);assert.equal(dot.hollow,count>5&&index===4);
  for(let y=-5;y<=5;y++)for(let x=-5;x<=5;x++)if(x*x+y*y<=25){
   const px=dot.x+x,py=dot.y+y;
   assert.ok(py>=134&&py<150,'dots stay below reserved foreground area');
   assert.ok((px-80)**2+(py-80)**2<=71**2,'dots leave at least nine pixels of radial bezel clearance');
  }
 });
 assert.equal(dots.reduce((sum,d)=>sum+d.x,0),80*dots.length,'symmetric around screen center');
}
const five=taskDotLayout(5);assert.ok(five[0].y<five[2].y&&five[4].y<five[2].y,'a lower-screen arc');
console.log('Original complementary colors, enlarged arc, circular bounds, foreground exclusion and overflow: PASS');
