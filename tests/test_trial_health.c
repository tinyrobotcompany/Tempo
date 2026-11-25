#include <assert.h>
#include "trial_health.h"

static unsigned calls;
static bool durable;
static bool validate(void){calls++;return durable;}

int main(void)
{
    assert(station_trial_health(false,false,0,180000000,validate)==STATION_TRIAL_WAIT);
    assert(station_trial_health(true,false,2000,5000000,validate)==STATION_TRIAL_ROLLBACK);
    assert(!calls);
    durable=true;
    assert(station_trial_health(true,true,2000,119999999,validate)==STATION_TRIAL_WAIT);
    assert(station_trial_health(true,true,99,120000000,validate)==STATION_TRIAL_WAIT);
    assert(!calls);
    assert(station_trial_health(true,true,100,120000000,validate)==STATION_TRIAL_ACCEPT);
    assert(calls==1);
    durable=false;
    assert(station_trial_health(true,true,2000,120000000,validate)==STATION_TRIAL_WAIT);
    assert(calls==2);
    assert(station_trial_health(true,true,2000,180000000,validate)==STATION_TRIAL_ROLLBACK);
    assert(calls==2); /* Deadline cannot accept an overdue trial. */
    /* A healthy updater retries durable acceptance within the trial window. */
    durable=true;
    assert(station_trial_health(true,true,2000,125000000,validate)==STATION_TRIAL_ACCEPT);
    assert(station_trial_health(true,true,0,180000000,validate)==STATION_TRIAL_ROLLBACK);
}
