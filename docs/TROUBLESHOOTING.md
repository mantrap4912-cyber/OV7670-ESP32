# Troubleshooting

Organised by symptom. Work top to bottom — the earlier sections are
prerequisites for the later ones, and almost every "capture is broken" report
turns out to be one of the first three items.

---

## `begin()` fails, PID reads 0x00 or the sensor never answers

**By far the most likely cause: XCLK is not reaching the sensor.**

The OV7670 contains no oscillator. Every internal state machine, including the
SCCB slave that is supposed to answer you, is clocked from XCLK. Before the clock
runs, the sensor is not slow to respond — it is electrically incapable of
responding. This library starts LEDC before the first SCCB byte for exactly this
reason, but the clock still has to physically arrive.

Check, in order:

1. **Power.** Measure 3.3 V at the module itself, not at the ESP32. The OV7670 is
   a 3.3 V part; 5 V destroys it.
2. **XCLK.** `01_ChipIDTest` prints the frequency LEDC actually produced. If that
   looks right, scope the pin, or bridge it temporarily to an LED-visible divider.
   The acceptance target is within 5% of the requested value.
3. **SIOC / SIOD not swapped.** Easy to do and produces exactly this symptom.
4. **Pull-ups.** 4.7 kΩ from each of SIOC and SIOD to 3V3. Many breakout boards
   omit them; the ESP32's internal pull-ups are too weak to be relied on.
5. **RESET high, PWDN low.** Driven, not floating. Floating is not the same as
   low.
6. **Ribbon orientation.** Trivially easy to get backwards.

If all of that checks out, try the bit-banged SCCB transport, which tolerates
sloppier bus timing than the hardware I2C peripheral:

```cpp
cfg.i2c_port = -1;
```

## PID is right sometimes and garbage other times

The bus is marginal rather than broken.

- Fit or lower the pull-ups.
- Slow the bus: `cfg.sccb_freq_hz = 50000`.
- Switch to the bit-banged transport: `cfg.i2c_port = -1`.
- Shorten the SCCB wires.

`01_ChipIDTest` reads the ID 100 times precisely so this shows up as a count
rather than as luck.

---

## `captureFrame()` always times out

SCCB works (you got past `begin()`), so the sensor is configured — nothing is
arriving on the pixel bus.

1. **PCLK not connected.** Without it the DMA engine gets no sample clock and
   nothing ever moves. This is the single most common cause.
2. **VSYNC not connected.** Frame boundaries come from a GPIO interrupt on it. No
   VSYNC edges, no frames.
3. **VSYNC polarity.** Some breakouts buffer and invert the sync lines. Try:
   ```cpp
   cfg.vsync_invert = true;
   ```
4. **HREF polarity.** If VSYNC edges are arriving but no lines are, HREF may be
   gating the data off rather than on:
   ```cpp
   cfg.href_invert = true;
   ```
5. **The sensor is asleep.** Check PWDN really is low.

A quick way to tell edges are arriving at all: attach a plain
`attachInterrupt()` counter to VSYNC in a scratch sketch and print the count. It
should be roughly the frame rate.

---

## The image is wrong — diagnose by what it looks like

First, always: **turn on the sensor's colour bar.**

```cpp
camera.setColorBar(true);
```

The colour bar is generated inside the sensor, downstream of the imaging array
and upstream of the output pins. So:

- **Bars come out clean** → the capture path, DMA, wiring and unpacking are all
  correct. Your problem is sensor configuration, lens, focus or lighting.
- **Bars come out wrong** → the problem is in capture. Match the distortion
  against the list below.

### Diagonal shear — the image slants progressively across the frame

Lines are being lost or duplicated, so each row starts at the wrong offset.

- Raise `cfg.dma_node_count` (try 8 or 16) to give the CPU more slack.
- Raise `cfg.clkrc_prescaler` to slow PCLK down.
- Shorten the wiring. If slowing the clock fixes it, the fault is electrical.
- Check the `dma_overruns` and `frames_torn` counters from `getStats()`; if
  either is climbing, this is what you are looking at.

