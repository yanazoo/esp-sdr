/* XIAO ESP32-S3 front end: 2.4 GHz access point + captive-portal web page
 * showing a 5 GHz sweep measured by an ESP-SDR XIAO ESP32-C5 over UART.
 *
 * The C5 sees about 30 MHz at a time, so the sweep task retunes it in steps,
 * runs a short on-chip spectrum (SPEC) at each step and stitches the centre
 * part of every spectrum into one trace of SWEEP_POINTS display points. */
#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
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
    /* Display only; changing them does not restart the sweep. */
    int cal_tenths = 0;      /* dBm = dBFS + cal_tenths / 10 */
    unsigned bands = 0x11;   /* FPV band lines shown: bit 0 A, 1 B, 2 E, 3 F, 4 R */
};

static const unsigned rate_khz[] = {80000, 40000, 20000, 10000};

/* Width of each sweep step: the centre 75 % of the spectrum, at most
 * +-20 MHz (the C5's widest analog filter). */
static double half_use_khz(unsigned rate_code) {
    double half = rate_khz[rate_code] * 0.375;
    return half > 20000 ? 20000 : half;
}

static WebServer http(80);
static WebSocketsServer ws(81);
static DNSServer dns;
static SdrLink sdr;
static SemaphoreHandle_t lock;
static SweepConfig config;
static bool config_changed = true;

/* Latest sweep, shared with the web task: 24-byte header + codes. */
static const size_t HEADER = 24;
static uint8_t frame[HEADER + SWEEP_POINTS];
static uint32_t frame_seq, sent_seq;
static char status_text[512];
static bool status_dirty = true;
static unsigned gain_max;
static char receiver_error[64];
static bool settings_dirty;

/* Noise reference saved from the page: same layout as a sweep frame. */
static uint8_t noise[HEADER + SWEEP_POINTS];
static bool has_noise;
static Preferences prefs;

static void load_settings() {
    prefs.begin("fpv", false);
    SweepConfig &c = config;
    c.start_mhz = prefs.getUInt("start", c.start_mhz);
    c.stop_mhz = prefs.getUInt("stop", c.stop_mhz);
    c.rate_code = prefs.getUInt("rate", c.rate_code);
    c.dwell_ms = prefs.getUInt("dwell", c.dwell_ms);
    c.max_hold = prefs.getBool("det", c.max_hold);
    c.gain = prefs.getInt("gain", c.gain);
    c.cal_tenths = prefs.getInt("cal", c.cal_tenths);
    c.bands = prefs.getUInt("bands", c.bands);
    has_noise = prefs.getBytes("noise", noise, sizeof(noise)) == sizeof(noise) && !memcmp(noise, "NSE1", 4);
}

static void save_settings() {
    xSemaphoreTake(lock, portMAX_DELAY);
    SweepConfig c = config;
    settings_dirty = false;
    xSemaphoreGive(lock);
    prefs.putUInt("start", c.start_mhz);
    prefs.putUInt("stop", c.stop_mhz);
    prefs.putUInt("rate", c.rate_code);
    prefs.putUInt("dwell", c.dwell_ms);
    prefs.putBool("det", c.max_hold);
    prefs.putInt("gain", c.gain);
    prefs.putInt("cal", c.cal_tenths);
    prefs.putUInt("bands", c.bands);
}

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void put16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }

/* The settings reported are those of the sweep that just finished (applied),
 * so the page can show whether a change has taken effect. */
static struct {
    bool linked;
    const char *info = "";
    SweepConfig c;
    unsigned steps, sweep_ms;
} last;
static void set_status(bool linked, const char *info, const SweepConfig &c, unsigned steps, unsigned sweep_ms) {
    xSemaphoreTake(lock, portMAX_DELAY);
    last.linked = linked;
    last.info = info;
    last.c = c;
    last.steps = steps;
    last.sweep_ms = sweep_ms;
    snprintf(status_text, sizeof(status_text),
             "{\"type\":\"status\",\"linked\":%s,\"info\":\"%s\",\"start\":%u,\"stop\":%u,\"rate\":%u,"
             "\"dwell\":%u,\"det\":%u,\"gain\":%d,\"gain_max\":%u,\"steps\":%u,\"step_khz\":%u,"
             "\"sweep_ms\":%u,\"error\":\"%s\",\"cal\":%d,\"bands\":%u,\"noise\":%s}",
             linked ? "true" : "false", info, c.start_mhz, c.stop_mhz, c.rate_code, c.dwell_ms,
             c.max_hold ? 1 : 0, c.gain, gain_max, steps, (unsigned)(2 * half_use_khz(c.rate_code)), sweep_ms,
             receiver_error, config.cal_tenths, config.bands, has_noise ? "true" : "false");
    status_dirty = true;
    xSemaphoreGive(lock);
}
/* Display-only settings and the noise reference take effect at once. */
static void refresh_status() {
    xSemaphoreTake(lock, portMAX_DELAY);
    auto l = last;
    xSemaphoreGive(lock);
    set_status(l.linked, l.info, l.c, l.steps, l.sweep_ms);
}

/* Returns false only when the link broke. A rejected gain falls back to
 * the hardware AGC and is reported on the page. */
