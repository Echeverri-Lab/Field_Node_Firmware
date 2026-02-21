# Field_Node_Firmware
Firmware for all field node sensors and management.

- **Architecture**: FreeRTOS, C/C++ Orchestrator (ESP-IDF)
- **Strategy**: Bottom-up development—reliable drivers first, then independent tasks, then the orchestrator.

## Collaboration

1. Please make a branch for yourself to work on
2. For commits please write out thorough commit messages
3. Try to limit work to a few finals each commit so there is less chance of two people colliding
4. Possibly set up text chain to make sure two people aren't coding same sensor before pushing changes

## Repository Structure

This repository is organized into three main areas to support development from prototype to production:

- **docs/**: Documentation, specifications, and planning artifacts.
- **breadboardtest/**: Initial prototyping code used by Adam for verifying hardware connections on the breadboard.
- **MVP/**: The main ESP-IDF firmware project. This serves as the structured starting point for the field node firmware, implementing the architecture described in `docs/ARCHITECTURE.md`.

## Quick Start (MVP)
The `MVP/` directory contains the active development project.

1. Install ESP-IDF (v5.x).
2. Navigate to `MVP/`: `cd MVP`
3. Set target: `idf.py set-target esp32s3`
4. Build: `idf.py build`
5. Flash: `idf.py flash`
