/*
 * 09_ESP32DevModuleSelfTest - guided bring-up for the ESP32 Dev Module
 *                             (works on both the 30-pin and the 38-pin board).
 *
 * Board setting in the Arduino IDE:  Tools -> Board -> "ESP32 Dev Module"
 *
 * This sketch wires nothing new: it uses the same pin map as every other
 * example, written out explicitly below so you can check it against your
 * breadboard and change a single line if you move a wire. Every GPIO used here
 * is broken out on the 30-pin board (D5 and D6 are the pins labelled VP and VN).
 *
 *   OV7670   ESP32      OV7670   ESP32
 *   SIOC     GPIO27     D0       GPIO34
 *   SIOD     GPIO26     D1       GPIO35
 *   VSYNC    GPIO25     D2       GPIO32
 *   HREF     GPIO23     D3       GPIO33
 *   PCLK     GPIO22     D4       GPIO4
 *   XCLK     GPIO21     D5       GPIO36 (VP)
 *   RESET    GPIO19     D6       GPIO39 (VN)
 *   PWDN     GPIO18     D7       GPIO13
 *   3V3      3V3 (3.3 V ONLY)    GND  GND
 *   + 4.7k pull-ups from SIOC and SIOD to 3V3.
 *
 * What it does, in order, and stops at the first failure with a hint:
 *
 *   1. Starts XCLK and talks to the sensor        (checks SCCB + XCLK wiring)
 *   2. Switches on the sensor's built-in colour bar and captures a frame
 *      - line count == frame height?               (checks VSYNC / HREF / PCLK)
 *      - does a row show several distinct colours? (checks D0..D7 and PCLK edge)
 *      - are the rows identical to each other?     (checks for tearing / shear)
 *   3. Switches to the real image and captures 20 frames, reporting fps, torn
 *      frames and DMA overruns.
 *
 * The colour bar does not depend on lens, focus or lighting, so a pass here
 * means the whole wiring + DMA path is good. If step 2 passes but the picture
 * is poor, the problem is optics or lighting, not the capture path.
 *
 * Serial commands (115200 baud):
 *   t  run the self-test again
 *   c  dump one frame as base64 (decode with tools/decode_frame.py)
 *   b  toggle the colour bar and dump a frame
 *   v  toggle vsync_invert and re-run   (try this if capture times out)
 *   h  toggle href_invert  and re-run
 *   p  toggle pclk_invert  and re-run   (try this if colours are wrong/noisy)
 *   k  slow the pixel clock (clkrc_prescaler +1) and re-run (long/flaky wires)
 */

#include <OV7670.h>

// ---- Pin map: edit here if you wired differently --------------------------
static OV7670Pins pins = {
  /* sioc  */ 27,
  /* siod  */ 26,
  /* vsync */ 25,
  /* href  */ 23,
  /* pclk  */ 22,
  /* xclk  */ 21,
  /* reset */ 19,
  /* pwdn  */ 18,
  /* d0..7 */ { 34, 35, 32, 33, 4, 36, 39, 13 }
};

static OV7670Camera camera;
static OV7670Config cfg;
static bool colorbar = false;

// ---------------------------------------------------------------------------

static const char b64chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void dumpBase64(const uint8_t *data, size_t len)
{
    size_t i = 0;
    int col = 0;
    while (i < len) {
        uint32_t v = (uint32_t)data[i] << 16;
        int have = 1;
        if (i + 1 < len) { v |= (uint32_t)data[i + 1] << 8; have++; }
        if (i + 2 < len) { v |= (uint32_t)data[i + 2];      have++; }
        i += 3;

        char out[4];
        out[0] = b64chars[(v >> 18) & 0x3F];
        out[1] = b64chars[(v >> 12) & 0x3F];
        out[2] = (have > 1) ? b64chars[(v >> 6) & 0x3F] : '=';
        out[3] = (have > 2) ? b64chars[v & 0x3F]        : '=';
        Serial.write(out, 4);

        col += 4;
        if (col >= 60) { Serial.println(); col = 0; }
    }
    if (col) Serial.println();
}

static void dumpFrame()
{
    if (!camera.captureFrame(2000)) {
        Serial.println(F("capture FAILED (timeout)"));
        return;
    }
    size_t len = 0;
    uint8_t *fb = camera.getFrameBuffer(&len);
    if (!fb) { Serial.println(F("no frame buffer")); return; }

    Serial.printf("---BEGIN FRAME %u %u %s---\n", camera.width(), camera.height(),
                  ov7670_pixformat_name(camera.pixelFormat()));
    dumpBase64(fb, len);
    Serial.println(F("---END FRAME---"));
    camera.releaseFrameBuffer();
}

