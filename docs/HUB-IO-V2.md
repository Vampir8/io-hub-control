# Detailed I/O Hub guide

## Graph and shared templates

The graph is nControl → ESP32-C3 → devices. Connect one device per logical I/O
port. A wire assigns the GPIO from the ESP32 port map; a device settings form does
not duplicate that GPIO selector. Disconnecting a wire leaves the device in the
configuration and counts toward the ten-device limit, but disables its MQTT
control. Root nControl/ESP32 nodes cannot be deleted. Rename changes a display
name, never the MQTT source label.

A template contains states and actions, not GPIO or an MQTT source. Reusing a
template shares lamp patterns/angles across devices. Duplicate creates an
independent copy. Changes take effect only after Save and apply. A template
angle outside any assigned servo's limits is rejected.

Each signal state defines every local lamp: off, solid or blinking, RGBW color,
brightness and ON/OFF times. Phase offset produces alternating crossing lamps:
for equal 500/500 ms timing, give the second lamp a 500 ms phase offset.

Nine heads were imported from the Vampir8 nControl definitions: classic-RG,
shunt, 3-GRW, 3-YRW, 3-GRY, 3-YRY, 4-GRYW, 4-YRYW and 5-GYRYW. They preserve
physical top-to-bottom lamp order, available S1–S9/Ms1/Ms2/OFF states and colors.
Classic-RG uses r/g/rf/gf/o with red at index 0 and green at index 1. In the imported
Vampir8 profile S4 means steady upper yellow, S5 blinking upper yellow, S8 two
steady yellows and S9 blinking upper plus steady lower yellow. These are concrete
nControl profile definitions, not universal meanings for every signal system.

The committed generated profiles allow building without a local nControl install.
To refresh from your own compatible definitions:

```sh
python tools/import_signals.py --source /path/to/polish_signals.py
node web-editor/test.cjs
node web-editor/build.cjs
```

Import regenerates both the C++ signal table and browser templates. English
display names are generated separately from unchanged protocol codes/roles.

## RMT scheduling and animation

ESP32-C3 has two RMT transmitters. The driver installs at most two and schedules
up to ten chains through them. After transmission completion it disconnects the
old GPIO from the RMT matrix, holds it LOW, waits at least 300 microseconds and
only then reuses the slot. The data buffer stays unchanged until completion.
Encoding is selected per transmission, allowing the supported WS/SK types to mix.

Each signal maintains its own state and animation origin. Repeated states do
not restart blinking. The output task runs at roughly 2 ms intervals; a chain is
refreshed at most about every 30 ms. Animation edges can therefore differ by
several tens of milliseconds, and Wi-Fi/FreeRTOS load can increase this delay.
There is no guarantee of simultaneous edges on all DATA pins.

Supported encodings: WS2812B RGB, SK6812 RGB and RGBW with GRB/RGB or GRBW/RGBW
orders. This is not a claim of support for every WS-family chip. Version 2 signal
ports allow five lamps; longer chains remain part of legacy v1 configuration.

## Servo resources and movement

The first six configured servos use hardware LEDC, at most four frequencies.
The next four use TIMER_GROUP_1/TIMER_0, an IRAM level-3 interrupt and a 1 µs timer
step to generate edges in a 20 ms frame. These four require 50 Hz. Travel is
interpolated without delay loops; detach-after-movement and calibration exclusion
also work on these outputs.

Interrupts can stretch software PWM pulses. Mock tests do not replace measuring
jitter with Wi-Fi, MQTT and WS updates active. Use hardware LEDC channels when
precision is essential. Startup defaults to no commanded movement/no PWM.
Unknown mechanical position cannot be interpolated from a measured starting
angle, because the system has no servo feedback. Later moves interpolate the
commanded position at the configured speed.

Manual calibration takes priority over MQTT for that servo. MQTT servo permission
starts disabled, is disabled by saving configuration, and does not automatically
apply cached states. Apply cached states separately if desired. Offline behavior
can hold the last commanded position, select a safe position or disable PWM;
disabling pulses does not mean physically holding a load.

## GPIO, power and setup

Allowed GPIO and boot/JTAG/UART restrictions are listed in the main README.
Defaults reserve no application GPIO for UART0 or external JTAG; do not connect
a debugger to assigned GPIO4–7. USB CDC uses GPIO18/19 outside the allowed list.
Inspect your specific SuperMini variant for onboard LED/button connections.
External loads can affect boot strapping and reset-time levels.

