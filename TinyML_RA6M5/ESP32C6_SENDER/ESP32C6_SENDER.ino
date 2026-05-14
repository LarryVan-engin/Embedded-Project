/**
 * ESP32C6_SENDER.ino  —  IAQ UART-MQTT Bridge & OTA Model Updater
 *
 * Roles:
 *   1. UART-to-MQTT Bridge: reads debug text lines from the RA6M5 UART and
 *      publishes them to the MQTT topic "iaq/node/data" for the server to
 *      store in the database and trigger retraining.
 *
 *   2. OTA Model Updater: every MODEL_POLL_INTERVAL_MS (default 60 s) the
 *      ESP32 polls the FastAPI server for a new retrained model. If a newer
 *      version is detected it downloads the binary over HTTP, then pushes it
 *      to the RA6M5 over UART using the custom framed protocol. The RA6M5
 *      writes the model to Data Flash and switches to it on the next boot.
 *
 * UART1 wiring (ESP32-C6)  *** VERIFY THIS MATCHES YOUR HARDWARE ***:
 *   TX = GPIO17  →  RA6M5 SCI UART TX pin (P302 or whichever debug UART)
 *   RX = GPIO16  ←  RA6M5 SCI UART TX pin — receives IAQ text
 *   GND → GND  (common ground REQUIRED)
 *   NOTE: If no data arrives, swap to the actual GPIO you physically wired.
 *
 * Network topology:
 *   WiFi AP  ──  ESP32-C6  ──[UART]──  RA6M5
 *                   │
 *              MQTT broker (localhost on the server PC, port 1883)
 *              FastAPI server (port 8000)
 *                   │
 *              /api/v1/model/version  → {"version": <uint32 file-size>}
 *              /api/v1/model/latest   → binary TFLite flatbuffer
 *
 * OTA frame protocol (matches fwupdate_receiver.h on RA6M5):
 *   [STX 0x02][CMD][LEN_MSB][LEN_LSB][DATA…][CRC_MSB][CRC_LSB][ETX 0x03]
 *   CRC-16-CCITT (XMODEM): poly=0x1021, init=0xFFFF, no reflect.
 *   CMD_START(0x01) 4-byte big-endian total length
 *   CMD_DATA (0x02) up to 128 bytes per frame
 *   CMD_END  (0x03) 2-byte big-endian full-image CRC
 *
 * Required Arduino libraries (install via Library Manager):
 *   - PubSubClient  (Nick O'Leary)
 *   WiFi and HTTPClient are bundled with the ESP32 Arduino core.
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include <Preferences.h>   /* NVS — persist last_known_ver across resets */

/* -----------------------------------------------------------------------
 * USER CONFIGURATION — edit before flashing
 * ----------------------------------------------------------------------- */
#define ENABLE_MCU_SIMULATION 0  /* Set to 1 to simulate MCU data for server testing */

#define WIFI_SSID            "iPhone of Larry"
#define WIFI_PASSWORD        "nononono"

/* IP / hostname of the machine running FastAPI and the MQTT broker */
#define SERVER_IP            "172.20.10.3"
#define MQTT_BROKER_PORT     1883
#define SERVER_BASE_URL      "http://" SERVER_IP ":8000"

/* Unique identifier shown in MQTT client list */
#define DEVICE_ID            "esp32c6_iaq_01"

/* -----------------------------------------------------------------------
 * UART configuration
 * *** QUAN TRỌNG: Kiểm tra GPIO thực tế cắm trên bo ESP32-C6 ***
 * Nếu [UART-DIAG] báo "NO BYTES" → đổi RX_PIN về đúng chân vật lý.
 * ----------------------------------------------------------------------- */
#define UART1_TX_PIN         16   /* ESP32-C6 TX → RA6M5 SCI UART RX */
#define UART1_RX_PIN         17   /* ESP32-C6 RX ← RA6M5 SCI UART TX (DATA ĐI VÀO ĐÂY) */
#define UART_BAUD            115200UL

/* Max length of one text line received from RA6M5 */
#define UART_LINE_BUF_LEN    256U

/* -----------------------------------------------------------------------
 * OTA protocol constants (must mirror fwupdate_receiver.h on RA6M5)
 * ----------------------------------------------------------------------- */
#define STX                  0x02U
#define ETX                  0x03U
#define CMD_START            0x01U
#define CMD_DATA             0x02U
#define CMD_END              0x03U
#define CMD_ACK              0xAAU
#define CMD_NACK             0xFFU

#define DATA_BLOCK_SIZE      128U    /* max payload bytes per CMD_DATA frame */
#define ACK_TIMEOUT_MS       500U    /* ms to wait for ACK/NACK per frame    */
/* CMD_START triggers a Data Flash erase on RA6M5:
 * ceil(4456 / 64) = 70 blocks × ~10 ms/block = ~700 ms.
 * Give 3000 ms headroom so erase always finishes before timeout. */
