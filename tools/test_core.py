"""Host tests run the actual C++ parser and firmware validator, without ESP32."""
from pathlib import Path
import subprocess, sys, shutil, json
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'test'/'build';out.mkdir(exist_ok=True)
arduinojson=ROOT/'.pio/libdeps/supermini/ArduinoJson/src'
if not arduinojson.exists(): sys.exit('Run pio pkg install -e supermini first')
if shutil.which('g++'): compiler=['g++']
else:
    try:
        import ziglang
        compiler=[str(Path(ziglang.__file__).parent/'zig.exe'),'c++']
    except ImportError: sys.exit('Install g++ or pip install ziglang==0.13.0')
exe=out/('core.exe' if sys.platform=='win32' else 'core')
subprocess.run(compiler+['-std=c++17','-DHUB_HOST_TEST=1','-DARDUINOJSON_ENABLE_STD_STRING=1','-I'+str(ROOT/'include'),'-I'+str(arduinojson),str(ROOT/'test/test_core.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe),str(ROOT/'examples/config.json')],check=True)
out_exe=out/('outputs.exe' if sys.platform=='win32' else 'outputs')
subprocess.run(compiler+['-std=c++17','-DHUB_HOST_TEST=1','-DARDUINOJSON_ENABLE_STD_STRING=1','-I'+str(ROOT/'test/stubs'),'-I'+str(ROOT/'include'),'-I'+str(arduinojson),str(ROOT/'test/test_outputs.cpp'),'-o',str(out_exe)],check=True)
subprocess.run([str(out_exe),str(ROOT/'examples/config.json')],check=True)
store_exe=out/('store.exe' if sys.platform=='win32' else 'store')
subprocess.run(compiler+['-std=c++17','-DHUB_HOST_TEST=1','-DARDUINOJSON_ENABLE_STD_STRING=1','-I'+str(ROOT/'test/stubs'),'-I'+str(ROOT/'include'),'-I'+str(arduinojson),str(ROOT/'test/test_store.cpp'),'-o',str(store_exe)],check=True)
subprocess.run([str(store_exe),str(ROOT/'examples/config.json')],check=True)
ports_exe=out/('ports.exe' if sys.platform=='win32' else 'ports')
subprocess.run(compiler+['-std=c++17','-DHUB_HOST_TEST=1','-DARDUINOJSON_ENABLE_STD_STRING=1','-I'+str(ROOT/'test/stubs'),'-I'+str(ROOT/'include'),'-I'+str(arduinojson),str(ROOT/'test/test_ports.cpp'),'-o',str(ports_exe)],check=True)
subprocess.run([str(ports_exe),str(ROOT/'examples')],check=True)
import jsonschema
jsonschema.Draft202012Validator.check_schema(json.loads((ROOT/'config.schema.json').read_text(encoding='utf-8')))
jsonschema.validate(json.loads((ROOT/'examples/config.json').read_text(encoding='utf-8')),json.loads((ROOT/'config.schema.json').read_text(encoding='utf-8')))
print('PASS JSON Schema and example')

for name in ['ports-signals.json','ports-servos.json','ports-mixed.json']:
    jsonschema.validate(json.loads((ROOT/'examples'/name).read_text(encoding='utf-8')),json.loads((ROOT/'config.schema.json').read_text(encoding='utf-8')))
print('PASS v2 examples JSON Schema')
