/* ═══════════════════════════════════════════════════════════
   main.c – Motorcycle OS
   CSC 220 – Operating Systems and Systems Programming

   Usage: ./motorcycle_os <RPM> <ENGINE_STATE> <SPEED> <FUEL_LEVEL> <A/D>

   Example: ./motorcycle_os 3000 1 45 3.5 A
   ═══════════════════════════════════════════════════════════ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include "subsystems.h"

static SystemState g_state;

static void handle_sigint(int sig) {
    (void)sig;
    g_state.system_running = 0;
    /* Wake any threads waiting on condition variables */
    pthread_cond_broadcast(&g_state.sync.engine_on_cond);
    pthread_cond_broadcast(&g_state.sync.state_changed_cond);
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <RPM> <ENGINE_STATE> <SPEED> <FUEL_LEVEL> <A/D>\n", prog);
    fprintf(stderr, "  RPM          : Initial engine RPM (0-16500)\n");
    fprintf(stderr, "  ENGINE_STATE : 0 = OFF, 1 = ON\n");
    fprintf(stderr, "  SPEED        : Initial speed in MPH (0-200)\n");
    fprintf(stderr, "  FUEL_LEVEL   : Initial fuel in gallons (0.0-4.7)\n");
    fprintf(stderr, "  A/D          : A = accelerating, D = decelerating\n");
}

int main(int argc, char *argv[]) {

    if (argc < 6) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* Parse command-line args */
    int    rpm          = atoi(argv[1]);
    int    engine_on    = atoi(argv[2]);
    double speed        = atof(argv[3]);
    double fuel         = atof(argv[4]);
    int    accelerating = (argv[5][0] == 'A' || argv[5][0] == 'a') ? 1 : 0;

    srand((unsigned)time(NULL));

    init_system_state(&g_state, rpm, engine_on, speed, fuel, accelerating);

    signal(SIGINT, handle_sigint);

    pthread_t t_engine, t_motion, t_fuel, t_ecu, t_dashboard;

    pthread_create(&t_engine,    NULL, engine_thread,    &g_state);
    pthread_create(&t_motion,    NULL, motion_thread,    &g_state);
    pthread_create(&t_fuel,      NULL, fuel_thread,      &g_state);
    pthread_create(&t_ecu,       NULL, ecu_thread,       &g_state);
    pthread_create(&t_dashboard, NULL, dashboard_thread, &g_state);

    pthread_join(t_engine,    NULL);
    pthread_join(t_motion,    NULL);
    pthread_join(t_fuel,      NULL);
    pthread_join(t_ecu,       NULL);
    pthread_join(t_dashboard, NULL);

    destroy_sync(&g_state.sync);

    printf("\n\nMotorcycle OS shut down.\n");
    return EXIT_SUCCESS;
}
