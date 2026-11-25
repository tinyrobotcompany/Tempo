# Static HTTPS location helper

Deploy `index.html`, `helper.js`, `location.js` and `style.css` together at a trusted HTTPS URL. No build step, account service or database is needed. Do not add analytics or injected scripts. The return destination, pairing token and coordinates are URL fragments; they must not be moved into query parameters. Use top-level navigation from the local display panel.

`location.js` and `style.css` are shared copies of the firmware web assets. Keep them aligned; `scripts/test-phone.sh` verifies the location logic copy. Automatic location requires the user's browser permission. Address lookup uses IGN Geoplateforme; map tiles use OpenStreetMap with visible attribution and normal browser caching.

This helper is **not deployed**. Once the final URL exists, set `CONFIG_STATION_LOCATION_HELPER_URL` through ESP-IDF menuconfig → Weather station and rebuild. See [deployment and acceptance notes](../../docs/IPHONE_SETUP.md).
