/*
 * 01_ChipIDTest - milestone M0/M1: prove SCCB and XCLK before anything else.
 *
 * This sketch does not capture a single pixel. It starts XCLK, brings the
 * sensor out of reset, and reads the read-only ID registers 100 times in a
 * row. That is the acceptance test from TASKS.MD milestone M0:
 *
 *     PID (0x0A) must read 0x76 and VER (0x0B) must read 0x73, reliably.
 *
 * If this does not pass, nothing further will work, and the fault is in
 * wiring, power or the clock - not in the capture code. Work the checklist
 * this sketch prints before moving on.
 *
 * Wiring: see docs/WIRING.md. Only SIOC, SIOD, XCLK, RESET, PWDN, 3V3 and GND
 * need to be connected for this test; the data bus is unused.
 */

#include <OV7670.h>

// Adjust to match your board. See docs/WIRING.md.
static OV7670Pins pins = OV7670_PINS_DEFAULT;

static OV7670Camera camera;

static void printChecklist()
{
    Serial.println();
    Serial.println(F("The sensor did not answer. Check, in this order:"));
    Serial.println(F("  1. 3V3 and GND actually reach the module. Measure them."));
    Serial.println(F("     The OV7670 is a 3.3 V part - 5 V will damage it."));
    Serial.println(F("  2. XCLK is running. Scope GPIO or trust the frequency"));
    Serial.println(F("     printed above; without a clock the sensor is dead."));
    Serial.println(F("  3. SIOC and SIOD are not swapped."));
    Serial.println(F("  4. Pull-ups (4.7k) on SIOC and SIOD to 3V3. Many"));
    Serial.println(F("     breakout boards do not fit them."));
    Serial.println(F("  5. RESET is high and PWDN is low. Floating is not the"));
    Serial.println(F("     same as low - drive them or tie them off."));
    Serial.println(F("  6. Ribbon cable seated the right way round."));
    Serial.println(F("  7. Try i2c_port = -1 to use the bit-banged SCCB path,"));
    Serial.println(F("     which tolerates sloppier bus timing."));
    Serial.println();
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println(F("=== OV7670 chip ID test (milestone M0/M1) ==="));
    Serial.printf("library %s, capture back end: %s\n",
                  OV7670_LIB_VERSION, camera.backendName());

    OV7670Config cfg;
    ov7670_config_defaults(&cfg);
    cfg.xclk_freq_hz = 16000000;
    cfg.sccb_freq_hz = 100000;
    // Smallest possible frame so begin() cannot fail on memory here.
    cfg.frame_size = OV7670_FRAMESIZE_QQVGA;

    esp_err_t rc = camera.begin(pins, cfg);

    Serial.printf("XCLK requested %u Hz, LEDC delivered %u Hz\n",
                  (unsigned)cfg.xclk_freq_hz,
                  (unsigned)camera.xclkFrequency());

    if (rc != ESP_OK) {
        Serial.printf("begin() failed: %s\n", esp_err_to_name(rc));
        printChecklist();
        return;
    }

    Serial.println(F("begin() succeeded. Hammering the ID registers..."));

    int ok = 0;
    uint8_t pid = 0, ver = 0;
    for (int i = 0; i < 100; i++) {
        if (camera.probe(&pid, &ver)) {
            ok++;
        } else {
            Serial.printf("  read %3d FAILED (PID=0x%02X VER=0x%02X)\n", i, pid, ver);
        }
        delay(5);
    }

    Serial.printf("\n%d / 100 reads returned PID=0x76 VER=0x73\n", ok);
    if (ok == 100) {
        Serial.println(F("M0 PASSED - SCCB and XCLK are solid."));
        Serial.println(F("Move on to 02_StillCaptureSerial."));
    } else {
        Serial.println(F("M0 FAILED - intermittent SCCB. Usually weak or"));
        Serial.println(F("missing pull-ups, or an SCCB clock that is too fast."));
        Serial.println(F("Try cfg.sccb_freq_hz = 50000, or cfg.i2c_port = -1."));
    }

    // Also dump the manufacturer ID, which is a second independent check.
    uint8_t midh = 0, midl = 0;
    camera.readReg(REG_MIDH, &midh);
    camera.readReg(REG_MIDL, &midl);
    Serial.printf("manufacturer ID: 0x%02X%02X (expected 0x7FA2)\n", midh, midl);
}

void loop()
{
    delay(1000);
}
