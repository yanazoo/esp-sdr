#pragma once
/* Client for the ESP-SDR line protocol (docs/protocol.md, docs/spectrum.md)
 * running on the XIAO ESP32-C5 over UART. */
#include <Arduino.h>

class SdrLink {
  public:
    void begin(int rx_pin, int tx_pin, uint32_t baud);
    /* SYNC handshake, then check that a C5 answers INFO. */
    bool connect();
    bool connected() const { return ok_; }
    /* One text command with one text reply ("OK" expected unless reply given). */
    bool command(const char *cmd, char *reply = nullptr, size_t reply_size = 0, uint32_t timeout_ms = 2000);
    /* Tune to mhz and run SPEC for dwell_ms. bins receives the combined
     * power codes (natural FFT order, n entries); returns false on error. */
    bool spectrum(unsigned mhz, unsigned rate_code, unsigned n, unsigned dwell_ms, bool max_hold, uint8_t *bins);
    const char *info() const { return info_; }

  private:
    bool read_line(char *line, size_t size, uint32_t timeout_ms);
    bool read_exact(uint8_t *buffer, size_t size, uint32_t timeout_ms);
    HardwareSerial &port_ = Serial1;
    bool ok_ = false;
    char info_[48] = "";
};
