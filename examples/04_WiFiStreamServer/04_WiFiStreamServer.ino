/*
 * 04_WiFiStreamServer - stream frames over HTTP to a browser.
 *
 * The OV7670 has no JPEG encoder, so there is nothing to hand out as MJPEG
 * without compressing on the ESP32 first. This example takes the simpler and
 * far more reliable route: it serves each frame as an uncompressed 24-bit BMP,
 * generated on the fly straight out of the RGB565 frame buffer, and the page
 * refreshes an <img> as fast as the link allows.
 *
 * That is honest about the bandwidth involved. A QQVGA BMP is 57 kB, so over a
 * decent WiFi link you get roughly 10-15 frames a second. QVGA is 230 kB per
 * frame and will feel slow - it is a good demonstration of why real camera
 * projects want a sensor with hardware JPEG.
 *
 * Endpoints:
 *   /            live view page
 *   /frame.bmp   one frame as a BMP
 *   /stats       capture statistics as JSON
 *
 * Set your WiFi credentials below. For a version that needs no router at all,
 * see 08_CaptivePortalCam.
 */

#include <OV7670.h>
#include <WiFi.h>
#include <WebServer.h>

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASS = "your-password";

static OV7670Pins pins = OV7670_PINS_DEFAULT;
static OV7670Camera camera;
static WebServer server(80);

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OV7670 live</title>
<style>
 body{background:#111;color:#ddd;font:14px system-ui,sans-serif;
      margin:0;display:flex;flex-direction:column;align-items:center;gap:12px;padding:16px}
 img{image-rendering:pixelated;width:min(90vw,640px);border:1px solid #333;background:#000}
 #stats{font-family:ui-monospace,monospace;color:#8b8}
</style>
<h2>OV7670 live view</h2>
<img id="v" alt="camera">
<div id="stats">connecting...</div>
<script>
const img = document.getElementById('v');
let n = 0, t0 = performance.now();
function next() {
  // Cache-bust so the browser actually refetches each frame.
  img.src = '/frame.bmp?n=' + (n++);
}
img.onload = () => {
  if (n % 10 === 0) {
    const fps = 10000 / (performance.now() - t0);
    t0 = performance.now();
    document.getElementById('stats').textContent = fps.toFixed(1) + ' fps client-side';
  }
  next();
};
img.onerror = () => setTimeout(next, 500);
next();
</script>
)HTML";

static void handleRoot()
{
    server.send_P(200, "text/html", INDEX_HTML);
}

// Write a 24-bit BMP header for a bottom-up image.
static void writeBmpHeader(uint8_t *hdr, uint16_t w, uint16_t h, uint32_t pixel_bytes)
{
    memset(hdr, 0, 54);
    uint32_t filesize = 54 + pixel_bytes;
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = filesize & 0xFF; hdr[3] = (filesize >> 8) & 0xFF;
    hdr[4] = (filesize >> 16) & 0xFF; hdr[5] = (filesize >> 24) & 0xFF;
    hdr[10] = 54;                       // pixel data offset
    hdr[14] = 40;                       // BITMAPINFOHEADER size
    hdr[18] = w & 0xFF; hdr[19] = (w >> 8) & 0xFF;
    hdr[22] = h & 0xFF; hdr[23] = (h >> 8) & 0xFF;
    hdr[26] = 1;                        // planes
    hdr[28] = 24;                       // bits per pixel
    hdr[34] = pixel_bytes & 0xFF; hdr[35] = (pixel_bytes >> 8) & 0xFF;
    hdr[36] = (pixel_bytes >> 16) & 0xFF; hdr[37] = (pixel_bytes >> 24) & 0xFF;
}

static void handleFrame()
{
    if (!camera.waitFrame(2000)) {
        server.send(503, "text/plain", "capture timeout");
        return;
    }

    size_t len = 0;
    uint8_t *fb = camera.getFrameBuffer(&len);
    if (!fb) {
        server.send(500, "text/plain", "no frame buffer");
        return;
    }

    const uint16_t w = camera.width();
    const uint16_t h = camera.height();
    // BMP rows are padded to a 4-byte boundary.
    const uint32_t row_bytes = ((uint32_t)w * 3 + 3) & ~3U;
    const uint32_t pixel_bytes = row_bytes * h;

    uint8_t hdr[54];
    writeBmpHeader(hdr, w, h, pixel_bytes);

    server.setContentLength(54 + pixel_bytes);
    server.send(200, "image/bmp", "");
    server.sendContent((const char *)hdr, sizeof(hdr));

    // One row at a time, bottom-up, converting RGB565 -> BGR888 as we go.
    // Only a single row is ever buffered, so this costs a few hundred bytes
    // no matter how big the frame is.
    uint8_t *row = (uint8_t *)malloc(row_bytes);
    if (!row) {
        camera.releaseFrameBuffer();
        return;
    }
    memset(row, 0, row_bytes);

    for (int y = h - 1; y >= 0; y--) {
        const uint8_t *src = fb + (size_t)y * w * 2;
        uint8_t *dst = row;
        for (uint16_t x = 0; x < w; x++) {
            uint8_t r, g, b;
            ov7670_rgb565_to_rgb888(src + x * 2, &r, &g, &b);
            *dst++ = b;         // BMP stores BGR
            *dst++ = g;
            *dst++ = r;
        }
        server.sendContent((const char *)row, row_bytes);
    }

    free(row);
    camera.releaseFrameBuffer();
}

static void handleStats()
{
    OV7670Stats st;
    camera.getStats(&st);
    char json[256];
    snprintf(json, sizeof(json),
             "{\"fps\":%.2f,\"frames\":%u,\"torn\":%u,\"dropped\":%u,"
             "\"overruns\":%u,\"sccb_errors\":%u,\"heap\":%u}",
             st.fps, (unsigned)st.frames_captured, (unsigned)st.frames_torn,
             (unsigned)st.frames_dropped, (unsigned)st.dma_overruns,
             (unsigned)st.sccb_errors, (unsigned)ESP.getFreeHeap());
    server.send(200, "application/json", json);
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println(F("=== OV7670 WiFi stream server ==="));

    OV7670Config cfg;
    ov7670_config_defaults(&cfg);
    cfg.frame_size   = OV7670_FRAMESIZE_QQVGA;
    cfg.pixel_format = OV7670_PIXFORMAT_RGB565;
    cfg.fb_count     = 2;
    cfg.clkrc_prescaler = 1;

    esp_err_t rc = camera.begin(pins, cfg);
    if (rc != ESP_OK) {
        Serial.printf("begin() failed: %s\n", esp_err_to_name(rc));
        while (true) delay(1000);
    }
    camera.startStreaming();

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print(F("connecting to WiFi"));
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
        delay(400);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("WiFi failed - check the credentials at the top of"));
        Serial.println(F("this sketch, or use 08_CaptivePortalCam instead,"));
        Serial.println(F("which needs no router."));
        while (true) delay(1000);
    }

    Serial.print(F("open http://"));
    Serial.println(WiFi.localIP());

    server.on("/", handleRoot);
    server.on("/frame.bmp", handleFrame);
    server.on("/stats", handleStats);
    server.begin();
}

void loop()
{
    server.handleClient();
}
