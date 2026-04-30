/* ═══════════════════════════════════════════════════════════
   dashboard.c – Dashboard Subsystem
   Reads a consistent snapshot of shared state using locks.
   Displays system mode + ECU warnings. Pure visualization.
   ═══════════════════════════════════════════════════════════ */

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include "subsystems.h"

#define DASH_TICK_US         100000
#define DASH_WIDTH           54
#define FUEL_BAR_LEN         20
#define RPM_BAR_LEN          20

/* ── Snapshot struct for consistent reads ── */
typedef struct {
    EngineState  engine;
    MotionState  motion;
    FuelState    fuel;
    ECUState     ecu;
    DisplayState display;
    long         total_secs;
    long         trip_secs;
} DashSnapshot;

static void format_time(long seconds, char *buf, int buflen) {
    int h = (int)(seconds / 3600);
    int m = (int)((seconds % 3600) / 60);
    int s = (int)(seconds % 60);
    snprintf(buf, buflen, "%02d:%02d:%02d", h, m, s);
}

static const char *rpm_zone_str(RPMZone zone) {
    switch (zone) {
        case RPM_ZONE_OFF:     return "OFF    ";
        case RPM_ZONE_IDLE:    return "IDLE   ";
        case RPM_ZONE_NORMAL:  return "NORMAL ";
        case RPM_ZONE_HIGH:    return "HIGH   ";
        case RPM_ZONE_REDLINE: return "REDLINE";
        default:               return "???    ";
    }
}

static const char *temp_zone_str(TempZone zone) {
    switch (zone) {
        case TEMP_COLD:     return "COLD    ";
        case TEMP_NORMAL:   return "NORMAL  ";
        case TEMP_HOT:      return "HOT     ";
        case TEMP_OVERHEAT: return "OVERHEAT";
        default:            return "???     ";
    }
}

static const char *system_mode_str(SystemMode mode) {
    switch (mode) {
        case SYS_ENGINE_OFF: return "ENGINE OFF";
        case SYS_IDLE:       return "IDLE      ";
        case SYS_NORMAL:     return "NORMAL    ";
        case SYS_HIGH_LOAD:  return "HIGH LOAD ";
        case SYS_CRITICAL:   return "CRITICAL  ";
        default:             return "???       ";
    }
}

static void build_bar(char *buf, int buflen, double value, double max_val, int bar_len) {
    int filled = (int)((value / max_val) * bar_len);
    if (filled < 0) filled = 0;
    if (filled > bar_len) filled = bar_len;
    int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < bar_len; i++) {
        if (pos >= buflen - 2) break;
        buf[pos++] = (i < filled) ? '#' : '-';
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
}

static void print_hline(char c, int width) {
    for (int i = 0; i < width; i++) putchar(c);
    putchar('\n');
}

/* ── Take a consistent snapshot using locks ── */
/* Lock order: engine → motion → fuel → ecu    */
static void take_snapshot(SystemState *state, DashSnapshot *snap) {
    pthread_mutex_lock(&state->sync.engine_lock);
    snap->engine = state->engine;
    pthread_mutex_unlock(&state->sync.engine_lock);

    pthread_mutex_lock(&state->sync.motion_lock);
    snap->motion = state->motion;
    pthread_mutex_unlock(&state->sync.motion_lock);

    pthread_mutex_lock(&state->sync.fuel_lock);
    snap->fuel = state->fuel;
    pthread_mutex_unlock(&state->sync.fuel_lock);

    pthread_mutex_lock(&state->sync.ecu_lock);
    snap->ecu = state->ecu;
    pthread_mutex_unlock(&state->sync.ecu_lock);

    pthread_mutex_lock(&state->sync.display_lock);
    snap->display = state->display;
    pthread_mutex_unlock(&state->sync.display_lock);

    /* Timer */
    time_t now = time(NULL);
    snap->trip_secs  = (long)(difftime(now, state->timer.trip_start_time));
    snap->total_secs = state->timer.total_seconds + snap->trip_secs;
}

static void render_dashboard(DashSnapshot *snap) {
    char total_time[16], trip_time[16];
    format_time(snap->total_secs, total_time, sizeof(total_time));
    format_time(snap->trip_secs,  trip_time,  sizeof(trip_time));

    char fuel_bar[64], rpm_bar[64];
    build_bar(fuel_bar, sizeof(fuel_bar), snap->fuel.fuel_gallons, 4.7, FUEL_BAR_LEN);
    build_bar(rpm_bar,  sizeof(rpm_bar),  (double)snap->engine.rpm, 16500.0, RPM_BAR_LEN);

    const char *left_sig  = " ";
    const char *right_sig = " ";
    switch (snap->display.signal) {
        case SIGNAL_LEFT:   left_sig  = "<"; break;
        case SIGNAL_RIGHT:  right_sig = ">"; break;
        case SIGNAL_HAZARD: left_sig  = "<"; right_sig = ">"; break;
        default: break;
    }

    printf("\033[H\033[J");

    print_hline('=', DASH_WIDTH);
    printf("|              MOTORCYCLE OS                   |\n");
    printf("|         MODE: %s                 |\n", system_mode_str(snap->ecu.system_mode));
    print_hline('=', DASH_WIDTH);

    printf("  ENG: %s        RPM: %-5d  [%s]\n",
           snap->engine.engine_on ? "ON " : "OFF",
           snap->engine.rpm,
           rpm_zone_str(snap->ecu.rpm_zone));
    printf("  RPM  %s\n", rpm_bar);

    print_hline('-', DASH_WIDTH);

    printf("  TEMP: %5.1f C  [%s]    SPEED: %5.1f MPH\n",
           snap->engine.temperature_c,
           temp_zone_str(snap->ecu.temp_zone),
           snap->motion.speed_mph);

    print_hline('-', DASH_WIDTH);

    printf("  FUEL  E %s F   %.2f gal\n", fuel_bar, snap->fuel.fuel_gallons);

    /* Warnings */
    if (snap->ecu.overheat_active && snap->fuel.low_fuel)
        printf("  !! OVERHEAT !!   !! LOW FUEL !!              \n");
    else if (snap->ecu.overheat_active)
        printf("  !! OVERHEAT - RPM/SPEED LIMITED !!           \n");
    else if (snap->fuel.low_fuel)
        printf("  !! LOW FUEL - SPEED LIMITED !!               \n");
    else
        printf("                                               \n");

    print_hline('-', DASH_WIDTH);

    printf("  TOTAL DIST: %9.1f mi    TRIP: %8.1f mi\n",
           snap->motion.total_distance, snap->motion.trip_distance);
    printf("  TOTAL TIME: %s        TRIP: %s\n", total_time, trip_time);

    print_hline('-', DASH_WIDTH);

    printf("  %s BLINK-L        HEADLIGHT: %s       BLINK-R %s\n",
           left_sig,
           snap->display.headlight_on ? "ON " : "OFF",
           right_sig);

    print_hline('=', DASH_WIDTH);

    fflush(stdout);
}

void *dashboard_thread(void *arg) {
    SystemState *state = (SystemState *)arg;
    DashSnapshot snap;

    while (state->system_running) {
        take_snapshot(state, &snap);
        render_dashboard(&snap);
        usleep(DASH_TICK_US);
    }
    return NULL;
}