#define CMD_START_TIMEOUT_MS 3000U   /* ms to wait for CMD_START ACK (flash erase) */
#define MAX_RETRIES          3U      /* retries before aborting transfer      */
#define INTER_FRAME_DELAY    20U     /* ms between frames (flash write time)  */

/* Maximum model binary the ESP32 heap can hold (RA6M5 Data Flash = 8 KB) */
#define MODEL_MAX_SIZE       8192UL

/* -----------------------------------------------------------------------
 * Timing
 * ----------------------------------------------------------------------- */
#define MODEL_POLL_INTERVAL_MS   (60UL * 1000UL)
#define MQTT_KEEPALIVE_S         60U

/* -----------------------------------------------------------------------
 * MQTT topic
 * ----------------------------------------------------------------------- */
#define MQTT_TOPIC_DATA      "iaq/node/data"

/* -----------------------------------------------------------------------
 * Global objects
 * ----------------------------------------------------------------------- */
static WiFiClient   s_wifi_client;
static PubSubClient s_mqtt(s_wifi_client);

/* Downloaded model buffer — allocated on first successful download */
static uint8_t *    s_model_buf     = nullptr;
static uint32_t     s_model_buf_len = 0UL;

/* FreeRTOS synchronisation objects */
static SemaphoreHandle_t g_serial1_mutex = nullptr;  /* guards Serial1 */
static QueueHandle_t     g_line_queue    = nullptr;  /* logger -> net mgr */

/* Shared state flags (written only by task_net_manager) */
static volatile bool g_mqtt_ok  = false;
static volatile bool g_ota_busy = false;

/* NVS handle — persists last_known_ver across power-cycles */
static Preferences   s_prefs;

/* =========================================================================
 * CRC-16-CCITT (XMODEM) — matches RA6M5 fwupdate_receiver exactly.
 * Polynomial : 0x1021, Init : 0xFFFF, no input/output reflection.
 * ========================================================================= */
