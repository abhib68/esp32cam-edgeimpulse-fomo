/* ESP32-CAM + Edge Impulse (FOMO) with a live web dashboard
 * Adapted to work with sensors that have NO hardware JPEG (e.g. GC2145).
 *
 * Based on the Edge Impulse Arduino ESP32 camera example
 * Copyright (c) 2022 EdgeImpulse Inc. - MIT License
 *
 * Web pages on the ESP32:   http://<ESP32-IP>/
 *       /          -> dashboard (live image + boxes + detections list)
 *       /json      -> latest results as JSON
 *       /capture   -> latest camera frame as JPEG
 *
 * Arduino IDE: Board "AI Thinker ESP32-CAM", CPU 240MHz, PSRAM Enabled,
 *              Partition Scheme "Huge APP"
 */

#include <object_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"
#include "img_converters.h"
#include <WiFi.h>
#include <WebServer.h>

/* ---------- CHANGE THESE ---------- */
const char* WIFI_SSID     = "Hello";
const char* WIFI_PASSWORD = "sharath12";
/* ---------------------------------- */

#if EI_CLASSIFIER_OBJECT_DETECTION != 1
#error "This sketch expects an object detection (FOMO) model"
#endif

#define CAMERA_MODEL_AI_THINKER // Has PSRAM

#if defined(CAMERA_MODEL_AI_THINKER)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#else
#error "Camera model not selected"
#endif

#define EI_CAMERA_RAW_FRAME_BUFFER_COLS           320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS           240
#define EI_CAMERA_FRAME_BYTE_SIZE                 3

#define JPG_MAX_SIZE   50000   // max bytes of the JPEG copy kept for the web page
#define MAX_DET        10

/* ---------- Private variables ---------- */
static bool debug_nn = false;
static bool is_initialised = false;
static bool system_ok = false;

uint8_t *snapshot_buf = nullptr;   // RGB888 working buffer (allocated once)
uint8_t *jpg_buf = nullptr;        // latest JPEG for the website (allocated once)
size_t   jpg_len = 0;

struct Det { const char* label; float value; uint16_t x, y, w, h; };
Det      dets[MAX_DET];
uint8_t  det_count = 0;
uint32_t frame_id  = 0;
int      t_dsp = 0, t_cls = 0;
float    fps = 0;
uint32_t last_ms = 0;

WebServer server(80);

