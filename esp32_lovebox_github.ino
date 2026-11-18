/*
 * =========================================================================
 *   HỘP TÌNH YÊU 💕 - ESP32 FIRMWARE (ẢNH + TIỆN ÍCH + NÚT NHẤN RIÊNG)
 *   - Kết nối Cloudflare Worker API (bảo mật DEVICE_KEY từng hộp)
 *   - Màn hình: 240x320 TFT SPI (ST7789 / ILI9341 - Chuẩn ROHS)
 *   - Nút nhấn vật lý riêng: Chuyển qua lại Ảnh / Đồng hồ & Ngày / Lịch hẹn
 *   - Tích hợp NTP (UTC+7) cho Đồng hồ, Báo thức & Tích hợp OTA qua Wi-Fi
 * =========================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <ArduinoJson.h>
#include <time.h>

// ==================== 1. CẤU HÌNH WI-FI & PHẦN CỨNG ====================
const char* WIFI_SSID     = "VINH NHUNG";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD"; // Thay mật khẩu Wi-Fi thực tế

// Chân nút nhấn vật lý (nối nút nhấn giữa GPIO này và GND)
// Mặc định GPIO 0 (chính là nút BOOT có sẵn trên mọi mạch ESP32 để test ngay!)
#define BUTTON_PIN 0
// Chân còi Buzzer báo thức (đặt -1 nếu chưa gắn còi, màn hình vẫn nháy báo thức)
#define BUZZER_PIN -1

// ==================== 2. CẤU HÌNH HỘP BẢO MẬT ====================
const char* API_BASE   = "https://lovebox-api.tonepite.workers.dev";
const char* BOX_ID     = "LOVE01";
const char* DEVICE_KEY = "PASTE_DEVICE_KEY_HERE";

const unsigned long POLL_INTERVAL_MS = 15000;

TFT_eSPI tft = TFT_eSPI();

// Trang hiển thị trên màn hình khi bấm nút: 0 = Ảnh, 1 = Đồng hồ & Ngày, 2 = Lịch hẹn
int activePage = 0;
String defaultScreenMode = "auto_button";
String buttonPinMode     = "cycle_pages";

// Trạng thái Ảnh
String currentMode = "current";
String targetPhotoId = "";
String displayedPhotoId = "";
unsigned long slideIntervalMs = 30000;
#define MAX_PHOTOS 30
String photoIds[MAX_PHOTOS];
int photoCount = 0;
int slideIndex = 0;

// Trạng thái Tiện ích (Đồng hồ, Báo thức, Lịch)
bool clock24h = true;
bool clockShowSec = true;
bool clockShowDate = true;
String clockTheme = "love";
String coupleName = "Love Box";

struct AlarmItem {
    String time;    // "HH:MM"
    String label;
    bool days[7];   // 0=CN .. 6=T7
    bool enabled;
};
#define MAX_ALARMS 20
AlarmItem alarms[MAX_ALARMS];
int alarmCount = 0;
bool alarmRinging = false;
String activeAlarmLabel = "";
int lastAlarmMinute = -1;

struct EventItem {
    String date;    // "YYYY-MM-DD"
    String time;
    String title;
};
#define MAX_EVENTS 15
EventItem events[MAX_EVENTS];
int eventCount = 0;

unsigned long lastPollTime = 0;
unsigned long lastSlideTime = 0;
unsigned long lastClockTick = 0;

// Debounce nút nhấn vật lý
bool btnLastState = HIGH;
unsigned long btnPressStart = 0;
bool btnLongHandled = false;

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
    tft.setCursor(20, 100);
    tft.println(title);
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(20, 145);
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
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    if (BUZZER_PIN >= 0) {
        pinMode(BUZZER_PIN, OUTPUT);
        digitalWrite(BUZZER_PIN, LOW);
    }

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

    // Đồng bộ giờ Việt Nam (UTC+7)
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("\nWi-Fi Connected! IP: " + WiFi.localIP().toString());
    drawStatusScreen("HOP TINH YEU <3", "Ket noi Cloud OK!\nDang tai du lieu...");

    setupOTA();
    pollServer();
}

void handlePhysicalButton() {
    bool state = digitalRead(BUTTON_PIN);
    unsigned long now = millis();

    if (btnLastState == HIGH && state == LOW) {
        btnPressStart = now;
        btnLongHandled = false;
    } else if (state == LOW && !btnLongHandled && (now - btnPressStart >= 1200)) {
        // Nhấn giữ: tắt báo thức
        btnLongHandled = true;
        if (alarmRinging) {
            stopAlarm();
        }
    } else if (btnLastState == LOW && state == HIGH) {
        if (!btnLongHandled && (now - btnPressStart >= 35)) {
            // Nhấn ngắn
            if (alarmRinging) {
                stopAlarm();
            } else if (buttonPinMode == "toggle_clock") {
                activePage = (activePage == 1) ? 0 : 1;
                renderActivePage(true);
            } else if (buttonPinMode != "alarm_ack") {
                activePage = (activePage + 1) % 3;
                renderActivePage(true);
            }
        }
    }
    btnLastState = state;
}

void stopAlarm() {
    alarmRinging = false;
    if (BUZZER_PIN >= 0) digitalWrite(BUZZER_PIN, LOW);
    renderActivePage(true);
}

void checkAlarms() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) return;
    if (timeinfo.tm_min == lastAlarmMinute) return;
    lastAlarmMinute = timeinfo.tm_min;

    char hhmm[6];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    String nowStr = String(hhmm);
    int wday = timeinfo.tm_wday; // 0=CN .. 6=T7

    for (int i = 0; i < alarmCount; i++) {
        if (alarms[i].enabled && alarms[i].days[wday] && alarms[i].time == nowStr) {
            alarmRinging = true;
            activeAlarmLabel = alarms[i].label;
            drawAlarmAlert();
            break;
        }
    }
}

void drawAlarmAlert() {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextSize(3);
    tft.setCursor(25, 90);
    tft.println("BAO THUC!");
    tft.setTextSize(2);
    tft.setCursor(20, 150);
    tft.println(activeAlarmLabel);
    tft.setTextSize(1);
    tft.setCursor(20, 250);
    tft.println("Bam nut tren hop de tat chuong");
}

void renderClockPage(bool fullClear) {
    struct tm timeinfo;
    bool gotTime = getLocalTime(&timeinfo, 20);
    uint16_t bg = (clockTheme == "love") ? tft.color565(58, 16, 42) :
                  (clockTheme == "led" ? TFT_BLACK : tft.color565(20, 30, 48));
    uint16_t accent = (clockTheme == "led") ? TFT_CYAN : TFT_PINK;

    if (fullClear) {
        tft.fillScreen(bg);
        tft.setTextColor(accent, bg);
        tft.setTextSize(2);
        tft.setCursor(16, 22);
        tft.println("DONG HO & NGAY");
    }

    if (!gotTime) {
        tft.setTextColor(TFT_WHITE, bg);
        tft.setTextSize(2);
        tft.setCursor(20, 140);
        tft.println("Dang dong bo gio...");
        return;
    }

    int hr = timeinfo.tm_hour;
    String suffix = "";
    if (!clock24h) {
        suffix = hr >= 12 ? " PM" : " AM";
        hr = hr % 12;
        if (hr == 0) hr = 12;
    }

    char timeBuf[16];
    if (clockShowSec) {
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", hr, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", hr, timeinfo.tm_min);
    }

    tft.setTextColor(TFT_WHITE, bg);
    tft.setTextSize(clockShowSec ? 3 : 4);
    tft.setCursor(clockShowSec ? 38 : 48, 115);
    tft.print(timeBuf);
    if (suffix != "") {
        tft.setTextSize(2);
        tft.print(suffix);
    }

    if (clockShowDate) {
        const char* daysVi[] = {"Chu Nhat", "Thu Hai", "Thu Ba", "Thu Tu", "Thu Nam", "Thu Sau", "Thu Bay"};
        char dateBuf[32];
        snprintf(dateBuf, sizeof(dateBuf), "%s, %02d/%02d/%04d",
                 daysVi[timeinfo.tm_wday], timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
        tft.setTextColor(accent, bg);
        tft.setTextSize(2);
        tft.setCursor(18, 175);
        tft.print(dateBuf);
    }

    // Hiện báo thức kế tiếp
    tft.fillRect(10, 245, 220, 50, tft.color565(30, 15, 35));
    tft.setTextColor(TFT_YELLOW, tft.color565(30, 15, 35));
    tft.setTextSize(1);
    tft.setCursor(18, 260);
    String nextAl = "Chua co bao thuc bat";
    for (int i = 0; i < alarmCount; i++) {
        if (alarms[i].enabled) {
            nextAl = "Bao thuc: " + alarms[i].time + " - " + alarms[i].label;
            break;
        }
    }
    tft.print(nextAl.substring(0, 34));
}

void renderCalendarPage() {
    uint16_t bg = tft.color565(15, 23, 42);
    tft.fillScreen(bg);
    tft.setTextColor(TFT_PINK, bg);
    tft.setTextSize(2);
    tft.setCursor(18, 20);
    tft.println("LICH & SU KIEN");
    tft.drawFastHLine(16, 48, 208, TFT_MAGENTA);

    if (eventCount == 0) {
        tft.setTextColor(TFT_LIGHTGREY, bg);
        tft.setTextSize(1);
        tft.setCursor(20, 130);
        tft.println("Chua co su kien nao.");
        tft.setCursor(20, 150);
        tft.println("Hay upload lich tren Web nhe!");
        return;
    }

    int y = 62;
    for (int i = 0; i < eventCount && i < 6; i++) {
        tft.fillRoundRect(12, y, 216, 36, 6, tft.color565(30, 41, 59));
        tft.setTextColor(TFT_CYAN, tft.color565(30, 41, 59));
        tft.setTextSize(1);
        tft.setCursor(18, y + 6);
        tft.print(events[i].date + (events[i].time != "" ? (" " + events[i].time) : ""));
        tft.setTextColor(TFT_WHITE, tft.color565(30, 41, 59));
        tft.setCursor(18, y + 20);
        tft.print(events[i].title.substring(0, 32));
        y += 42;
    }
}

void renderActivePage(bool forceRedraw) {
    if (alarmRinging) return;
    if (activePage == 0) {
        if (forceRedraw) displayedPhotoId = ""; // Buộc vẽ lại ảnh hiện tại
        if (photoCount > 0) {
            String desired = targetPhotoId != "" ? targetPhotoId : photoIds[0];
            if (desired != displayedPhotoId) downloadAndShowPhoto(desired);
        } else {
            drawStatusScreen("HOP TINH YEU <3", "Chua co anh nao!");
        }
    } else if (activePage == 1) {
        renderClockPage(forceRedraw);
    } else if (activePage == 2) {
        if (forceRedraw) renderCalendarPage();
    }
}

void loop() {
    ArduinoOTA.handle();
    handlePhysicalButton();
    checkAlarms();

    unsigned long now = millis();
    if (alarmRinging) {
        if (BUZZER_PIN >= 0) digitalWrite(BUZZER_PIN, (now / 300) % 2);
        return;
    }

    if (now - lastPollTime >= POLL_INTERVAL_MS || lastPollTime == 0) {
        lastPollTime = now;
        pollServer();
    }

    if (activePage == 0 && currentMode == "slideshow" && photoCount > 1 && (now - lastSlideTime >= slideIntervalMs)) {
        lastSlideTime = now;
        slideIndex = (slideIndex + 1) % photoCount;
        downloadAndShowPhoto(photoIds[slideIndex]);
    }

    if (activePage == 1 && (now - lastClockTick >= 1000)) {
        lastClockTick = now;
        renderClockPage(false);
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

        DynamicJsonDocument doc(16384);
        DeserializationError err = deserializeJson(doc, payload);
        if (err) return;

        currentMode     = doc["mode"] | "current";
        targetPhotoId   = doc["currentId"] | "";
        coupleName      = doc["coupleName"] | "Love Box";
        unsigned long sec = doc["slideInterval"] | 30;
        slideIntervalMs = sec * 1000UL;

        JsonArray arr = doc["photos"].as<JsonArray>();
        photoCount = 0;
        for (JsonObject obj : arr) {
            if (photoCount >= MAX_PHOTOS) break;
            photoIds[photoCount++] = obj["id"] | "";
        }

        // Parse Utilities (Clock, Alarms, Events, Button mode)
        JsonObject util = doc["utilities"].as<JsonObject>();
        if (!util.isNull()) {
            String newScreenMode = util["screenMode"] | "auto_button";
            buttonPinMode        = util["buttonPinMode"] | "cycle_pages";

            if (newScreenMode != defaultScreenMode) {
                defaultScreenMode = newScreenMode;
                if (defaultScreenMode == "photo") activePage = 0;
                else if (defaultScreenMode == "clock") activePage = 1;
                else if (defaultScreenMode == "calendar") activePage = 2;
                renderActivePage(true);
            }

            JsonObject clk = util["clock"].as<JsonObject>();
            if (!clk.isNull()) {
                clock24h      = clk["format24h"] | true;
                clockShowSec  = clk["showSeconds"] | true;
                clockShowDate = clk["showDate"] | true;
                clockTheme    = clk["theme"] | "love";
            }

            JsonArray alArr = util["alarms"].as<JsonArray>();
            alarmCount = 0;
            for (JsonObject a : alArr) {
                if (alarmCount >= MAX_ALARMS) break;
                alarms[alarmCount].time    = a["time"] | "07:00";
                alarms[alarmCount].label   = a["label"] | "Bao thuc";
                alarms[alarmCount].enabled = a["enabled"] | false;
                for (int d = 0; d < 7; d++) alarms[alarmCount].days[d] = false;
                for (int d : a["days"].as<JsonArray>()) {
                    if (d >= 0 && d < 7) alarms[alarmCount].days[d] = true;
                }
                alarmCount++;
            }

            JsonArray evArr = util["events"].as<JsonArray>();
            eventCount = 0;
            for (JsonObject e : evArr) {
                if (eventCount >= MAX_EVENTS) break;
                events[eventCount].date  = e["date"] | "";
                events[eventCount].time  = e["time"] | "";
                events[eventCount].title = e["title"] | "";
                eventCount++;
            }
        }

        if (activePage == 0 && currentMode == "current") {
            renderActivePage(false);
        } else if (activePage == 0 && displayedPhotoId == "" && photoCount > 0) {
            slideIndex = 0;
            lastSlideTime = millis();
            downloadAndShowPhoto(photoIds[0]);
        } else if (activePage == 2) {
            renderCalendarPage();
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
    }
    https.end();
}