static inline uint16_t px565(const uint8_t *fb, size_t stride, int x, int y)
{
    const uint8_t *p = fb + (size_t)y * stride + (size_t)x * 2;
    return ((uint16_t)p[0] << 8) | p[1];          // sensor sends high byte first
}

// ---------------------------------------------------------------------------
// Steps
// ---------------------------------------------------------------------------

static bool stepInit()
{
    Serial.println(F("\n[1/3] Starting XCLK and probing the sensor..."));
    camera.end();

    esp_err_t rc = camera.begin(pins, cfg);
    if (rc != ESP_OK) {
        Serial.printf("  FAIL: begin() returned %s\n", esp_err_to_name(rc));
        if (rc == ESP_ERR_TIMEOUT) {
            Serial.println(F("  -> no SCCB answer. Check SIOC=27 / SIOD=26, the 4.7k pull-ups,"));
            Serial.println(F("     3.3 V power and GND. XCLK (GPIO21) must reach the sensor."));
        } else if (rc == ESP_ERR_NOT_FOUND) {
            Serial.println(F("  -> SCCB works but the ID is wrong: SIOC/SIOD swapped, or not an OV7670."));
        } else if (rc == ESP_ERR_NO_MEM) {
            Serial.println(F("  -> out of memory; use a smaller frame_size."));
        }
        return false;
    }

    uint8_t pid = 0, ver = 0;
    camera.probe(&pid, &ver);
    Serial.printf("  OK: PID=0x%02X VER=0x%02X (expect 0x76 0x73), XCLK %u Hz, backend: %s\n",
                  pid, ver, (unsigned)camera.xclkFrequency(), camera.backendName());
    return true;
}

static bool stepColorBar()
{
    Serial.println(F("[2/3] Colour-bar capture (independent of lens and light)..."));
    camera.setColorBar(true);
    colorbar = true;
    delay(300);                                   // let a few frames pass
    camera.resetStats();

    if (!camera.captureFrame(2000)) {
        Serial.println(F("  FAIL: capture timed out."));
        Serial.println(F("  -> PCLK (22), VSYNC (25) and HREF (23) are the suspects."));
        Serial.println(F("     Try 'v' (vsync_invert) then 'h' (href_invert)."));
        camera.setColorBar(false);
        colorbar = false;
        return false;
    }

    size_t len = 0;
    uint8_t *fb = camera.getFrameBuffer(&len);
    if (!fb || camera.pixelFormat() != OV7670_PIXFORMAT_RGB565) {
        Serial.println(F("  FAIL: no RGB565 frame buffer."));
        camera.setColorBar(false);
        colorbar = false;
        return false;
    }

    const int w = camera.width(), h = camera.height();
    const size_t stride = (size_t)w * 2;
    bool ok = true;

    // 1. Line count.
    unsigned lines = dvp_last_line_count();
    Serial.printf("  lines received: %u of %d -> %s\n", lines, h,
                  lines == (unsigned)h ? "OK" : "FAIL");
    if (lines != (unsigned)h) {
        ok = false;
        Serial.println(F("    -> wrong line count: HREF polarity/wiring, or the DMA ring was overrun."));
    }

    // 2. Distinct colours along a middle row (the bar has 8 vertical bars).
    const int ry = h / 2;
    uint16_t seen[16];
    int distinct = 0;
    for (int x = 0; x < w; x++) {
        uint16_t v = px565(fb, stride, x, ry);
        bool found = false;
        for (int i = 0; i < distinct; i++) {
            // Compare on the top bits so slight noise does not count as a new colour.
            if ((seen[i] & 0xF79E) == (v & 0xF79E)) { found = true; break; }
        }
        if (!found && distinct < 16) seen[distinct++] = v;
    }
    Serial.printf("  distinct colours on row %d: %d (expect ~8 flat bars) -> %s\n",
                  ry, distinct, (distinct >= 6 && distinct <= 12) ? "OK" : "CHECK");
    if (distinct < 6) {
        ok = false;
        Serial.println(F("    -> too few colours: a data line is stuck or shorted, or PCLK/HREF is wrong."));
        Serial.println(F("       Try 'p' (pclk_invert). Check D0..D7 against the pin map."));
    } else if (distinct > 12) {
        ok = false;
        Serial.println(F("    -> too many colours: data lines swapped/noisy or PCLK edge wrong."));
        Serial.println(F("       Try 'p', or 'k' to slow the clock, or shorten the wires."));
    }

    // 3. Vertical bars -> every row should match the middle row.
    int bad_rows = 0;
    for (int y = 4; y < h - 4; y += 4) {
        int diff = 0;
        for (int x = 0; x < w; x += 4) {
            if ((px565(fb, stride, x, y) & 0xF79E) != (px565(fb, stride, x, ry) & 0xF79E)) diff++;
        }
        if (diff > (w / 4) / 6) bad_rows++;
    }
    Serial.printf("  rows differing from the middle row: %d -> %s\n", bad_rows,
                  bad_rows <= 2 ? "OK" : "FAIL");
    if (bad_rows > 2) {
        ok = false;
        Serial.println(F("    -> image is torn/sheared. Use 'k' (slower PCLK) or shorten the wires."));
    }

    camera.releaseFrameBuffer();
    camera.setColorBar(false);
    colorbar = false;
    return ok;
}

