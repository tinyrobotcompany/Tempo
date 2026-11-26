#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { STATION_TRIAL_WAIT, STATION_TRIAL_ACCEPT, STATION_TRIAL_ROLLBACK } station_trial_result_t;

/* Only local recovery-critical health gates acceptance. Enrollment, phone TLS,
 * Wi-Fi availability and external providers must not prevent a repair update.
 * validate() confirms that the updater has durably accepted this trial. */
static inline station_trial_result_t station_trial_health(bool pending, bool updater_ready,
    unsigned heartbeat, int64_t elapsed_us, bool (*validate)(void))
{
    if(!pending)return STATION_TRIAL_WAIT;
    if(!updater_ready||elapsed_us>=180000000)return STATION_TRIAL_ROLLBACK;
    if(heartbeat>=100&&elapsed_us>=120000000&&validate())return STATION_TRIAL_ACCEPT;
    return STATION_TRIAL_WAIT;
}