/* ---------- Web page (stored in flash) ---------- */
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32-CAM Detection</title>
<style>
  body{font-family:system-ui,Arial,sans-serif;background:#10141a;color:#e8edf3;margin:0;padding:16px;text-align:center}
  h1{font-size:20px;margin:0 0 12px}
  #wrap{display:inline-block;position:relative;max-width:100%}
  canvas{width:100%;max-width:640px;border-radius:10px;background:#000}
  #status{margin:12px auto;padding:10px 16px;border-radius:8px;max-width:640px;font-weight:600;background:#1e2630}
  #status.alert{background:#b3261e}
  #status.ok{background:#1f6f43}
  table{margin:8px auto;border-collapse:collapse;width:100%;max-width:640px}
  th,td{padding:6px 10px;border-bottom:1px solid #2a3340;text-align:left}
  .meta{font-size:13px;color:#9aa7b5;margin-top:8px}
</style>
</head>
<body>
<h1>ESP32-CAM Live Detection</h1>
<div id="wrap"><canvas id="c" width="320" height="240"></canvas></div>
<div id="status">Connecting...</div>
<table id="tbl"><thead><tr><th>Label</th><th>Confidence</th><th>Position (x,y)</th></tr></thead><tbody></tbody></table>
<div class="meta" id="meta"></div>

<script>
const canvas = document.getElementById('c');
const ctx = canvas.getContext('2d');
const statusEl = document.getElementById('status');
const tbody = document.querySelector('#tbl tbody');
const meta = document.getElementById('meta');
const COLORS = {accident:'#ff3b30', ambulance:'#ffd60a', vip:'#0a84ff'};

async function tick() {
  try {
    const d = await (await fetch('/json', {cache:'no-store'})).json();

    // draw the latest frame, then the boxes on top
    const blob = await (await fetch('/capture?f=' + d.frame, {cache:'no-store'})).blob();
    const bmp = await createImageBitmap(blob);
    ctx.drawImage(bmp, 0, 0, canvas.width, canvas.height);

    const sx = canvas.width / d.w, sy = canvas.height / d.h;
    ctx.lineWidth = 2; ctx.font = '12px sans-serif';
    tbody.innerHTML = '';
    d.dets.forEach(o => {
      const col = COLORS[o.label] || '#34c759';
      ctx.strokeStyle = col; ctx.fillStyle = col;
      ctx.strokeRect(o.x * sx, o.y * sy, Math.max(o.w * sx, 6), Math.max(o.h * sy, 6));
      ctx.fillText(o.label + ' ' + (o.value * 100).toFixed(0) + '%', o.x * sx, Math.max(o.y * sy - 3, 10));
      tbody.innerHTML += '<tr><td>' + o.label + '</td><td>' + (o.value * 100).toFixed(1) +
                         '%</td><td>' + o.x + ', ' + o.y + '</td></tr>';
    });

    if (d.dets.length) {
      const names = [...new Set(d.dets.map(o => o.label))].join(', ');
      statusEl.textContent = 'Detected: ' + names;
      statusEl.className = 'alert';
    } else {
      statusEl.textContent = 'No object detected';
      statusEl.className = 'ok';
    }
    meta.textContent = 'Frame ' + d.frame + '  |  ' + d.fps.toFixed(1) + ' FPS  |  DSP ' + d.dsp +
                       ' ms  |  Classification ' + d.cls + ' ms';
  } catch (e) {
    statusEl.textContent = 'Connection lost... retrying';
    statusEl.className = '';
  }
  setTimeout(tick, 300);
}
tick();
</script>
</body>
</html>
)rawliteral";

/* ---------- Web handlers ---------- */
void handleRoot() {
    server.send_P(200, "text/html", INDEX_HTML);
}

void handleJson() {
    String j;
    j.reserve(600);
    j += "{\"frame\":";  j += frame_id;
    j += ",\"fps\":";    j += String(fps, 1);
    j += ",\"dsp\":";    j += t_dsp;
    j += ",\"cls\":";    j += t_cls;
    j += ",\"w\":";      j += EI_CLASSIFIER_INPUT_WIDTH;
    j += ",\"h\":";      j += EI_CLASSIFIER_INPUT_HEIGHT;
    j += ",\"dets\":[";
    for (uint8_t i = 0; i < det_count; i++) {
        if (i) j += ",";
        j += "{\"label\":\""; j += dets[i].label; j += "\"";
        j += ",\"value\":";   j += String(dets[i].value, 3);
        j += ",\"x\":";       j += dets[i].x;
        j += ",\"y\":";       j += dets[i].y;
        j += ",\"w\":";       j += dets[i].w;
        j += ",\"h\":";       j += dets[i].h;
        j += "}";
    }
    j += "]}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", j);
}

void handleCapture() {
    if (jpg_len == 0) {
        server.send(503, "text/plain", "No frame yet");
        return;
    }
    WiFiClient client = server.client();
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(jpg_len);
    server.send(200, "image/jpeg", "");
    client.write(jpg_buf, jpg_len);
}

/* ---------- Function declarations ---------- */
bool ei_camera_init(void);
void ei_camera_deinit(void);
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf);
static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr);

