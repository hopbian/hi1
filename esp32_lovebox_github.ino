/*
 * =========================================================================
 *    HỘP TÌNH YÊU 💕 - ESP32 FIRMWARE (BẢO MẬT BẰNG DEVICE KEY + OTA)
 *   - Kết nối Cloudflare Worker API (0đ, 24/24, siêu nhanh 15 giây kiểm tra 1 lần)
 *   - Màn hình: 240x320 TFT SPI (ST7789 / ILI9341 - Chuẩn ROHS)
 *   - Tích hợp OTA: Nạp code không dây qua Wi-Fi
 * =========================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <ArduinoJson.h>

// ==================== 1. CẤU HÌNH WI-FI ====================
const char* WIFI_SSID     = "VINH NHUNG";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD"; // Thay mật khẩu Wi-Fi thực tế

// ==================== 2. CẤU HÌNH HỘP BẢO MẬT ====================
// Lấy BOX_ID và DEVICE_KEY trong bảng Quản Lý Admin trên web hopbian.github.io/hi1/
const char* API_BASE   = "https://lovebox-api.tonepite.workers.dev";
const char* BOX_ID     = "LOVE01";
const char* DEVICE_KEY = "PASTE_DEVICE_KEY_HERE";

// Kiểm tra ảnh mới mỗi 15 giây (Cloudflare miễn phí 100.000 lượt/ngày, rất thoải mái)
const unsigned long POLL_INTERVAL_MS = 15000;

TFT_eSPI tft = TFT_eSPI();

String currentMode = "current";
String targetPhotoId = "";
String displayedPhotoId = "";
unsigned long slideIntervalMs = 30000;

#define MAX_PHOTOS 30
String photoIds[MAX_PHOTOS];
int photoCount = 0;
int slideIndex = 0;

unsigned long lastPollTime = 0;
unsigned long lastSlideTime = 0;

class JpgMemStream : public Stream {
public:
    uint8_t* buf;
    size_t cap;
    size_t len;
    JpgMemStream(size_t maxCap) : cap(maxCap), len(0) {
        buf = (uint8_t*)malloc(cap);
    }
    ~JpgMemStream() {
        if (buf) free(buf);
    }
    size_t write(uint8_t b) override {
        if (!buf || len >= cap) return 0;
        buf[len++] = b;
        return 1;
    }
    size_t write(const uint8_t* data, size_t size) override {
        if (!buf) return 0;
        size_t avail = cap - len;
        size_t toCopy = size < avail ? size : avail;
        if (toCopy > 0) {
            memcpy(buf + len, data, toCopy);
            len += toCopy;
        }
        return toCopy;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= tft.height()) return 0;
    tft.pushImage(x, y, w, h, bitmap);
    return 1;
}

void drawStatusScreen(const char* title, const char* msg, uint16_t color = TFT_WHITE) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(24, 100);
    tft.println(title);

    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(24, 145);
    tft.println(msg);
}

void setupOTA() {
    String host = String("HopTinhYeu-") + BOX_ID;
    ArduinoOTA.setHostname(host.c_str());
    ArduinoOTA.onStart([]() {
        drawStatusScreen("DANG CAP NHAT", "Vui long khong tat nguon...", TFT_YELLOW);
    });
    ArduinoOTA.onEnd([]() {
        drawStatusScreen("CAP NHAT XONG", "Dang khoi dong lai...", TFT_GREEN);
    });
    ArduinoOTA.begin();
}

void setup() {
    Serial.begin(115200);
    delay(300);

    tft.init();
    tft.setRotation(0); // 0: Dọc 240x320
    tft.fillScreen(TFT_BLACK);

    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(tft_output);

    drawStatusScreen("HOP TINH YEU <3", "Dang ket noi Wi-Fi...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(400);
        Serial.print(".");
    }

    Serial.println("\nWi-Fi Connected! IP: " + WiFi.localIP().toString());
    drawStatusScreen("HOP TINH YEU <3", "Ket noi Cloud OK!\nDang tai ky niem...");

    setupOTA();
    pollServer();
}

void loop() {
    ArduinoOTA.handle();

    unsigned long now = millis();
    if (now - lastPollTime >= POLL_INTERVAL_MS || lastPollTime == 0) {
        lastPollTime = now;
        pollServer();
    }

    if (currentMode == "slideshow" && photoCount > 1 && (now - lastSlideTime >= slideIntervalMs)) {
        lastSlideTime = now;
        slideIndex = (slideIndex + 1) % photoCount;
        downloadAndShowPhoto(photoIds[slideIndex]);
    }
}

void pollServer() {
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
        return;
    }

    WiFiClientSecure client;
    client.setInsecure();

    String pollUrl = String(API_BASE) + "/api/esp/poll?box=" + BOX_ID + "&key=" + DEVICE_KEY;
    HTTPClient https;
    if (!https.begin(client, pollUrl)) return;

    int code = https.GET();
    if (code == HTTP_CODE_OK) {
        String payload = https.getString();
        https.end();

        DynamicJsonDocument doc(8192);
        DeserializationError err = deserializeJson(doc, payload);
        if (err) return;

        currentMode     = doc["mode"] | "current";
        targetPhotoId   = doc["currentId"] | "";
        unsigned long sec = doc["slideInterval"] | 30;
        slideIntervalMs = sec * 1000UL;

        JsonArray arr = doc["photos"].as<JsonArray>();
        photoCount = 0;
        for (JsonObject obj : arr) {
            if (photoCount >= MAX_PHOTOS) break;
            photoIds[photoCount++] = obj["id"] | "";
        }

        if (photoCount == 0) {
            drawStatusScreen("HOP TINH YEU <3", "Chua co anh nao!\nHay gui anh qua Web nhe.");
            return;
        }

        if (currentMode == "current") {
            String desired = targetPhotoId != "" ? targetPhotoId : photoIds[0];
            if (desired != displayedPhotoId) {
                downloadAndShowPhoto(desired);
            }
        } else if (displayedPhotoId == "") {
            slideIndex = 0;
            lastSlideTime = millis();
            downloadAndShowPhoto(photoIds[0]);
        }
    } else {
        Serial.printf("Poll Error: %d\n", code);
        https.end();
    }
}

void downloadAndShowPhoto(String id) {
    if (id == "") return;
    String photoUrl = String(API_BASE) + "/api/esp/photo?box=" + BOX_ID + "&key=" + DEVICE_KEY + "&id=" + id;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient https;
    if (!https.begin(client, photoUrl)) return;

    int code = https.GET();
    if (code == HTTP_CODE_OK) {
        JpgMemStream mem(100000);
        if (mem.buf != nullptr) {
            https.writeToStream(&mem);
            if (mem.len > 0) {
                TJpgDec.drawJpg(0, 0, mem.buf, mem.len);
                displayedPhotoId = id;
                Serial.printf("Hien thi anh %s (%u bytes)\n", id.c_str(), (unsigned)mem.len);
            }
        }
    } else {
        Serial.printf("Loi tai anh: %d\n", code);
    }
    https.end();
}
