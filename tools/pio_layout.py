# Executed by SCons, not standalone Python.
Import("env")
# Arduino's stock script adds an OTA boot_app0 blob at 0xe000.
# Our factory-only layout uses that address for NVS: never overwrite it.
env.Replace(FLASH_EXTRA_IMAGES=[
    (offset, path) for offset, path in env.get("FLASH_EXTRA_IMAGES", [])
    if int(offset, 0) != 0xe000
])
env.Replace(ESP32_APP_OFFSET="0x20000")
# post scripts run after the uploader may already have flattened that list.
flags = list(env.get("UPLOADERFLAGS", []))
clean = []
index = 0
while index < len(flags):
    if str(flags[index]).lower() == "0xe000":
        index += 2
    else:
        clean.append(flags[index])
        index += 1
env.Replace(UPLOADERFLAGS=clean)