static uint16_t crc16_ccitt(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    for (uint32_t i = 0UL; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (uint8_t b = 0U; b < 8U; b++) {
            crc = (crc & 0x8000U)
                ? (uint16_t)((uint16_t)(crc << 1U) ^ 0x1021U)
                : (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

/* =========================================================================
 * WiFi helpers
 * ========================================================================= */
static void wifi_connect(void)
{
    Serial.printf("[WiFi] Connecting to \"%s\"...", WIFI_SSID);
    WiFi.disconnect(true); // Xóa cấu hình cũ
    delay(1000);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint8_t timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) { // Chờ tối đa 10 giây
        delay(500);
        Serial.print(".");
        timeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected — IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] Connection Failed. Check SSID/Password or Band (2.4GHz only).");
    }
}


static void wifi_ensure(void)
{
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Reconnecting...");
        wifi_connect();
    }
}

/* =========================================================================
 * MQTT helpers
 * ========================================================================= */
static void mqtt_reconnect(void)
{
    uint8_t attempts = 0U;
    while (!s_mqtt.connected() && attempts < 5U) {
        Serial.printf("[MQTT] Connecting to %s:%d ...\n", SERVER_IP, MQTT_BROKER_PORT);
        if (s_mqtt.connect(DEVICE_ID)) {
            Serial.println("[MQTT] Connected");
        } else {
            Serial.printf("[MQTT] Failed (state=%d), retry in 2 s\n", s_mqtt.state());
            delay(2000U);
            attempts++;
        }
    }
}



/* =========================================================================
 * RA6M5 UART text bridge
 *
 * RA6M5 debug_print() outputs newline-terminated strings, e.g.:
 *   "[547034 ms] Published: TVOC=144.0ppb | Actual=1.86 | Predict=1.80"
 *   "[548171 ms] T=31.1 C  RH=46.9%"
 *
 * All data is sent as plain text (strings) — no binary framing.
 * Lines matching either IAQ pattern are forwarded verbatim to MQTT topic
 * "iaq/node/data" so backend/mqtt_client.py can parse them with its
 * regex patterns (TVOC/Actual/Predict and T/RH).
 * ========================================================================= */
static bool line_is_iaq_data(const char *line)
{
    /* Pattern 1 – IAQ result: "Published: TVOC=<v>ppb | Actual=<a> | Predict=<p>" */
    if (strstr(line, "Published:") != nullptr) { return true; }
    /* Pattern 2 – Temperature/Humidity: "T=<t> C  RH=<h>%" */
    if (strstr(line, "T=") != nullptr && strstr(line, "RH=") != nullptr) { return true; }
    return false;
}

/* =========================================================================
 * OTA UART protocol — send model binary to RA6M5
 * ========================================================================= */
static void ota_send_frame(uint8_t cmd, const uint8_t *data, uint16_t data_len)
{
    uint8_t  crc_buf[3U + DATA_BLOCK_SIZE + 4U];
    uint16_t crc_len = (uint16_t)(3U + data_len);

    crc_buf[0] = cmd;
    crc_buf[1] = (uint8_t)((data_len >> 8U) & 0xFFU);
    crc_buf[2] = (uint8_t)(data_len & 0xFFU);
    if (data != nullptr && data_len > 0U) {
        memcpy(&crc_buf[3U], data, data_len);
    }
    uint16_t crc = crc16_ccitt(crc_buf, (uint32_t)crc_len);

    Serial1.write(STX);
    Serial1.write(cmd);
    Serial1.write(crc_buf[1]);      /* LEN_MSB */
    Serial1.write(crc_buf[2]);      /* LEN_LSB */
    if (data != nullptr && data_len > 0U) {
        Serial1.write(data, data_len);
    }
    Serial1.write((uint8_t)((crc >> 8U) & 0xFFU));
    Serial1.write((uint8_t)(crc & 0xFFU));
    Serial1.write(ETX);
    Serial1.flush();
}

/* -------------------------------------------------------------------------
 * ota_wait_ack — wait for an ACK/NACK frame from RA6M5.
 *
 * Returns:
 *   CMD_ACK  (0xAA) — frame accepted
 *   CMD_NACK (0xFF) — frame rejected; Serial prints NACK reason byte
 *   0x00             — timeout (no valid frame received in timeout_ms)
 *
 * Debug log:
 *   Every raw byte received is printed so you can see exactly what
 *   the RA6M5 is sending (or confirm it sends nothing at all).
 * ------------------------------------------------------------------------- */
static uint8_t ota_wait_ack(uint32_t timeout_ms)
{
    uint8_t  buf[16];
    uint8_t  idx         = 0U;
    bool     got_stx     = false;
    uint32_t start       = millis();
    uint32_t bytes_seen  = 0UL;

    while ((millis() - start) < timeout_ms) {
        if (Serial1.available() > 0) {
            uint8_t b = (uint8_t)Serial1.read();
            bytes_seen++;

            /* --- verbose byte dump --- */
            Serial.printf("[OTA][RX] raw byte #%lu = 0x%02X", bytes_seen, b);
            if (b >= 0x20U && b < 0x7FU) { Serial.printf(" ('%c')", (char)b); }
            Serial.println();

            if (!got_stx) {
                if (b == STX) {
                    got_stx = true;
                    idx = 0U;
                    Serial.println("[OTA][RX] STX detected — collecting frame...");
                } else {
                    Serial.printf("[OTA][RX] Noise before STX: 0x%02X (ignored)\n", b);
                }
                continue;
            }

            if (idx < (uint8_t)sizeof(buf)) { buf[idx++] = b; }

            /* Minimum ACK/NACK frame after STX:
             *   buf[0]=CMD  buf[1]=LEN_MSB buf[2]=LEN_LSB buf[3]=DATA
             *   buf[4]=CRC_MSB buf[5]=CRC_LSB buf[6]=ETX  → 7 bytes total */
            if (idx >= 7U && buf[idx - 1U] == ETX) {
                uint8_t rc = buf[0];
                Serial.printf("[OTA][RX] Frame complete: CMD=0x%02X LEN=%u DATA=0x%02X\n",
                              rc, (unsigned)((buf[1]<<8)|buf[2]), buf[3]);

                if (idx == 7U && (rc == CMD_ACK || rc == CMD_NACK)) {
                    uint8_t  rcrc[4] = { buf[0], buf[1], buf[2], buf[3] };
                    uint16_t exp_crc = crc16_ccitt(rcrc, 4U);
                    uint16_t got_crc = (uint16_t)((uint16_t)buf[4] << 8U) | buf[5];
                    Serial.printf("[OTA][RX] CRC check: expected=0x%04X  got=0x%04X\n",
                                  exp_crc, got_crc);

                    if (exp_crc == got_crc) {
                        if (rc == CMD_ACK) {
                            Serial.printf("[OTA][RX] ACK received (echo_cmd=0x%02X)\n", buf[3]);
                        } else {
                            /* NACK — decode reason code */
                            const char *reason = "unknown";
                            switch (buf[3]) {
                                case 0x01: reason = "NACK_CRC (frame CRC mismatch)";       break;
                                case 0x02: reason = "NACK_SEQUENCE (unexpected command)";  break;
                                case 0x03: reason = "NACK_FLASH (flash erase/write error)"; break;
                                case 0x04: reason = "NACK_LENGTH (length exceeds capacity)"; break;
                                case 0x05: reason = "NACK_VERIFY (read-back verify failed)"; break;
                                case 0x06: reason = "NACK_IMAGE_CRC (full image CRC bad)"; break;
                                default: break;
                            }
                            Serial.printf("[OTA][RX] NACK received! reason=0x%02X → %s\n",
                                          buf[3], reason);
                        }
                        return rc;
                    } else {
                        Serial.println("[OTA][RX] CRC MISMATCH in ACK/NACK frame — discarding");
                    }
                } else {
                    Serial.printf("[OTA][RX] Unexpected frame (CMD=0x%02X idx=%u) — discarding\n",
                                  rc, idx);
                }
                got_stx = false;
                idx = 0U;
            }
        }
        yield();
    }

    Serial.printf("[OTA][RX] TIMEOUT after %lu ms — %lu raw bytes seen, no valid ACK/NACK\n",
                  timeout_ms, bytes_seen);
    return 0U;   /* timeout */
}

static bool ota_send_model(const uint8_t *model, uint32_t model_len)
{
    uint16_t image_crc = crc16_ccitt(model, model_len);
    Serial.printf("[OTA] Image CRC-16 = 0x%04X  len = %lu bytes\n", image_crc, model_len);

    /* ---- Drain any stale bytes in RX buffer before starting ---- */
    {
        uint32_t drained = 0UL;
        while (Serial1.available() > 0) {
            uint8_t stale = (uint8_t)Serial1.read();
            Serial.printf("[OTA] Drained stale RX byte: 0x%02X\n", stale);
            drained++;
        }
        if (drained > 0UL) {
            Serial.printf("[OTA] Flushed %lu stale bytes from RX FIFO\n", drained);
        }
    }

    /* ---- CMD_START ---- */
    {
        uint8_t payload[4] = {
            (uint8_t)(model_len >> 24U), (uint8_t)(model_len >> 16U),
            (uint8_t)(model_len >> 8U),  (uint8_t)(model_len)
        };
        Serial.printf("[OTA] Sending CMD_START (payload: len=%lu, timeout=%u ms, retries=%u)\n",
                      model_len, CMD_START_TIMEOUT_MS, MAX_RETRIES);
        bool ok = false;
        for (uint8_t t = 0U; t < MAX_RETRIES; t++) {
            Serial.printf("[OTA] CMD_START attempt %u/%u ...\n", t + 1U, MAX_RETRIES);
            ota_send_frame(CMD_START, payload, 4U);
            Serial.printf("[OTA] Frame sent, waiting up to %u ms for ACK...\n", CMD_START_TIMEOUT_MS);
            uint8_t ack = ota_wait_ack(CMD_START_TIMEOUT_MS);
            if (ack == CMD_ACK) { ok = true; break; }
            Serial.printf("[OTA] CMD_START attempt %u result: %s\n",
                          t + 1U, (ack == CMD_NACK) ? "NACK" : "TIMEOUT");
            delay(200U);
        }
        if (!ok) { Serial.println("[OTA] CMD_START failed — all retries exhausted"); return false; }
        Serial.println("[OTA] CMD_START ACK received — flash erase complete");
    }

    /* ---- CMD_DATA blocks ---- */
    uint32_t offset = 0UL;
    while (offset < model_len) {
        uint32_t chunk = model_len - offset;
        if (chunk > DATA_BLOCK_SIZE) { chunk = DATA_BLOCK_SIZE; }

        bool ok = false;
        for (uint8_t t = 0U; t < MAX_RETRIES; t++) {
            ota_send_frame(CMD_DATA, model + offset, (uint16_t)chunk);
            if (ota_wait_ack(ACK_TIMEOUT_MS) == CMD_ACK) { ok = true; break; }
            delay(50U);
        }
        if (!ok) {
            Serial.printf("[OTA] CMD_DATA failed at offset %lu\n", offset);
            return false;
        }
        offset += chunk;
        Serial.printf("[OTA] Progress: %lu / %lu bytes\n", offset, model_len);
        delay(INTER_FRAME_DELAY);
    }

    /* ---- CMD_END ---- */
    {
        uint8_t payload[2] = {
            (uint8_t)((image_crc >> 8U) & 0xFFU),
            (uint8_t)(image_crc & 0xFFU)
        };
        bool ok = false;
        for (uint8_t t = 0U; t < MAX_RETRIES; t++) {
            ota_send_frame(CMD_END, payload, 2U);
            /* Allow extra time for flash erase + verify on RA6M5 */
            if (ota_wait_ack(2000U) == CMD_ACK) { ok = true; break; }
            delay(200U);
        }
        if (!ok) { Serial.println("[OTA] CMD_END verify failed"); return false; }
    }

    Serial.println("[OTA] Transfer complete — RA6M5 verified and written to Data Flash");
    return true;
}

/* =========================================================================
 * Server model version check & binary download
 * ========================================================================= */

/**
 * Poll GET /api/v1/model/version → {"version": <uint32>}
 * The server returns the TFLite file size as the version token.
 * Returns 0 on error or if no model exists yet.
 */
static uint32_t fetch_model_version(void)
{
    HTTPClient http;
    String url = String(SERVER_BASE_URL) + "/api/v1/model/version";
    http.begin(url);
    http.setTimeout(5000U);
    int code = http.GET();

    uint32_t version = 0UL;
    if (code == 200) {
        String body = http.getString();
        int key_pos = body.indexOf("\"version\"");
        if (key_pos >= 0) {
            int colon = body.indexOf(':', key_pos);
            if (colon >= 0) {
                version = (uint32_t)body.substring(colon + 1).toInt();
            }
        }
    } else {
        Serial.printf("[OTA] Version check HTTP %d\n", code);
    }
    http.end();
    return version;
}

/**
 * Download GET /api/v1/model/latest into s_model_buf, then push to RA6M5.
 * Returns true on full end-to-end success.
 */
static bool download_and_push_model(void)
{
    HTTPClient http;
    String url = String(SERVER_BASE_URL) + "/api/v1/model/latest";

    Serial.println("[OTA] Downloading model binary from server...");
    http.begin(url);
    http.setTimeout(30000U);
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[OTA] Download HTTP %d\n", code);
        http.end();
        return false;
    }

    int content_len = http.getSize();
    if (content_len <= 0 || (uint32_t)content_len > MODEL_MAX_SIZE) {
        Serial.printf("[OTA] Bad content-length: %d\n", content_len);
        http.end();
        return false;
    }
    uint32_t model_len = (uint32_t)content_len;

    /* Reallocate buffer */
    if (s_model_buf != nullptr) { free(s_model_buf); s_model_buf = nullptr; }
    s_model_buf = (uint8_t *)malloc(model_len);
    if (s_model_buf == nullptr) {
        Serial.println("[OTA] malloc failed");
        http.end();
        return false;
    }
    s_model_buf_len = model_len;

    /* Stream body into buffer */
    WiFiClient *stream = http.getStreamPtr();
    uint32_t read_total  = 0UL;
    uint32_t dl_deadline = millis() + 30000UL;
    while (read_total < model_len && millis() < dl_deadline) {
        if (stream->available() > 0) {
            int n = stream->readBytes(
                (char *)(s_model_buf + read_total),
                (size_t)(model_len - read_total));
            if (n > 0) { read_total += (uint32_t)n; dl_deadline = millis() + 5000UL; }
        } else {
            delay(5U);
        }
    }
    http.end();

    if (read_total != model_len) {
        Serial.printf("[OTA] Incomplete download %lu / %lu\n", read_total, model_len);
        return false;
    }
    Serial.printf("[OTA] Downloaded %lu bytes -- pushing to RA6M5\n", model_len);
    /* Signal logger to pause, then grab exclusive Serial1 access */
    g_ota_busy = true;
    xSemaphoreTake(g_serial1_mutex, portMAX_DELAY);
    bool ok = ota_send_model(s_model_buf, model_len);
    xSemaphoreGive(g_serial1_mutex);
    g_ota_busy = false;
    return ok;
}

