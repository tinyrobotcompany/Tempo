# Update experience work list

Branch: feature/update-experience, based on main 2b80e87.

- [x] 1. Synchronise live update state on device and phone; distinguish installed and available versions.
- [x] 2. Preserve pairing across refresh and firmware restarts; keep authentication mandatory.
- [x] 3. Add explicit Install now and make automatic countdown/postponement obvious.
- [x] 4. Show clear checking, available, no-new-updates, progress and error states without needless redraws.
- [x] 5. Prevent visible flash-write flicker; show calm preparation, guard backlight before flash writes, and restore on failure.
- [x] 6. Run regression tests, coverage gates, firmware build and visual inspection; record physical checks still needed.
- [ ] 7. Return to the user with a separate startup / first-time setup / shutdown proposal. This is explicitly deferred by agreement, not forgotten or authorised for implementation.

## Evidence and scope

The phone already polls /api/status, but the ESP generates a new pairing token on every restart, invalidating paired clients after OTA. Browser pairing uses only sessionStorage, and an unavailable storage API can abort page initialisation. Both views show installed and available versions without a clear primary state. The manual install command always starts a 60-second countdown. The device update labels are rewritten on each poll.

The ESP-IDF DPI driver identifies blue output as a possible external-memory underrun. The user's observation is consistent with that path during flash writes; device fault logs have not yet established the exact bandwidth cause. Visual safety must not depend on an unverified display-driver tuning change: confirm backlight-off before any firmware erase/write, keep it off through activation, and restore after failures. Present progress on the paired phone and a preparation screen on Tempo before blanking.

No merges, release publication, or edits to other agents' worktrees.

## Implementation and verification

Pairing is a random 128-bit bearer credential persisted in `tempo_pair`, separate from API keys and appearance settings. Failed reads/writes/commits fail closed. The browser remembers it per device origin; if browser storage is blocked, the URL fragment is retained so a refresh still works. The API still requires authentication. An upgrade from old firmware requires one initial QR scan because its old token was never persisted. Pairing survives subsequent reboots; changing the device IP/origin or clearing/resetting pairing still requires scanning its current QR. Storage is not encrypted, and local HTTP still requires a trusted LAN.

Both interfaces use the same worker snapshot. Normal polling is every three seconds, active update/countdown polling every second, with existing bounded retry after disconnect. Explicit installed and available labels eliminate the ambiguity between running software and a release offered for installation. `upToDate` is true only after a successful no-new-release check in this boot, never inferred from an empty available field or old persisted timestamp.

`install_now` bypasses the 60-second scheduling delay but retains preparation, signature/size/hash verification, inactive-slot writes and trial health/rollback. Automatic installs still use the countdown. Install and postpone controls are disabled during checks/downloads/activation. A large countdown appears on the update page; other views receive an overlay. The preparation screen has no spinner or flashing animation. During flash writes the device is deliberately dark; progress remains on the phone. Failed installs restore the display, with worker retries if the display lock or brightness driver initially refuses restoration.

Host tests cover real phone handlers, browser refresh/storage denial, OTA reconnect/version changes, countdown/live progress, immediate install, pairing persistence/errors, flash guards and display recovery. The full host suite and coverage gates passed during implementation. The ESP-IDF build passed; final commit validation reruns the gates against the staged snapshot.

## Physical acceptance still required

1. Scan the new pairing QR once, refresh Safari, close/reopen the same device URL, restart Tempo, and confirm automatic reconnection with the same paired origin.
2. Check on one interface and observe both: Checking, Available, No new updates, and errors. Confirm installed and available labels agree; no manual phone refresh.
3. Exercise an automatic countdown from the clock and update page. Verify Install now and Postpone; disable automatic installs again if used only for testing.
4. Install a newer signed build. Confirm the calm preparation screen, fully dark panel throughout writes, live phone progress and reconnection after reboot. Observe failure recovery; host mocks do not establish physical luminance or eliminate all possible hardware faults.
5. Confirm unsaved phone appearance/location edits survive transient reconnection. Verify idle dim/off and touch wake outside installation.

## Deferred design

[Startup, setup and shutdown proposal](STARTUP_SHUTDOWN_PROPOSAL.md) is prepared separately for the user's approval. It is not implemented by this branch.
