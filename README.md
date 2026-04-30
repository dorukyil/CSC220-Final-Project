# Motorcycle OS – Phase 1: Threads & Architecture

## CSC 220 – Operating Systems and Systems Programming (Spring '26)

---

## Overview

This project implements a simulated motorcycle operating system with a live
text-based terminal dashboard. Five concurrent subsystem threads share a single
`SystemState` struct and cooperate to simulate engine behavior, vehicle motion,
fuel consumption, system control, and display.

---

## Building & Running

```bash
make            # Compile
./motorcycle_os # Run (Ctrl-C to quit)
make clean      # Remove build files
```

Requires: GCC with POSIX threads support (`-pthread`).

---

## Architecture

The system follows a three-layer architecture:

```
 ┌──────────────────────────────────┐
 │  Presentation Layer (Dashboard)  │  Reads state → renders terminal
 ├──────────────────────────────────┤
 │    Control Layer (ECU)           │  Reads state → derives zones
 ├──────────────────────────────────┤
 │  Simulation Layer                │  Produces state
 │   Engine  |  Motion  |  Fuel     │
 └──────────────────────────────────┘
         ▼ all share ▼
      [ SystemState struct ]
```

### Threads

| Thread      | File          | Tick Rate | Responsibility                                      |
|-------------|---------------|-----------|------------------------------------------------------|
| Engine      | `engine.c`    | 100 ms    | Updates RPM (sweep), engine temperature              |
| Motion      | `motion.c`    | 200 ms    | Updates speed (oscillation), accumulates distance    |
| Fuel        | `fuel.c`      | 200 ms    | Drains fuel based on speed + RPM                     |
| ECU         | `ecu.c`       | 100 ms    | Classifies RPM zone and temperature zone             |
| Dashboard   | `dashboard.c` | 100 ms    | Reads all state, renders terminal UI                 |

### Shared State

All state is held in a single `SystemState` struct (defined in `system_state.h`)
containing nested sub-structs for each domain:

- `EngineState` – engine_on, rpm, temperature_c
- `MotionState` – speed_mph, total_distance, trip_distance
- `FuelState` – fuel_gallons, low_fuel
- `ECUState` – rpm_zone, temp_zone
- `DisplayState` – signal, headlight_on
- `TimeState` – total_seconds, trip_seconds, trip_start_time

This keeps related fields grouped while still allowing any thread to read the
entire system snapshot.

---

## Data Flow

### RPM:  Engine → ECU → Dashboard
1. `engine.c` updates `state->engine.rpm` every 100 ms.
2. `ecu.c` reads `state->engine.rpm` and writes `state->ecu.rpm_zone`.
3. `dashboard.c` reads both `state->engine.rpm` and `state->ecu.rpm_zone` to display.

### Fuel → Dashboard
1. `fuel.c` reads `state->motion.speed_mph` and `state->engine.rpm`.
2. It computes consumption and updates `state->fuel.fuel_gallons` and `state->fuel.low_fuel`.
3. `dashboard.c` reads `state->fuel.*` to render the fuel gauge and low-fuel warning.

### Speed → Distance
1. `motion.c` oscillates `state->motion.speed_mph` between 50–70 MPH.
2. Each tick, distance is computed as `speed * tick_duration / 3600` (converting MPH·hours to miles).
3. Both `total_distance` and `trip_distance` are incremented.

---

## Dashboard Layout

The dashboard prioritizes readability and completeness:

- Engine state and RPM with zone classification on the top row
- A visual RPM bar for at-a-glance RPM magnitude
- Temperature with zone label alongside current speed
- Fuel gauge with visual bar (E to F) and numeric gallons remaining
- Low fuel warning when below 0.7 gallons
- Total and trip distance/time
- Signal indicators and headlight state at the bottom

---

## Phase 1 Limitations & Future Work

- **No synchronization**: Shared state is accessed without mutexes. Race
  conditions exist on every shared variable. Phase 2 will add pthread mutexes
  and condition variables.
- **No user input**: Signal, headlight, and engine on/off are static. Phase 2/3
  will add keyboard input handling.
- **Simplified physics**: Speed oscillates deterministically; RPM sweeps
  linearly. Future phases will connect RPM to speed via gear ratios.
- **No safety logic**: The ECU classifies but does not enforce limits. Phase 2
  will add redline protection, overheat shutdown, and fuel-based speed limiting.