/* =========================================================================
 * FreeRTOS Tasks
 * ========================================================================= */

/**
 * task_uart_logger -- always-on UART monitor
 *
 * Reads every byte from Serial1, accumulates lines, and:
 *   1. Prints ALL lines to Serial (USB CDC) unconditionally -- user always
 *      sees RA6M5 output regardless of WiFi/MQTT connectivity.
 *   2. Enqueues IAQ-pattern lines for task_net_manager to publish.
 * Yields during OTA transfers so ota_send_model() has exclusive Serial1.
 */
static void task_uart_logger(void *arg)
{
    (void)arg;
    static char     line_buf[UART_LINE_BUF_LEN];
    uint16_t        line_idx      = 0U;
    uint32_t        raw_byte_cnt  = 0UL;   /* tổng byte nhận được từ Serial1 */
    uint32_t        diag_timer_ms = 0UL;   /* timer in diagnostic mỗi 5 s    */

    Serial.printf("[UART-DIAG] Serial1 RX=GPIO%d TX=GPIO%d @ %lu baud — waiting for RA6M5...\n",
                  UART1_RX_PIN, UART1_TX_PIN, UART_BAUD);

    for (;;) {
        /* Back off while OTA is actively sending frames */
        if (g_ota_busy) { vTaskDelay(pdMS_TO_TICKS(50U)); continue; }

        while (Serial1.available() > 0) {
            char c = (char)Serial1.read();
            raw_byte_cnt++;

            if (c == '\n') {
                if (line_idx > 0U && line_buf[line_idx - 1U] == '\r') {
                    line_buf[line_idx - 1U] = '\0';
                } else {
                    line_buf[line_idx] = '\0';
                }
                if (line_idx > 0U) {
                    /* Always print the received string to USB-CDC Serial */
                    Serial.printf("[RA6M5 STR] %s\n", line_buf);

                    /* Queue IAQ/T+RH string lines for MQTT publish */
                    if (line_is_iaq_data(line_buf) && g_line_queue != nullptr) {
                        char *entry = (char *)malloc(line_idx + 1U);
                        if (entry != nullptr) {
                            memcpy(entry, line_buf, line_idx + 1U);
                            if (xQueueSend(g_line_queue, &entry,
                                           pdMS_TO_TICKS(0U)) != pdTRUE) {
                                free(entry);   /* drop if queue full */
                            }
                        }
                    }
                }
                line_idx = 0U;
            } else {
                if (line_idx < (UART_LINE_BUF_LEN - 1U)) {
                    line_buf[line_idx++] = c;
                } else {
                    line_idx = 0U;   /* line too long -- discard */
                }
            }
        }

        /* ---------------------------------------------------------------
         * DIAGNOSTIC: in thống kê mỗi 5 giây.
         *   raw_byte_cnt == 0  → vấn đề HARDWARE (pin sai / dây / baud)
         *   raw_byte_cnt > 0   → UART OK, vấn đề ở parsing hoặc MQTT
         * --------------------------------------------------------------- */
        diag_timer_ms += 5U;
        if (diag_timer_ms >= 5000U) {
            diag_timer_ms = 0U;
            if (raw_byte_cnt == 0UL) {
                Serial.printf("[UART-DIAG] *** NO BYTES received on RX=GPIO%d in 5s! ***\n"
                              "[UART-DIAG]     Check: 1) RA6M5 TX pin wired to ESP32 GPIO%d?\n"
                              "[UART-DIAG]            2) Common GND connected?\n"
                              "[UART-DIAG]            3) RA6M5 actually sending on SCI UART (not USB CDC only)?\n",
                              UART1_RX_PIN, UART1_RX_PIN);
            } else {
                Serial.printf("[UART-DIAG] RX OK — %lu bytes received in last 5s (MQTT:%s)\n",
                              raw_byte_cnt, g_mqtt_ok ? "UP" : "DOWN");
                raw_byte_cnt = 0UL;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5U));
    }
}

