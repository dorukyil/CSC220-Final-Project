#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <pthread.h>
#include <time.h>

/* ── RPM Zone Classifications ── */
typedef enum {
    RPM_ZONE_OFF,
    RPM_ZONE_IDLE,
    RPM_ZONE_NORMAL,
    RPM_ZONE_HIGH,
    RPM_ZONE_REDLINE
} RPMZone;

/* ── Engine Temperature Classifications ── */
typedef enum {
    TEMP_COLD,
    TEMP_NORMAL,
    TEMP_HOT,
    TEMP_OVERHEAT
} TempZone;

/* ── Signal State ── */
typedef enum {
    SIGNAL_OFF,
    SIGNAL_LEFT,
    SIGNAL_RIGHT,
    SIGNAL_HAZARD
} SignalState;

/* ── System-Level State (ECU manages this) ── */
typedef enum {
    SYS_ENGINE_OFF,
    SYS_IDLE,
    SYS_NORMAL,
    SYS_HIGH_LOAD,
    SYS_CRITICAL
} SystemMode;

/* ── Engine Subsystem State ── */
typedef struct {
    int           engine_on;
    int           rpm;
    double        temperature_c;
} EngineState;

/* ── Motion Subsystem State ── */
typedef struct {
    double        speed_mph;
    double        total_distance;
    double        trip_distance;
    int           accelerating;     /* 1 = accel, 0 = decel (from cmd arg)  */
} MotionState;

/* ── Fuel Subsystem State ── */
typedef struct {
    double        fuel_gallons;
    int           low_fuel;
} FuelState;

/* ── ECU Derived State ── */
typedef struct {
    RPMZone       rpm_zone;
    TempZone      temp_zone;
    SystemMode    system_mode;
    int           rpm_limit;        /* Max RPM allowed (ECU enforced)       */
    double        speed_limit;      /* Max speed allowed (ECU enforced)     */
    int           overheat_active;  /* 1 if overheat protection engaged     */
    int           low_fuel_active;  /* 1 if low fuel speed limiting active  */
} ECUState;

/* ── Dashboard / Display State ── */
typedef struct {
    SignalState    signal;
    int            headlight_on;
} DisplayState;

/* ── Time Tracking ── */
typedef struct {
    long          total_seconds;
    long          trip_seconds;
    time_t        trip_start_time;
} TimeState;

/* ══════════════════════════════════════════════════════════
   Synchronization primitives
   Lock acquisition order: engine → motion → fuel → ecu
   ══════════════════════════════════════════════════════════ */
typedef struct {
    pthread_mutex_t engine_lock;
    pthread_mutex_t motion_lock;
    pthread_mutex_t fuel_lock;
    pthread_mutex_t ecu_lock;
    pthread_mutex_t display_lock;

    /* Condition: engine has been turned ON                  */
    pthread_cond_t  engine_on_cond;
} SyncPrimitives;

/* ══════════════════════════════════════════════════════════
   Master shared system state
   ══════════════════════════════════════════════════════════ */
typedef struct {
    EngineState   engine;
    MotionState   motion;
    FuelState     fuel;
    ECUState      ecu;
    DisplayState  display;
    TimeState     timer;
    SyncPrimitives sync;
    int           system_running;
} SystemState;

#endif /* SYSTEM_STATE_H */
