# Legacy version 1 configuration

The current firmware retains v1 support for existing installations. New setups
should use v2 ports and shared templates; the graphical panel converts supported
v1 configurations into an editable draft before any explicit save.

Version 1 separates physical LED chains, logical LED groups, servo resources and
bindings. A binding maps one T label or S segment index to states and action
lists. LED groups reference chain-local indices; overlapping independently
controlled groups require explicit, different priorities. Each active target has
one binding owner. An action can command a servo angle or set an LED group to
solid/off/blink/alternate without clearing other groups.

Legacy limits: six LEDC servos, four LEDC frequencies, two RMT chains, up to 300
LEDs per chain, 32 groups, 32 bindings, 128 states and 256 actions. These differ
from the ten-device v2 scheduler. See config.schema.json and examples/config.json.

A legacy chain longer than five LEDs or with multiple independent source owners
cannot be migrated automatically into a v2 one-port/one-source signal head.
Adjust the old configuration or preserve a backup before migrating. The panel
reports the issue and leaves stored NVS data unchanged until a successful save.

MQTT parsing, startup movement permission, calibration, GPIO reservations,
external power/common-ground requirements and explicit physical-test behavior
remain as described in the main README. Version 1 is a compatibility format;
there is no separate old web interface.
