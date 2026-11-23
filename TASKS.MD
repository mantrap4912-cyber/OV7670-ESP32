# MASTER PROMPT — ESP32 ↔ OV7670 (Non‑FIFO) Camera Library

Paste this whole document into an AI coding assistant (Claude Code, Cursor, etc.) as your
opening instruction, or work through it yourself milestone by milestone. It's written so
you can hand it one section (milestone) at a time instead of asking for "the whole
library" in one shot — that's the #1 reason these builds stall.

---

## ROLE / FRAMING (give this to the AI verbatim)

> You are an embedded systems engineer experienced with ESP32 low-level peripherals
> (I2S0 camera/DVP mode, LCD_CAM, LEDC clock generation), SCCB/I2C sensor protocols,
> and DMA-based parallel data capture. Build a clean, reusable, documented C/C++
> library that interfaces a **bare OV7670 camera module with no AL422B FIFO chip**
> directly to an ESP32, using hardware DMA — not GPIO bit-banging — to capture the
> parallel pixel stream. Work in the milestone order given below; do not skip ahead
> to streaming/WiFi/display code before frame capture is verified working.

---

## 1. Hardware context (so the AI doesn't guess wrong)

- **Sensor:** OV7670, SCCB control (I2C-like) on `SIOC`/`SIOD`, write address `0x42`,
  read address `0x43` (7-bit address `0x21`).
- **Signals:** `XCLK` (ESP32 → sensor, must supply 8–24 MHz *before* any SCCB write),
  `PCLK` (sensor → ESP32, pixel clock), `VSYNC`, `HREF`, `D0–D7` (8-bit parallel data),
  plus `RESET` and `PWDN` control lines.
- **No internal buffer.** Every pixel byte must be captured in lock-step with PCLK or
  it's gone — there is nothing downstream holding the data for you.
- **Resolutions:** up to VGA (640×480), scaled down internally to QVGA (320×240),
  QQVGA (160×120), CIF, etc.
- **Output formats:** YUV422 (power-on default), RGB565, RGB555, Bayer RAW — selected
  via `COM7`/`COM15` and related registers.

## 2. Why "no FIFO" is hard, and the correct fix

Bit-banging `digitalRead()` on 8 GPIOs fast enough to catch PCLK transitions is **not
feasible** on ESP32 — the sensor's pixel clock is multiple MHz, far faster than GPIO
polling in software.

The actual solution (used by every working non-FIFO project, including the official
esp32-camera driver for FIFO-based sensors):

- **Classic ESP32 (WROOM/WROVER):** use the **I2S0 peripheral's camera/DVP mode**.
  It samples the 8-bit parallel bus on PCLK edges, gated by VSYNC/HREF, and DMAs the
  result into RAM automatically. This *is* your FIFO — implemented in hardware/DMA
  instead of an external chip.
- **ESP32-S2/S3:** I2S doesn't have camera mode; use the **LCD_CAM peripheral**
  (camera receive mode) instead. Same idea, different peripheral — check which chip
  you're targeting before writing capture code.
- **Memory:** a full VGA RGB565 frame is 640×480×2 = **614 KB**. A no-PSRAM ESP32
  only has ~320 KB SRAM total, so full-frame VGA capture is impossible without a
  PSRAM (WROVER-class) board. Plan for QQVGA/QVGA on plain boards, or line-by-line
  streaming instead of full-frame buffering (this is what most hobby non-FIFO
  projects do).

## 3. Deliverables — what the finished library must contain

1. **`sccb.{h,cpp}`** — register read/write over SCCB, init at low clock (~100 kHz),
   with retry/timeout handling.
2. **`xclk.{h,cpp}`** — LEDC-based clock generator supplying XCLK to the sensor at a
   configurable frequency.
3. **`ov7670_regs.h`** — register addresses and bitmasks (from the OV7670 datasheet).
4. **`ov7670_presets.{h,cpp}`** — known-good register tables per resolution/format
   combo (QQVGA/QVGA/CIF × RGB565/YUV422).
5. **`dvp_capture.{h,cpp}`** — the I2S0 (or LCD_CAM) DMA parallel-capture driver,
   double-buffered.
6. **`OV7670Camera` class** — high-level API (see §4).
7. **Examples**, in order of complexity:
   - `ChipIDTest` — SCCB-only sanity check (read PID/VER registers).
   - `StillCaptureSerial` — one QQVGA frame dumped as hex/base64 over serial for
     offline decoding.
   - `LiveViewTFT` — line-by-line streaming to an SPI TFT (low RAM footprint).
   - `WiFiStreamServer` — frames pushed over HTTP multipart or WebSocket.
8. **Packaging** — `library.properties` (Arduino) *and* `CMakeLists.txt` /
   `idf_component.yml` (ESP-IDF), so it installs cleanly either way.