Servo and LED power must be external and sufficient for their load, with a common
ground to ESP32. GPIO is signal only. WS wiring is GPIO → DIN, then DOUT → DIN.
Use an appropriate logic-level shifter where 3.3 V DATA does not meet the LED's
input specification at its supply voltage.

First boot creates IO-Hub-xxxxxx-setup, password ncontrol-setup, at 192.168.4.1.
The ESP32 Settings form configures Wi-Fi and broker. Setup AP is also offered
when Wi-Fi cannot connect; the main loop remains nonblocking. Keepalive is 15 s
and MQTT retry is every 5 s. Ordinary password fields do not reveal saved values;
leaving them blank preserves the stored password.

## Persistence, migration and preview

The firmware accepts v1 and v2. Migration runs in the browser draft only and
requires explicit Save and apply. Multiple independently controlled groups on
one legacy chain, or a chain longer than five LEDs, do not have an unambiguous
one-port/one-source mapping: adjust them before migration. The original saved
JSON is retained until a valid replacement is saved.

Two NVS slots store generation, length and CRC; saving verifies the stored bytes
before selecting the new slot. Previous valid data remains available. MQTT
messages and animation frames are not written to flash. Limits: 24 KiB serialized
JSON, at most 64 KiB parsing pool, 24 templates, 128 states and 320 lamp definitions.
Default pixel timing/brightness fields are omitted when equal to 255/500/500/0
and restored in the editor. Parsing sizes account for deduplicated JSON strings.
Request bodies and public exports avoid unnecessary full-document copies.

Import and backup restoration load a draft. Export with passwords is explicitly
separate. GPIO/regulations are validated before application. Saving stops old
outputs, frees resources, then allocates the validated outputs. Default servo
startup settings do not command a new position merely because a form was saved.

Preview in the template editor uses the draft without operating hardware. API
preview uses saved rules without operating hardware. Physical test and calibration
are separate actions requiring confirmation. Diagnostics exposes Wi-Fi/MQTT,
uptime, heap, largest free block, config pool usage, bounded logs, detected
sources, matched rules and movement blocking reason.

## MQTT details

Subscribe to nControl/websocket/rx, then publish W|ST once to
nControl/websocket/tx after all subscription acknowledgments. The topic names
are nControl protocol names, not directions relative to ESP32. An S index is an
internal index in the current plan, not a GPIO or catalog track number.

T labels and states preserve case and UTF-8; the separator | is reserved. Commas
remain part of the state. Invalid lengths, numbers and incomplete payloads are
rejected. Unknown states do not imply a guessed action. Duplicate states do not
restart a movement or blink cycle. Reconnect re-subscribes and synchronizes.

T/S messages also appear in Simulation. A custom permission topic can map
OPERATIONAL to enabled and SIMULATION/DESIGN to blocked, but it must be configured
separately in nControl. Listening for W|SM alone cannot establish actual program
mode. A Crossover publishing the same l for both positions needs S or a corrected
source. Do not infer the geometry/motor count of a Double Slip/Monster from its
name: explicitly map the controlling source and each output's actions.

## Build, upload and two boards

Build commands are in the README. Python tests need g++ or ziglang and jsonschema.
The UI has no CDN assets. Its classic theme, 24 px grid, palette and 22 px compact
node buttons/pills share the design of the sibling Powered Up Hub Control project.

Custom flash offsets: bootloader 0x0, table 0x8000, application 0x20000,
LittleFS 0x220000. Never write stock boot_app0 at 0xe000 or erase flash when
preserving configuration. If the normal esptool stub loses USB, use the ROM
loader (PowerShell example):

```powershell
python "$env:USERPROFILE/.platformio/packages/tool-esptoolpy/esptool.py" --chip esp32c3 --port COMx --baud 115200 --no-stub write_flash --flash_size 4MB 0x20000 .pio/build/supermini/firmware.bin 0x220000 .pio/build/supermini/littlefs.bin
```

Check the processor and current serial port, not a remembered COM number.
Upload and verify one board before the next. Two ESP32s can use the same broker
and state topic, but each needs a distinct Client ID/hostname and its own output
assignments. For example board A subscribes to Turnout1/Signal1 in its templates,
and board B to Turnout2/Signal2, with separate physical GPIO mappings.
