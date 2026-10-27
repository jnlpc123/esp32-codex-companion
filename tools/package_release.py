"""Package an already verified application and a committed clean source tree."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,zipfile

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--app',required=True,type=Path)
p.add_argument('--source-repo',type=Path,default=root)
p.add_argument('--out',type=Path)
p.add_argument('--guide-html',type=Path)
p.add_argument('--validation',required=True,type=Path,help='Fresh passing JSON from VIBE_VALIDATION_REPORT + npm test')
args=p.parse_args()
version=json.loads((root/'package.json').read_text())['version']
args.out=args.out or root/'dist'/version
args.out.mkdir(parents=True,exist_ok=True)
assert re.fullmatch(r'\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?',version),'Invalid version'
validation=json.loads(args.validation.read_text(encoding='utf8'))
assert validation.get('passed') and validation.get('host_tests',0)>0 and validation.get('node_suites',0)>0,'Passing validation required'
assert args.app.stat().st_size<=0x140000,'Application exceeds existing partition'
description=args.app.parent/'project_description.json'
if description.exists():assert json.loads(description.read_text())['project_version']==version
def git(*parts):return subprocess.check_output(['git','-C',str(args.source_repo),*parts]).decode().strip()
commit=git('rev-parse','HEAD')
assert not git('status','--porcelain','--untracked-files=normal'),'Commit reviewed source before packaging'
assert validation.get('source_commit')==commit,'Validation must come from this clean source commit'
names=subprocess.check_output(['git','-C',str(args.source_repo),'ls-files','-z']).decode().split('\0')
for name in filter(None,names):
    assert not any(part in ('runtime','__pycache__','build-short','managed_components','node_modules','.codex','素材','图片') for part in Path(name).parts),name
    assert name not in ('companion_host/config.json','companion_host/review-hooks.json','sdkconfig'),name
    assert not name.endswith(('.sqlite','.sqlite-wal','.bin','.log')),name
prefix='codex-companion-v'+version
app=args.out/(prefix+'-esp32c3.bin');app.write_bytes(args.app.read_bytes())
manifest=dict(product='ESP32 Codex Companion',version=version,source_commit=commit,
    application=dict(file=app.name,offset='0x10000',bytes=app.stat().st_size,
                     sha256=hashlib.sha256(app.read_bytes()).hexdigest(),blank_chip_image=False),
    validation=dict(**validation,platform='Windows / ESP32-C3',audio_audible=False,release_stage='candidate' if '-' in version else 'stable'),
    privacy='No account data, credentials, local configuration, runtime queues or full-flash backups included')
windows=args.out/(prefix+'-windows.zip')
with zipfile.ZipFile(windows,'w',zipfile.ZIP_DEFLATED) as archive:
    for name in filter(None,names):
        if not name.startswith('companion_host/'):continue
        path=root/name
        if path.suffix not in ('.py','.cs','.exe') and path.name!='pyserial-LICENSE.txt':continue
        archive.write(path,path.relative_to(root).as_posix())
    for path in (root/'distribution/config-templates').glob('*.json'):archive.write(path,'config-templates/'+path.name)
    for name in ('index.html','companion.css','companion.js','alpha-codec.js','vendor/gifuct.js'):
        archive.write(root/'web'/name,'web/'+name)
    archive.write(root/'package.json','package.json')
    archive.write(root/'tests/run-tests.cjs','tests/run-tests.cjs')
    for name in ('test_host.py','test_multitask.py','test_realtime.py','test_failover.py','test_history_observer.py','test_ui_approval.py','test_management_queue.py','test_usb_media.py','test_local_management.py','test_web.cjs','test_alpha.cjs','test_gif_frames.cjs','test_storage.cjs','test_upload_failure.cjs','test_task_dots.cjs','test_cleanup.cjs','test_media_storage.cjs','test_media_storage.c','test_render_memory.cjs','test_render_memory.c','test_wifi_provision.c','test_wifi_provision.cjs','test_wifi_ui.cjs','test_wifi_credentials.c','test_wifi_credentials.cjs','test_config_migration.cjs'):
        archive.write(root/'tests/companion'/name,'tests/companion/'+name)
    for name in ('media_storage.c','media_storage.h','legacy_assets.h'):
        archive.write(root/'main/storage'/name,'main/storage/'+name)
    for name in ('wifi_provision.c','wifi_provision.h','wifi_manager.h','wifi_prompt.h','wifi_credentials.c','wifi_credentials.h'):
        archive.write(root/'main/wifi'/name,'main/wifi/'+name)
    for name in ('ap-mode.jpg','wifi-connecting.jpg','wifi-connected.jpg'):
        archive.write(root/'main/wifi/assets'/name,'main/wifi/assets/'+name)
    for name in ('frame_transform.h','animation_cache.c','animation_cache.h','animation_crc.h','jpeg_decode.c','display_init.h','companion_render.c','companion_render.h','alpha_cache.h','task_dots.h','animation_timing.h'):
        archive.write(root/'main/display'/name,'main/display/'+name)
    for name in ('companion.c','companion.h','task_status.c','task_status.h'):archive.write(root/'main/companion'/name,'main/companion/'+name)
    for name in ('cJSON.c','cJSON.h','LICENSE'):archive.write(root/'components/cjson'/name,'components/cjson/'+name)
    archive.write(root/'docs/usb-management.md','docs/usb-management.md')
    for name in ('usb-wifi-1.2.0-rc.1.md','release-1.1.0.md','effects-memory-1.1.0-rc.2.md','usb-storage-1.1.0-rc.1.md','cleanup-acceptance-2026-10-27.md'):
        archive.write(root/'docs/validation'/name,'docs/validation/'+name)
    for name in ('使用说明与Codex配置.md','LICENSE','THIRD_PARTY_NOTICES.md','CHANGELOG.md'):archive.write(root/name,name)
    archive.write(root/'distribution/先读我_快速开始.md','先读我_快速开始.md')
    if args.guide_html:archive.write(args.guide_html,'使用说明与Codex配置.html')
    archive.writestr('README.txt',f'Codex Companion v{version} Windows bridge. Read 先读我_快速开始.md. Review Hooks locally, then run bridge.py --resume. USB management: http://127.0.0.1:17831/ (requires 1.1 firmware). No flash is needed when moving an existing compatible board to another computer.\n')
    archive.writestr('manifest.json',json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
source=args.out/(prefix+'-source.zip')
subprocess.run(['git','-C',str(args.source_repo),'archive','--format=zip','--output',str(source.resolve()),'HEAD'],check=True)
(args.out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
checks=[]
for path in sorted(args.out.iterdir()):
    if path.is_file() and path.name!='SHA256SUMS.txt':checks.append(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name)
(args.out/'SHA256SUMS.txt').write_text('\n'.join(checks)+'\n',encoding='ascii')
for path in (windows,source):
    with zipfile.ZipFile(path) as archive:assert archive.testzip() is None
print(json.dumps(manifest,ensure_ascii=False,indent=2))
