#pragma once
#include "esp_https_server.h"
/* Device-generated key and cloud-issued certificate, stored in the device partition. */
esp_err_t station_phone_tls_start(httpd_handle_t *server, const httpd_config_t *config);
esp_err_t station_phone_tls_stop(httpd_handle_t server);
const char *station_phone_tls_hostname(void);