### Only the top part of the image is filled, the rest is black

Fewer lines arrived than the configured height. `dvp_last_line_count()` tells you
how many. Causes: HREF dropping out mid-frame (electrical), or a sensor window
configuration that produces fewer rows than expected.

### Colours are scrambled, brightness is wrong, but the structure is visible

**D0–D7 are in the wrong order.** The image geometry comes from VSYNC/HREF/PCLK,
which are evidently fine; only the bit weighting is wrong. Recheck the data pin
mapping — `d[0]` is D0, the least significant bit.

### Colours are inverted or the image is blue/orange

Byte order. The OV7670 emits RGB565 high byte first; most displays and decoders
want the opposite.

```cpp
ov7670_swap_bytes(fb, len);              // in your own code
tft.setSwapBytes(true);                  // or let TFT_eSPI do it
python tools/decode_frame.py --swap ...  // or the decoder
```

### The image is shifted horizontally and wraps around

Windowing registers. HSTART/HSTOP carry 11 bits split across their own register
and the low 3 bits of HREF; VSTART/VSTOP carry 10 bits split with VREF. The
library packs these correctly, so if you have been writing windowing registers by
hand, that is where to look.

### Uniform grey, black or white noise, and the colour bar is also wrong

Nothing meaningful is being sampled. Recheck PCLK, HREF and at least a couple of
data lines with a scope. Verify `cfg.pclk_invert` — if the data is being sampled
on the wrong clock edge you get exactly this.

### Horizontal bands that drift slowly

Mains flicker beating against the frame rate. Set the banding filter to match
your grid:

```cpp
camera.setBandingFilter(OV7670_BANDING_50HZ);  // or _60HZ
```

---

## Out of memory

```
frame buffer of 614400 bytes could not be allocated
```

Expected at VGA on a board without PSRAM — a WROOM has around 320 kB of SRAM
total and never all of it contiguous once WiFi is up. Options, best first:

1. Use a smaller frame. QQVGA (38 kB) and QVGA (154 kB) are comfortable.
2. Use `GRAYSCALE`, which halves the buffer for free — the luma channel is
   extracted during DMA unpacking rather than post-processed.
3. Use `OV7670_CAPTURE_LINE_CALLBACK`, which allocates no frame buffer at all.
   This is how you get VGA out of a WROOM: straight to a display or a socket.
4. Use a WROVER-class board and set `cfg.use_psram = true`.

Note the DMA ring is separate and must be internal RAM regardless; it is small
(a few kB) but it does have to be contiguous.

---

## Frame rate is lower than expected

- Drop `cfg.clkrc_prescaler` to 0 for the fastest PCLK.
- Raise `cfg.xclk_freq_hz` towards 24 MHz.
- Use `cfg.fb_count = 2` so capture overlaps with your processing.
- Check `frames_torn` — if the driver is discarding work, fix that first.
- Remember what you are asking for: VGA RGB565 is 614 kB per frame. Moving that
  anywhere, over WiFi especially, is the bottleneck, not the capture.

`05_ContinuousBenchmark` measures every combination on your actual board.

---

## Heap slowly shrinks over hours

Run `07_StressTest`, which rotates through resolutions to exercise the
teardown/rebuild path and reports the heap every 30 seconds. If the free-heap
column trends down, that is a genuine leak — please
[open an issue](https://github.com/mantrap4912-cyber/OV7670-ESP32/issues) with the
log.

---

## Compile errors

**`#error "ESP32-S3 support ... is experimental and disabled by default"`** —
intentional. The S3 back end exists but has not been validated on hardware.
Either add `-DOV7670_ENABLE_LCDCAM_EXPERIMENTAL` and help test it, or use
Espressif's `esp32-camera` component.

**`#error "OV7670-ESP32-NonFIFO requires a parallel camera peripheral"`** — the
ESP32-S2, C3 and C6 have no peripheral this library can drive.

**Undefined references to `I2S0` or `lldesc_t`** — the classic-ESP32 back end is
being compiled for a target it does not support. Check your board selection.
