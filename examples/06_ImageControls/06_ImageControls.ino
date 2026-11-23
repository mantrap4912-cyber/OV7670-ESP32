/*
 * 06_ImageControls - milestone M6: exercise every image control interactively.
 *
 * Streams continuously and lets you drive brightness, contrast, saturation,
 * mirror, flip, white balance, special effects, night mode and the exposure
 * and gain loops from the serial monitor. Press '?' for the menu.
 *
 * Press 'd' to dump one frame as base64 for tools/decode_frame.py, so you can
 * see on a PC exactly what a given setting did.
 *
 * This is also the sketch to reach for when a picture looks wrong but you are
 * not sure whether the sensor or the capture path is at fault: turn on the
 * colour bar with 'k'. If the bars are clean, capture is fine and you have a
 * sensor-configuration or lighting problem.
 */

#include <OV7670.h>

static OV7670Pins pins = OV7670_PINS_DEFAULT;
static OV7670Camera camera;

static int  brightness = 0;
static int  contrast   = 0;
static int  saturation = 0;
static bool mirror = false, vflip = false, colorbar = false;
static bool aec = true, agc = true, awb = true;
static int  effect = 0;
static int  wb = 0;
static int  night = 0;

static const char b64chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void menu()
{
    Serial.println();
    Serial.println(F("--- OV7670 image controls -------------------------"));
    Serial.println(F("  1 / 2   brightness  down / up      (-4..+4)"));
    Serial.println(F("  3 / 4   contrast    down / up      (-4..+4)"));
    Serial.println(F("  5 / 6   saturation  down / up      (-4..+4)"));
    Serial.println(F("  m       toggle mirror"));
    Serial.println(F("  v       toggle vertical flip"));
    Serial.println(F("  e       cycle special effect"));
    Serial.println(F("  w       cycle white balance preset"));
    Serial.println(F("  n       cycle night mode"));
    Serial.println(F("  a       toggle auto exposure"));
    Serial.println(F("  g       toggle auto gain"));
    Serial.println(F("  b       toggle auto white balance"));
    Serial.println(F("  k       toggle the sensor colour bar"));
    Serial.println(F("  r       reset everything to defaults"));
    Serial.println(F("  d       dump one frame as base64"));
    Serial.println(F("  x       dump the whole register bank"));
    Serial.println(F("  ?       this menu"));
    Serial.println(F("---------------------------------------------------"));
}

static void status()
{
    static const char *effects[] = { "none", "b&w", "sepia", "bluish",
                                     "greenish", "reddish" };
    static const char *wbs[]     = { "auto", "sunny", "cloudy", "office", "home" };
    static const char *nights[]  = { "off", "/2", "/4", "/8" };

    OV7670Stats st;
    camera.getStats(&st);
    Serial.printf("bri %+d  con %+d  sat %+d  mirror %d  flip %d | "
                  "fx %s  wb %s  night %s | aec %d agc %d awb %d bar %d | "
                  "%.1f fps\n",
                  brightness, contrast, saturation, mirror, vflip,
                  effects[effect], wbs[wb], nights[night],
                  aec, agc, awb, colorbar, st.fps);
}

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
        char out[4] = {
            b64chars[(v >> 18) & 0x3F],
            b64chars[(v >> 12) & 0x3F],
            (char)((have > 1) ? b64chars[(v >> 6) & 0x3F] : '='),
            (char)((have > 2) ? b64chars[v & 0x3F] : '='),
        };
        Serial.write(out, 4);
        col += 4;
        if (col >= 60) { Serial.println(); col = 0; }
    }
    if (col) Serial.println();
}

static void dumpFrame()
{
    if (!camera.waitFrame(2000)) {
        Serial.println(F("capture timeout"));
        return;
    }
    size_t len = 0;
    uint8_t *fb = camera.getFrameBuffer(&len);
    if (!fb) return;
    Serial.printf("---BEGIN FRAME %u %u %s---\n",
                  camera.width(), camera.height(),
                  ov7670_pixformat_name(camera.pixelFormat()));
    dumpBase64(fb, len);
    Serial.println(F("---END FRAME---"));
    camera.releaseFrameBuffer();
}

static void applyAll()
{
    camera.setBrightness(brightness);
    camera.setContrast(contrast);
    camera.setSaturation(saturation);
    camera.setMirror(mirror);
    camera.setVFlip(vflip);
    camera.setSpecialEffect((ov7670_effect_t)effect);
    camera.setLightMode((ov7670_wb_mode_t)wb);
    camera.setNightMode((ov7670_night_mode_t)night);
    camera.setAutoExposure(aec);
    camera.setAutoGain(agc);
    camera.setAutoWhiteBalance(awb);
    camera.setColorBar(colorbar);
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println(F("=== OV7670 image controls (milestone M6) ==="));

    OV7670Config cfg;
    ov7670_config_defaults(&cfg);
    cfg.frame_size   = OV7670_FRAMESIZE_QQVGA;
    cfg.pixel_format = OV7670_PIXFORMAT_RGB565;
    cfg.fb_count     = 2;

    esp_err_t rc = camera.begin(pins, cfg);
    if (rc != ESP_OK) {
        Serial.printf("begin() failed: %s\n", esp_err_to_name(rc));
        while (true) delay(1000);
    }
    camera.startStreaming();
    menu();
    status();
}

void loop()
{
    if (!Serial.available()) {
        camera.waitFrame(500);
        camera.releaseFrameBuffer();
        return;
    }

    int c = Serial.read();
    switch (c) {
    case '1': brightness--; camera.setBrightness(brightness); break;
    case '2': brightness++; camera.setBrightness(brightness); break;
    case '3': contrast--;   camera.setContrast(contrast);     break;
    case '4': contrast++;   camera.setContrast(contrast);     break;
    case '5': saturation--; camera.setSaturation(saturation); break;
    case '6': saturation++; camera.setSaturation(saturation); break;
    case 'm': mirror = !mirror; camera.setMirror(mirror);     break;
    case 'v': vflip = !vflip;   camera.setVFlip(vflip);       break;
    case 'e': effect = (effect + 1) % OV7670_EFFECT_COUNT;
              camera.setSpecialEffect((ov7670_effect_t)effect); break;
    case 'w': wb = (wb + 1) % OV7670_WB_COUNT;
              camera.setLightMode((ov7670_wb_mode_t)wb);      break;
    case 'n': night = (night + 1) % 4;
              camera.setNightMode((ov7670_night_mode_t)night); break;
    case 'a': aec = !aec; camera.setAutoExposure(aec);        break;
    case 'g': agc = !agc; camera.setAutoGain(agc);            break;
    case 'b': awb = !awb; camera.setAutoWhiteBalance(awb);    break;
    case 'k': colorbar = !colorbar; camera.setColorBar(colorbar); break;
    case 'r':
        brightness = contrast = saturation = 0;
        mirror = vflip = colorbar = false;
        aec = agc = awb = true;
        effect = wb = night = 0;
        camera.softReset();
        applyAll();
        Serial.println(F("reset to defaults"));
        break;
    case 'd': dumpFrame(); return;
    case 'x': camera.dumpRegisters(); return;
    case '?': menu(); return;
    default: return;
    }

    // Clamp the levels back into range so the display never lies about them.
    if (brightness < -4) brightness = -4;
    if (brightness >  4) brightness =  4;
    if (contrast   < -4) contrast   = -4;
    if (contrast   >  4) contrast   =  4;
    if (saturation < -4) saturation = -4;
    if (saturation >  4) saturation =  4;

    status();
}
