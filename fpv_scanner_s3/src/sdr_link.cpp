#include "sdr_link.h"
#include "esp_rom_crc.h"

void SdrLink::begin(int rx_pin, int tx_pin, uint32_t baud) {
    port_.setRxBufferSize(16384);
    port_.begin(baud, SERIAL_8N1, rx_pin, tx_pin);
}

bool SdrLink::read_line(char *line, size_t size, uint32_t timeout_ms) {
    size_t used = 0;
    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        int c = port_.read();
        if (c < 0) {
            delay(1);
            continue;
        }
        if (c == '\r') continue;
        if (c == '\n') {
            line[used] = '\0';
            return true;
        }
        if (used + 1 < size) line[used++] = (char)c;
    }
    line[used] = '\0';
    return false;
}

bool SdrLink::read_exact(uint8_t *buffer, size_t size, uint32_t timeout_ms) {
    size_t done = 0;
    uint32_t start = millis();
    while (done < size && millis() - start < timeout_ms) {
        size_t got = port_.read(buffer + done, size - done);
        if (!got) delay(1);
        done += got;
    }
    return done == size;
}

bool SdrLink::connect() {
    ok_ = false;
    /* Boot messages and half-finished transfers are discarded up to the echo. */
    char line[96], expect[32];
    uint32_t nonce = esp_random();
    for (int attempt = 0; attempt < 3 && !ok_; attempt++) {
        while (port_.read() >= 0) {}
        port_.print("\n");
        snprintf(expect, sizeof(expect), "SYNC %lu", (unsigned long)(nonce + attempt));
        port_.printf("%s\n", expect);
        uint32_t start = millis();
        while (millis() - start < 1000)
            if (read_line(line, sizeof(line), 1000 - (millis() - start)) && !strcmp(line, expect)) {
                ok_ = true;
                break;
            }
    }
    if (!ok_) return false;
    ok_ = command("INFO", info_, sizeof(info_)) && !strncmp(info_, "C5SDR", 5);
    return ok_;
}

bool SdrLink::command(const char *cmd, char *reply, size_t reply_size, uint32_t timeout_ms) {
    char line[160];
    port_.printf("%s\n", cmd);
    if (!read_line(line, sizeof(line), timeout_ms)) return false;
    if (reply) {
        strlcpy(reply, line, reply_size);
        return strncmp(line, "ERR", 3) != 0;
    }
    return !strcmp(line, "OK");
}

bool SdrLink::spectrum(unsigned mhz, unsigned rate_code, unsigned n, unsigned dwell_ms, bool max_hold,
                       uint8_t *bins) {
    char cmd[64], line[96];
    snprintf(cmd, sizeof(cmd), "FREQ %u", mhz);
    if (!command(cmd, nullptr, 0, 3000)) return false;
    snprintf(cmd, sizeof(cmd), "SPEC %u 1 1 %u %u %u", dwell_ms, max_hold ? 1 : 0, rate_code, n);
    port_.printf("%s\n", cmd);
    if (!read_line(line, sizeof(line), 2000) || strncmp(line, "SPEC ", 5)) return false;

    /* Frames: "SPC1" + 24 header bytes + n codes + CRC32, until "SPECEND ...\n". */
    static uint8_t frame[28 + 2048 + 4];
    static uint32_t sum[2048];
    unsigned frames = 0;
    memset(sum, 0, sizeof(sum));
    for (;;) {
        if (!read_exact(frame, 4, dwell_ms + 3000)) return false;
        if (!memcmp(frame, "SPC1", 4)) {
            if (!read_exact(frame + 4, 24, 1000)) return false;
            unsigned bins_in_frame = 1u << frame[26];
            if (bins_in_frame != n || !read_exact(frame + 28, n + 4, 1000)) return false;
            uint32_t crc;
            memcpy(&crc, frame + 28 + n, 4);
            if (esp_rom_crc32_le(0, frame, 28 + n) != crc) continue; /* skip a damaged frame */
            for (unsigned j = 0; j < n; j++) {
                if (max_hold) {
                    if (frame[28 + j] > sum[j]) sum[j] = frame[28 + j];
                } else
                    sum[j] += frame[28 + j];
            }
            frames++;
        } else if (!memcmp(frame, "SPEC", 4)) {
            read_line(line, sizeof(line), 1000); /* rest of SPECEND */
            break;
        } else
            return false; /* out of step; the caller resynchronizes */
    }
    if (!frames) return false;
    for (unsigned j = 0; j < n; j++) bins[j] = max_hold ? sum[j] : (sum[j] + frames / 2) / frames;
    return true;
}
