"""Package an already verified application and a committed clean source tree."""
from pathlib import Path
import argparse,hashlib,json,subprocess,zipfile

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--app',required=True,type=Path)
p.add_argument('--source-repo',type=Path,default=root)
p.add_argument('--out',type=Path,default=root/'dist/1.0.0')
p.add_argument('--guide-html',type=Path)
args=p.parse_args();args.out.mkdir(parents=True,exist_ok=True)
version=json.loads((root/'package.json').read_text())['version']
assert version=='1.0.0'
description=args.app.parent/'project_description.json'
if description.exists():assert json.loads(description.read_text())['project_version']==version
def git(*parts):return subprocess.check_output(['git','-C',str(args.source_repo),*parts]).decode().strip()
commit=git('rev-parse','HEAD')
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
    validation=dict(host_tests=71,node_suites=6,platform='Windows / ESP32-C3',audio_audible=False),
    privacy='No account data, credentials, local configuration, runtime queues or full-flash backups included')
windows=args.out/(prefix+'-windows.zip')
with zipfile.ZipFile(windows,'w',zipfile.ZIP_DEFLATED) as archive:
    for path in (root/'companion_host').rglob('*'):
        if not path.is_file() or any(part in ('runtime','__pycache__') for part in path.parts):continue
        if path.suffix not in ('.py','.cs','.exe') and path.name!='pyserial-LICENSE.txt':continue
        archive.write(path,path.relative_to(root).as_posix())
    for path in (root/'distribution/config-templates').glob('*.json'):archive.write(path,'config-templates/'+path.name)
    for name in ('使用说明与Codex配置.md','LICENSE','THIRD_PARTY_NOTICES.md','CHANGELOG.md'):archive.write(root/name,name)
    archive.write(root/'distribution/先读我_快速开始.md','先读我_快速开始.md')
    if args.guide_html:archive.write(args.guide_html,'使用说明与Codex配置.html')
    archive.writestr('README.txt','Codex Companion v1.0.0 Windows bridge. Read 先读我_快速开始.md. Install Python/Codex, generate and review Hooks locally, then run bridge.py --resume. No firmware flash is needed when moving an existing board to another computer.\n')
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
