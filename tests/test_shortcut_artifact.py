"""Verify the shipped native export matches its reviewed workflow and protocol."""
import hashlib
import json
import plistlib
import struct
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[1]
folder = root / 'companion/shortcut'
manifest = json.loads((folder / 'manifest.json').read_text())
installer = folder / 'Weather Station Location.shortcut'
data = installer.read_bytes()
assert hashlib.sha256(data).hexdigest() == manifest['sha256']
assert data[:4] == b'AEA1'
metadata = plistlib.loads(data[12:12 + struct.unpack_from('<I', data, 8)[0]])
with TemporaryDirectory() as temporary:
    tmp = Path(temporary)
    (tmp / 'leaf.der').write_bytes(metadata['SigningCertificateChain'][0])
    with (tmp / 'public.pem').open('wb') as output:
        subprocess.run(['openssl', 'x509', '-inform', 'DER', '-in', str(tmp / 'leaf.der'),
                        '-pubkey', '-noout'], stdout=output, check=True)
    # Verify archive integrity against the native export's signing public key.
    subprocess.run(['aea', 'decrypt', '-i', str(installer), '-o', str(tmp / 'archive'),
                    '-sign-pub', str(tmp / 'public.pem')], check=True)
    subprocess.run(['aa', 'extract', '-i', str(tmp / 'archive'), '-d', str(tmp / 'out')], check=True)
    source = (tmp / 'out/Shortcut.wflow').read_bytes()
assert hashlib.sha256(source).hexdigest() == manifest['source_sha256']
assert source == (folder / 'Weather Station Location.source.plist').read_bytes()
workflow = plistlib.loads(source)
actions = workflow['WFWorkflowActions']
assert len(actions) == 4
assert [a['WFWorkflowActionIdentifier'] for a in actions] == [
    'is.workflow.actions.getcurrentlocation',
    'is.workflow.actions.properties.locations',
    'is.workflow.actions.properties.locations',
    'is.workflow.actions.gettext',
]
parameters = [a['WFWorkflowActionParameters'] for a in actions]
for index, coordinate in [(1, 'Latitude'), (2, 'Longitude')]:
    assert parameters[index]['WFContentItemPropertyName'] == coordinate
    assert parameters[index]['WFInput']['Value']['OutputUUID'] == parameters[0]['UUID']
text = parameters[3]['WFTextActionText']['Value']
assert text['string'] == 'WSLOC1\n\ufffc\n\ufffc'
assert set(text['attachmentsByRange']) == {'{7, 1}', '{9, 1}'}
for index, offset in [(1, 7), (2, 9)]:
    token = text['attachmentsByRange'][f'{{{offset}, 1}}']
    assert token['Type'] == 'ActionOutput'
    assert token['OutputUUID'] == parameters[index]['UUID']
assert not workflow['WFWorkflowImportQuestions']
print('PASS: signed installer integrity, native source, shared location reading and both output coordinates')
