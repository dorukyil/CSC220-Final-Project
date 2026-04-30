#ifndef SUBSYSTEMS_H
#define SUBSYSTEMS_H

#include "system_state.h"

/* ── Thread entry points ── */
void *engine_thread(void *arg);
void *motion_thread(void *arg);
void *fuel_thread(void *arg);
void *ecu_thread(void *arg);
void *dashboard_thread(void *arg);

/* ── Initialization from command-line args ── */
void init_system_state(SystemState *state, int rpm, int engine_on,
                       double speed, double fuel, int accelerating);

/* ── Sync init / destroy ── */
void init_sync(SyncPrimitives *s);
void destroy_sync(SyncPrimitives *s);

#endif /* SUBSYSTEMS_H */
