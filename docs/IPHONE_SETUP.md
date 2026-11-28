# Phone setup and location

Tempo serves its control page directly over HTTPS on the home network. The certificate service manages device enrollment, certificates and DNS; it does not proxy phone controls. See [secure phone access](SECURE_PHONE_ACCESS.md) for provisioning and acceptance requirements.

1. Connect Tempo to Wi-Fi using its touchscreen.
2. Open phone setup on Tempo and scan the current QR code from a phone on the same network. Wait for secure setup if enrollment or DNS reconciliation is pending.
3. Change appearance or views. Wait for the saved confirmation before closing the page.
4. Choose **Use my phone location** and grant the browser location permission. Check the map pin and choose **Confirm location** to save it.
5. If positioning is unavailable or permission is denied, search for an address, adjust the map and confirm. This is a complete alternative on iPhone and Android; it needs no installed app or Shortcut.

Location remains saved across restarts and network changes. Changing Wi-Fi does not move the saved location. Address lookup targets France and sends the requested address/coordinates to IGN; map loading sends the viewed area to OpenStreetMap. The page discloses these external services.

The obsolete Apple Shortcut callback, signed installer and hosted HTTP helper have been retired. Old callback URLs are not interpreted as location commands. There is no automatic HTTP fallback and no instruction to bypass a certificate warning. The only plain-HTTP path is user-chosen Local mode, below. Previously installed Shortcuts can be removed from the phone.

The current pairing credential is scoped to the device origin and persists in browser storage. Revoking phones on the display invalidates it. This remains a shared bearer, not individual per-phone sessions; see the security documentation for that limitation. Static assets contain no device settings, and API writes require pairing and bounded, validated JSON.

The panel reconnects automatically with capped backoff, pauses background polling when hidden and retains unsaved edits during connection loss. Writes are not replayed automatically: a lost reply may mean the device already saved. A pairing error requires the current QR. DNS/router reachability and browser certificate trust remain necessary for HTTPS access; Local mode needs neither.

For an iPhone hotspot, enable the hotspot and connect Tempo using its touchscreen. The hotspot-hosting phone's ability to reach another client must be tested; internet access alone does not establish local reachability. No Bluetooth or SoftAP workaround is provided. If the hotspot or router blocks the HTTPS name, Local mode can be chosen on the display.

## Local mode (not encrypted)

If the phone cannot open the HTTPS page, tap **Can't open the page?** under the QR code, or **Local mode (not encrypted)** in Phone setup. The display explains that traffic on your Wi-Fi is not encrypted and asks for confirmation. Nothing else can turn it on: not the phone page, the API, the cloud service or an update.

While Local mode is on, the QR code opens `http://<display address>/` with the pairing code, the page shows a permanent "Local mode – not encrypted" banner, and the transport-key form shows an extra warning. Browser location permission needs HTTPS, so on the plain-HTTP page use the address search to set your location. Turn Local mode off on the display (it asks for confirmation); the pairing code is then replaced, so scan the new QR code on each phone.

## Verification

`scripts/test-phone.sh` exercises embedded assets, authenticated handlers, native browser positioning, denied/unavailable positioning, explicit confirmation and reconnection. The full suite retains browser coverage requirements. Device acceptance must additionally cover Safari and other supported browsers, denied/retried permissions, reboot persistence, home Wi-Fi/hotspot changes, DNS rebinding protections and expired/replaced certificates. Passing host tests does not establish those hardware/network outcomes.
