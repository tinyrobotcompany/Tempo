#pragma once
/* Contract between the display and the phone server for Local mode
 * (unencrypted phone access on port 80). Only the display may send a request,
 * and only after its own confirmation screen. No phone, cloud or OTA path can. */
#define STATION_LOCAL_QUERY 0
#define STATION_LOCAL_REQUEST_ON 1
#define STATION_LOCAL_REQUEST_OFF 2
/* Status flags returned for every request. */
#define STATION_LOCAL_ON 1u          /* Saved on; port 80 open or opening. */
#define STATION_LOCAL_BUSY 2u        /* A request or token replacement is in progress. */
#define STATION_LOCAL_FAILED 4u      /* The last request could not be saved. */
#define STATION_LOCAL_OFFER_NO_HTTPS 8u   /* No usable identity/certificate: HTTPS is not running. */
#define STATION_LOCAL_OFFER_RENEWAL 16u   /* Certificate renewal overdue by 7 days or more. */
#define STATION_LOCAL_HTTPS_READY 32u     /* The HTTPS page is reachable with a valid certificate. */
#define STATION_LOCAL_OFFER_EXPIRED 64u   /* The installed certificate has expired. */
