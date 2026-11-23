#!/usr/bin/env python3
"""One-time paired LAN provisioning. Read the key privately; never compile it into firmware."""
import argparse, getpass, json, urllib.parse, urllib.request, urllib.error, ssl
from pathlib import Path

def key_from_env(path):
    found=[]
    for line in path.read_text().splitlines():
        name,sep,value=line.strip().removeprefix('export ').partition('=')
        if sep and name.strip()=='IDFM_PRIM_API_KEY':found.append(value.strip().strip('\"\''))
    if len(found)!=1 or not found[0] or len(found[0])>512 or any(ord(c)<33 or ord(c)>126 for c in found[0]):
        raise ValueError('Expected one valid IDFM_PRIM_API_KEY; credential withheld.')
    return found[0]

def destination(qr):
    url=urllib.parse.urlsplit(qr)
    if url.scheme != 'https' or url.username or url.password or url.query:
        raise ValueError('Use the HTTPS setup QR URL on your display.')
    if not url.hostname or url.path not in ('','/') or url.port not in (None,443):
        raise ValueError('Use the HTTPS setup QR URL on your display.')
    token=urllib.parse.parse_qs(url.fragment).get('token',[''])[0]
    if len(token)!=32 or any(c not in '0123456789abcdef' for c in token):raise ValueError('Invalid pairing token.')
    return urllib.parse.urlunsplit((url.scheme,url.netloc,'/api/credential','','')),token
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args):return None

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--ca',type=Path,help='Explicit development CA PEM; otherwise use system trust');p.add_argument('--env',type=Path,help='Existing private .env, read locally only');args=p.parse_args()
    try:
        target,token=destination(getpass.getpass('Current setup QR URL (hidden): '))
        key=key_from_env(args.env) if args.env else getpass.getpass('PRIM API key (hidden): ')
        request=urllib.request.Request(target,data=json.dumps({'key':key}).encode(),headers={'Authorization':'Bearer '+token,'Content-Type':'application/json'},method='POST')
        context=ssl.create_default_context(cafile=str(args.ca) if args.ca else None)
        context.minimum_version=ssl.TLSVersion.TLSv1_2
        with urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect(), urllib.request.HTTPSHandler(context=context)).open(request,timeout=10) as response:
            result=json.loads(response.read(1024))
        if result.get('saved') is not True:raise ValueError('Save was not confirmed.')
        print('Transport credential saved on the device. No firmware key was generated.')
    except (OSError,ValueError,urllib.error.URLError):
        raise SystemExit('Provisioning failed. Verify the device firmware, current pairing QR and trusted LAN; no credential values logged.')
if __name__=='__main__':main()