static bool apply_receiver(SweepConfig &c) {
    char reply[96], cmd[32];
    receiver_error[0] = '\0';
    if (sdr.command("LIMITS?", reply, sizeof(reply))) {
        const char *g = strstr(reply, "\"gain\":[");
        unsigned lo, hi;
        if (g && sscanf(g, "\"gain\":[%u,%u", &lo, &hi) == 2) gain_max = hi;
    }
    if (!sdr.command("BANDWIDTH 0")) return false; /* widest analog filter */
    if (c.gain >= 0) {
        snprintf(cmd, sizeof(cmd), "GAIN MANUAL %d", c.gain);
        reply[0] = '\0';
        if (sdr.command(cmd, reply, sizeof(reply)) && !strcmp(reply, "OK")) return true;
        if (strncmp(reply, "ERR", 3)) return false;
        snprintf(receiver_error, sizeof(receiver_error), "gain %d rejected (2-%u), using AGC", c.gain, gain_max);
        c.gain = -1;
        xSemaphoreTake(lock, portMAX_DELAY);
        config.gain = -1;
        settings_dirty = true;
        xSemaphoreGive(lock);
    }
    return sdr.command("GAIN HARDWARE");
}

static bool changed_since_start(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    bool changed = config_changed;
    xSemaphoreGive(lock);
    return changed;
}

static void sweep_task(void *) {
    static uint8_t bins[2048];
    static uint8_t trace[SWEEP_POINTS];
    static bool filled[SWEEP_POINTS];
    SweepConfig c;
    for (;;) {
        if (!sdr.connected()) {
            xSemaphoreTake(lock, portMAX_DELAY);
            c = config;
            xSemaphoreGive(lock);
            set_status(false, "C5 not found", c, 0, 0);
            if (!sdr.connect()) {
                delay(1000);
                continue;
            }
            xSemaphoreTake(lock, portMAX_DELAY);
            config_changed = true;
            xSemaphoreGive(lock);
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

        /* Skip the bins next to the LO, where DC correction leaves residue. */
        const double fs = rate_khz[c.rate_code], half_use = half_use_khz(c.rate_code);
        const double start = c.start_mhz * 1000.0, stop = c.stop_mhz * 1000.0, span = stop - start;
        const unsigned steps = (unsigned)ceil(span / (2 * half_use));
        memset(trace, 0, sizeof(trace));
        memset(filled, 0, sizeof(filled));
        uint32_t t0 = millis();
        bool ok = true, restarted = false;
        for (unsigned s = 0; s < steps && ok; s++) {
            /* A new setting restarts the sweep instead of waiting for it to end. */
            if (changed_since_start()) {
                restarted = true;
                break;
            }
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
        if (restarted) continue;
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
        set_status(true, sdr.info(), c, steps, sweep_ms);
    }
}

/* "cfg start=5650 stop=5850 rate=1 dwell=25 det=1 gain=-1 cal=0 bands=17",
 * sent by the page's apply buttons; every accepted change is saved. */
static void handle_text(const char *text) {
    if (strncmp(text, "cfg", 3)) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    SweepConfig c = config;
    xSemaphoreGive(lock);
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
        else if (!strcmp(key, "cal")) c.cal_tenths = constrain(value, -1000, 1000);
        else if (!strcmp(key, "bands")) c.bands = value & 0x1f;
    }
    if (c.stop_mhz < c.start_mhz + 5) c.stop_mhz = c.start_mhz + 5;
    xSemaphoreTake(lock, portMAX_DELAY);
    const SweepConfig &o = config;
    if (c.start_mhz != o.start_mhz || c.stop_mhz != o.stop_mhz || c.rate_code != o.rate_code ||
        c.dwell_ms != o.dwell_ms || c.max_hold != o.max_hold || c.gain != o.gain)
        config_changed = true;
    config = c;
    settings_dirty = true;
    xSemaphoreGive(lock);
    refresh_status();
}

static void store_noise(const uint8_t *data, size_t length) {
    if (length != sizeof(noise) || memcmp(data, "NSE1", 4)) return;
    memcpy(noise, data, sizeof(noise));
    has_noise = true;
    prefs.putBytes("noise", noise, sizeof(noise));
    ws.broadcastBIN(noise, sizeof(noise));
    refresh_status();
}

static void on_ws(uint8_t client, WStype_t type, uint8_t *payload, size_t length) {
    if (type == WStype_CONNECTED) {
        if (has_noise) ws.sendBIN(client, noise, sizeof(noise));
        status_dirty = true;
    } else if (type == WStype_TEXT) {
        char text[128];
        size_t n = length < sizeof(text) - 1 ? length : sizeof(text) - 1;
        memcpy(text, payload, n);
        text[n] = '\0';
        if (!strcmp(text, "noise clear")) {
            has_noise = false;
            prefs.remove("noise");
            refresh_status();
        } else
            handle_text(text);
    } else if (type == WStype_BIN) {
        store_noise(payload, length);
    }
}

/* Captive portal: every DNS name resolves to us, and any unknown URL
 * (including the OS connectivity checks) redirects to the scanner page. */
static void redirect_to_portal() {
    http.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    http.send(302, "text/plain", "");
}

void setup() {
    Serial.begin(115200);
    lock = xSemaphoreCreateMutex();
    load_settings();
    last.c = config;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);
    WiFi.setSleep(false);
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", WiFi.softAPIP());
    http.on("/", [] { http.send_P(200, "text/html", INDEX_HTML); });
    http.onNotFound(redirect_to_portal);
    http.begin();
    ws.begin();
    ws.onEvent(on_ws);
    sdr.begin(SDR_RX_PIN, SDR_TX_PIN, SDR_BAUD);
    xTaskCreatePinnedToCore(sweep_task, "sweep", 8192, nullptr, 2, nullptr, 0);
    Serial.printf("AP %s  http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void loop() {
    static uint8_t out[HEADER + SWEEP_POINTS];
    static char status_out[sizeof(status_text)];
    dns.processNextRequest();
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
    bool save = settings_dirty;
    xSemaphoreGive(lock);
    if (save) save_settings();
    if (send_frame) ws.broadcastBIN(out, sizeof(out));
    if (send_status) ws.broadcastTXT(status_out);
    delay(2);
}
