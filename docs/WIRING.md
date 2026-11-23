# Wiring

## The 18 connections

An OV7670 breakout has 18 pins. All of them matter; there is nothing optional
except the choice of which ESP32 GPIO each one lands on.

| OV7670 pin | Direction | Purpose |
|---|---|---|
| 3V3 | power | 3.3 V supply. **Never 5 V.** |
| GND | power | Ground |
| SIOC | ESP32 → sensor | SCCB clock (I2C-like) |
| SIOD | bidirectional | SCCB data |
| VSYNC | sensor → ESP32 | Frame boundary |
| HREF | sensor → ESP32 | Line / pixel-valid gate |
| PCLK | sensor → ESP32 | Pixel clock, one edge per byte |
| XCLK | ESP32 → sensor | System clock **into** the sensor |
| RESET | ESP32 → sensor | Active low |
| PWDN | ESP32 → sensor | Active high |
| D0–D7 | sensor → ESP32 | 8-bit parallel pixel data |

## Default map, and why these pins

`OV7670_PINS_DEFAULT` targets a 38-pin ESP32 DevKit-C:

```
SIOC  27      VSYNC 25      PCLK  22      RESET 19
SIOD  26      HREF  23      XCLK  21      PWDN  18

D0 34   D1 35   D2 32   D3 33   D4 4   D5 36   D6 39   D7 13
```

The constraints that shaped this:

- **GPIO 6–11 are off limits.** They are wired to the SPI flash. Touching them
  bricks the boot.
- **Strapping pins are avoided where possible.** GPIO 0, 2, 5, 12 and 15 are
  sampled at reset to choose the boot mode. A camera module driving them at power
  up can stop the board booting. GPIO 4 and 13 are used here because they are
  safe; 0, 2, 12 and 15 are not.
- **GPIO 34–39 are input-only**, which is exactly what data lines need, so they
  are spent on D0–D3 and D5–D6 rather than wasted.
- **GPIO 34–39 have no internal pull-ups.** That is fine for driven data lines,
  but do not put SIOC or SIOD there — SCCB needs pull-ups.

## Pull-ups

SIOC and SIOD need pull-up resistors to 3.3 V, around **4.7 kΩ**. Many cheap
breakout boards do not fit them, and the ESP32's internal pull-ups (roughly
45 kΩ) are too weak for a reliable bus at 100 kHz with any real cable length.

If you are seeing intermittent PID reads in `01_ChipIDTest`, missing pull-ups is
the first thing to suspect. As a stopgap, `cfg.i2c_port = -1` selects the
bit-banged SCCB transport, which is slower and much more tolerant.

## Signal integrity

This is a parallel bus running at several megahertz on, most likely, jumper
wires. It is the least forgiving part of the whole project.

- Keep every wire as short as you can. Under 10 cm is comfortable; 20 cm of
  unshielded ribbon is already marginal; 30 cm will fail intermittently in ways
  that look like software bugs.
- PCLK is the most timing-critical line. Give it its own short, direct route and
  keep it away from XCLK.
- Add ground returns. If you are using a ribbon cable, tie several conductors to
  ground and interleave them with the data lines.
- Decouple the sensor's 3V3 with 100 nF as close to the module as you can manage.

If capture is unreliable, **fix the wiring before you touch the code**. Raising
`cfg.clkrc_prescaler` slows PCLK down and will paper over marginal wiring at the
cost of frame rate, which is a useful diagnostic: if slowing the clock fixes it,
the problem is electrical.

## RESET and PWDN

Drive both. Do not leave them floating and do not assume the breakout ties them
off — many do not, and a floating RESET is one of the most common reasons a
sensor never enumerates over SCCB.

- **RESET** is active low. `begin()` holds it low, starts XCLK, then releases it.
- **PWDN** is active high. `begin()` holds it high (powered down) through the
  initial setup, then drops it before talking to the sensor.

If your board hard-wires them to the correct levels, set the corresponding field
in `OV7670Pins` to `-1` and the library will leave them alone.

## Power

The OV7670 is a 3.3 V part throughout — supply and I/O both. Applying 5 V to
either destroys it. Draw is modest (well under 100 mA), so the ESP32 board's
3V3 regulator is normally fine, but a long thin supply wire will brown the sensor
out under load; use a decent gauge and decouple locally.

## Choosing your own pins

Any GPIO that is not flash-connected and not a problematic strapping pin will
work — the parallel data lines go through the GPIO matrix, so there is no fixed
pinout requirement. Fill in an `OV7670Pins` struct and pass it to `begin()`:

```cpp
OV7670Pins pins = {
  .sioc = 27, .siod = 26,
  .vsync = 25, .href = 23, .pclk = 22, .xclk = 21,
  .reset = 19, .pwdn = 18,
  .d = { 34, 35, 32, 33, 4, 36, 39, 13 }   // D0 .. D7, D0 is the LSB
};
```

Getting D0–D7 in the wrong order does not stop capture; it produces an image with
scrambled colours and wrong brightness, which is a distinctive and easily
recognised symptom. See [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
