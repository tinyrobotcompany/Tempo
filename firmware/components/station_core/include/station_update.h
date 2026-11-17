#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define TEMPO_UPDATE_TARGET "waveshare-p4-10.1-rev3-32mb"
#define TEMPO_UPDATE_LAYOUT "ab-8mb-v1"
#define TEMPO_UPDATE_BASE "https://github.com/tinyrobotcompany/Tempo/releases/download/"
typedef enum { UPDATE_IDLE, UPDATE_CHECKING, UPDATE_AVAILABLE, UPDATE_DOWNLOADING,
 UPDATE_VERIFYING, UPDATE_RESTARTING, UPDATE_ERROR } station_update_state_t;
typedef struct {
 station_update_state_t state;
 bool automatic, credential_configured;
 unsigned interval_hours, window_start, window_end, progress, countdown_seconds;
 int64_t last_check, next_check;
 char installed[32], available[32], message[160], notes_url[160];
} station_update_status_t;
typedef struct {
 char version[32], sha256[65], url[256];
 unsigned size;
} station_update_manifest_t;
/* Pure policy, shared with host tests. */
bool station_update_version(const char *text, unsigned parts[3]);
bool station_update_newer(const char *candidate,const char *current);
bool station_update_manifest(const char *json,const char *current,station_update_manifest_t *out);
bool station_update_window(unsigned minute,unsigned start,unsigned end);
uint32_t station_update_retry(unsigned failures);
int64_t station_update_next(int64_t now,unsigned hours,unsigned jitter);
