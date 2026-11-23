/*
 * 07_StressTest - milestone M8: long-run stability and recovery.
 *
 * Captures continuously for hours and reports every 30 seconds. It is looking
 * for the three things that kill these projects after they "work":
 *
 *   1. Torn frames - lines lost because the CPU could not drain the DMA ring
 *      in time. A handful under load is survivable; a steady trickle means the
 *      margins are too tight for your PCLK.
 *   2. Slow leaks - the free-heap column must be flat. A downward trend means
 *      something is not being released on the reconfigure path.
 *   3. Wedging - a sensor that stops producing VSYNC after a glitch and never
 *      recovers. This sketch detects a stall and re-initialises the camera,
 *      then counts how often that was needed.
 *
 * It also periodically hops between resolutions and formats, because tearing
 * down and rebuilding the capture path is where resource leaks show up.
 *
 * The acceptance target from TASKS.MD is zero dropped or malformed frames over
 * 100 consecutive captures; this runs far past that on purpose.
 */

#include <OV7670.h>

static OV7670Pins pins = OV7670_PINS_DEFAULT;
static OV7670Camera camera;
static OV7670Config cfg;

static uint32_t total_frames = 0;
static uint32_t recoveries   = 0;
static uint32_t stalls       = 0;
static uint32_t reconfigs    = 0;
static uint32_t heap_at_start = 0;
static uint32_t heap_min = 0xFFFFFFFF;
static uint32_t t_start = 0;

// Rotate through configurations that fit on a board without PSRAM.
static const ov7670_framesize_t ROTATE_SIZES[] = {
    OV7670_FRAMESIZE_QQVGA,
    OV7670_FRAMESIZE_QCIF,
    OV7670_FRAMESIZE_QVGA,
};
static size_t rotate_idx = 0;

static bool bringUp()
{
    esp_err_t rc = camera.begin(pins, cfg);
    if (rc != ESP_OK) {
        Serial.printf("begin() failed: %s\n", esp_err_to_name(rc));
        return false;
    }
    return camera.startStreaming();
}

// A stalled sensor produces no VSYNC at all, so waitFrame() just times out.
// Tearing the whole thing down and starting again is the only reliable cure -
// a soft reset alone does not help if XCLK itself glitched.
static void recover()
{
    Serial.println(F("!! capture stalled - reinitialising the camera"));
    recoveries++;
    camera.end();
    delay(200);
    if (!bringUp()) {
        Serial.println(F("!! recovery failed, retrying in 2 s"));
        delay(2000);
    }
}

static void report()
{
    OV7670Stats st;
    camera.getStats(&st);

    uint32_t heap = ESP.getFreeHeap();
    if (heap < heap_min) heap_min = heap;

    uint32_t secs = (millis() - t_start) / 1000;
    Serial.printf("[%6us] %-5s %-9s frames %8u  %.1f fps | torn %5u  "
                  "overrun %5u  sccb_err %4u | stalls %3u recov %3u "
                  "reconf %3u | heap %6u (min %6u, start %6u)\n",
                  (unsigned)secs,
                  ov7670_framesize_name(camera.frameSize()),
                  ov7670_pixformat_name(camera.pixelFormat()),
                  (unsigned)total_frames, st.fps,
                  (unsigned)st.frames_torn, (unsigned)st.dma_overruns,
                  (unsigned)st.sccb_errors,
                  (unsigned)stalls, (unsigned)recoveries, (unsigned)reconfigs,
                  (unsigned)heap, (unsigned)heap_min, (unsigned)heap_at_start);

    if (heap + 8192 < heap_at_start) {
        Serial.println(F("!! heap has dropped more than 8 kB below the start "
                         "value - suspect a leak"));
    }
}

// Exercise the reconfiguration path, which is where handles and buffers leak.
static void rotateConfig()
{
    rotate_idx = (rotate_idx + 1) % (sizeof(ROTATE_SIZES) / sizeof(ROTATE_SIZES[0]));
    ov7670_framesize_t fs = ROTATE_SIZES[rotate_idx];

    Serial.printf("-- switching to %s\n", ov7670_framesize_name(fs));
    if (!camera.setResolution(fs)) {
        Serial.println(F("   setResolution() failed (probably out of RAM); "
                         "staying put"));
        return;
    }
    reconfigs++;
    camera.startStreaming();
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println(F("=== OV7670 stress test (milestone M8) ==="));
    Serial.println(F("leave this running for hours; it reports every 30 s"));

    ov7670_config_defaults(&cfg);
    cfg.frame_size   = ROTATE_SIZES[0];
    cfg.pixel_format = OV7670_PIXFORMAT_RGB565;
    cfg.fb_count     = 2;
    cfg.clkrc_prescaler = 1;

    if (!bringUp()) {
        while (true) delay(1000);
    }

    heap_at_start = ESP.getFreeHeap();
    heap_min = heap_at_start;
    t_start = millis();
    Serial.printf("back end: %s, XCLK %u Hz, heap %u\n",
                  camera.backendName(), (unsigned)camera.xclkFrequency(),
                  (unsigned)heap_at_start);
}

void loop()
{
    static uint32_t last_report = 0;
    static uint32_t last_rotate = 0;
    static uint32_t consecutive_timeouts = 0;

    if (camera.waitFrame(1000)) {
        total_frames++;
        consecutive_timeouts = 0;

        // Touch the frame so the compiler cannot optimise the read away and so
        // a buffer that was freed underneath us would fault loudly here.
        size_t len = 0;
        volatile uint8_t *fb = camera.getFrameBuffer(&len);
        if (fb && len) {
            volatile uint8_t sink = fb[0] ^ fb[len - 1] ^ fb[len / 2];
            (void)sink;
        }
        camera.releaseFrameBuffer();
    } else {
        stalls++;
        if (++consecutive_timeouts >= 3) {
            recover();
            consecutive_timeouts = 0;
        }
    }

    if (millis() - last_report >= 30000) {
        last_report = millis();
        report();
    }

    // Every five minutes, tear the capture path down and rebuild it.
    if (millis() - last_rotate >= 300000) {
        last_rotate = millis();
        rotateConfig();
    }
}
