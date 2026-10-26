const {spawnSync}=require('node:child_process');
const path=require('node:path');
const fs=require('node:fs');
const cwd=path.resolve(__dirname,'..');
const reportFile=process.env.VIBE_VALIDATION_REPORT;
const pendingFile=reportFile?reportFile+'.pending':null;
function source(){const id=spawnSync('git',['rev-parse','HEAD'],{cwd,encoding:'utf8'}),state=spawnSync('git',['status','--porcelain'],{cwd,encoding:'utf8'});return id.status===0&&state.status===0&&!state.stdout.trim()?id.stdout.trim():null;}
const initialSource=reportFile?source():null;
if(reportFile){for(const file of [reportFile,pendingFile])if(fs.existsSync(file))fs.unlinkSync(file);process.env.VIBE_VALIDATION_REPORT=pendingFile;}
function run(command,args){const result=spawnSync(command,args,{cwd,stdio:'inherit'});if(result.error)throw result.error;if(result.status!==0)process.exit(result.status||1);}
const suites=['test_web','test_alpha','test_gif_frames','test_storage','test_upload_failure','test_task_dots','test_cleanup','test_media_storage'];
for(const file of suites)run(process.execPath,['tests/companion/'+file+'.cjs']);
const python=process.env.PYTHON||(process.platform==='win32'?'python':'python3');
run(python,['-c',"import sys,unittest,json,os,pathlib;sys.path.insert(0,'tests/companion');names=['test_host','test_multitask','test_realtime','test_failover','test_history_observer','test_ui_approval','test_management_queue','test_usb_media','test_local_management'];r=unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromNames(names));p=os.environ.get('VIBE_VALIDATION_REPORT');p and pathlib.Path(p).write_text(json.dumps(dict(host_tests=r.testsRun,passed=r.wasSuccessful(),failures=len(r.failures),errors=len(r.errors))),encoding='utf8');sys.exit(not r.wasSuccessful())"]);
if(reportFile){const report=JSON.parse(fs.readFileSync(pendingFile,'utf8'));report.node_suites=suites.length;const current=source();report.source_commit=current===initialSource?current:null;report.finished_at=new Date().toISOString();fs.writeFileSync(pendingFile,JSON.stringify(report,null,2)+'\n');fs.renameSync(pendingFile,reportFile);}
