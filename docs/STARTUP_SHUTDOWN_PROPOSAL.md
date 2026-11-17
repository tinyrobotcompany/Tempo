# Startup, first-time setup and shutdown — proposal for approval

This work is deliberately deferred until the update/phone work is reviewed. No onboarding or shutdown behaviour is implemented by this PR.

## First startup

Show a calm Tempo welcome screen and a resumable setup checklist, not an empty dashboard. Start with Wi-Fi on the display (the phone cannot reach Tempo before it joins a network). Once connected, offer a QR code for finishing setup on the phone, alongside Continue on Tempo.

Steps: Wi-Fi; home/location and time zone; household/name and appearance; optional transport API access; display sleep preferences; review and finish. Explain why location is needed, show which items are complete, and allow optional transport/profile choices to be skipped. Never show or prefill an existing API key. Save each completed step and resume after a power cut. Make setup accessible later from Settings. Starting weather/transport should depend on their prerequisites, not completion of optional onboarding.

## Everyday startup

Keep the backlight off until the first stable screen is ready, then reveal a calm welcome/status screen. Load saved preferences immediately. Show Connecting, Setting time and Refreshing information independently; avoid a spinner that implies everything has failed because one provider is unavailable. Continue to the usual view when locally ready, allowing offline operation. Do not re-run onboarding after OTA.

## Sleep and wake

A clearly named Sleep display action blanks the screen until touched. Wi-Fi, clock and services continue. First touch wakes without activating hidden controls. This is display sleep, not physical power off.

## Prepare to unplug

Offer this separately from Sleep. The design must coordinate with update installation and pending saves. Acknowledgement must come from the actual workers: prevent new installs, finish committed saves and reject the request while activation/flash work is unsafe to interrupt. Only after those acknowledgements may Tempo say Ready to unplug. That state must stay quiet until wake/restart; do not keep background writes running behind the promise.

The present board has no established software-controlled mains disconnect. Final power-off therefore uses the physical switch or unplugging; do not label display sleep as a complete shutdown. No battery or last-second save guarantee is implied.

## Acceptance before implementation is considered complete

Fresh device; interrupted setup at each step; skipped transport access; setup over phone; offline restart; failed credential/location save; OTA restart without onboarding; sleep/wake gesture; and shutdown requested during settings save, download, activation and normal operation. Verify all messages on the real panel and iPhone.
