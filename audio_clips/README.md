# Audio Clips

Place raw PCM audio files here and generate C arrays from them.

## Format

- Sample rate: 16000 Hz
- Bit depth: 16-bit signed integer
- Channels: Mono
- Endian: Little-endian

## Converting an audio file

```bash
# Convert any audio file to the required raw PCM format
sox input.wav -r 16000 -b 16 -c 1 -e signed-integer output.raw

# Generate a C array header
xxd -i output.raw > ../src/clip_name.h
```

Then in `audio_clips.cpp`, replace the extern stub with:
```cpp
#include "clip_name.h"
// and point the ClipEntry to the generated array
```

## Clip index map

| Index | ID                     | Filename              |
|-------|------------------------|-----------------------|
| 0     | CLIP_GEOFENCE_ALERT    | geofence_alert.raw    |
| 1     | CLIP_COLLISION_WARNING | collision_warning.raw |
| 2     | CLIP_LOW_BATTERY       | low_battery.raw       |
| 3     | CLIP_HELMET_ON         | helmet_on.raw         |
| 4     | CLIP_HELMET_OFF        | helmet_off.raw        |
