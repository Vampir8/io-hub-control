"""Package sources and verified build artifacts; exclude caches and local credentials."""
from pathlib import Path
import zipfile,json,hashlib,subprocess
ROOT=Path(__file__).resolve().parents[1]
DEST=ROOT/'dist';DEST.mkdir(exist_ok=True)
archive=DEST/'nchub-supermini.zip'
if (ROOT/'.git').exists():
    names=subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode().split('\0')
    files=[ROOT/p for p in names if p and (ROOT/p).is_file()]
else:
    files=[ROOT/p for p in ['platformio.ini','partitions.csv','README.md','AGENTS.md','config.schema.json','.gitignore','.gitattributes','requirements-dev.txt']]
    for directory in ['src','include','data','examples','tools','docs','test','web-editor']:
        files.extend(p for p in (ROOT/directory).rglob('*') if p.is_file() and not any(n in ('build','__pycache__','node_modules') for n in p.parts)
                     and p.suffix not in ('.log','.zip')
                     and not (p.suffix=='.png' and p.name not in ('panel-io-desktop.png','panel-io-mobile.png')))
build=ROOT/'.pio/build/supermini'
bins={'bootloader.bin':'0x0','partitions.bin':'0x8000','firmware.bin':'0x20000','littlefs.bin':'0x220000'}
manifest={'chip':'esp32c3','flashSize':'4MB','files':[],'note':'Use the included PlatformIO project for upload; do not write OTA boot_app0 at 0xe000 (NVS).'}
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for p in files:z.write(p,Path('nchub-supermini')/p.relative_to(ROOT))
    for name,offset in bins.items():
        p=build/name
        if not p.exists():raise SystemExit('Run pio run and pio run -t buildfs first')
        z.write(p,'nchub-supermini/prebuilt/'+name)
        manifest['files'].append({'file':name,'offset':offset,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    z.writestr('nchub-supermini/prebuilt/manifest.json',json.dumps(manifest,indent=2)+'\n')
print(f'Created {archive} ({archive.stat().st_size} bytes)')