static bool stepStream()
{
    Serial.println(F("[3/3] Live image: capturing 20 frames..."));
    delay(300);
    camera.resetStats();

    int got = 0;
    uint32_t t0 = millis();
    for (int i = 0; i < 20; i++) {
        if (camera.captureFrame(1500)) {
            got++;
            camera.releaseFrameBuffer();
        }
    }
    uint32_t ms = millis() - t0;

    OV7670Stats st;
    camera.getStats(&st);
    Serial.printf("  frames: %d/20, %u ms total (%.1f fps incl. overhead)\n",
                  got, (unsigned)ms, ms ? got * 1000.0f / ms : 0.0f);
    Serial.printf("  torn: %u, DMA overruns: %u, SCCB errors: %u\n",
                  (unsigned)st.frames_torn, (unsigned)st.dma_overruns,
                  (unsigned)st.sccb_errors);
    Serial.printf("  free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());

    bool ok = (got == 20) && st.frames_torn == 0 && st.dma_overruns == 0;
    if (!ok) {
        Serial.println(F("  -> frames lost or torn: raise cfg.dma_node_count, press 'k', or shorten wires."));
    }
    return ok;
}

static void runSelfTest()
{
    Serial.println(F("\n=============== OV7670 self-test ==============="));
    Serial.printf("vsync_invert=%d href_invert=%d pclk_invert=%d clkrc_prescaler=%u\n",
                  cfg.vsync_invert, cfg.href_invert, cfg.pclk_invert, cfg.clkrc_prescaler);

    bool ok = stepInit() && stepColorBar() && stepStream();

    Serial.println(ok ? F("\nRESULT: PASS - wiring and capture path are good.\n"
                          "Send 'c' to dump a frame, then decode it with tools/decode_frame.py.")
                      : F("\nRESULT: FAIL - see the hint above, fix, then press 't'. "
                          "docs/TROUBLESHOOTING.md has more."));
}

void setup()
{
    Serial.begin(115200);
    delay(800);

    ov7670_config_defaults(&cfg);
    cfg.frame_size      = OV7670_FRAMESIZE_QQVGA;   // 160x120, 38.4 kB: fits any board
    cfg.pixel_format    = OV7670_PIXFORMAT_RGB565;
    cfg.xclk_freq_hz    = 16000000;
    cfg.clkrc_prescaler = 1;                        // conservative for jumper wires
    cfg.use_psram       = false;                    // plain ESP32 Dev Module has none
    cfg.fb_count        = 1;

    Serial.println(F("\nOV7670 NonFIFO - ESP32 Dev Module self-test"));
    Serial.println(F("Commands: t=retest c=dump b=colorbar v/h/p=invert vsync/href/pclk k=slower clock"));
    runSelfTest();
}

void loop()
{
    if (!Serial.available()) { delay(20); return; }
    int c = Serial.read();

    switch (c) {
    case 't': case 'T':
        runSelfTest();
        break;
    case 'c': case 'C':
        dumpFrame();
        break;
    case 'b': case 'B':
        colorbar = !colorbar;
        camera.setColorBar(colorbar);
        Serial.printf("colour bar %s\n", colorbar ? "ON" : "OFF");
        delay(300);
        dumpFrame();
        break;
    case 'v': case 'V':
        cfg.vsync_invert = !cfg.vsync_invert;
        runSelfTest();
        break;
    case 'h': case 'H':
        cfg.href_invert = !cfg.href_invert;
        runSelfTest();
        break;
    case 'p': case 'P':
        cfg.pclk_invert = !cfg.pclk_invert;
        runSelfTest();
        break;
    case 'k': case 'K':
        if (cfg.clkrc_prescaler < 8) cfg.clkrc_prescaler++;
        runSelfTest();
        break;
    default:
        break;
    }
}
