/* ═══════════════════════════════════════════════════════════
   fuel.c – Fuel Subsystem
   Maintains: fuel level
   Consumption logic inside condition variable (engine ON).
   All shared reads/writes protected by mutexes.
   ═══════════════════════════════════════════════════════════ */

#include <unistd.h>
#include "subsystems.h"

#define FUEL_TICK_US         200000
#define FUEL_LOW_THRESHOLD   0.7
#define IDLE_CONSUMPTION     0.0002
#define SPEED_CONSUMPTION    0.000005
#define RPM_CONSUMPTION      0.00000002

void *fuel_thread(void *arg) {
    SystemState *state = (SystemState *)arg;

    while (state->system_running) {

        /* ── Wait for engine ON (condition variable) ── */
        pthread_mutex_lock(&state->sync.engine_lock);
        while (!state->engine.engine_on && state->system_running) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 200000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&state->sync.engine_on_cond,
                                   &state->sync.engine_lock, &ts);
        }
        pthread_mutex_unlock(&state->sync.engine_lock);

        if (!state->system_running) break;

        /* ── Read speed (motion lock) ── */
        pthread_mutex_lock(&state->sync.motion_lock);
        double speed = state->motion.speed_mph;
        pthread_mutex_unlock(&state->sync.motion_lock);

        /* ── Read RPM (engine lock) ── */
        pthread_mutex_lock(&state->sync.engine_lock);
        int rpm = state->engine.rpm;
        pthread_mutex_unlock(&state->sync.engine_lock);

        /* ── CRITICAL SECTION: update fuel ── */
        double consumption = IDLE_CONSUMPTION
                           + speed * SPEED_CONSUMPTION
                           + rpm   * RPM_CONSUMPTION;

        pthread_mutex_lock(&state->sync.fuel_lock);
        state->fuel.fuel_gallons -= consumption;
        if (state->fuel.fuel_gallons < 0.0)
            state->fuel.fuel_gallons = 0.0;
        state->fuel.low_fuel = (state->fuel.fuel_gallons < FUEL_LOW_THRESHOLD);
        pthread_mutex_unlock(&state->sync.fuel_lock);

        /* Signal ECU that fuel state has changed */
        pthread_cond_signal(&state->sync.state_changed_cond);

        usleep(FUEL_TICK_US);
    }
    return NULL;
}
