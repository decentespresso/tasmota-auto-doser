# Future Features

- Cache the validated relay GPIO and inactive level so the cutoff path can call the lowest-level safe GPIO write directly.
- Build a leaner grinder-only firmware profile by removing unused Tasmota features such as MQTT, rules, timers, BLE, extra sensors, and nonessential drivers while keeping Web UI and OTA if convenient updates remain useful.