/* ---------- Setup ---------- */
void setup()
{
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 3000) delay(10);

    Serial.println("Edge Impulse + Web Dashboard");

    if (!ei_camera_init()) {
        ei_printf("Failed to initialize Camera!\r\n");
        return;
    }
    ei_printf("Camera initialized\r\n");

    size_t buf_size = EI_CAMERA_RAW_FRAME_BUFFER_COLS * EI_CAMERA_RAW_FRAME_BUFFER_ROWS * EI_CAMERA_FRAME_BYTE_SIZE;
    snapshot_buf = (uint8_t*)ps_malloc(buf_size);
    jpg_buf      = (uint8_t*)ps_malloc(JPG_MAX_SIZE);
    if (!snapshot_buf || !jpg_buf) {
        ei_printf("ERR: buffer allocation failed. Is PSRAM enabled?\r\n");
        return;
    }

    // ---- WiFi ----
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    ei_printf("Connecting to WiFi");
    uint32_t w0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - w0) < 20000) {
        delay(500);
        ei_printf(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        ei_printf("\r\nWiFi connected. Open this in your browser:  http://%s/\r\n",
                  WiFi.localIP().toString().c_str());
        server.on("/", handleRoot);
        server.on("/json", handleJson);
        server.on("/capture", handleCapture);
        server.begin();
    } else {
        ei_printf("\r\nWiFi FAILED - inference will still run, but no web page.\r\n");
    }

    system_ok = true;
    last_ms = millis();
}

/* ---------- Main loop ---------- */
void loop()
{
    server.handleClient();

    if (!system_ok) {
        delay(1000);
        return;
    }

    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &ei_camera_get_data;

    if (!ei_camera_capture((size_t)EI_CLASSIFIER_INPUT_WIDTH, (size_t)EI_CLASSIFIER_INPUT_HEIGHT, snapshot_buf)) {
        ei_printf("Failed to capture image\r\n");
        return;
    }

    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);
    if (err != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier (%d)\n", err);
        return;
    }

    // ---- store results for the web page ----
    det_count = 0;
    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
        ei_impulse_result_bounding_box_t bb = result.bounding_boxes[i];
        if (bb.value == 0) continue;
        if (det_count < MAX_DET) {
            dets[det_count++] = { bb.label, bb.value, (uint16_t)bb.x, (uint16_t)bb.y,
                                  (uint16_t)bb.width, (uint16_t)bb.height };
        }
    }
    t_dsp = result.timing.dsp;
    t_cls = result.timing.classification;
    uint32_t now = millis();
    if (now > last_ms) fps = 1000.0f / (now - last_ms);
    last_ms = now;
    frame_id++;

    // ---- Serial output ----
    ei_printf("Predictions (DSP: %d ms., Classification: %d ms.)\r\n", t_dsp, t_cls);
    for (uint8_t i = 0; i < det_count; i++) {
        ei_printf("  %s (%f) [ x: %u, y: %u, width: %u, height: %u ]\r\n",
                  dets[i].label, dets[i].value, dets[i].x, dets[i].y, dets[i].w, dets[i].h);
    }

    server.handleClient();
}

/* ---------- Camera helpers ---------- */
static void fill_camera_config(camera_config_t &c, pixformat_t fmt) {
    memset(&c, 0, sizeof(c));
    c.ledc_channel = LEDC_CHANNEL_0;
    c.ledc_timer   = LEDC_TIMER_0;
    c.pin_d0 = Y2_GPIO_NUM;
    c.pin_d1 = Y3_GPIO_NUM;
    c.pin_d2 = Y4_GPIO_NUM;
    c.pin_d3 = Y5_GPIO_NUM;
    c.pin_d4 = Y6_GPIO_NUM;
    c.pin_d5 = Y7_GPIO_NUM;
    c.pin_d6 = Y8_GPIO_NUM;
    c.pin_d7 = Y9_GPIO_NUM;
    c.pin_xclk  = XCLK_GPIO_NUM;
    c.pin_pclk  = PCLK_GPIO_NUM;
    c.pin_vsync = VSYNC_GPIO_NUM;
    c.pin_href  = HREF_GPIO_NUM;
    c.pin_sccb_sda = SIOD_GPIO_NUM;
    c.pin_sccb_scl = SIOC_GPIO_NUM;
    c.pin_pwdn  = PWDN_GPIO_NUM;
    c.pin_reset = RESET_GPIO_NUM;
    c.xclk_freq_hz = 20000000;
    c.pixel_format = fmt;
    c.frame_size   = FRAMESIZE_QVGA;   // 320x240
    c.jpeg_quality = 12;               // ignored for RGB565
    c.fb_count     = 2;
    c.fb_location  = CAMERA_FB_IN_PSRAM;
    c.grab_mode    = CAMERA_GRAB_LATEST;
}

