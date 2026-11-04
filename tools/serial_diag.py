"""Bounded USB diagnostic monitor; '?' queries cannot execute physical outputs."""
import argparse,time,re
import serial
p=argparse.ArgumentParser();p.add_argument('--port',default='COM4');p.add_argument('--seconds',type=float,default=10);a=p.parse_args()
s=serial.Serial(port=None,baudrate=115200,timeout=.25,write_timeout=1)
s.port=a.port;s.dtr=False;s.rts=False;s.open();s.dtr=True
start=time.monotonic();end=start+a.seconds;data=bytearray();sent=0
try:
    while time.monotonic()<end:
        if time.monotonic()-sent>=1:
            s.write(b'?');sent=time.monotonic()
        data.extend(s.read(4096))
finally:s.close()
text=data.decode('utf-8',errors='replace').replace('\r','')
lines=[x for x in text.splitlines() if x.startswith(('WWW ','NET ')) or 'Guru Meditation' in x or 'Rebooting' in x]
print('panic_count=',text.count('Guru Meditation'),'reboot_count=',text.count('Rebooting'))
if not lines:print('No diagnostic response received')
for x in lines[:4]:print(x)
if len(lines)>8:print('...')
for x in lines[4:] if len(lines)<=8 else lines[-4:]:print(x)
if 'Guru Meditation' in text:raise SystemExit(1)
if not any(x.startswith('WWW ') for x in lines):raise SystemExit(2)
