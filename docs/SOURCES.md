# Source register and validation notes

Hardware and OTA sources updated: 2 November 2026. Transport/weather research: 30 October 2026. Sources document component capabilities and provider contracts; the proposed system still requires bench and authenticated API validation.

| Topic | Primary source | What it supports |
| --- | --- | --- |
| RATP starting point | [Explore API console](https://data.ratp.fr/api-console/explore/v2.1/) | Original user-supplied entry point. It does not itself establish live regional departure coverage. |
| Regional departures | [PRIM per-stop service](https://prim.iledefrance-mobilites.fr/fr/apis/idfm-ivtr-requete_unitaire) | SIRI Lite departures, documented stop-monitoring path, partial coverage and account-dependent quota. Published historical quota categories do not settle a new account's allowance. |
| Stop identifiers | [PRIM request parameters](https://prim.iledefrance-mobilites.fr/aide-et-contact/documentation/prise-en-main-des-api/prise-en-main-des-api-prochains-passages/structure-des-requetes-parametres-dappel) | Stop/platform/line identifiers and station grouping considerations. |
| Transport disruptions | [PRIM traffic messages](https://prim.iledefrance-mobilites.fr/fr/apis/idfm-disruptions_bulk) | Indexed official description of disruptions linked to stops and lines. Full page timed out; authenticated contract validation remains necessary. |
| Stop and network references | [Official transport dataset](https://transport.data.gouv.fr/datasets/reseau-urbain-et-interurbain-dile-de-france-mobilites) | Regional datasets and associated transport resources. |
| Weather fields | [Open-Meteo documentation](https://open-meteo.com/en/docs) | Coordinate-based weather variables and forecasts. |
| Weather service terms | [Open-Meteo pricing](https://open-meteo.com/en/pricing) | Non-commercial free endpoint versus commercial plans and attribution. |
| Integrated P4 display | [Waveshare board documentation](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-X) | P4/C6 architecture, listed memory, 10.1-inch variants and programming access. Exact shipped revision still requires confirmation. |
| Display resolution and examples | [Manufacturer repository](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-X) | 10.1-inch 800 × 1280 panel and board examples. Landscape UI is a project design choice. |
| Purchase family | [Waveshare product family](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7-8-10.1.htm) | Link from manufacturer repository; choose 10.1-inch kit and confirm accessories/power. |
| Embedded OTA | [ESP-IDF P4 OTA guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/system/ota.html) | Application slots, metadata and rollback; bootloader/shared-data updates have different failure characteristics. |
| Browser location | [W3C Geolocation](https://www.w3.org/TR/geolocation/) | Secure-context and permission requirements. |

No API credentials were supplied or used. No electronics have been purchased or tested. The user selected the smaller integrated display for lower cost; no current price or availability is guaranteed. Hardware models and service contracts should be rechecked before procurement and implementation.
