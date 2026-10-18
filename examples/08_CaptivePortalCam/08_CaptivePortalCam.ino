/*
 * 08_CaptivePortalCam - live camera view over a DNS captive portal.
 *
 * No router, no credentials, no typing an IP address. The ESP32 brings up its
 * own open access point and runs a DNS server that answers *every* query with
 * its own address. Phones and laptops probe a known URL right after joining a
 * network to check whether they are behind a paywall; because our DNS sends
 * that probe to us and we answer it with a redirect, the operating system pops
 * up its "sign in to network" sheet, which lands straight on the camera view.
 *
 * How to use it:
 *   1. Flash and power the board.
 *   2. On a phone, join the WiFi network "OV7670-CAM" (open, no password).
 *   3. The captive portal sheet opens by itself. If it does not, browse to
 *      any http:// address, or to http://4.3.2.1
 *
 * Notes worth knowing:
 *   - Captive portal detection only works over plain HTTP. If the sheet does
 *     not appear, the phone probably tried an HTTPS probe; just open any
 *     http:// URL manually.
 *   - Some Android versions will report "no internet" and offer to disconnect.
 *     Choose to stay connected.
 *   - The AP is open on purpose so that joining is one tap. Anyone in range
 *     can see the camera. Set AP_PASSWORD below if that matters; note that a
 *     password does not stop the portal from working, it just adds a step.
 */

#include <OV7670.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

static const char *AP_SSID     = "OV7670-CAM";
static const char *AP_PASSWORD = nullptr;   // nullptr = open network

// A tidy, memorable address that does not collide with common home subnets.
// Not const: older cores declare IPAddress::toString() without const.
static IPAddress AP_IP(4, 3, 2, 1);
static IPAddress AP_MASK(255, 255, 255, 0);

static const byte DNS_PORT = 53;

static OV7670Pins pins = OV7670_PINS_DEFAULT;
static OV7670Camera camera;
static WebServer server(80);
static DNSServer dns;

