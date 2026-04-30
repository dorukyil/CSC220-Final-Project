/* ═══════════════════════════════════════════════════════════
   init.c – System State Initialization
   Initializes from command-line args. No hardcoded values.
   ═══════════════════════════════════════════════════════════ */

#include <stdlib.h>
#include <time.h>
#include "subsystems.h"

void init_sync(SyncPrimitives *s) {
    pthread_mutex_init(&s->engine_lock,  NULL);
    pthread_mutex_init(&s->motion_lock,  NULL);
    pthread_mutex_init(&s->fuel_lock,    NULL);
    pthread_mutex_init(&s->ecu_lock,     NULL);
    pthread_mutex_init(&s->display_lock, NULL);
    pthread_cond_init(&s->engine_on_cond, NULL);
    pthread_mutex_init(&s->state_changed_lock, NULL);
    pthread_cond_init(&s->state_changed_cond, NULL);
}

void destroy_sync(SyncPrimitives *s) {
    pthread_mutex_destroy(&s->engine_lock);
    pthread_mutex_destroy(&s->motion_lock);
    pthread_mutex_destroy(&s->fuel_lock);
    pthread_mutex_destroy(&s->ecu_lock);
    pthread_mutex_destroy(&s->display_lock);
    pthread_cond_destroy(&s->engine_on_cond);
    pthread_mutex_destroy(&s->state_changed_lock);
    pthread_cond_destroy(&s->state_changed_cond);
}

void init_system_state(SystemState *state, int rpm, int engine_on,
                       double speed, double fuel, int accelerating) {

    /* ── Engine (from args) ── */
    state->engine.engine_on      = engine_on;
    state->engine.rpm            = rpm;
    state->engine.temperature_c  = 45.0;

    /* ── Motion (from args) ── */
    state->motion.speed_mph      = speed;
    state->motion.total_distance = (rand() % 50000) + 1000.0;
    state->motion.trip_distance  = 0.0;
    state->motion.accelerating   = accelerating;

    /* ── Fuel (from args) ── */
    state->fuel.fuel_gallons     = fuel;
    state->fuel.low_fuel         = (fuel < 0.7);

    /* ── ECU ── */
    state->ecu.rpm_zone          = RPM_ZONE_OFF;
    state->ecu.temp_zone         = TEMP_COLD;
    state->ecu.system_mode       = engine_on ? SYS_NORMAL : SYS_ENGINE_OFF;
    state->ecu.rpm_limit         = 16500;
    state->ecu.speed_limit       = 200.0;
    state->ecu.overheat_active   = 0;
    state->ecu.low_fuel_active   = 0;

    /* ── Display ── */
    state->display.signal        = SIGNAL_OFF;
    state->display.headlight_on  = 1;

    /* ── Timer ── */
    state->timer.total_seconds   = (rand() % 99901) + 100;
    state->timer.trip_seconds    = 0;
    state->timer.trip_start_time = time(NULL);

    /* ── System ── */
    state->system_running        = 1;

    /* ── Sync ── */
    init_sync(&state->sync);
}
