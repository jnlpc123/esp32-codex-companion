const {spawnSync}=require('node:child_process');
const path=require('node:path');
const cwd=path.resolve(__dirname,'..');
function run(command,args){const result=spawnSync(command,args,{cwd,stdio:'inherit'});if(result.error)throw result.error;if(result.status!==0)process.exit(result.status||1);}
for(const file of ['test_web','test_alpha','test_gif_frames','test_storage','test_upload_failure','test_task_dots'])run(process.execPath,['tests/companion/'+file+'.cjs']);
const python=process.env.PYTHON||(process.platform==='win32'?'python':'python3');
run(python,['-c',"import sys,unittest;sys.path.insert(0,'tests/companion');names=['test_host','test_multitask','test_realtime','test_failover','test_history_observer','test_ui_approval'];r=unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromNames(names));sys.exit(not r.wasSuccessful())"]);
