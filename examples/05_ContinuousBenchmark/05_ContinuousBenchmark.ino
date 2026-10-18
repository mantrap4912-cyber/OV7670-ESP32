/*
 * 05_ContinuousBenchmark - milestone M5: measure what this actually achieves.
 *
 * Walks every resolution and pixel format the library supports, runs each one
 * in continuous mode for a few seconds, and prints the frame rate, the RAM it
 * needed and how many frames came out torn. The table it produces is the
 * honest answer to "how fast is it?" on *your* board with *your* wiring - the
 * numbers vary a lot with PCLK, cable length and whether you have PSRAM.
 *
 * Configurations that do not fit in RAM are reported as skipped rather than
 * failing the run, so a plain WROOM board still produces a useful table.
 */

#include <OV7670.h>

static OV7670Pins pins = OV7670_PINS_DEFAULT;
static OV7670Camera camera;

static const ov7670_framesize_t SIZES[] = {
    OV7670_FRAMESIZE_QQCIF,
    OV7670_FRAMESIZE_QQVGA,
    OV7670_FRAMESIZE_QCIF,
    OV7670_FRAMESIZE_QVGA,
    OV7670_FRAMESIZE_CIF,
    OV7670_FRAMESIZE_VGA,
};

static const ov7670_pixformat_t FORMATS[] = {
    OV7670_PIXFORMAT_RGB565,
    OV7670_PIXFORMAT_YUV422,
    OV7670_PIXFORMAT_GRAYSCALE,
};

static const uint32_t MEASURE_MS = 4000;

static void banner()
{
    Serial.println();
    Serial.println(F("size   format     bytes    fps   torn  overrun  free heap"));
    Serial.println(F("------------------------------------------------------------"));
}

static void runOne(ov7670_framesize_t fs, ov7670_pixformat_t fmt)
{
    size_t need = ov7670_frame_bytes(fs, fmt);

    if (!camera.setPixelFormat(fmt) || !camera.setResolution(fs)) {
        Serial.printf("%-6s %-9s %7u  --- could not configure (likely out of RAM)\n",
                      ov7670_framesize_name(fs), ov7670_pixformat_name(fmt),
                      (unsigned)need);
        return;
    }

    camera.resetStats();
    if (!camera.startStreaming()) {
        Serial.printf("%-6s %-9s %7u  --- streaming failed\n",
                      ov7670_framesize_name(fs), ov7670_pixformat_name(fmt),
                      (unsigned)need);
        return;
    }

    // Let AGC/AEC settle and the rolling average populate before measuring.
    uint32_t t0 = millis();
    while (millis() - t0 < 800) camera.waitFrame(500);

    camera.resetStats();
    uint32_t frames = 0;
    t0 = millis();
    while (millis() - t0 < MEASURE_MS) {
        if (camera.waitFrame(1000)) {
            frames++;
            camera.releaseFrameBuffer();
        }
    }
    uint32_t elapsed = millis() - t0;
    camera.stopStreaming();

    OV7670Stats st;
    camera.getStats(&st);

    float fps = (elapsed > 0) ? (frames * 1000.0f / elapsed) : 0.0f;
    Serial.printf("%-6s %-9s %7u %6.2f %6u %8u %10u\n",
                  ov7670_framesize_name(fs), ov7670_pixformat_name(fmt),
                  (unsigned)need, fps,
                  (unsigned)st.frames_torn, (unsigned)st.dma_overruns,
                  (unsigned)ESP.getFreeHeap());
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println(F("=== OV7670 continuous capture benchmark (milestone M5) ==="));

    OV7670Config cfg;
    ov7670_config_defaults(&cfg);
    cfg.frame_size   = OV7670_FRAMESIZE_QQVGA;
    cfg.pixel_format = OV7670_PIXFORMAT_RGB565;
    cfg.fb_count     = 2;
    cfg.clkrc_prescaler = 0;      // fastest PCLK - this is a benchmark

    esp_err_t rc = camera.begin(pins, cfg);
    if (rc != ESP_OK) {
        Serial.printf("begin() failed: %s\n", esp_err_to_name(rc));
        while (true) delay(1000);
    }

    Serial.printf("back end: %s\n", camera.backendName());
    Serial.printf("XCLK: %u Hz, CLKRC prescaler: %u\n",
                  (unsigned)camera.xclkFrequency(), cfg.clkrc_prescaler);
    Serial.printf("free heap at start: %u, PSRAM: %u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getPsramSize());
    Serial.printf("each configuration is measured for %u ms with 2 buffers\n",
                  (unsigned)MEASURE_MS);
    banner();

    for (size_t f = 0; f < sizeof(FORMATS) / sizeof(FORMATS[0]); f++) {
        for (size_t s = 0; s < sizeof(SIZES) / sizeof(SIZES[0]); s++) {
            runOne(SIZES[s], FORMATS[f]);
        }
    }

    Serial.println(F("------------------------------------------------------------"));
    Serial.println(F("done. A non-zero 'torn' column means lines were lost;"));
    Serial.println(F("raise cfg.dma_node_count or cfg.clkrc_prescaler."));
}

void loop()
{
    delay(1000);
}