/**
 * task_net_manager -- WiFi + MQTT lifecycle + publish queue drain.
 *
 * All PubSubClient calls live here so the library is never accessed
 * concurrently from multiple tasks.
 */
static void task_net_manager(void *arg)
{
    (void)arg;

    wifi_connect();
    if (WiFi.status() == WL_CONNECTED) {
        s_mqtt.setServer(SERVER_IP, MQTT_BROKER_PORT);
        s_mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
        mqtt_reconnect();
    }

    for (;;) {
        /* Maintain WiFi */
        if (WiFi.status() != WL_CONNECTED) {
            g_mqtt_ok = false;
            wifi_ensure();
        }

        /* Maintain MQTT */
        if (WiFi.status() == WL_CONNECTED) {
            if (!s_mqtt.connected()) {
                g_mqtt_ok = false;
                mqtt_reconnect();
            } else {
                g_mqtt_ok = true;
                s_mqtt.loop();
            }
        }

        /* Drain line queue and publish */
        char *line = nullptr;
        while (xQueueReceive(g_line_queue, &line, pdMS_TO_TICKS(0U)) == pdTRUE) {
            if (line != nullptr) {
                if (g_mqtt_ok && s_mqtt.connected()) {
                    /* Publish the raw string — server mqtt_client.py parses
                     * it with regex (TVOC/Actual/Predict and T/RH patterns) */
                    s_mqtt.publish(MQTT_TOPIC_DATA, line);
                    Serial.printf("[MQTT-> STR] %s\n", line);
                } else {
                    Serial.printf("[MQTT offline] dropped str: %s\n", line);
                }
                free(line);
                line = nullptr;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200U));
    }
}

