const {spawnSync}=require('node:child_process');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const root=path.resolve(__dirname,'../..'),tmp=fs.mkdtempSync(path.join(os.tmpdir(),'vibe-storage-test-'));
const output=path.join(tmp,process.platform==='win32'?'fixture.exe':'fixture');
function run(cmd,args){const r=spawnSync(cmd,args,{stdio:'inherit'});if(r.error)throw r.error;if(r.status!==0)throw Error('C storage fixture failed: '+r.status);}
try{
 const compiler=process.env.ZIG||process.env.CC||(process.platform==='win32'?'zig':'cc');
 const args=process.env.ZIG||path.basename(compiler).startsWith('zig')?['cc']:[];
 run(compiler,[...args,'-Wall','-Wextra','-Werror','-I',path.join(root,'main/storage'),path.join(__dirname,'test_media_storage.c'),path.join(root,'main/storage/media_storage.c'),'-o',output]);
 run(output,[]);
}finally{if(path.dirname(path.resolve(tmp))!==path.resolve(os.tmpdir())||!path.basename(tmp).startsWith('vibe-storage-test-'))throw Error('Unsafe test cleanup target');fs.rmSync(tmp,{recursive:true,force:true});}
