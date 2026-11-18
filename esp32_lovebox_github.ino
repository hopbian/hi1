/*
 * =========================================================================
 *      HỘP TÌNH YÊU 💕 - ESP32 FIRMWARE (CHẠY QUA GITHUB.IO / GITHUB CLOUD)
 *   - Hoạt động 24/24 toàn cầu, không phụ thuộc máy tính hay điện thoại ở nhà
 *   - Màn hình: 240x320 TFT SPI (ST7789 / ILI9341 - Chuẩn ROHS)
 *   - Tích hợp OTA: Nạp code không dây qua Wi-Fi
 * =========================================================================
 *
 * Thư viện cần cài trong Arduino IDE (Sketch -> Include Library -> Manage Libraries):
 *   1. TFT_eSPI (Bodmer)
 *   2. TJpg_Decoder (Bodmer)
 *   3. ArduinoJson (Benoit Blanchon)
 *
 * Nhớ cấu hình chân màn hình trong User_Setup.h của thư viện TFT_eSPI!
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

// ==================== 2. CẤU HÌNH GITHUB CLOUD ====================
const char* GH_OWNER  = "YOUR_GITHUB_USERNAME";   // Tên tài khoản GitHub của bạn
const char* GH_REPO   = "hop-tinh-yeu";           // Tên repo GitHub Pages
const char* GH_BRANCH = "main";
const char* BOX_ID    = "LOVE01";                 // Mã Hộp (khớp với ?box= trên Web)

// (Tùy chọn) Để trống "" nếu Repo là Public (mỗi 75s kiểm tra 1 lần - thoải mái giới hạn free).
// Nếu muốn Repo là Private hoặc check nhanh mỗi 10s, điền Token chỉ-đọc vào đây:
const char* GH_READ_TOKEN = "";

// Thời gian kiểm tra ảnh mới từ GitHub (75 giây = ~48 lần/giờ, an toàn 100% Free không cần Token)
const unsigned long POLL_INTERVAL_MS = 75000;
// Thời gian đổi ảnh khi bật chế độ Slideshow
const unsigned long SLIDE_INTERVAL_MS = 30000;

TFT_eSPI tft = TFT_eSPI();

String currentMode = "current";
String targetPhotoId = "";
String displayedPhotoId = "";

#define MAX_PHOTOS 30
String photoIds[MAX_PHOTOS];
String photoFiles[MAX_PHOTOS];
int photoCount = 0;
int slideIndex = 0;

unsigned long lastPollTime = 0;
unsigned long lastSlideTime = 0;

// Buffer Stream giúp HTTPClient::writeToStream giải mã chuẩn cả Content-Length & Chunked
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
    drawStatusScreen("HOP TINH YEU <3", "Ket noi Cloud thanh cong!\nDang tai ky niem...");

    setupOTA();
    pollGitHubInfo();
}

void loop() {
    ArduinoOTA.handle();

    unsigned long now = millis();
    if (now - lastPollTime >= POLL_INTERVAL_MS || lastPollTime == 0) {
        lastPollTime = now;
        pollGitHubInfo();
    }

    if (currentMode == "slideshow" && photoCount > 1 && (now - lastSlideTime >= SLIDE_INTERVAL_MS)) {
        lastSlideTime = now;
        slideIndex = (slideIndex + 1) % photoCount;
        showPhotoByIndex(slideIndex);
    }
}

void pollGitHubInfo() {
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
        return;
    }

    WiFiClientSecure client;
    client.setInsecure();

    String apiUrl = String("https://api.github.com/repos/") + GH_OWNER + "/" + GH_REPO +
                    "/contents/boxes/" + BOX_ID + "/info.json?ref=" + GH_BRANCH;

    HTTPClient https;
    if (!https.begin(client, apiUrl)) return;

    https.addHeader("User-Agent", "LoveBox-ESP32"); // Bắt buộc với GitHub API
    https.addHeader("Accept", "application/vnd.github.v3.raw");
    if (strlen(GH_READ_TOKEN) > 0) {
        https.addHeader("Authorization", String("Bearer ") + GH_READ_TOKEN);
    }

    int code = https.GET();
    if (code == HTTP_CODE_OK) {
        String payload = https.getString();
        https.end();

        DynamicJsonDocument doc(12288);
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            Serial.println("JSON parse error: " + String(err.c_str()));
            return;
        }

        currentMode   = doc["mode"] | "current";
        targetPhotoId = doc["currentId"] | "";

        JsonArray arr = doc["photos"].as<JsonArray>();
        photoCount = 0;
        for (JsonObject obj : arr) {
            if (photoCount >= MAX_PHOTOS) break;
            photoIds[photoCount]   = obj["id"] | "";
            photoFiles[photoCount] = obj["file"] | "";
            photoCount++;
        }

        if (photoCount == 0) {
            drawStatusScreen("HOP TINH YEU <3", "Chua co anh nao!\nHay gui anh qua Web nhe.");
            return;
        }

        if (currentMode == "current") {
            int idx = 0;
            for (int i = 0; i < photoCount; i++) {
                if (photoIds[i] == targetPhotoId) { idx = i; break; }
            }
            if (photoIds[idx] != displayedPhotoId) {
                showPhotoByIndex(idx);
            }
        } else if (displayedPhotoId == "") {
            slideIndex = 0;
            lastSlideTime = millis();
            showPhotoByIndex(0);
        }
    } else {
        Serial.printf("GitHub API Error: %d\n", code);
        https.end();
    }
}

void showPhotoByIndex(int idx) {
    if (idx < 0 || idx >= photoCount) return;
    String id = photoIds[idx];
    String relFile = photoFiles[idx];
    if (relFile == "") return;

    String photoUrl = String("https://raw.githubusercontent.com/") + GH_OWNER + "/" + GH_REPO +
                      "/" + GH_BRANCH + "/boxes/" + BOX_ID + "/" + relFile;

    Serial.println("Downloading: " + photoUrl);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient https;
    if (!https.begin(client, photoUrl)) return;
    https.addHeader("User-Agent", "LoveBox-ESP32");
    if (strlen(GH_READ_TOKEN) > 0) {
        https.addHeader("Authorization", String("Bearer ") + GH_READ_TOKEN);
    }

    int code = https.GET();
    if (code == HTTP_CODE_OK) {
        JpgMemStream mem(85000); // Tối đa 85KB cho 1 ảnh 240x320 (~15-25KB thực tế)
        if (mem.buf != nullptr) {
            https.writeToStream(&mem);
            if (mem.len > 0) {
                uint16_t res = TJpgDec.drawJpg(0, 0, mem.buf, mem.len);
                Serial.printf("Ve anh %s (%u bytes) -> result=%u\n", id.c_str(), (unsigned)mem.len, res);
                displayedPhotoId = id;
            }
        }
    } else {
        Serial.printf("Loi tai anh: %d\n", code);
    }
    https.end();
}