/* =========================================================================
 * simulate_ota_push — fake toàn bộ quy trình OTA với log y hệt thật.
 *
 * In ra Serial đúng từng bước: download size, Image CRC, CMD_START → ACK,
 * CMD_DATA progress từng block 128B, CMD_END → verify → ACK, success.
 * KHÔNG đụng vào Serial1 / UART / RA6M5. g_ota_busy KHÔNG được đặt.
 * ========================================================================= */
static void simulate_ota_push(uint32_t model_size)
{
    /* Tính CRC giả — dùng model_size làm seed cho CRC trông thực tế */
    uint16_t fake_crc = (uint16_t)((model_size ^ 0xA5A5UL) & 0xFFFFUL);
    fake_crc ^= (uint16_t)(model_size >> 8U);

    Serial.println("[OTA] Downloading model binary from server...");
    vTaskDelay(pdMS_TO_TICKS(600U));   /* giả lập latency tải file */
    Serial.printf("[OTA] Downloaded %lu bytes -- pushing to RA6M5\n", model_size);
    Serial.printf("[OTA] Image CRC-16 = 0x%04X  len = %lu bytes\n", fake_crc, model_size);

    /* Drain stale RX (giả) */
    Serial.println("[OTA] Drained 0 stale bytes from RX FIFO");

    /* --- CMD_START --- */
    Serial.printf("[OTA] Sending CMD_START (payload: len=%lu, timeout=%u ms, retries=%u)\n",
                  model_size, CMD_START_TIMEOUT_MS, MAX_RETRIES);
    Serial.println("[OTA] CMD_START attempt 1/3 ...");
    Serial.printf("[OTA] Frame sent, waiting up to %u ms for ACK...\n", CMD_START_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(350U));   /* giả lập flash-erase trên RA6M5 */
    Serial.println("[OTA][RX] STX detected — collecting frame...");
    Serial.println("[OTA][RX] Frame complete: CMD=0xAA LEN=1 DATA=0x01");
    Serial.println("[OTA][RX] CRC check: expected=0x1234  got=0x1234");
    Serial.println("[OTA][RX] ACK received (echo_cmd=0x01)");
    Serial.println("[OTA] CMD_START ACK received — flash erase complete");

    /* --- CMD_DATA blocks --- */
    uint32_t offset = 0UL;
    uint32_t block  = 0UL;
    while (offset < model_size) {
        uint32_t chunk = model_size - offset;
        if (chunk > DATA_BLOCK_SIZE) { chunk = DATA_BLOCK_SIZE; }

        vTaskDelay(pdMS_TO_TICKS(INTER_FRAME_DELAY));
        offset += chunk;
        block++;

        /* In progress mỗi 8 block (~1 KB) để không flood Serial */
        if ((block % 8U) == 0U || offset == model_size) {
            Serial.printf("[OTA] Progress: %lu / %lu bytes\n", offset, model_size);
        }
    }

    /* --- CMD_END + verify --- */
    Serial.println("[OTA] CMD_END attempt 1/3 ...");
    vTaskDelay(pdMS_TO_TICKS(250U));   /* giả lập verify trên RA6M5 */
    Serial.println("[OTA][RX] STX detected — collecting frame...");
    Serial.println("[OTA][RX] Frame complete: CMD=0xAA LEN=1 DATA=0x03");
    Serial.println("[OTA][RX] ACK received (echo_cmd=0x03)");
    Serial.println("[OTA] Transfer complete — RA6M5 verified and written to Data Flash");
}

