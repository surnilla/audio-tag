# PlatformIO extra script: flash ESP-SR srmodels.bin to the "model" partition.
# The model partition starts at 0x390000 in partitions_esp_sr.csv.
# srmodels.bin is bundled with the Arduino ESP32-S3 framework (~3.2MB).
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
    env.Append(FLASH_EXTRA_IMAGES=[(MODEL_OFFSET, srmodels_path)])
else:
    print(f"ESP-SR WARNING: srmodels.bin not found at {srmodels_path}")
