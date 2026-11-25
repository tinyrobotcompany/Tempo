"""Product identity must come from manufacturing, never a caller-selected namespace."""
import base64
import json

import pytest
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from test_service import setup, signed, csr, NOW, SUFFIX
from tempo_certificates.protocol import Rejected, hostname
from tempo_certificates.service import DeviceService

WAVEFORM = 'waveform.tinyrobotcompany.io'
CONTEXT = b'WAVEFORM-DEVICE-V1\nPOST\n/v1/device\n'


def waveform_request(key, store, action='status', **fields):
    body, _ = signed(key, store, action, product='waveform', **fields)
    return body, base64.b64encode(key.sign(CONTEXT + body, ec.ECDSA(hashes.SHA256()))).decode()


def test_waveform_admitted_in_its_own_namespace(setup):
    key, store, _ = setup
    store.row.update(product='waveform', hostname=hostname(store.row['PartitionKey'], WAVEFORM))
    service = DeviceService(store, SUFFIX, clock=lambda: NOW, waveform_suffix=WAVEFORM)
    reply = service.handle(*waveform_request(key, store, 'certificate', csr=csr(store.row['hostname'])))
    assert reply.status == 202


def test_tempo_registration_cannot_select_waveform_namespace(setup):
    key, store, _ = setup
    service = DeviceService(store, SUFFIX, clock=lambda: NOW, waveform_suffix=WAVEFORM)
    with pytest.raises(Rejected, match='product_mismatch'):
        service.handle(*waveform_request(key, store))
    assert not store.nonces


def test_waveform_cannot_use_legacy_tempo_protocol(setup):
    key, store, service = setup
    store.row['product'] = 'waveform'
    with pytest.raises(Rejected, match='product_mismatch'):
        service.handle(*signed(key, store))


def test_existing_tempo_device_needs_no_migration(setup):
    key, store, _ = setup
    service = DeviceService(store, SUFFIX, clock=lambda: NOW, waveform_suffix=WAVEFORM)
    assert service.handle(*signed(key, store)).status == 200


def test_waveform_csr_cannot_request_tempo_hostname(setup):
    key, store, _ = setup
    tempo_name = store.row['hostname']
    store.row.update(product='waveform', hostname=hostname(store.row['PartitionKey'], WAVEFORM))
    service = DeviceService(store, SUFFIX, clock=lambda: NOW, waveform_suffix=WAVEFORM)
    with pytest.raises(Rejected, match='invalid_csr'):
        service.handle(*waveform_request(key, store, 'certificate', csr=csr(tempo_name)))


def test_waveform_signature_is_domain_separated(setup):
    key, store, _ = setup
    store.row.update(product='waveform', hostname=hostname(store.row['PartitionKey'], WAVEFORM))
    service = DeviceService(store, SUFFIX, clock=lambda: NOW, waveform_suffix=WAVEFORM)
    with pytest.raises(Rejected, match='invalid_signature'):
        service.handle(*signed(key, store, product='waveform'))


def test_waveform_product_is_registry_owned():
    from unittest.mock import Mock
    from test_adapters import Entity
    from tempo_certificates.store import TableStore
    registry, state = Mock(), Mock()
    registry.get_entity.return_value = Entity(public_key='public', hostname='host', enabled=True, product='waveform')
    state.get_entity.return_value = Entity(PartitionKey='id', RowKey='device', product='tempo')
    store = TableStore(state, registry)
    row = store.get('id')
    assert row['product'] == 'waveform'
    assert 'product' not in store.entity(row)


def test_shared_dns_allows_only_configured_product_hosts():
    from tempo_certificates.dns import Spaceship
    dns = Spaceship('tinyrobotcompany.io', SUFFIX, 'unused', 'unused', waveform_suffix=WAVEFORM)
    assert dns.label('d-' + 'a'*32 + '.' + WAVEFORM).endswith('.waveform')
    with pytest.raises(ValueError):
        dns.label('d-' + 'a'*32 + '.other.tinyrobotcompany.io')


