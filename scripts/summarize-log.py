#!/usr/bin/env python3
"""Summarize a serial capture without copying raw lines, credentials or locations."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re


def summarize(text):
    text = re.sub(r'\x1b\[[0-9;]*m', '', text)
    def counts(pattern):
        return dict(Counter(re.findall(pattern, text)))
    health = re.findall(r'Display health: heartbeat=(\d+), submitted_frames=(\d+), uptime=(\d+) s', text)
    memory = re.findall(r'Internal memory: free=(\d+), largest=(\d+), minimum=(\d+)', text)
    weather = re.findall(r'weather: Fetch complete: HTTP (\d+), (\d+) bytes, [^\r\n]*?reason=(ok|http|parse|overflow|transport), elapsed=(\d+) ms', text)
    transport = re.findall(r'transport: [^\r\n]*?HTTP (\d+), (\d+) bytes, complete=([01]), oversized=([01]), elapsed=(\d+) ms', text)
    return {
        'versions': sorted(set(re.findall(r'App version:\s*(\d+\.\d+\.\d+)', text))),
        'reset_reasons_numeric': counts(r'Reset reason: (\d+)'),
        'ui_watchdog_restarts': text.count('UI stopped responding; restart'),
        'panic_markers': len(re.findall(r'Guru Meditation Error|Stack smashing protect failure|assert failed:', text)),
        'wifi_disconnect_reasons': counts(r'Wi-Fi disconnected: reason=(\d+)'),
        'wifi_scan_starts': text.count('Wi-Fi scan: starting'),
        'wifi_scans_completed': len(re.findall(r'Wi-Fi scan: received \d+ access points', text)),
        'time_syncs': text.count('Time synchronised'),
        'last_display_health': dict(zip(('heartbeat','submitted_frames','uptime_seconds'),map(int,health[-1]))) if health else None,
        'last_internal_memory': dict(zip(('free','largest','minimum'),map(int,memory[-1]))) if memory else None,
        'weather_requests': [dict(http=int(h),bytes=int(b),reason=r,elapsed_ms=int(t)) for h,b,r,t in weather],
        'transport_requests': [dict(http=int(h),bytes=int(b),complete=bool(int(c)),oversized=bool(int(o)),elapsed_ms=int(t)) for h,b,c,o,t in transport],
        'limitations': 'Only observed log events are reported. Missing events do not prove health. Keep the private original capture for backtrace decoding.'
    }


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    args=parser.parse_args()
    print(json.dumps(summarize(args.capture.read_text(errors='replace')),indent=2))
