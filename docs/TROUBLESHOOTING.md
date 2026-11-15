# Troubleshooting Tempo

## Capture evidence

Use the development guide's serial monitor/capture procedure. Keep the firmware ELF corresponding to the running image so panic addresses can be decoded. Record the exact app version, action that triggered the problem, approximate time and whether the display recovered. A boot log alone cannot prove sustained stability.

Capture with the monitor (substitute the actual serial port if it changes):

```sh
mkdir -p build-host
./scripts/idf.sh -p /dev/cu.usbmodem5C940599581 monitor 2>&1 | tee build-host/device-runtime-current.log
```

Exit the monitor with Ctrl+]. This command monitors only; it does not flash. The separate `scripts/watch-device.py` builds/flashes automatically and does not capture runtime serial logs. Stop it before monitoring so it does not contend for the port. Do not run two monitors against the same port. Raw captures can include SDK network information; keep them private and inspect before sharing.

Summarize an existing capture without changing the device:

```sh
python3 scripts/summarize-log.py build-host/device-runtime-current.log
```

The summary contains only recognised version/numeric health fields, failure categories and timings. It does not copy raw messages, passwords, tokens, request URLs, IP addresses or coordinates. Empty/missing observations are not interpreted as success. Keep the private original if stack traces or detailed event ordering are needed.

## Available runtime evidence

| Area | Log evidence | Interpretation |
| --- | --- | --- |
| Startup/reset | ESP boot reason, app version, explicit reset reason, panic/assert/backtrace | Distinguish a software restart from power-on or panic; do not assume a blue display means an update. |
| Display/health | Heartbeat, submitted-frame counter, uptime, internal free/largest/minimum memory, explicit UI watchdog restart | Compare counters and memory over time; include the period before the failure. |
| Wi-Fi/time | Scan start/result/failure, disconnect reason, acquired IP event, time synchronisation and operation failures | Distinguish discovery, connection, DHCP and time-sync stages. Never add passwords to these logs. |
| Weather | HTTP status, body byte count, outcome, reason (`ok`, `transport`, `http`, `parse`, `overflow`), elapsed milliseconds and location generation | Separate provider/connection errors from parsing and response limits. Generation is an opaque counter, not coordinates. |
| Transport | HTTP status, bytes, completion/oversize flags, duration, discovery counts, parsed rows and accepted/superseded publications | Distinguish slow HTTP from a stale result rejected after changing location or selection. |
| Map | Tile decode/cancel/failure, byte count and duration | Distinguish downloading/decoding from viewport cancellation. Tile identifiers can reveal map area; keep raw logs private. |
| Settings | Save duration and failure result | Confirm persistence and locate delayed or failed saves. |
| Phone | Setup availability transitions and server startup | Diagnose missing QR prerequisites. Browser reconnect/status messages provide client-side progress. |

The new weather reason/timing fields require building/flashing the updated firmware; older captures will not contain them. Logs are not a permanent on-device support archive. Persistent ring-buffer export and OTA diagnostics can be designed later without claiming they exist now.

## CI failures

Open the failing job and download `host-validation-<commit>` for the test log and coverage reports. A coverage failure names the file below 80%; missing instrumentation also fails. Fix tests or behaviour rather than skipping the gate. For hardware-only failures, use private serial captures and the matching ELF; a green host CI result does not replace hardware testing.

## Display dark at night

Touch once to wake, release, then touch a control. Check Settings → Power & night mode and the saved timezone. Wake lasts 60 seconds from the last touch. Look for `power: Backlight` logs; UI heartbeat should continue while dark. A dark backlight is not a reboot. After total power loss with no Wi-Fi, the clock waits for synchronisation; reconnect without resetting saved settings. See [power behaviour](POWER_MANAGEMENT.md).
