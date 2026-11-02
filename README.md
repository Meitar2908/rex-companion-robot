# REX

**A standalone desktop robot with an animated face, full voice conversation,
and a dry, sarcastic AI personality.**

![platform](https://img.shields.io/badge/platform-ESP32--S3-blue)
![framework](https://img.shields.io/badge/framework-Arduino%20%2F%20PlatformIO-00979D)
![language](https://img.shields.io/badge/language-C%2B%2B17-00599C)
![AI](https://img.shields.io/badge/personality-Claude-6B4FBB)
![license](https://img.shields.io/badge/license-All%20Rights%20Reserved-lightgrey)
[![LinkedIn](https://img.shields.io/badge/LinkedIn-Meitar%20Amir-0A66C2?logo=linkedin&logoColor=white)](https://www.linkedin.com/in/meitaramir)

![REX, front view](media/photos/rex_main_photo.jpg)

Say **"Hey REX"** and just talk to it. No app, no button, no PC — it
listens, thinks, talks back, and reacts on an animated face, powered by an
ESP32 the size of a stick of gum.

**Status:** Fully assembled and working end-to-end on real hardware —
display, mic, speaker, WiFi, voice pipeline, personality, and wake word are
all live and operating together, not just individually tested.

**[Watch REX in action](https://drive.google.com/file/d/1YCF7tVUBQWpwD_MZ__kD1XeX5-LFlLnb/view?usp=sharing)**

## What it is

REX started as a question: how much "personality" can you fit on a $10
microcontroller? The answer turned out to be: a surprisingly convincing
character — one that wakes up when you say its name, holds a real spoken
conversation, remembers things about you between sessions, checks the
weather or sets a timer when asked, and expresses all of it through a face
made of nothing but two eyes and a mouth, animated from first principles.

Everything — audio capture, wake-word detection, the voice pipeline, face
rendering, and the network calls to Claude — runs on one ESP32-S3. There's
no Raspberry Pi, no laptop in the loop, no cloud dashboard. Unplug it from
WiFi and it still blinks and breathes; plug it back in and it's REX again.

## Features

- **A face built from curves, not sprites** — the mouth is three overlapping
  Gaussian bell curves redrawn every frame from live audio amplitude, so it
  shapes itself to whatever REX is actually saying instead of cycling
  through a fixed set of mouth images. Eyes get idle gaze wander and a slow
  breathing pulse so REX never looks frozen, even in silence. 20+ discrete
  expressions (happy, skeptical, evil, smug, dizzy, and more) layer on top.
- **Hands-free voice conversation** — mic → voice-activity detection →
  speech-to-text → Claude → text-to-speech, with no button required. A
  custom-trained wake-word model ("Hey REX") runs continuously on-device via
  TensorFlow Lite Micro, so REX is always listening for its name without
  ever streaming audio anywhere until it hears it.
- **An actual personality, not a chatbot wrapper** — sarcastic and dry by
  design, with tone that shifts across the day and a persistent memory of
  facts it's picked up about you, carried across reboots.
- **Real tool use** — Claude can reach for live weather and time for any
  city, exact arithmetic, nearby places, sports results, web search, and
  timers REX announces on its own when they're due — the same tool-calling
  pattern production AI products use, running off a microcontroller.
- **Idle behavior** — small ambient fidgets and a drift toward a sleepy
  expression after a few quiet minutes, so an idle REX still feels alive
  rather than paused.

## Gallery

| Default | Angry | Confused |
|---|---|---|
| ![REX's default, idle expression](media/photos/rex_default_face.jpg) | ![REX looking angry](media/photos/angry_face.jpg) | ![REX looking confused](media/photos/confused_face.jpg) |

| Sad | Surprised | Smug |
|---|---|---|
| ![REX looking sad](media/photos/sad_face.jpg) | ![REX looking surprised](media/photos/surprised_face.jpg) | ![REX looking smug](media/photos/smug_face.jpg) |

![REX from behind, showing the exterior USB-C connector port](media/photos/rex_connector_port.jpg)

### Inside REX

![REX's case opened up, showing the ESP32-S3, amp, and wiring](media/photos/rex_inside.jpg)

### Designed in Fusion 360

| Full assembly | Front view |
|---|---|
| ![CAD model, isometric view](media/photos/cad_model_iso.png) | ![CAD model, front view](media/photos/cad_model_front.png) |

| Internal mounting | Back view |
|---|---|
| ![CAD cutaway showing the ESP32-S3 perfboard mount and mic/speaker cutouts](media/photos/cad_model_cutaway.png) | ![CAD model, back view showing the vents and USB-C port cutout](media/photos/cad_model_angle.png) |

The case wasn't designed around a generic robot shape — it was built around
the hardware REX actually uses. The front cutout is sized to the exact
3.2" ILI9341 panel, and the interior cavity was shaped to fit the
hand-soldered perfboard, speaker, and mic snugly rather than leaving generic
empty space inside. The electronics themselves were soldered point-to-point
specifically to fit that footprint, not assembled from an off-the-shelf
carrier board.

Every arm and leg's joint rotates a full 360° in its socket, so REX can be
posed however you like — arms up or down, legs out to either side.

### From prototype to finished build

| Early breadboard prototype | Hand-soldered perfboard | Printing the case |
|---|---|---|
| ![An early breadboard prototype with the display and mic](media/photos/early_process.jpg) | ![The ESP32-S3 perfboard, hand-soldered](media/photos/inside_esp32.jpg) | ![The case printing on a Bambu printer](media/photos/3d_printing_bts.jpg) |

## How it works

```
   mic ──▶ voice-activity   ──▶ speech-to-text ──▶ Claude ──▶ text-to-speech
           detection (VAD)       (streaming)        + tools      │
             ▲                                                   │
    "Hey REX" wake word                                          ▼
     (always listening,                                   animated face
      runs on-device)                                    (eyes + mouth)
```

Two FreeRTOS tasks split the work across the ESP32-S3's two cores: one
drives face rendering and audio capture continuously so the animation never
stutters, while the other handles the network round-trip (STT → Claude →
TTS) so a slow API call can't freeze REX's face. Speech-to-text streams
audio to the transcription API *while the user is still talking*, instead
of waiting for them to finish and uploading the whole clip afterward — it
collapses several seconds of dead air into something closer to real-time.

## Hardware

| Part | Detail |
|---|---|
| MCU | ESP32-S3-WROOM-1 N16R8 (16MB flash, 8MB octal PSRAM) |
| Display | 3.2" ILI9341 SPI, 240×320 |
| Mic | I2S MEMS mic (INMP441-class) |
| Amp + speaker | MAX98357A I2S amp, 4Ω 3W speaker |
| Power | 5V/2A USB-C wall adapter |
| Enclosure | 3D-printed case, exterior USB-C breakout for firmware updates, 360°-rotating ball-jointed arms and legs |
| Dimensions | ~164mm tall × ~165mm wide, including antenna and arms |

The S3 has two independent I2S controllers, so mic capture and speaker
playback run simultaneously with zero contention — one of the reasons it
was chosen over a cheaper single-core alternative.

## Challenges along the way

- **Driver mixup cost a full debugging session.** The display panel looks
  identical to boards that ship with an ST7789 controller, which produces
  garbled, half-correct output if you drive it that way. It's actually an
  ILI9341 — same resolution, different controller, no visible way to tell
  from the board alone.
- **The enclosure changed the acoustics.** Voice-activity detection was
  tuned and working well with the mic exposed on the bench. Once sealed
  into the printed case, the mic hole attenuated incoming audio by roughly
  half, which pushed quiet speech close enough to the silence floor that
  natural mid-sentence pauses started getting misread as the end of an
  utterance. Fixed by re-measuring noise and speech levels *through the
  case* rather than reusing the open-air numbers.
- **Spring physics for the mouth looked wrong.** An earlier version drove
  mouth movement with a physical spring model. It was technically smoother
  but read as mechanical and uncanny — summed Gaussian curves with simple
  linear interpolation ended up looking far more like actual speech.
- **The hardest bug wasn't in any obvious place.** A rare total hang
  turned out to be the ESP32 Arduino core's internal DNS resolver, which
  has its own ~30 second internal timeout that isn't covered by any
  HTTP-client-level timeout setting — invisible until a top-level watchdog
  was added specifically to catch it.

## Tech stack

- **Firmware**: C++17, Arduino framework, [PlatformIO](https://platformio.org/)
- **AI**: [Claude](https://www.anthropic.com/claude) (personality + tool use), OpenAI (speech-to-text), ElevenLabs (text-to-speech)
- **Wake word**: TensorFlow Lite Micro, custom-trained via [microWakeWord](https://github.com/kahrendt/microWakeWord)
- **Display**: [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)
- **Enclosure**: Fusion 360 (CAD), 3D printed

## Repo layout

- `src/` — all firmware (C++/Arduino, PlatformIO)
- `notebooks/` — the wake-word model training notebook (Colab) and the
  trained model itself
- `media/` — photos and video for this README

## License

All rights reserved — see [LICENSE](LICENSE). Shared publicly for portfolio
and demonstration purposes only; no reuse is permitted without permission.
