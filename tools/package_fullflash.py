"""Build a clean ESP32-C3 4MB factory image from one verified ESP-IDF build.

Run with the ESP-IDF Python environment (esptool installed) and IDF_PATH set.
No device is read and no user data is included.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,struct,subprocess,sys,tempfile
from esptool.bin_image import LoadFirmwareImage

def sha(data):return hashlib.sha256(data).hexdigest()
def check_image(data):
    image=LoadFirmwareImage('esp32c3',data)
    assert image.chip_id==5 and image.flash_mode==2 and image.flash_size_freq==0x2f
    assert image.checksum==image.calculate_checksum()
    assert image.append_digest and image.stored_digest==image.calc_digest
    return image

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',required=True,type=Path)
    parser.add_argument('--source-commit',required=True)
    parser.add_argument('--out',required=True,type=Path)
    args=parser.parse_args();build=args.build_dir;out=args.out
    assert len(args.source_commit)==40 and all(c in '0123456789abcdef' for c in args.source_commit)
    description=json.loads((build/'project_description.json').read_text())
    config=json.loads((build/'config/sdkconfig.json').read_text())
    version=description['project_version'];assert description['target']=='esp32c3'
    boot=(build/'bootloader/bootloader.bin').read_bytes()
    table=(build/'partition_table/partition-table.bin').read_bytes()
    app=(build/description['app_bin']).read_bytes()
    check_image(boot);image=check_image(app)
    assert struct.unpack_from('<I',image.segments[0].data)[0]==0xabcd5432
    assert image.segments[0].data[16:48].split(b'\0')[0].decode()==version
    assert len(boot)<=0x8000 and len(app)<=0x140000
    idf=Path(os.environ['IDF_PATH'])
    spec=importlib.util.spec_from_file_location('partitions',idf/'components/partition_table/gen_esp32part.py')
    partitions=importlib.util.module_from_spec(spec);spec.loader.exec_module(partitions)
    pt=partitions.PartitionTable.from_binary(table);pt.verify()
    assert [(p.name,p.type,p.subtype,p.offset,p.size) for p in pt]==[
        ('nvs',1,2,0x9000,0x6000),('phy_init',1,1,0xf000,0x1000),
        ('factory',0,0,0x10000,0x140000),('storage',1,0x82,0x150000,0x2b0000)]
    assert not any(p.encrypted for p in pt)
    out.mkdir(parents=True,exist_ok=True)
    filename=f'codex-companion-v{version}-esp32c3-fullflash.bin';target=out/filename
    with tempfile.TemporaryDirectory() as temporary:
        temp=Path(temporary);empty=temp/'empty';empty.mkdir();fs_file=temp/'storage.bin'
        subprocess.run([sys.executable,str(idf/'components/spiffs/spiffsgen.py'),'0x2b0000',str(empty),str(fs_file),
            '--page-size',str(config['SPIFFS_PAGE_SIZE']),'--block-size','4096',
            '--obj-name-len',str(config['SPIFFS_OBJ_NAME_LEN']),'--meta-len',str(config['SPIFFS_META_LENGTH']),
            '--use-magic' if config['SPIFFS_USE_MAGIC'] else '--no-magic',
            '--use-magic-len' if config['SPIFFS_USE_MAGIC_LENGTH'] else '--no-magic-len'],check=True)
        fs=fs_file.read_bytes();assert len(fs)==0x2b0000 and fs!=b'\xff'*len(fs)
        subprocess.run([sys.executable,'-m','esptool','--chip','esp32c3','merge-bin','-o',str(target),
            '--flash-mode','keep','--flash-freq','keep','--flash-size','keep','--pad-to-size','4MB',
            '0x0',str(build/'bootloader/bootloader.bin'),'0x8000',str(build/'partition_table/partition-table.bin'),
            '0x10000',str(build/description['app_bin']),'0x150000',str(fs_file)],check=True)
    data=target.read_bytes();assert len(data)==0x400000
    components=[('bootloader',0,boot),('partition_table',0x8000,table),('application',0x10000,app),('empty_spiffs',0x150000,fs)]
    cursor=0
    for _,offset,component in components:
        assert data[cursor:offset]==b'\xff'*(offset-cursor)
        assert data[offset:offset+len(component)]==component
        cursor=offset+len(component)
    assert cursor==len(data) and data[0x9000:0x10000]==b'\xff'*0x7000
    manifest=dict(product='ESP32 Codex Companion',version=version,source_commit=args.source_commit,
        image=dict(file=filename,offset='0x0',bytes=len(data),sha256=sha(data),chip='esp32c3',flash_size='4MB',flash_mode='dio',flash_freq='80m',erases_user_data=True,blank_chip_image=True),
        components=[dict(name=n,offset=hex(o),bytes=len(b),sha256=sha(b)) for n,o,b in components],
        filesystem=dict(files=0,page_size=config['SPIFFS_PAGE_SIZE'],block_size=4096,obj_name_len=config['SPIFFS_OBJ_NAME_LEN'],meta_len=config['SPIFFS_META_LENGTH'],magic=config['SPIFFS_USE_MAGIC'],magic_length=config['SPIFFS_USE_MAGIC_LENGTH']),
        privacy='Blank NVS/PHY and generated empty SPIFFS. No device backup, personal files, Wi-Fi, account data or custom settings.',
        validation='ESP checksum/SHA, partition MD5/layout, exact component slices, erased gaps and 4MB size verified. No hardware full erase claimed.')
    (out/'fullflash-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    guide=f'''ESP32 Codex Companion v{version} 完整烧录

仅适用于原项目同款 ESP32-C3、4MB Flash 板。
文件：{filename}；烧录地址：0x0；模式 DIO，频率 80MHz，大小 4MB。
包含启动程序、分区表、本版本应用及干净的素材文件系统，不含个人数据。

完整烧录会覆盖全部素材、Wi-Fi 和自定义设置，请先备份。
已有兼容固件且要保留数据时，请使用 -esp32c3.bin 应用升级文件，地址 0x10000。

下载后用 fullflash-SHA256SUMS.txt 或 SHA256SUMS.txt 校验。先暂停电脑桥接并关闭串口监视器。
将 COMx 替换为设备实际串口，在下载目录的 PowerShell 中运行：
Get-FileHash .\\{filename} -Algorithm SHA256
python -m pip install esptool
python -m esptool --chip esp32c3 --port COMx --baud 460800 write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x0 .\\{filename}

只需这一份完整文件，无需另写应用/分区表，也无需额外整片擦除。
连接失败时按住 BOOT、短按 RESET、再松开 BOOT 后重试。完成后按 RESET，按使用说明配网或选择离线使用，导入自己的素材。
也可用 Espressif Flash Download Tool：ESP32-C3，地址0x0，DIO/80MHz/4MB。
组成与原应用校验值见 fullflash-manifest.json；使用说明见仓库 docs/flashing.md。
'''
    (out/'fullflash-README.txt').write_text(guide,encoding='utf8')
    (out/'fullflash-SHA256SUMS.txt').write_text(''.join(f'{sha(p.read_bytes())}  {p.name}\n' for p in (target,out/'fullflash-manifest.json',out/'fullflash-README.txt')),encoding='ascii')
    print('Clean full image verified:',filename,sha(data))

if __name__=='__main__':main()