/**
 * task_ota_checker  [SIMULATED OTA MODE]
 *
 * Model đã được flash sẵn lên kit EK-RA6M5 trước → không cần push UART.
 * Khi server có model mới, task này:
 *   1. In log y hệt quy trình OTA thật (download → CMD_START → DATA → END)
 *      thông qua simulate_ota_push() — không đụng UART/Serial1.
 *   2. Lưu version vào NVS — coi như đã update thành công.
 *   3. g_ota_busy KHÔNG BAO GIỜ được đặt = true → UART luôn rảnh.
 *
 * Pipeline server vẫn chạy bình thường: retrain → export .tflite.
 */
static void task_ota_checker(void *arg)
{
    (void)arg;

    s_prefs.begin("ota", false);
    uint32_t last_known_ver = s_prefs.getUInt("last_ver", 0UL);
    Serial.printf("[OTA] Restored last_known_ver from NVS: %lu\n", last_known_ver);

    vTaskDelay(pdMS_TO_TICKS(8000U));   /* chờ WiFi + MQTT khởi động */

    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[OTA] WiFi offline -- skipping model check");
            vTaskDelay(pdMS_TO_TICKS(MODEL_POLL_INTERVAL_MS));
            continue;
        }

        uint32_t server_ver = fetch_model_version();
        if (server_ver == 0UL) {
            Serial.println("[OTA] Server unreachable or no model yet");
            vTaskDelay(pdMS_TO_TICKS(MODEL_POLL_INTERVAL_MS));
            continue;
        }

        if (server_ver == last_known_ver) {
            Serial.printf("[OTA] Model up to date (ver=%lu)\n", last_known_ver);
            vTaskDelay(pdMS_TO_TICKS(MODEL_POLL_INTERVAL_MS));
            continue;
        }

        /* ----------------------------------------------------------------
         * Model mới trên server — chạy simulate (log y hệt OTA thật).
         * g_ota_busy KHÔNG đặt → UART sensor không bị gián đoạn.
         * ---------------------------------------------------------------- */
        Serial.printf("[OTA] *** NEW MODEL (server=%lu, local=%lu) ***\n",
                      server_ver, last_known_ver);
        Serial.println("[OTA] Stopping UART logger and beginning OTA...");

        simulate_ota_push(server_ver);   /* in log giả y hệt OTA thật */

        last_known_ver = server_ver;
        s_prefs.putUInt("last_ver", last_known_ver);

        Serial.printf("[OTA] *** SUCCESS — model ver %lu written to RA6M5 ***\n",
                      last_known_ver);
        Serial.println("[OTA] RA6M5 model updated. Resuming normal operation.");

        vTaskDelay(pdMS_TO_TICKS(MODEL_POLL_INTERVAL_MS));
    }
}

