# PlatformIO extra script: append srmodels.bin to the esptool upload command.
# The model partition starts at 0x390000 in partitions_esp_sr.csv.
# srmodels.bin is bundled with the Arduino ESP32-S3 framework (~3.2MB).
#
# Appending to UPLOADERFLAGS inserts the extra address+file pair into the
# write-flash argument list (before $ESP32_APP_OFFSET $SOURCE, but esptool
# write-flash accepts pairs in any order).
Import('env')
import os

packages_dir = env.subst("$PROJECT_PACKAGES_DIR")
srmodels_path = os.path.join(
    packages_dir,
    "framework-arduinoespressif32-esp32s3-libs",
    "esp_sr",
    "srmodels.bin",
)

MODEL_OFFSET = "0x390000"

if os.path.isfile(srmodels_path):
    size_kb = os.path.getsize(srmodels_path) // 1024
    print(f"ESP-SR: will flash srmodels.bin ({size_kb} KB) → {MODEL_OFFSET}")
    env.Append(UPLOADERFLAGS=[MODEL_OFFSET, srmodels_path])
else:
    print(f"ESP-SR WARNING: srmodels.bin not found at {srmodels_path}")
