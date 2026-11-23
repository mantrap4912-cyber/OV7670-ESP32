/*
 * 02_StillCaptureSerial - milestone M3: the one that proves the whole idea.
 *
 * Captures a single QQVGA RGB565 frame over I2S DMA and dumps it to the serial
 * port as base64 between marker lines. tools/decode_frame.py turns that back
 * into a PNG on your PC.
 *
 * If the decoded image is recognisable, the DVP capture path works and every
 * later feature is incremental. If it is noise, stop and fix it here - see
 * docs/TROUBLESHOOTING.md, which is organised by what the broken image looks
 * like (diagonal tearing, half a frame, all black, and so on).
 *
 * Send 'c' over serial to capture another frame, or 'b' to toggle the sensor's
 * internal colour bar - that is the fastest way to tell a capture problem
 * (bars come out wrong) from an optics or lighting problem (bars are perfect).
 *
 * Usage:
 *   1. Flash, open the serial monitor at 115200, copy everything between the
 *      BEGIN and END markers to a file called frame.txt
 *   2. python tools/decode_frame.py frame.txt -o frame.png
 */

#include <OV7670.h>

static OV7670Pins pins = OV7670_PINS_DEFAULT;
static OV7670Camera camera;
static bool colorbar = false;

static const char b64chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Stream base64 straight out of the frame buffer, 60 characters per line, so
// nothing has to be buffered a second time.
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

static void captureAndDump()
{
    Serial.println(F("capturing..."));
    uint32_t t0 = millis();

    if (!camera.captureFrame(2000)) {
        Serial.println(F("capture FAILED (timeout)"));
        Serial.println(F("  - is PCLK connected? Without it the DMA engine"));
        Serial.println(F("    never receives a sample clock and nothing moves."));
        Serial.println(F("  - is VSYNC connected? Frame boundaries come from it."));
        Serial.println(F("  - try cfg.vsync_invert = true if your board buffers"));
        Serial.println(F("    and inverts the sync lines."));
        return;
    }

    size_t len = 0;
    uint8_t *fb = camera.getFrameBuffer(&len);
    if (!fb) {
        Serial.println(F("no frame buffer"));
        return;
    }

    OV7670Stats st;
    camera.getStats(&st);

    Serial.printf("captured %ux%u %s, %u bytes in %u ms (%u lines received)\n",
                  camera.width(), camera.height(),
                  ov7670_pixformat_name(camera.pixelFormat()),
                  (unsigned)len, (unsigned)(millis() - t0),
                  (unsigned)dvp_last_line_count());
    Serial.printf("torn frames so far: %u, DMA overruns: %u\n",
                  (unsigned)st.frames_torn, (unsigned)st.dma_overruns);

    Serial.printf("---BEGIN FRAME %u %u %s---\n",
                  camera.width(), camera.height(),
                  ov7670_pixformat_name(camera.pixelFormat()));
    dumpBase64(fb, len);
    Serial.println(F("---END FRAME---"));

    camera.releaseFrameBuffer();
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println(F("=== OV7670 still capture (milestone M3) ==="));

    OV7670Config cfg;
    ov7670_config_defaults(&cfg);
    cfg.frame_size   = OV7670_FRAMESIZE_QQVGA;   // 160x120, 38400 bytes
    cfg.pixel_format = OV7670_PIXFORMAT_RGB565;
    cfg.xclk_freq_hz = 16000000;
    // Halving the sensor's internal clock slows PCLK down, which makes the
    // capture far more tolerant of long or unshielded jumper wires. Raise it
    // to 0 once you have a picture and want more frames per second.
    cfg.clkrc_prescaler = 1;

    esp_err_t rc = camera.begin(pins, cfg);
    if (rc != ESP_OK) {
        Serial.printf("begin() failed: %s\n", esp_err_to_name(rc));
        Serial.println(F("run 01_ChipIDTest first - it isolates SCCB problems."));
        while (true) delay(1000);
    }

    Serial.println(F("press 'c' to capture, 'b' to toggle the colour bar"));
    captureAndDump();
}

void loop()
{
    if (!Serial.available()) { delay(20); return; }

    int c = Serial.read();
    if (c == 'c' || c == 'C') {
        captureAndDump();
    } else if (c == 'b' || c == 'B') {
        colorbar = !colorbar;
        camera.setColorBar(colorbar);
        Serial.printf("colour bar %s\n", colorbar ? "ON" : "OFF");
        delay(200);          // let a couple of frames go by
        captureAndDump();
    }
}
