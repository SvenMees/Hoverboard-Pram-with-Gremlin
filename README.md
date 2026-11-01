[README.md](https://github.com/user-attachments/files/34239773/README.md)# Halloween Hoverthing

A **remote-controlled hoverboard** pulls a **children's pram** with a **gremlin animatronic** inside. The gremlin pops up, lights up and makes noise to scare people on Halloween. Everything is powered by the hoverboard battery. It was used successfully on Halloween 2026.

This project was designed, debugged and fine-tuned together with **Claude** (Anthropic's AI assistant, via Claude Code): it helped with the firmware choice, wiring, the Arduino sketches (zone grid, stepper, LED strip, sound), troubleshooting and this documentation. Note that Claude could not run the code on real hardware, so all testing was done by me on the actual build.

## What was built

| Part | What it does |
|---|---|
| Hoverboard | Drives via the RC remote: channels 1 and 2 control the wheels |
| Hoverboard firmware | EFeru `hoverboard-firmware-hack-FOC` on the STM32F103, `VARIANT_PWM`, `CONTROL_PWM_RIGHT` |
| Remote | FlySky FS-T4B transmitter with FS-R6B receiver |
| Arduino | Reads receiver channel 3 (pin 10) and channel 4 (pin 5) |
| Gremlin | Stepper motor via DM542 driver; goes up and comes back down |
| Gremlin bright LED | Via MOSFET (pin 12); turns on 500 ms after the gremlin starts rising, flickers while it lowers |
| LED strip | WS2812, 40 LEDs of which the first 10 always stay off |
| Sound | DY-SV17F mp3 module (software serial on pins 2 and 3) |

## Pins

| Pin | Function |
|---|---|
| D10 | Receiver channel 3 |
| D5 | Receiver channel 4 |
| D9 / D8 / D7 | DM542 PUL / DIR / ENA (ENA high = motor unpowered) |
| D12 | MOSFET for the gremlin's bright LED |
| D11 | WS2812 data through 330 Ω |
| D2 / D3 | DY-SV17F TX / RX (RX through 1 kΩ) |

## The grid

Two sticks (channels 3 and 4) form a 3x3 grid. Row c (channel 3 low) is the rest position.

| | Column 1 | Column 2 | Column 3 |
|---|---|---|---|
| **Row a** | sound 6 looping + red breathing | nothing | gremlin up, scare sound 2 (once), bright LED on 500 ms later |
| **Row b** | sound 1 looping + green breathing | sound 8 looping + blue breathing | sound 5 looping + red/green blinking (500 ms) |
| **Row c** | sound 3 looping + pink-white breathing | **rest**: nothing, gremlin down | sound 7 looping + red/green blinking (500 ms) |

Sounds are stored on the module as `00001.mp3`, `00002.mp3`, and so on. Needed: 1, 2, 3, 5, 6, 7 and 8.

## What went wrong, and how it was fixed

- **Flashing the hoverboard**: the chip was read-protected (RDP). The connection only worked after a full chip erase with the ST-Link.
- **Stepper, not servo**: the gremlin runs on a stepper motor with a DM542 driver, so the sketch was rewritten for that.
- **Pin conflict**: LED strip and MOSFET were both on pin 11. Now strip on pin 11, bright LED on pin 12.
- **Channel 4 showed no signal**: `pulseIn()` had a 25 ms timeout, but the channel 4 pulse arrives later. A **50 ms** timeout fixes it.
- **Grid logic**: hysteresis on the thresholds (1350 and 1650 µs, 40 µs margin) and 4 confirmations in a row, so the zone does not jitter.
- **Safety**: after power-up nothing happens until the stick has been in row c (rest) at least once.
- **Sound**: a sound is not restarted if the previous cell used the same one.
- **LED strip effects**: moving bars, breathing (sin² for a natural curve), blinking and red/green alternating, all on LEDs 11 to 40.

## Files

- `halloween_koets_raster_3x3.ino`: the main sketch.
- `fs_r6b_1_kanaal.ino`: test one receiver channel.
- `raster_3x3_serial.ino`: show the grid and the measured stick range on the Serial Monitor.
- `ledstrip_test.ino`: test the LED strip on its own (colours and a running dot).
- `aansluitschema_fs_r6b.svg`: receiver wiring diagram.

## Libraries

[Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) (via the Library Manager). `SoftwareSerial` ships with the Arduino IDE.

## Resources that helped

- [EFeru/hoverboard-firmware-hack-FOC](https://github.com/EFeru/hoverboard-firmware-hack-FOC): the firmware used on the hoverboard.
- [DeepWiki: RC remote control (EFeru FOC)](https://deepwiki.com/EFeru/hoverboard-firmware-hack-FOC/5.3-rc-remote-control): how the PWM remote control mode works.
- [NiklasFauth/hoverboard-firmware-hack](https://github.com/NiklasFauth/hoverboard-firmware-hack): the original hoverboard firmware hack that started it all.
- [Pionierland hoverboard hack guide](https://pionierland.de/hoverhack/): hardware and flashing background.
- Voice effects for the sounds: [Voicechanger.io](https://topai.tools/t/voicechanger-io), [Violet Recording voice changer](https://violetrecording.com/tools/voice-changer/).

## Ideas for next year

- Read the channels with interrupts for a faster response.
- Replace SoftwareSerial with hardware serial or AltSoftSerial, because it can clash with the LED strip timing.
- Record new sounds with a voice effect.
