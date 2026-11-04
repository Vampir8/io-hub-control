# Verification and known limits

## English release 2.2.0

The panel, firmware diagnostics/validation messages, built-in display names,
examples, setup instructions and documentation are in English. MQTT topics,
case-sensitive state codes, template IDs, device identities and user-defined
labels remain unchanged. Dedicated UTF-8 parser tests retain non-English text.

Run the following checks from a checkout:

- node web-editor/test.cjs: migration, independent lamp patterns, shared templates,
  ten ports, eleventh-device rejection and compact example generation.
- node web-editor/test-servo-status.cjs: turnout, Three Way, Wye, barrier, custom
  states, unknown state, calibration and received versus commanded state.
- python tools/test_core.py: 74 parser/configuration/public-serialization checks,
  28 output-driver checks, 11 storage checks, 125 port/template/scheduler checks,
  and schema validation. GPIO/LEDC/RMT/NVS are mocked; no real movement occurs.
- node web-editor/ui-test.cjs: optional Playwright/Chrome mock browser checks for
  desktop/mobile layout, English labels, polling, failed-save draft preservation,
  shared templates, palette containment, protected nodes and received/target
  distinction. No ESP32 or broker is contacted.
- python -m platformio run -e supermini, followed by -t buildfs: build the actual
  C3 firmware and LittleFS image with pinned dependencies.

When the sibling lego_esp32power checkout exists, UI tests also compare shared
CSS byte-for-byte. The published project builds and its main tests run without
the sibling checkout or a local nControl install. tools/test_identity.py and
tools/test_node_info.py are additional local integration checks requiring those
external source trees; they are not required for a standalone build.

## Previous hardware verification

The 2.1.x firmware and UI were uploaded sequentially to a C3 SuperMini. The
configuration read-back matched, normal exports hid passwords, MQTT reconnected
and filled the 64-source cache. Three consecutive writes/read-backs of the
existing configuration succeeded without commanding servo movement or enabling
PWM. mDNS answered with the expected device address. Actual browser tests checked
served HTML, desktop/mobile views and absence of JavaScript errors.

The 2.2.0 translation is built and tested locally for publication; physical
flashing/movement is not part of the translation request. UI tests use mock
values. Earlier hardware results are not claimed as new 2.2.0 physical tests.

## Physical measurements still required

Software servo PWM channels 7–10 can have interrupt-induced jitter. Measure pulse
widths under realistic Wi-Fi/MQTT/WS load before relying on their precision.
RMT multiplexing does not guarantee simultaneous animation edges on all GPIO.
SuperMini onboard components vary; external circuits can affect boot strapping.
No reset-time pulse-free guarantee or mechanical servo feedback is provided.

Digital sensors and the proposed 12-device design remain future work. ETCS and
German signal discussions did not implement additional firmware features.

## Publication checks — 2026-11-04

English release builds: ESP32-C3 application and LittleFS — PASS. Main host
tests (74 + 28 + 11 + 125 checks and schemas), model and servo-state tests, and
Chrome desktop/mobile tests for both panels — PASS. Shared CSS remains byte-
identical. The release audit covered 63 tracked files and the source/binary ZIP;
local logs, build caches, real network settings and credentials are excluded.
No physical firmware upload or servo movement was performed for this release.
