/*
 * 03_LiveViewTFT - live view on an SPI TFT (ILI9341 / ST7789 / similar).
 *
 * Two ways to get pixels onto a display are shown, selected by the switch
 * below. They trade RAM against timing margin, and which one you want depends
 * on your board:
 *
 *   USE_LINE_CALLBACK == 0  (default, recommended)
 *       Capture a whole frame into RAM, then push it to the TFT. QQVGA RGB565
 *       is 38 kB, which fits comfortably in internal RAM on a plain WROOM.
 *       The SPI transfer happens between frames, so it cannot disturb capture.
 *
 *   USE_LINE_CALLBACK == 1  (lowest RAM)
 *       No frame buffer at all - each line is pushed to the TFT as it is
 *       unpacked. This is what you want at QVGA or larger on a board without
 *       PSRAM. The catch is timing: the callback must finish inside one line
 *       period or the DMA ring overruns and the image tears. If that happens,
 *       raise cfg.dma_node_count, raise cfg.clkrc_prescaler to slow PCLK, or
 *       lower the resolution.
 *
 * Requires the TFT_eSPI library, configured for your display in its
 * User_Setup.h. Any display library will do - only the two push calls below
 * are TFT_eSPI-specific.
 */

#include <OV7670.h>

#define USE_LINE_CALLBACK 0

#if __has_include(<TFT_eSPI.h>)
#include <TFT_eSPI.h>
#define HAVE_TFT 1
static TFT_eSPI tft = TFT_eSPI();
#else
#define HAVE_TFT 0
#endif

static OV7670Pins pins = OV7670_PINS_DEFAULT;
static OV7670Camera camera;

static const ov7670_framesize_t FRAME_SIZE = OV7670_FRAMESIZE_QQVGA;
static uint16_t g_width, g_height;
static int16_t  g_x0, g_y0;      // where the image sits on the panel

#if USE_LINE_CALLBACK
// Called from the capture task once per line. Keep it short.
static void onLine(const uint8_t *line, size_t len, uint16_t row, void *ctx)
{
#if HAVE_TFT
    // The sensor emits RGB565 high byte first; TFT_eSPI wants the native
    // order, so let it swap during the push rather than touching the buffer.
    tft.pushImage(g_x0, g_y0 + row, g_width, 1, (uint16_t *)line);
#endif
}
#endif

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println(F("=== OV7670 live view on TFT ==="));

#if !HAVE_TFT
    Serial.println(F("TFT_eSPI was not found."));
    Serial.println(F("Install it and configure User_Setup.h for your display,"));
    Serial.println(F("or adapt the two pushImage() calls to your own library."));
    Serial.println(F("The camera side of this sketch still runs."));
#else
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setSwapBytes(true);       // sensor is big-endian, panel is not
#endif

    OV7670Config cfg;
    ov7670_config_defaults(&cfg);
    cfg.frame_size   = FRAME_SIZE;
    cfg.pixel_format = OV7670_PIXFORMAT_RGB565;
    cfg.clkrc_prescaler = 1;

#if USE_LINE_CALLBACK
    cfg.capture_mode = OV7670_CAPTURE_LINE_CALLBACK;
    cfg.dma_node_count = 8;       // extra slack for the SPI writes
    camera.onLine(onLine, nullptr);
#else
    cfg.capture_mode = OV7670_CAPTURE_FULL_FRAME;
    cfg.fb_count = 2;             // capture the next frame while pushing this one
#endif

    esp_err_t rc = camera.begin(pins, cfg);
    if (rc != ESP_OK) {
        Serial.printf("begin() failed: %s\n", esp_err_to_name(rc));
        while (true) delay(1000);
    }

    g_width  = camera.width();
    g_height = camera.height();

#if HAVE_TFT
    g_x0 = (tft.width()  - (int16_t)g_width)  / 2;
    g_y0 = (tft.height() - (int16_t)g_height) / 2;
    if (g_x0 < 0) g_x0 = 0;
    if (g_y0 < 0) g_y0 = 0;
#endif

    Serial.printf("streaming %ux%u\n", g_width, g_height);
    camera.startStreaming();
}

void loop()
{
    static uint32_t last_report = 0;

#if USE_LINE_CALLBACK
    // Lines are being pushed from the callback; nothing to do here but let
    // the frame counter tick over so the report below stays meaningful.
    camera.waitFrame(1000);
#else
    if (camera.waitFrame(1000)) {
        size_t len = 0;
        uint8_t *fb = camera.getFrameBuffer(&len);
#if HAVE_TFT
        if (fb) tft.pushImage(g_x0, g_y0, g_width, g_height, (uint16_t *)fb);
#endif
        camera.releaseFrameBuffer();
    }
#endif

    if (millis() - last_report > 2000) {
        last_report = millis();
        OV7670Stats st;
        camera.getStats(&st);
        Serial.printf("%.1f fps, frames %u, torn %u, overruns %u\n",
                      st.fps, (unsigned)st.frames_captured,
                      (unsigned)st.frames_torn, (unsigned)st.dma_overruns);
    }
}
