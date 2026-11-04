"""Local UI preview only; this mock never controls hardware or a broker."""
from http.server import SimpleHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
import json,time,argparse
ROOT=Path(__file__).resolve().parents[1]
cfg=json.loads((ROOT/'examples/ports-mixed.json').read_text(encoding='utf-8'))
started=time.time()
class Handler(SimpleHTTPRequestHandler):
    def __init__(self,*a,**kw):super().__init__(*a,directory=str(ROOT/'data'),**kw)
    def json(self,d,status=200):
        payload=json.dumps(d,ensure_ascii=False).encode();self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload)
    def do_GET(self):
        if self.path=='/api/config':return self.json(cfg)
        if self.path=='/api/status':return self.json({'token':'preview','wifi':True,'mqtt':True,'ip':'PREVIEW — no ESP','ap':False,'apName':'preview-setup','uptime':int(time.time()-started),'heap':150000,'movement':False,'allowed':False,'blocked':'DEMO PREVIEW — no device; saving and movement disabled','mode':'blocked','dropped':0,'hardwareError':'','servos':[{'id':'servo1','known':False,'commanded':90,'target':90,'pwm':False,'moving':False,'calibration':False}],'cache':[{'type':'T','source':'Switch 2','state':'s','ageMs':1000,'matched':'Turnout1'},{'type':'T','source':'Light 1','state':'gf','ageMs':4000,'matched':'No matching rule'}],'logs':['WEB PREVIEW — demonstration data, no device','T|Turnout1|s']})
        return super().do_GET()
    def do_POST(self):
        n=int(self.headers.get('Content-Length',0));req=json.loads(self.rfile.read(n))
        if self.path=='/api/export':return self.json(cfg)
        if self.path=='/api/preview':
            s=req.get('payload','').split('|',2);actions=[]
            if len(s)==3:
                for p in cfg['ports']:
                    if p['enabled'] and p['type']==s[0] and p['source']==s[1]:
                        template=next(t for t in cfg['templates'] if t['id']==p['template'])
                        for state in template['states']:
                            if state['value']==s[2]:actions.append({'port':p['id'],'gpio':p['pin'],'state':state})
            return self.json({'ok':True,'actions':actions,'preview':True})
        return self.json({'ok':False,'message':'This is a panel preview; physical operations and saving are disabled'},409)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--port',type=int,default=8765);a=p.parse_args()
    print('UI mock: http://127.0.0.1:'+str(a.port),flush=True)
    ThreadingHTTPServer(('127.0.0.1',a.port),Handler).serve_forever()
