# esp32-fw-showcase

A modular ESP32 firmware stack built with ESP-IDF and FreeRTOS, showcasing production-grade architecture patterns for embedded systems.

## Architecture

```
components/
├── bsp/          Board Support Package — hardware abstraction layer
├── ble_stack/    BLE peripheral — remote command interface
├── ota_manager/  OTA updates with automatic rollback
└── power_mgr/    Power management — deep sleep / wake triggers
main/             Application entry point and task orchestration
test/             Unit tests (Unity framework)
.github/          CI/CD pipeline (build + static analysis)
```

## Hardware

- **Target**: ESP32 (compatible with ESP32-P4 / C5 with minor BSP changes)
- **Simulator**: [Wokwi](https://wokwi.com) — run without physical hardware

## Getting Started

### Prerequisites

- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/)
- Python 3.8+

### Build

```bash
idf.py set-target esp32
idf.py build
```

### Flash & Monitor

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

### Run tests

```bash
cd test
idf.py build flash monitor
```

## Features

- **FreeRTOS task architecture** with inter-task communication via queues
- **BSP abstraction** — swap hardware without touching application logic
- **BLE peripheral** — control the device remotely via standard GATT profile
- **OTA with rollback** — safe firmware updates, auto-revert on boot failure
- **Power management** — tickless idle + deep sleep on inactivity
- **CI/CD** — GitHub Actions builds and runs static analysis on every push
- **Unit tested** — critical components covered with Unity test framework

## Development

Built with [Claude Code](https://claude.ai/code) as AI pair programmer.
