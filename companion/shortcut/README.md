# Location shortcut — signed installer supplied

`Weather Station Location.shortcut` is the complete native macOS Shortcuts export, using File → Export → Anyone. Firmware serves this exact file from the local Add location shortcut button. Customers do not edit actions or enter network addresses.

`Weather Station Location.source.plist` is the canonical source extracted from that signed export. `manifest.json` pins both artifact and source hashes. Run `python3 tests/test_shortcut_artifact.py` from the project root to verify archive signature integrity, source identity and the output contract: three text lines, `WSLOC1`, latitude, longitude. Latitude and longitude both reference the same Current Location result. There are no network actions, device addresses, pairing tokens or import questions in the workflow.

The earlier unsigned generator remains a reference only. CLI signing rejected its input here; native File → Export → Anyone is the verified packaging path. Future changes must be exported again, reviewed, and the canonical source and manifest updated together. Never rename an unsigned plist into the signed artifact path.

Local artifact and HTTP-download tests pass. Safari installation, location permissions and the full return-to-map flow still require testing on an iPhone, including fresh installation and non-Safari default browser behaviour. See `docs/IPHONE_SETUP.md` for acceptance criteria. No claim of completed device acceptance is made.