bool ei_camera_init(void) {
    if (is_initialised) return true;

    camera_config_t cfg;

    // 1) try hardware JPEG first (OV2640 etc.)
    fill_camera_config(cfg, PIXFORMAT_JPEG);
    esp_err_t err = esp_camera_init(&cfg);

    // 2) sensor has no JPEG (e.g. GC2145) -> fall back to RGB565
    if (err == ESP_ERR_NOT_SUPPORTED) {
        Serial.println("JPEG not supported by this sensor, using RGB565");
        esp_camera_deinit();
        fill_camera_config(cfg, PIXFORMAT_RGB565);
        err = esp_camera_init(&cfg);
    }

    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }

    sensor_t * s = esp_camera_sensor_get();
    Serial.printf("Sensor PID: 0x%x\n", s->id.PID);
    if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1);
        s->set_brightness(s, 1);
        s->set_saturation(s, 0);
    }

    is_initialised = true;
    return true;
}

void ei_camera_deinit(void) {
    esp_err_t err = esp_camera_deinit();
    if (err != ESP_OK) {
        ei_printf("Camera deinit failed\n");
        return;
    }
    is_initialised = false;
}

bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf) {
    bool do_resize = false;

    if (!is_initialised) {
        ei_printf("ERR: Camera is not initialized\r\n");
        return false;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ei_printf("Camera capture failed\n");
        return false;
    }

    // keep a JPEG copy for the website
    if (jpg_buf) {
        if (fb->format == PIXFORMAT_JPEG) {
            if (fb->len <= JPG_MAX_SIZE) {
                memcpy(jpg_buf, fb->buf, fb->len);
                jpg_len = fb->len;
            }
        } else {
            // RGB565 frame -> encode to JPEG in software
            uint8_t *enc = nullptr;
            size_t enc_len = 0;
            if (frame2jpg(fb, 80, &enc, &enc_len)) {
                if (enc_len <= JPG_MAX_SIZE) {
                    memcpy(jpg_buf, enc, enc_len);
                    jpg_len = enc_len;
                }
                free(enc);
            }
        }
    }

    // convert whatever the camera gave us (JPEG or RGB565) to RGB888
    bool converted = fmt2rgb888(fb->buf, fb->len, fb->format, snapshot_buf);
    esp_camera_fb_return(fb);

    if (!converted) {
        ei_printf("Conversion failed\n");
        return false;
    }

    if ((img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS) || (img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS)) {
        do_resize = true;
    }

    if (do_resize) {
        ei::image::processing::crop_and_interpolate_rgb888(
            out_buf,
            EI_CAMERA_RAW_FRAME_BUFFER_COLS,
            EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
            out_buf,
            img_width,
            img_height);
    }

    return true;
}

static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr)
{
    size_t pixel_ix = offset * 3;
    size_t pixels_left = length;
    size_t out_ptr_ix = 0;

    while (pixels_left != 0) {
        // Swap BGR to RGB (https://github.com/espressif/esp32-camera/issues/379)
        out_ptr[out_ptr_ix] = (snapshot_buf[pixel_ix + 2] << 16) + (snapshot_buf[pixel_ix + 1] << 8) + snapshot_buf[pixel_ix];
        out_ptr_ix++;
        pixel_ix += 3;
        pixels_left--;
    }
    return 0;
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "Invalid model for current sensor"
#endif
