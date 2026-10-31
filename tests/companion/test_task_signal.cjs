const {spawnSync}=require('node:child_process'),fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const root=path.resolve(__dirname,'../..'),tmp=fs.mkdtempSync(path.join(os.tmpdir(),'vibe-signal-'));
function run(cmd,args){const r=spawnSync(cmd,args,{stdio:'inherit'});if(r.error)throw r.error;if(r.status)throw Error('Task signal fixture failed: '+r.status);}
try{
 const compiler=process.env.ZIG||process.env.CC||(process.platform==='win32'?'zig':'cc');
 const args=process.env.ZIG||path.basename(compiler).startsWith('zig')?['cc']:[];
 const out=path.join(tmp,process.platform==='win32'?'fixture.exe':'fixture');
 run(compiler,[...args,'-std=c11','-Wall','-Wextra','-Werror','-I',path.join(root,'main/companion'),'-I',path.join(root,'components/cjson'),path.join(__dirname,'test_task_signal.c'),'-o',out]);run(out,[]);
}finally{if(path.dirname(path.resolve(tmp))!==path.resolve(os.tmpdir())||!path.basename(tmp).startsWith('vibe-signal-'))throw Error('Unsafe fixture path');fs.rmSync(tmp,{recursive:true,force:true});}