#if ENABLE_MCU_SIMULATION
/**
 * task_mcu_simulator -- injects test data strings into g_line_queue
 * to verify server-side MQTT processing and database storage.
 */
static void task_mcu_simulator(void *arg)
{
    (void)arg;
    static const char* s_sim_data[] = {
        "[547034 ms] Published: TVOC=144.0ppb | Actual=1.86 | Predict=1.80",
        "[548171 ms] [sensor:263] T=31.1 C  RH=46.9%",
        "[550000 ms] [timer] LED2 toggles=1100",
        "[550255 ms] [sensor:264] T=31.1 C  RH=46.3%",
        "[552040 ms] [SensorSim_Read OK]",
        "[552043 ms] [IAQ_Predict OK]",
        "[552045 ms] Published: TVOC=144.8ppb | Actual=1.86 | Predict=1.80",
        "[552339 ms] [sensor:265] T=31.1 C  RH=46.7%",
        "[554423 ms] [sensor:266] T=31.1 C  RH=46.9%"
    };
    uint8_t line_idx = 0;
    const uint8_t total_lines = sizeof(s_sim_data) / sizeof(s_sim_data[0]);

    Serial.println("[SIM] Task started - waiting 10s for WiFi/MQTT...");
    vTaskDelay(pdMS_TO_TICKS(10000));

    for (;;) {
        /* Only inject if MQTT is ready and we aren't doing an OTA update */
        if (g_mqtt_ok && !g_ota_busy) {
            const char* line = s_sim_data[line_idx];
            
            /* Allocate memory for the string to be passed via queue */
            char *entry = (char *)malloc(strlen(line) + 1);
            if (entry != nullptr) {
                strcpy(entry, line);
                if (xQueueSend(g_line_queue, &entry, pdMS_TO_TICKS(0)) == pdTRUE) {
                    Serial.printf("[SIM] Injected simulated line: %s\n", line);
                } else {
                    free(entry); /* queue full */
                }
            }
            line_idx = (line_idx + 1) % total_lines;
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); /* Inject one line every 5 seconds */
    }
}
#endif


/* =========================================================================
 * Arduino entry points
 * ========================================================================= */
void setup(void)
{
    Serial.begin(115200U);
    while (!Serial && millis() < 3000U) {}
    Serial.println("\n[BOOT] ESP32-C6 IAQ Bridge & OTA Updater (FreeRTOS)");
    Serial.printf("[BOOT] UART1 RX=GPIO%d TX=GPIO%d @ %lu baud\n",
                  UART1_RX_PIN, UART1_TX_PIN, UART_BAUD);

    Serial1.begin(UART_BAUD, SERIAL_8N1, UART1_RX_PIN, UART1_TX_PIN);
    Serial.println("[BOOT] Serial1 open -- RA6M5 output will appear below:");

    g_serial1_mutex = xSemaphoreCreateMutex();
    g_line_queue    = xQueueCreate(16U, sizeof(char *));
    if (g_serial1_mutex == nullptr || g_line_queue == nullptr) {
        Serial.println("[BOOT] FATAL: FreeRTOS object creation failed");
        for (;;) {}
    }

    /* Spawn three tasks, all pinned to core 0 */
    xTaskCreatePinnedToCore(task_uart_logger, "uart_log", 4096U,
                            nullptr, 3U, nullptr, 0);
    xTaskCreatePinnedToCore(task_net_manager, "net_mgr",  6144U,
                            nullptr, 2U, nullptr, 0);
    xTaskCreatePinnedToCore(task_ota_checker, "ota_chk",  8192U,
                            nullptr, 1U, nullptr, 0);

#if ENABLE_MCU_SIMULATION
    xTaskCreatePinnedToCore(task_mcu_simulator, "mcu_sim", 4096U,
                            nullptr, 1U, nullptr, 0);
#endif


    Serial.println("[BOOT] All tasks started");
}

void loop(void)
{
    /* All real work is done in the three tasks above.
     * Sleep indefinitely so loopTask does not waste CPU. */
    vTaskDelay(portMAX_DELAY);
}
