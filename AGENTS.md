# Shared nControl panel design

Apply visual changes consistently to this project and the sibling lego_esp32power,
when available. web-editor/src/style.css must be byte-identical in both projects.
The palette, grid, ports and common buttons share one design; hardware behavior
may differ. Build and inspect both panels after visual changes. Do not copy BLE
logic into the I/O controller or vice versa. The I/O panel is in English; protocol
codes, IDs and user-entered MQTT labels must remain unchanged.

The nControl and ESP root nodes are protected against Delete. Renaming a node
must not implicitly change its MQTT label. Output assignments and device deletions
take effect only after an explicit configuration save.

Upload devices sequentially: finish and verify the first board before uploading
the second. Avoid simultaneous firmware builds/uploads; the user reported host
computer resets during parallel device operations.
