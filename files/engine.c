/* ═══════════════════════════════════════════════════════════
   engine.c – Engine Subsystem (Phase 2)
   Maintains: RPM, engine temperature.
   Respects ECU-imposed RPM limits.
   All shared state access protected by mutexes.
   ═══════════════════════════════════════════════════════════ */

#include <stdlib.h>
#include <unistd.h>
#include "subsystems.h"

#define ENGINE_TICK_US       100000
#define RPM_STEP             50
#define IDLE_RPM_LOW         1100
#define IDLE_RPM_HIGH        1300
#define TEMP_AMBIENT_C       35.0
#define TEMP_RISE_FACTOR     0.02
#define TEMP_COOL_RATE       0.05

void *engine_thread(void *arg) {
    SystemState *state = (SystemState *)arg;
    int direction = 1;
    int target_low  = IDLE_RPM_LOW;
    int target_high = 10000;

    while (state->system_running) {

        /* ── CRITICAL SECTION: read engine_on ── */
        pthread_mutex_lock(&state->sync.engine_lock);
        int on = state->engine.engine_on;
        pthread_mutex_unlock(&state->sync.engine_lock);

        if (on) {
            /* Read ECU rpm limit */
            pthread_mutex_lock(&state->sync.ecu_lock);
            int rpm_cap = state->ecu.rpm_limit;
            pthread_mutex_unlock(&state->sync.ecu_lock);

            /* Clamp target_high to ECU limit */
            int effective_high = (target_high < rpm_cap) ? target_high : rpm_cap;

            /* ── CRITICAL SECTION: update RPM and temperature ── */
            pthread_mutex_lock(&state->sync.engine_lock);

            state->engine.rpm += direction * RPM_STEP;

            if (state->engine.rpm >= effective_high) {
                state->engine.rpm = effective_high;
                direction = -1;
            } else if (state->engine.rpm <= target_low) {
                state->engine.rpm = target_low;
                direction = 1;
            }

            /* Enforce RPM cap from ECU */
            if (state->engine.rpm > rpm_cap)
                state->engine.rpm = rpm_cap;

            /* Temperature: rises with RPM, cools toward ambient */
            double rpm_heat = (state->engine.rpm / 1000.0) * TEMP_RISE_FACTOR;
            double cool = (state->engine.temperature_c - TEMP_AMBIENT_C) * TEMP_COOL_RATE;
            if (cool < 0) cool = 0;
            state->engine.temperature_c += rpm_heat - cool;
            if (state->engine.temperature_c > 120.0)
                state->engine.temperature_c = 120.0;

            pthread_mutex_unlock(&state->sync.engine_lock);

        } else {
            /* Engine OFF: RPM = 0, cool toward ambient */
            pthread_mutex_lock(&state->sync.engine_lock);
            state->engine.rpm = 0;
            double cool = (state->engine.temperature_c - TEMP_AMBIENT_C) * TEMP_COOL_RATE;
            if (cool < 0) cool = 0;
            state->engine.temperature_c -= cool;
            pthread_mutex_unlock(&state->sync.engine_lock);
        }

        usleep(ENGINE_TICK_US);
    }
    return NULL;
}
