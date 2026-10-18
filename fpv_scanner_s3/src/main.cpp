/* XIAO ESP32-S3 front end: 2.4 GHz access point + web page showing a
 * 5 GHz sweep measured by an ESP-SDR XIAO ESP32-C5 over UART.
 *
 * The C5 sees about 30 MHz at a time, so the sweep task retunes it in steps,
 * runs a short on-chip spectrum (SPEC) at each step and stitches the centre
 * part of every spectrum into one trace of SWEEP_POINTS display points. */
#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include "config.h"
#include "index_html.h"
#include "sdr_link.h"

struct SweepConfig {
    unsigned start_mhz = DEFAULT_START_MHZ, stop_mhz = DEFAULT_STOP_MHZ;
    unsigned rate_code = 1; /* 0: 80, 1: 40, 2: 20, 3: 10 MS/s */
    unsigned fft_n = 1024;
    unsigned dwell_ms = 25;
    bool max_hold = true;
    int gain = -1; /* -1: hardware AGC, otherwise a PHY gain index */
};

static const unsigned rate_khz[] = {80000, 40000, 20000, 10000};

static WebServer http(80);
static WebSocketsServer ws(81);
static SdrLink sdr;
static SemaphoreHandle_t lock;
static SweepConfig config;
static bool config_changed = true;

/* Latest sweep, shared with the web task: 24-byte header + codes. */
static const size_t HEADER = 24;
static uint8_t frame[HEADER + SWEEP_POINTS];
static uint32_t frame_seq, sent_seq;
static char status_text[256];
static bool status_dirty = true;

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void put16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }

static void set_status(bool linked, const char *info, unsigned sweep_ms) {
    xSemaphoreTake(lock, portMAX_DELAY);
    snprintf(status_text, sizeof(status_text),
             "{\"type\":\"status\",\"linked\":%s,\"info\":\"%s\",\"start\":%u,\"stop\":%u,\"rate\":%u,"
             "\"dwell\":%u,\"det\":%u,\"gain\":%d,\"sweep_ms\":%u}",
             linked ? "true" : "false", info, config.start_mhz, config.stop_mhz, config.rate_code,
             config.dwell_ms, config.max_hold ? 1 : 0, config.gain, sweep_ms);
    status_dirty = true;
    xSemaphoreGive(lock);
}

static bool apply_receiver(const SweepConfig &c) {
    char cmd[32];
    if (!sdr.command("BANDWIDTH 0")) return false; /* widest analog filter */
    if (c.gain < 0)
        strcpy(cmd, "GAIN HARDWARE");
    else
        snprintf(cmd, sizeof(cmd), "GAIN MANUAL %d", c.gain);
    return sdr.command(cmd);
}

