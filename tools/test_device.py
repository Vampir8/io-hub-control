"""Read-only parser/preview checks on a flashed ESP32. No physical action endpoint."""
import argparse,json,urllib.request
p=argparse.ArgumentParser();p.add_argument('--host',required=True,help='http://192.168.4.1');args=p.parse_args();base=args.host.rstrip('/')
def get(path):
    return json.load(urllib.request.urlopen(base+'/api/'+path,timeout=10))
token=get('status')['token']
def preview(payload):
    req=urllib.request.Request(base+'/api/preview',data=json.dumps({'payload':payload}).encode(),headers={'Content-Type':'application/json','X-Hub-Token':token})
    try:return urllib.request.urlopen(req,timeout=10).status
    except urllib.error.HTTPError as e:return e.code
for s in ['T|Turnout1|s','S|12|0','T|Łódź Żółć|rf','T|Train1|p,50','T|A|nieznany']:
    assert preview(s)==200,s
for s in ['T|A','T||s','T|A|','S|-1|0','S|12|x','D!!!','T|A|r|x','T|A|s\x00']:
    assert preview(s)==400,s
print('PASS device parser/preview; no physical actions sent')
