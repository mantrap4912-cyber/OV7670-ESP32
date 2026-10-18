# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] — 2026-08-18

Initial release.

### Added

- **I2S0 camera-mode DVP capture** for the classic ESP32. One DMA descriptor per
  line, double-buffered ring, unpacking in a high-priority task so frame buffers
  may live in PSRAM.
- **SCCB transport** with two back ends: hardware I2C (Arduino `Wire`, or the
  ESP-IDF driver outside Arduino) and a bit-banged open-drain fallback selected
  with `i2c_port = -1`. Both issue register reads as two separate transactions,
  which is what the OV7670 requires.
- **LEDC XCLK generator**, started before the first SCCB access, with the
  delivered frequency reported back for verification.
- **Register presets** for QQCIF, QQVGA, QCIF, QVGA, CIF and VGA in RGB565,
  RGB555, RGB444, YUV422, grayscale and raw Bayer.
- **`OV7670Camera` class**: blocking single-frame capture, continuous streaming
  with optional double buffering, and a low-RAM per-line callback mode that
  allocates no frame buffer at all.
- **Image controls**: mirror, flip, brightness, contrast, saturation, special
  effects, white-balance presets, night mode, banding filter, manual and
  automatic exposure and gain, and the sensor's internal colour bar.
- **Capture statistics**: frame count, torn frames, DMA overruns, SCCB errors and
  a rolling frame rate.
- **Eight examples**, ordered as milestones: chip ID test, still capture over
  serial, TFT live view, WiFi stream server, continuous benchmark, image
  controls, long-run stress test, and a DNS captive-portal camera.
- **`tools/decode_frame.py`** to turn a serial dump into a PNG, with a
  dependency-free netpbm fallback.
- **Packaging** for Arduino IDE (`library.properties`), PlatformIO
  (`library.json`) and ESP-IDF (`CMakeLists.txt`, `idf_component.yml`).
- **Documentation**: wiring guide, symptom-indexed troubleshooting guide, and a
  clocks/timing/memory-bandwidth reference.

### Known limitations

- The register tables and the DMA capture path have **not been validated against
  a physical OV7670 module**. They are built from documented hardware behaviour
  and well-established reference settings, but first-run reports from real
  hardware are needed and very welcome.
- The **ESP32-S3 LCD_CAM back end is experimental** and gated behind
  `-DOV7670_ENABLE_LCDCAM_EXPERIMENTAL`. It is complete code but unverified, and
  LCD_CAM register field names have moved between ESP-IDF releases.
- **ESP32-S2, C3 and C6 are unsupported** — they have no parallel camera
  peripheral this library can drive.
- There is no JPEG encoder. The OV7670 has none in hardware, and software
  encoding on an ESP32 costs more than the capture does.
