#pragma once
#include "esp_https_server.h"
/* Identity is provisioned over USB into a dedicated partition, never in firmware. */
esp_err_t station_phone_tls_start(httpd_handle_t *server, const httpd_config_t *config);
void station_phone_tls_stop(httpd_handle_t server);
const char *station_phone_tls_hostname(void);