def test_unsigned_requests_are_bounded_before_storage(setup):
    from fastapi.testclient import TestClient
    from tempo_certificates.app import create_app
    _, store, service = setup
    reads = []
    original = store.get
    def get(*args):
        reads.append(args)
        return original(*args)
    store.get = get
    from tempo_certificates.admission import Admission
    client = TestClient(create_app(service, Admission(clock=lambda: 0)))
    payload = dict(device_id='f'*32, action='status', nonce='0'*64, issued_at=NOW)
    statuses = [client.post('/v1/device', json=payload).status_code for _ in range(100)]
    assert 429 in statuses
    assert len(reads) <= 60


def test_admission_recovers_without_retaining_untrusted_identifiers():
    from tempo_certificates.admission import Admission
    clock = [0]
    gate = Admission(clock=lambda: clock[0], capacity=2, rate=1)
    assert gate.accept() and gate.accept() and not gate.accept()
    clock[0] = 1
    assert gate.accept() and not gate.accept()


def test_dns_job_runs_while_certificate_is_waiting():
    import threading
    from tempo_certificates.worker import Worker
    entered, address_done = threading.Event(), threading.Event()
    class Store:
        def candidates(self, now):
            return iter([{'RowKey':'device'}, {'RowKey':'dns'}])
    worker = Worker(Store(), None, None)
    def process(row):
        if row['RowKey']=='device':
            entered.set()
            assert address_done.wait(2), 'Certificate work blocked DNS updates'
        else:
            assert entered.wait(2)
            address_done.set()
    worker.process = process
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = [pool.submit(worker.run_jobs, lane) for lane in ("device", "dns")]
        for job in jobs: job.result()
    assert address_done.is_set()


def test_dns_large_zone_lookup_uses_bounded_sorted_pages():
    from tempo_certificates.dns import Spaceship
    name = 'd-' + 'b'*32 + '.waveform'
    rows = sorted([{'name':f'a{i:06}', 'type':'TXT'} for i in range(11000)] +
                  [{'name':name,'type':'A','address':'192.168.1.2'}] +
                  [{'name':f'z{i:06}', 'type':'TXT'} for i in range(11000)], key=lambda row:row['name'])
    calls=[]
    dns=Spaceship('tinyrobotcompany.io',SUFFIX,'unused','unused',waveform_suffix=WAVEFORM)
    def call(method, **kwargs):
        params=kwargs['params'];calls.append(params)
        assert params['orderBy']=='name'
        return {'total':len(rows), 'items':rows[params['skip']:params['skip']+params['take']]}
    dns.call=call
    assert dns.records(name)==[{'name':name,'type':'A','address':'192.168.1.2'}]
    assert len(calls)<12


@pytest.mark.parametrize('label', ['', 'a000', 'middle', 'z999', 'zzzz'])
def test_dns_sorted_lookup_handles_empty_missing_and_page_boundaries(label):
    from tempo_certificates.dns import Spaceship
    dns = Spaceship('tinyrobotcompany.io', SUFFIX, 'unused', 'unused')
    for rows in ([], [{'name': f'a{i:03}'} for i in range(499)] +
                 [{'name':'middle','type':'TXT'}, {'name':'middle','type':'A'}] +
                 [{'name':f'z{i:03}'} for i in range(500)]):
        def call(method, **kwargs):
            params = kwargs['params']
            return {'total':len(rows), 'items':rows[params['skip']:params['skip']+500]}
        dns.call = call
        assert dns.records(label) == [r for r in rows if r['name'] == label]


def test_dns_zone_size_change_fails_for_retry():
    from tempo_certificates.dns import Spaceship
    dns = Spaceship('tinyrobotcompany.io', SUFFIX, 'unused', 'unused')
    def call(method, **kwargs):
        skip = kwargs['params']['skip']
        return {'total':1000 + bool(skip), 'items':[{'name':f'{i:04}'} for i in range(skip, skip+500)]}
    dns.call = call
    with pytest.raises(RuntimeError, match='dns_zone_changed_retry'):
        dns.records('0900')
