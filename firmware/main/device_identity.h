#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* All private-key operations execute on the ESP. Only public identity leaves it. */
esp_err_t station_identity_init(void);
const char *station_identity_id(void);
const char *station_identity_hostname(void);
esp_err_t station_identity_csr(char *output, size_t capacity);
esp_err_t station_identity_sign(const char *body, char *signature, size_t capacity);
esp_err_t station_identity_install(const char *chain, int64_t *renew_after);
bool station_identity_changed(void);
int64_t station_identity_renew_after(void);