static void sweep_task(void *) {
    static uint8_t bins[2048];
    static uint8_t trace[SWEEP_POINTS];
    static bool filled[SWEEP_POINTS];
    SweepConfig c;
    for (;;) {
        if (!sdr.connected()) {
            set_status(false, "C5 not found", 0);
            if (!sdr.connect()) {
                delay(1000);
                continue;
            }
            config_changed = true;
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        c = config;
        bool changed = config_changed;
        config_changed = false;
        xSemaphoreGive(lock);
        if (changed && !apply_receiver(c)) {
            sdr.connect();
            continue;
        }

        /* Use the centre 75 % of each spectrum, at most +-20 MHz (analog filter),
         * and skip the bins next to the LO, where DC correction leaves residue. */
        const double fs = rate_khz[c.rate_code];
        const double half_use = fs * 0.375 > 20000 ? 20000 : fs * 0.375;
        const double start = c.start_mhz * 1000.0, stop = c.stop_mhz * 1000.0, span = stop - start;
        const unsigned steps = (unsigned)ceil(span / (2 * half_use));
        memset(trace, 0, sizeof(trace));
        memset(filled, 0, sizeof(filled));
        uint32_t t0 = millis();
        bool ok = true;
        for (unsigned s = 0; s < steps && ok; s++) {
            unsigned centre_mhz = (unsigned)lround((start + (2 * s + 1) * half_use) / 1000.0);
            ok = sdr.spectrum(centre_mhz, c.rate_code, c.fft_n, c.dwell_ms, c.max_hold, bins);
            if (!ok) break;
            for (unsigned k = 0; k < c.fft_n; k++) {
                int signed_k = k < c.fft_n / 2 ? (int)k : (int)k - (int)c.fft_n;
                if (abs(signed_k) <= 2) continue;
                double offset = signed_k * fs / c.fft_n;
                if (fabs(offset) > half_use) continue;
                double f = centre_mhz * 1000.0 + offset;
                if (f < start || f >= stop) continue;
                unsigned i = (unsigned)((f - start) / span * SWEEP_POINTS);
                if (i >= SWEEP_POINTS) continue;
                if (!filled[i] || bins[k] > trace[i]) trace[i] = bins[k];
                filled[i] = true;
            }
        }
        if (!ok) {
            sdr.connect(); /* resynchronize after a broken transfer */
            continue;
        }
        /* Points narrower than one FFT bin (small spans) are interpolated. */
        int last = -1;
        for (int i = 0; i < SWEEP_POINTS; i++) {
            if (!filled[i]) continue;
            for (int j = last + 1; j < i; j++)
                trace[j] = last < 0 ? trace[i] : trace[last] + (trace[i] - trace[last]) * (j - last) / (i - last);
            last = i;
        }
        for (int j = last + 1; j < SWEEP_POINTS && last >= 0; j++) trace[j] = trace[last];
        unsigned sweep_ms = millis() - t0;

        xSemaphoreTake(lock, portMAX_DELAY);
        memcpy(frame, "FPV1", 4);
        put32(frame + 4, c.start_mhz * 1000);
        put32(frame + 8, c.stop_mhz * 1000);
        put16(frame + 12, SWEEP_POINTS);
        put16(frame + 14, sweep_ms > 65535 ? 65535 : sweep_ms);
        put32(frame + 16, ++frame_seq);
        frame[20] = c.rate_code;
        frame[21] = c.max_hold;
        frame[22] = steps;
        frame[23] = 0;
        memcpy(frame + HEADER, trace, SWEEP_POINTS);
        xSemaphoreGive(lock);
        set_status(true, sdr.info(), sweep_ms);
    }
}

/* "cfg start=5650 stop=5950 rate=1 dwell=25 det=1 gain=-1" */
static void handle_text(const char *text) {
    if (strncmp(text, "cfg", 3)) return;
    SweepConfig c = config;
    const char *p = text + 3;
    char key[16];
    int value, used;
    while (sscanf(p, " %15[a-z]=%d%n", key, &value, &used) == 2) {
        p += used;
        if (!strcmp(key, "start")) c.start_mhz = constrain(value, 2300, 5995);
        else if (!strcmp(key, "stop")) c.stop_mhz = constrain(value, 2305, 6000);
        else if (!strcmp(key, "rate")) c.rate_code = constrain(value, 0, 3);
        else if (!strcmp(key, "dwell")) c.dwell_ms = constrain(value, 5, 500);
        else if (!strcmp(key, "det")) c.max_hold = value != 0;
        else if (!strcmp(key, "gain")) c.gain = constrain(value, -1, 90);
    }
    if (c.stop_mhz < c.start_mhz + 5) c.stop_mhz = c.start_mhz + 5;
    xSemaphoreTake(lock, portMAX_DELAY);
    config = c;
    config_changed = true;
    xSemaphoreGive(lock);
}

static void on_ws(uint8_t client, WStype_t type, uint8_t *payload, size_t length) {
    if (type == WStype_CONNECTED) {
        status_dirty = true;
    } else if (type == WStype_TEXT) {
        char text[128];
        size_t n = length < sizeof(text) - 1 ? length : sizeof(text) - 1;
        memcpy(text, payload, n);
        text[n] = '\0';
        handle_text(text);
    }
}

void setup() {
    Serial.begin(115200);
    lock = xSemaphoreCreateMutex();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);
    WiFi.setSleep(false);
    http.on("/", [] { http.send_P(200, "text/html", INDEX_HTML); });
    http.onNotFound([] { http.send(404, "text/plain", "not found"); });
    http.begin();
    ws.begin();
    ws.onEvent(on_ws);
    sdr.begin(SDR_RX_PIN, SDR_TX_PIN, SDR_BAUD);
    xTaskCreatePinnedToCore(sweep_task, "sweep", 8192, nullptr, 2, nullptr, 0);
    Serial.printf("AP %s  http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void loop() {
    static uint8_t out[HEADER + SWEEP_POINTS];
    static char status_out[256];
    http.handleClient();
    ws.loop();
    bool send_frame = false, send_status = false;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (frame_seq != sent_seq) {
        memcpy(out, frame, sizeof(out));
        sent_seq = frame_seq;
        send_frame = true;
    }
    if (status_dirty) {
        strlcpy(status_out, status_text, sizeof(status_out));
        status_dirty = false;
        send_status = true;
    }
    xSemaphoreGive(lock);
    if (send_frame) ws.broadcastBIN(out, sizeof(out));
    if (send_status) ws.broadcastTXT(status_out);
    delay(2);
}
