/* ═══════════════════════════════════════════════════════════
   motion.c – Motion Subsystem (Phase 2)
   Maintains: speed, total distance, trip distance.
   Uses condition variable to wait for engine ON.
   Respects ECU speed limits. Gradual decel when engine OFF.
   ═══════════════════════════════════════════════════════════ */

#include <unistd.h>
#include "subsystems.h"

#define MOTION_TICK_US       200000
#define MOTION_TICK_SEC      0.2
#define ACCEL_STEP           1.0    /* MPH gained per tick when accelerating */
#define DECEL_STEP           0.5    /* MPH lost per tick when decelerating   */
#define ENGINE_OFF_DECEL     2.0    /* MPH lost per tick when engine is OFF  */
#define SECONDS_PER_HOUR     3600.0

void *motion_thread(void *arg) {
    SystemState *state = (SystemState *)arg;

    while (state->system_running) {

        /* ── Wait for engine to be ON (condition variable) ── */
        pthread_mutex_lock(&state->sync.engine_lock);
        while (!state->engine.engine_on && state->system_running) {

            /* Engine OFF: gradually reduce speed toward 0 */
            pthread_mutex_unlock(&state->sync.engine_lock);

            pthread_mutex_lock(&state->sync.motion_lock);
            if (state->motion.speed_mph > 0) {
                state->motion.speed_mph -= ENGINE_OFF_DECEL;
                if (state->motion.speed_mph < 0)
                    state->motion.speed_mph = 0;

                /* Still moving: accumulate distance */
                if (state->motion.speed_mph > 0) {
                    double delta = state->motion.speed_mph * (MOTION_TICK_SEC / SECONDS_PER_HOUR);
                    state->motion.total_distance += delta;
                    state->motion.trip_distance  += delta;
                }
            }
            pthread_mutex_unlock(&state->sync.motion_lock);

            usleep(MOTION_TICK_US);

            pthread_mutex_lock(&state->sync.engine_lock);
            /* If still OFF, wait on condition variable to avoid busy-wait */
            if (!state->engine.engine_on && state->system_running) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 200000000; /* 200ms timeout */
                if (ts.tv_nsec >= 1000000000) {
                    ts.tv_sec += 1;
                    ts.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&state->sync.engine_on_cond,
                                       &state->sync.engine_lock, &ts);
            }
        }
        pthread_mutex_unlock(&state->sync.engine_lock);

        if (!state->system_running) break;

        /* ── Read ECU speed limit ── */
        pthread_mutex_lock(&state->sync.ecu_lock);
        double speed_cap = state->ecu.speed_limit;
        pthread_mutex_unlock(&state->sync.ecu_lock);

        /* ── Read accelerating flag ── */
        pthread_mutex_lock(&state->sync.motion_lock);
        int accel = state->motion.accelerating;
        pthread_mutex_unlock(&state->sync.motion_lock);

        /* ── CRITICAL SECTION: update speed and distance ── */
        pthread_mutex_lock(&state->sync.motion_lock);

        if (accel) {
            state->motion.speed_mph += ACCEL_STEP;
        } else {
            state->motion.speed_mph -= DECEL_STEP;
        }

        /* Clamp to [0, speed_cap] */
        if (state->motion.speed_mph > speed_cap)
            state->motion.speed_mph = speed_cap;
        if (state->motion.speed_mph < 0)
            state->motion.speed_mph = 0;

        /* Reverse direction at bounds */
        if (state->motion.speed_mph >= speed_cap)
            state->motion.accelerating = 0;
        else if (state->motion.speed_mph <= 0)
            state->motion.accelerating = 1;

        /* Accumulate distance */
        if (state->motion.speed_mph > 0) {
            double delta = state->motion.speed_mph * (MOTION_TICK_SEC / SECONDS_PER_HOUR);
            state->motion.total_distance += delta;
            state->motion.trip_distance  += delta;
        }

        pthread_mutex_unlock(&state->sync.motion_lock);

        usleep(MOTION_TICK_US);
    }
    return NULL;
}
