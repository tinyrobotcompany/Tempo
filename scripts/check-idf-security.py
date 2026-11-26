#!/usr/bin/env python3
"""Reject stale or modified HTTP server code before firmware/TLS validation."""
import hashlib
from pathlib import Path
import subprocess
import sys

COMMIT = '051e4c4eb20346e043cdd47bc388fde75ff8af63'
PARSER = 'components/esp_http_server/src/httpd_parse.c'
PARSER_SHA256 = '2121bc06c00656fd2ad85b1a8a7eb3fe5d6010ccf3756b57d5520c3b8286df3e'


def check(sdk):
    commit = subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip()
    if commit != COMMIT:
        raise ValueError('Use the pinned ESP-IDF 6.1.1 checkout; the previous 6.1 parser is affected by Content-Length truncation')
    if hashlib.sha256((sdk / PARSER).read_bytes()).hexdigest() != PARSER_SHA256:
        raise ValueError('HTTP parser differs from the verified upstream security fix')


if __name__ == '__main__':
    check(Path(sys.argv[1]))
    print('PASS: fixed ESP-IDF 6.1.1 commit and HTTP parser verified')
