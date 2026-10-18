# Contributing

## The most valuable contribution right now

**Reports from real hardware.** The capture path and register tables in this
library are built from documented behaviour and established reference settings,
but they have not been validated against a physical OV7670. If you run the
examples on real hardware, please open an issue saying what happened — whether it
worked or not.

A useful report includes:

- Board (WROOM / WROVER / DevKit variant), and whether it has PSRAM
- Arduino-ESP32 core or ESP-IDF version
- Your pin map, if it differs from `OV7670_PINS_DEFAULT`
- Which example, and how far through the milestones you got
- The serial output, especially the line `01_ChipIDTest` prints about XCLK
- For image problems: a decoded frame, plus whether the colour bar
  (`camera.setColorBar(true)`) comes out clean

That last one is the single most informative diagnostic, because it splits the
problem cleanly into "capture path" versus "sensor configuration".

## Working on the ESP32-S3 back end

`src/dvp_capture_lcdcam.cpp` is complete but unverified. It is gated behind
`-DOV7670_ENABLE_LCDCAM_EXPERIMENTAL` so nobody gets it by accident. If you have
an S3 and want to help:

1. Build with the flag and report what happens, including compile errors — the
   LCD_CAM register field names have shifted between ESP-IDF versions and knowing
   which version works is half the job.
2. If you get it capturing, a PR promoting it out of experimental status, with
   the IDF versions you tested, would be excellent.

## Code style

Match what is already there rather than any external guide.

- 4-space indent, no tabs, roughly 88 columns.
- Comments explain *why*, not *what*. The register tables are the exception:
  there, comments record where a magic value came from.
- Public API is `camelCase` methods on `OV7670Camera`; the C layers underneath
  use `snake_case` with a `sccb_` / `xclk_` / `dvp_` prefix.
- Anything that runs in an ISR must be `IRAM_ATTR` and must not touch cached
  memory. Anything that runs per-line should be measured before it is added.

## Register tables

If you are adding or fixing entries in `src/ov7670_presets.cpp`, say where the
values came from in the commit message. Provenance matters here — a register
table with no source is a table nobody can debug later. Do not delete
undocumented "reserved" writes because they look pointless; several of them are
load-bearing.

## Adding an example

Examples are numbered in milestone order and each one should prove a specific
capability. Keep them self-contained, keep external library dependencies behind
`__has_include` so the whole set still compiles in CI, and make failure messages
diagnostic rather than decorative — an example that just prints "failed" wastes
the reader's time.

## Fast local compile loop

`tools/syntax_check.sh` type-checks the whole library against the real ESP32
toolchain in a couple of seconds per file, without needing `arduino-cli` or a
full sketch build. It reuses the compiler and flag files the Arduino ESP32 core
already installed, so the include paths and defines match a real build.

```sh
./tools/syntax_check.sh                    # library sources, esp32
./tools/syntax_check.sh esp32 --examples   # and every example sketch
./tools/syntax_check.sh esp32s3            # experimental S3 back end
```

Set `ARDUINO15` if your Arduino data directory is somewhere unusual.

## Before opening a pull request

- Run `./tools/syntax_check.sh esp32 --examples` and get a clean result.
- Confirm every example still compiles for `esp32:esp32:esp32`.
- Update `CHANGELOG.md`.
- If you changed the public API, update `keywords.txt` and the API sketch in
  `README.md`.

## Licence

Contributions are accepted under the MIT licence that covers the rest of the
project.