static const char PORTAL_HTML[] PROGMEM = R"HTML(
<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OV7670 Camera</title>
<style>
 :root{color-scheme:dark}
 body{background:#0e0f12;color:#e6e6e6;font:15px/1.5 system-ui,-apple-system,sans-serif;
      margin:0;padding:16px;display:flex;flex-direction:column;align-items:center;gap:14px}
 h1{font-size:18px;margin:0;font-weight:600}
 img{image-rendering:pixelated;width:min(94vw,560px);aspect-ratio:4/3;
     border-radius:10px;border:1px solid #2a2d34;background:#000;display:block}
 .row{display:flex;gap:8px;flex-wrap:wrap;justify-content:center}
 button{background:#1d2027;color:#e6e6e6;border:1px solid #333842;border-radius:8px;
        padding:9px 14px;font:inherit;cursor:pointer}
 button:active{background:#282c35}
 #stats{font-family:ui-monospace,SFMono-Regular,monospace;font-size:12px;color:#7f9}
</style>
<h1>OV7670 &middot; live</h1>
<img id="v" alt="camera view">
<div class="row">
  <button onclick="fx('mirror')">Mirror</button>
  <button onclick="fx('flip')">Flip</button>
  <button onclick="fx('bar')">Colour bar</button>
  <button onclick="fx('bw')">B&amp;W</button>
  <button onclick="fx('color')">Colour</button>
</div>
<div id="stats">starting...</div>
<script>
const img = document.getElementById('v');
let n = 0, t0 = performance.now();
function next(){ img.src = '/frame.bmp?n=' + (n++); }
img.onload = () => {
  if (n % 8 === 0) {
    const fps = 8000 / (performance.now() - t0);
    t0 = performance.now();
    document.getElementById('stats').textContent = fps.toFixed(1) + ' fps';
  }
  next();
};
img.onerror = () => setTimeout(next, 600);
function fx(what){ fetch('/set?a=' + what); }
next();
</script>
)HTML";

/* ---------------------------------------------------------------------------
 * BMP encoding, shared with 04_WiFiStreamServer
 * ------------------------------------------------------------------------ */

static void writeBmpHeader(uint8_t *hdr, uint16_t w, uint16_t h, uint32_t pixel_bytes)
{
    memset(hdr, 0, 54);
    uint32_t filesize = 54 + pixel_bytes;
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = filesize & 0xFF; hdr[3] = (filesize >> 8) & 0xFF;
    hdr[4] = (filesize >> 16) & 0xFF; hdr[5] = (filesize >> 24) & 0xFF;
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = w & 0xFF; hdr[19] = (w >> 8) & 0xFF;
    hdr[22] = h & 0xFF; hdr[23] = (h >> 8) & 0xFF;
    hdr[26] = 1;
    hdr[28] = 24;
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
    if (!fb) { server.send(500, "text/plain", "no buffer"); return; }

    const uint16_t w = camera.width();
    const uint16_t h = camera.height();
    const uint32_t row_bytes = ((uint32_t)w * 3 + 3) & ~3U;
    const uint32_t pixel_bytes = row_bytes * h;

    uint8_t hdr[54];
    writeBmpHeader(hdr, w, h, pixel_bytes);

    server.setContentLength(54 + pixel_bytes);
    server.send(200, "image/bmp", "");
    server.sendContent((const char *)hdr, sizeof(hdr));

    uint8_t *row = (uint8_t *)malloc(row_bytes);
    if (!row) { camera.releaseFrameBuffer(); return; }
    memset(row, 0, row_bytes);

    for (int y = h - 1; y >= 0; y--) {          // BMP is bottom-up
        const uint8_t *src = fb + (size_t)y * w * 2;
        uint8_t *dst = row;
        for (uint16_t x = 0; x < w; x++) {
            uint8_t r, g, b;
            ov7670_rgb565_to_rgb888(src + x * 2, &r, &g, &b);
            *dst++ = b; *dst++ = g; *dst++ = r;
        }
        server.sendContent((const char *)row, row_bytes);
    }
    free(row);
    camera.releaseFrameBuffer();
}

/* ---------------------------------------------------------------------------
 * Captive portal plumbing
 * ------------------------------------------------------------------------ */

static void handlePortal()
{
    server.send_P(200, "text/html", PORTAL_HTML);
}

// Every unmatched request - including the OS connectivity probes - gets a
// redirect to the portal page. Sending a 302 rather than the page itself is
// what convinces iOS and Android that this really is a captive network.
static void handleNotFound()
{
    String url = "http://";
    url += AP_IP.toString();
    url += "/";
    server.sendHeader("Location", url, true);
    server.send(302, "text/plain", "");
}

static void handleSet()
{
    static bool mirror = false, flip = false, bar = false;
    String a = server.arg("a");

    if      (a == "mirror") { mirror = !mirror; camera.setMirror(mirror); }
    else if (a == "flip")   { flip   = !flip;   camera.setVFlip(flip); }
    else if (a == "bar")    { bar    = !bar;    camera.setColorBar(bar); }
    else if (a == "bw")     { camera.setSpecialEffect(OV7670_EFFECT_BW); }
    else if (a == "color")  { camera.setSpecialEffect(OV7670_EFFECT_NONE); }
    else { server.send(400, "text/plain", "unknown action"); return; }

    server.send(200, "text/plain", "ok");
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println(F("=== OV7670 captive portal camera ==="));

    OV7670Config cfg;
    ov7670_config_defaults(&cfg);
    cfg.frame_size   = OV7670_FRAMESIZE_QQVGA;
    cfg.pixel_format = OV7670_PIXFORMAT_RGB565;
    cfg.fb_count     = 2;
    cfg.clkrc_prescaler = 1;

    esp_err_t rc = camera.begin(pins, cfg);
    if (rc != ESP_OK) {
        Serial.printf("begin() failed: %s\n", esp_err_to_name(rc));
        Serial.println(F("run 01_ChipIDTest to isolate the problem."));
        while (true) delay(1000);
    }
    camera.startStreaming();

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
    bool ap_ok = AP_PASSWORD ? WiFi.softAP(AP_SSID, AP_PASSWORD)
                             : WiFi.softAP(AP_SSID);
    if (!ap_ok) {
        Serial.println(F("softAP() failed"));
        while (true) delay(1000);
    }
    delay(200);   // let the AP settle before the DNS socket binds

    // "*" matches every domain, so all name lookups resolve to us.
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    if (!dns.start(DNS_PORT, "*", AP_IP)) {
        Serial.println(F("DNS server failed to start - the portal will not"));
        Serial.println(F("pop up, but http://4.3.2.1 still works."));
    }

    server.on("/", handlePortal);
    server.on("/frame.bmp", handleFrame);
    server.on("/set", handleSet);

    // The probe URLs each platform uses. Routing them explicitly is not
    // strictly necessary given handleNotFound(), but it documents what is
    // going on and avoids a redirect hop on the common cases.
    server.on("/generate_204", handleNotFound);          // Android
    server.on("/gen_204", handleNotFound);               // Android, older
    server.on("/hotspot-detect.html", handleNotFound);   // iOS / macOS
    server.on("/library/test/success.html", handleNotFound);
    server.on("/connecttest.txt", handleNotFound);       // Windows
    server.on("/ncsi.txt", handleNotFound);              // Windows
    server.on("/canonical.html", handleNotFound);        // Firefox
    server.on("/success.txt", handleNotFound);           // Firefox
    server.onNotFound(handleNotFound);

    server.begin();

    Serial.printf("AP up: SSID \"%s\"%s\n", AP_SSID,
                  AP_PASSWORD ? "" : " (open)");
    Serial.print(F("portal at http://"));
    Serial.println(AP_IP);
    Serial.println(F("join the network from a phone; the sign-in sheet"));
    Serial.println(F("should open the camera view by itself."));
}

void loop()
{
    dns.processNextRequest();
    server.handleClient();
}