9. **`README.md`** — wiring/pin table, timing notes, resolution/memory table, known
   limitations.

## 4. High-level API shape (guide the design, don't lock exact syntax)

```cpp
struct OV7670Pins {
  int sioc, siod, vsync, href, pclk, xclk, reset, pwdn;
  int d[8]; // D0..D7
};

struct OV7670Config {
  framesize_t frame_size;    // FRAMESIZE_QQVGA, QVGA, CIF...
  pixformat_t pixel_format;  // RGB565, YUV422
  int  xclk_freq_hz;         // e.g. 16'000'000
  bool use_psram;
};

class OV7670Camera {
public:
  bool begin(const OV7670Pins& pins, const OV7670Config& cfg);
  bool captureFrame();
  uint8_t* getFrameBuffer(size_t* len);
  void releaseFrameBuffer();
  bool setResolution(framesize_t fs);
  bool setPixelFormat(pixformat_t fmt);
  void setMirror(bool en);
  void setVFlip(bool en);
  void softReset();
};
```

## 5. Milestone plan — build in this exact order

Don't let the AI (or yourself) jump to "let's stream over WiFi" before M3 is proven.

- **M0 — SCCB bring-up.** Read-only. Success = PID register returns `0x76` and VER
  returns `0x73`, reliably, 100 times in a row.
- **M1 — XCLK generation.** Confirm a stable clock is actually reaching the sensor
  (scope/logic analyzer, or indirectly by M0 only succeeding once XCLK is running).
- **M2 — Register init.** Push the default register table, soft-reset, confirm no
  SCCB NACKs.
- **M3 — Smallest possible capture.** QQVGA (160×120) RGB565, one blocking frame via
  I2S DMA, dumped raw over serial and decoded offline (e.g. a small Python script) to
  confirm it's a real image, not noise. **This is the milestone that proves the whole
  approach works — don't move on until a real image comes out.**
- **M4 — More resolutions/formats.** Add QVGA, CIF, YUV422.
- **M5 — Continuous capture.** Double buffering, measure actual achievable frame rate.
- **M6 — Image controls.** Brightness/contrast/AWB/mirror/flip registers.
- **M7 — Packaging.** Examples, README, `library.properties`, ESP-IDF manifest.
- **M8 — Stress test.** Long-run stability, torn-frame handling, XCLK glitch recovery.

## 6. Acceptance checklist

- [ ] SCCB read of PID/VER matches datasheet values
- [ ] Measured XCLK frequency within 5% of configured value
- [ ] A captured QQVGA frame, saved and opened as an image, is visually recognizable
- [ ] Zero dropped/malformed frames over 100 consecutive captures
- [ ] Documented RAM usage per resolution/format, with and without PSRAM
- [ ] Verified on both a non-PSRAM WROOM board (QQVGA only) and a WROVER/PSRAM board
- [ ] Examples build in both Arduino IDE and PlatformIO

## 7. Pitfalls to explicitly avoid

- Trying to catch PCLK edges with `digitalRead()` — physically too slow.
- Writing SCCB registers before XCLK is running — the sensor has no clock to respond.
- Confusing the 7-bit SCCB address (`0x21`) with the write byte (`0x42`).
- Leaving `RESET`/`PWDN` floating instead of driven — sensor may not enumerate.
- Attempting to buffer a full VGA frame on a non-PSRAM board.
- Ignoring VSYNC/HREF polarity — some breakout boards invert these; make it
  configurable rather than hard-coded.

## 8. Prior art to study (for design ideas — respect their licenses, don't copy-paste)

- `bitluni/ESP32CameraI2S` — I2S DMA capture + TFT/web live view, the original
  reference most later non-FIFO projects build on.
- `kobatan/OV7670-ESP32` — Arduino library specifically framed as "OV7670 (non-FIFO)
  for ESP32," line-by-line capture, live view + WebSocket streaming examples.
- `alankrantas/OV7670-ESP32-TFT` — non-FIFO OV7670 to ILI9341 TFT.
- `Issafalcon/IoTCameraWithESP32` — PlatformIO project, non-FIFO OV7670 over WiFi.

## 9. How to actually use this document

1. Paste the **ROLE** block plus **§1–§4** as your first message to the AI coding tool.
2. Ask it to implement **only M0**, and don't move on until you've physically verified
   PID/VER over serial on real hardware.
3. Paste **M1**, then **M2**, etc., one at a time, each time confirming the previous
   milestone's checklist item before continuing.
4. Keep §7 (pitfalls) pinned in the conversation — remind the AI of it whenever a
   milestone stalls, since most non-FIFO OV7670 failures trace back to one of those six
   items.