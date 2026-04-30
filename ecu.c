/* ═══════════════════════════════════════════════════════════
   ecu.c – Electronic Control Unit

   SYNCHRONIZATION: The ECU waits on state_changed_cond, which
   is signaled by Engine, Motion, and Fuel threads after they
   update shared state. This avoids busy-polling. The ECU
   reacts purely to conditions happening in other subsystems.

   ECU RULES ENFORCED:
     Rule 1 (Engine OFF):  RPM=0, speed decelerates, distance stops
     Rule 2 (Overheat):    RPM capped to 8000, speed to 80 MPH
     Rule 3 (Low Fuel):    Speed capped to 40 MPH
     Rule 4 (Idle):        RPM stays in idle range when speed=0
   ═══════════════════════════════════════════════════════════ */

#include <unistd.h>
#include "subsystems.h"

/* RPM zone thresholds */
#define RPM_IDLE_HIGH        1300
#define RPM_NORMAL_HIGH      8000
#define RPM_HIGH_HIGH        14500

/* Temperature zone thresholds (Celsius) */
#define TEMP_COLD_MAX        60.0
#define TEMP_NORMAL_MAX      95.0
#define TEMP_HOT_MAX         105.0

/* ECU rule limits */
#define OVERHEAT_RPM_LIMIT   8000
#define OVERHEAT_SPEED_LIMIT 80.0
#define LOW_FUEL_SPEED_LIMIT 40.0
#define NORMAL_RPM_LIMIT     16500
#define NORMAL_SPEED_LIMIT   200.0
#define IDLE_RPM_HIGH_LIMIT  1300

void *ecu_thread(void *arg) {
    SystemState *state = (SystemState *)arg;

    while (state->system_running) {

        /* ── WAIT for subsystem state changes (condition variable) ──
           The ECU does not poll. It blocks here until Engine, Motion,
           or Fuel signals that state has changed. A timed wait is used
           so the thread can still check system_running for shutdown. */
        pthread_mutex_lock(&state->sync.state_changed_lock);
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 200000000; /* 200ms timeout for shutdown check */
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&state->sync.state_changed_cond,
                               &state->sync.state_changed_lock, &ts);
        pthread_mutex_unlock(&state->sync.state_changed_lock);

        if (!state->system_running) break;

        /* ── CRITICAL SECTION: read engine state ── */
        pthread_mutex_lock(&state->sync.engine_lock);
        int    on   = state->engine.engine_on;
        int    rpm  = state->engine.rpm;
        double temp = state->engine.temperature_c;
        pthread_mutex_unlock(&state->sync.engine_lock);

        /* ── CRITICAL SECTION: read motion state ── */
        pthread_mutex_lock(&state->sync.motion_lock);
        double speed = state->motion.speed_mph;
        pthread_mutex_unlock(&state->sync.motion_lock);

        /* ── CRITICAL SECTION: read fuel state ── */
        pthread_mutex_lock(&state->sync.fuel_lock);
        int low_fuel = state->fuel.low_fuel;
        pthread_mutex_unlock(&state->sync.fuel_lock);

        /* ── Classify RPM zone (derived from engine RPM) ── */
        RPMZone rz;
        if (!on || rpm == 0)            rz = RPM_ZONE_OFF;
        else if (rpm < RPM_IDLE_HIGH)   rz = RPM_ZONE_IDLE;
        else if (rpm < RPM_NORMAL_HIGH) rz = RPM_ZONE_NORMAL;
        else if (rpm < RPM_HIGH_HIGH)   rz = RPM_ZONE_HIGH;
        else                            rz = RPM_ZONE_REDLINE;

        /* ── Classify temperature zone (derived from engine temp) ── */
        TempZone tz;
        if (temp < TEMP_COLD_MAX)        tz = TEMP_COLD;
        else if (temp <= TEMP_NORMAL_MAX) tz = TEMP_NORMAL;
        else if (temp <= TEMP_HOT_MAX)   tz = TEMP_HOT;
        else                             tz = TEMP_OVERHEAT;

        /* ── Apply system rules and determine system mode ──
           These limits are written to ECU state and actively read
           by Engine (rpm_limit) and Motion (speed_limit) threads
           to constrain their behavior each tick. */
        SystemMode mode;
        int    rpm_limit    = NORMAL_RPM_LIMIT;
        double spd_limit    = NORMAL_SPEED_LIMIT;
        int    overheat     = 0;
        int    low_fuel_act = 0;

        if (!on) {
            /* RULE 1 – Engine OFF: RPM forced to 0 by engine thread,
               speed decelerates gradually in motion thread. */
            mode = SYS_ENGINE_OFF;
        } else if (tz == TEMP_OVERHEAT) {
            /* RULE 2 – Overheat protection: cap RPM and speed to
               reduce engine load and prevent damage. */
            mode      = SYS_CRITICAL;
            rpm_limit = OVERHEAT_RPM_LIMIT;
            spd_limit = OVERHEAT_SPEED_LIMIT;
            overheat  = 1;
        } else if (low_fuel) {
            /* RULE 3 – Low fuel: restrict max speed to conserve fuel. */
            mode         = SYS_CRITICAL;
            spd_limit    = LOW_FUEL_SPEED_LIMIT;
            low_fuel_act = 1;
        } else if (speed == 0 && on) {
            /* RULE 4 – Idle enforcement: when stationary, RPM must
               remain within idle range (1100-1300). */
            mode      = SYS_IDLE;
            rpm_limit = IDLE_RPM_HIGH_LIMIT;
        } else if (rz == RPM_ZONE_HIGH || rz == RPM_ZONE_REDLINE) {
            mode = SYS_HIGH_LOAD;
        } else {
            mode = SYS_NORMAL;
        }

        /* ── CRITICAL SECTION: write ECU decisions ──
           Engine thread reads rpm_limit, Motion reads speed_limit.
           These values actively modify system behavior. */
        pthread_mutex_lock(&state->sync.ecu_lock);
        state->ecu.rpm_zone        = rz;
        state->ecu.temp_zone       = tz;
        state->ecu.system_mode     = mode;
        state->ecu.rpm_limit       = rpm_limit;
        state->ecu.speed_limit     = spd_limit;
        state->ecu.overheat_active = overheat;
        state->ecu.low_fuel_active = low_fuel_act;
        pthread_mutex_unlock(&state->sync.ecu_lock);
    }
    return NULL;
}
