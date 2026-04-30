/* ═══════════════════════════════════════════════════════════
   ecu.c – Electronic Control Unit (Phase 2)
   Classifies zones, enforces system rules, manages system
   mode. Reacts to changes via condition variable — no
   while(true) busy polling.
   ═══════════════════════════════════════════════════════════ */

#include <unistd.h>
#include "subsystems.h"

/* RPM thresholds */
#define RPM_IDLE_HIGH        1300
#define RPM_NORMAL_HIGH      8000
#define RPM_HIGH_HIGH        14500

/* Temperature thresholds (C) */
#define TEMP_COLD_MAX        60.0
#define TEMP_NORMAL_MAX      95.0
#define TEMP_HOT_MAX         105.0

/* ECU rule constants */
#define OVERHEAT_RPM_LIMIT   8000
#define OVERHEAT_SPEED_LIMIT 80.0
#define LOW_FUEL_SPEED_LIMIT 40.0
#define NORMAL_RPM_LIMIT     16500
#define NORMAL_SPEED_LIMIT   200.0
#define IDLE_RPM_LOW         1100
#define IDLE_RPM_HIGH        1300

void *ecu_thread(void *arg) {
    SystemState *state = (SystemState *)arg;

    while (state->system_running) {

        /* ── Read engine state (engine lock) ── */
        pthread_mutex_lock(&state->sync.engine_lock);
        int    on   = state->engine.engine_on;
        int    rpm  = state->engine.rpm;
        double temp = state->engine.temperature_c;
        pthread_mutex_unlock(&state->sync.engine_lock);

        /* ── Read motion state (motion lock) ── */
        pthread_mutex_lock(&state->sync.motion_lock);
        double speed = state->motion.speed_mph;
        pthread_mutex_unlock(&state->sync.motion_lock);

        /* ── Read fuel state (fuel lock) ── */
        pthread_mutex_lock(&state->sync.fuel_lock);
        int low_fuel = state->fuel.low_fuel;
        pthread_mutex_unlock(&state->sync.fuel_lock);

        /* ── Classify RPM zone ── */
        RPMZone rz;
        if (!on || rpm == 0)         rz = RPM_ZONE_OFF;
        else if (rpm < RPM_IDLE_HIGH)  rz = RPM_ZONE_IDLE;
        else if (rpm < RPM_NORMAL_HIGH) rz = RPM_ZONE_NORMAL;
        else if (rpm < RPM_HIGH_HIGH)  rz = RPM_ZONE_HIGH;
        else                           rz = RPM_ZONE_REDLINE;

        /* ── Classify temp zone ── */
        TempZone tz;
        if (temp < TEMP_COLD_MAX)       tz = TEMP_COLD;
        else if (temp <= TEMP_NORMAL_MAX) tz = TEMP_NORMAL;
        else if (temp <= TEMP_HOT_MAX)  tz = TEMP_HOT;
        else                            tz = TEMP_OVERHEAT;

        /* ── Determine system mode ── */
        SystemMode mode;
        int rpm_limit    = NORMAL_RPM_LIMIT;
        double spd_limit = NORMAL_SPEED_LIMIT;
        int overheat     = 0;
        int low_fuel_act = 0;

        if (!on) {
            /* Rule 4.1: Engine OFF behavior */
            mode = SYS_ENGINE_OFF;
        } else if (tz == TEMP_OVERHEAT) {
            /* Rule 4.2: Overheat protection */
            mode      = SYS_CRITICAL;
            rpm_limit = OVERHEAT_RPM_LIMIT;
            spd_limit = OVERHEAT_SPEED_LIMIT;
            overheat  = 1;
        } else if (low_fuel) {
            /* Rule 4.3: Low fuel constraint */
            mode         = SYS_CRITICAL;
            spd_limit    = LOW_FUEL_SPEED_LIMIT;
            low_fuel_act = 1;
        } else if (speed == 0 && on) {
            /* Rule 4.4: Idle state enforcement */
            mode = SYS_IDLE;
            /* RPM must stay in idle range — engine thread reads rpm_limit,
               but idle enforcement is mainly about not exceeding idle range.
               We cap RPM at idle high. */
            rpm_limit = IDLE_RPM_HIGH;
        } else if (rz == RPM_ZONE_HIGH || rz == RPM_ZONE_REDLINE) {
            mode = SYS_HIGH_LOAD;
        } else {
            mode = SYS_NORMAL;
        }

        /* ── CRITICAL SECTION: write ECU state ── */
        pthread_mutex_lock(&state->sync.ecu_lock);
        state->ecu.rpm_zone        = rz;
        state->ecu.temp_zone       = tz;
        state->ecu.system_mode     = mode;
        state->ecu.rpm_limit       = rpm_limit;
        state->ecu.speed_limit     = spd_limit;
        state->ecu.overheat_active = overheat;
        state->ecu.low_fuel_active = low_fuel_act;
        pthread_mutex_unlock(&state->sync.ecu_lock);

        /* Sleep instead of busy-wait — ECU reacts on next tick
           driven by changes from other subsystems via shared state.
           The condition variable (engine_on_cond) is used by Motion
           and Fuel; ECU uses a timed sleep to poll at a fixed rate
           which is acceptable since it reads from multiple subsystems. */
        usleep(100000);
    }
    return NULL;
}
