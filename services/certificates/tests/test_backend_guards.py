"""Tempo admission, DNS and worker regressions retained from the security review."""
import pytest
from test_service import setup, signed, NOW, SUFFIX
from tempo_certificates.protocol import Rejected


def test_foreign_product_request_is_rejected(setup):
    key, store, service = setup
    with pytest.raises(Rejected, match='invalid_fields'):
        service.handle(*signed(key, store, product='waveform'))
    assert not store.nonces


def test_existing_tempo_device_needs_no_protocol_change(setup):
    key, store, service = setup
    assert service.handle(*signed(key, store)).status == 200


def test_dns_cannot_mutate_another_product_namespace():
    from tempo_certificates.dns import Spaceship
    dns = Spaceship('tinyrobotcompany.io', SUFFIX, 'unused', 'unused')
    with pytest.raises(ValueError, match='outside device namespace'):
        dns.label('d-' + 'a'*32 + '.waveform.tinyrobotcompany.io')


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
    name = 'd-' + 'b'*32 + '.tempo'
    rows = sorted([{'name':f'a{i:06}', 'type':'TXT'} for i in range(11000)] +
                  [{'name':name,'type':'A','address':'192.168.1.2'}] +
                  [{'name':f'z{i:06}', 'type':'TXT'} for i in range(11000)], key=lambda row:row['name'])
    calls=[]
    dns=Spaceship('tinyrobotcompany.io',SUFFIX,'unused','unused')
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
