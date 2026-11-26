#!/usr/bin/env python3
"""Detect changes to files consumed by the ESP32 firmware build, including deletions."""
import json
import os
from pathlib import Path
import subprocess


def affects_firmware(path):
    if path.startswith('firmware/'):
        return not (path.endswith(('.md', '.log')) or '/build/' in path or '/managed_components/' in path)
    return path in {'scripts/ota-release.py', 'scripts/generate_transport_config.py'}


def gate(results):
    infrastructure = results.get('infrastructure-changes', {})
    flag = infrastructure.get('outputs', {}).get('infrastructure')
    outcome = results.get('infrastructure', {}).get('result')
    if infrastructure.get('result') != 'success' or not (
        (flag == 'true' and outcome == 'success') or
        (flag == 'false' and outcome == 'skipped')
    ):
        return False
    for name in ('changes','host-tests','changeset','certificates'):
        if results.get(name,{}).get('result') != 'success':
            return False
    changed = results['changes'].get('outputs',{}).get('firmware')
    return (changed == 'true' and results['firmware']['result']=='success') or (changed == 'false' and results['firmware']['result']=='skipped')


def main():
    if os.environ.get('CHECK_RESULTS'):
        if not gate(json.loads(os.environ['CHECK_RESULTS'])):
            raise SystemExit('Required validation did not succeed')
        return
    event=json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
    pr=event.get('pull_request')
    if pr:
        base,head=pr['base']['sha'],pr['head']['sha']
        paths=subprocess.check_output(['git','diff','--name-only',base+'...'+head],text=True).splitlines()
    else:
        head=os.environ['GITHUB_SHA'];base=event.get('before','')
        if not base or set(base)=={'0'}:
            # New tags: compare with the preceding release, or all files for the first release.
            previous=subprocess.run(['git','describe','--tags','--abbrev=0','--match','v*',head+'^'],capture_output=True,text=True)
            base=previous.stdout.strip() if previous.returncode==0 else ''
        paths=subprocess.check_output(['git','diff','--name-only',base,head] if base else ['git','ls-tree','-r','--name-only',head],text=True).splitlines()
    changed=any(affects_firmware(path) for path in paths)
    value=str(changed).lower()
    with open(os.environ['GITHUB_OUTPUT'],'a') as output:output.write('firmware='+value+'\n')
    print('ESP32 build inputs changed: '+value)
    for path in paths:
        if affects_firmware(path):print(path)


if __name__=='__main__':main()
