/*
 * =========================================================================
 *   HỘP TÌNH YÊU 💕 - ESP32 FIRMWARE FULL TIỆN ÍCH (THƯƠNG MẠI)
 *   - Kết nối Cloudflare Worker API (bảo mật DEVICE_KEY từng hộp)
 *   - Màn hình: 240x320 TFT SPI (ST7789 / ILI9341 - Chuẩn ROHS)
 *   - Nút nhấn vật lý riêng (GPIO 0 / nút ngoài nối GND):
 *       * Bấm ngắn: Chuyển trang (Ảnh → Đồng hồ & Thời tiết → Lịch → Pomodoro → Mã QR)
 *       * Bấm đúp: Thả tim 2 chiều gửi ngược về Web cho người ấy!
 *       * Bấm giữ: Tắt báo thức / Start-Pause Pomodoro
 *   - Tích hợp OTA nạp code không dây qua Wi-Fi
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

#define BUTTON_PIN 0    // Nút nhấn vật lý (mặc định GPIO 0 = nút BOOT có sẵn trên mạch)
#define BUZZER_PIN -1   // Chân còi Buzzer báo thức (-1 nếu chưa gắn)
#define HEART_LED_PIN 2 // LED xanh trên board ESP32 nháy khi có nhịp tim
#define BLK_PIN -1      // Chân đèn nền LED/BLK để giảm sáng đêm (-1 nếu nối thẳng 3.3V)

// ==================== 2. CẤU HÌNH HỘP BẢO MẬT ====================
const char* API_BASE   = "https://lovebox-api.tonepite.workers.dev";
const char* BOX_ID     = "LOVE01";
const char* DEVICE_KEY = "PASTE_DEVICE_KEY_HERE";

const unsigned long POLL_INTERVAL_MS = 15000;

TFT_eSPI tft = TFT_eSPI();

// Trang hiển thị: 0=Ảnh, 1=Đồng hồ+Thời tiết+Quote, 2=Lịch, 3=Pomodoro, 4=Mã QR
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

// Đồng hồ, Thời tiết, Quote, Đêm
bool clock24h = true;
bool clockShowSec = true;
bool clockShowDate = true;
bool nightDim = true;
String clockTheme = "love";

String weatherText = "";
String weatherTip  = "";
String dailyQuote  = "";

// Thả tim 2 chiều
long lastHeartPulseId = -1;
String heartMsg = "Anh dang nho em <3";
unsigned long heartOverlayUntil = 0;

// Pomodoro
bool pomoEnabled = true;
int pomoFocusMin = 25;
int pomoBreakMin = 5;
String pomoLabel = "Tap trung";
bool pomoRunning = false;
bool pomoIsBreak = false;
long pomoRemainSec = 25 * 60;
unsigned long lastPomoTick = 0;

// QR Code
bool qrEnabled = false;
String qrTitle = "Ma QR";
String qrMatrix = "";

// Báo thức & Lịch
struct AlarmItem {
    String time;
    String label;
    bool days[7];
    bool enabled;
};
#define MAX_ALARMS 20
AlarmItem alarms[MAX_ALARMS];
int alarmCount = 0;
bool alarmRinging = false;
String activeAlarmLabel = "";
int lastAlarmMinute = -1;

struct EventItem {
    String date;
    String time;
    String title;
};
#define MAX_EVENTS 15
EventItem events[MAX_EVENTS];
int eventCount = 0;

unsigned long lastPollTime = 0;
unsigned long lastSlideTime = 0;
unsigned long lastClockTick = 0;

// Xử lý nút nhấn (Bấm ngắn / Bấm đúp / Bấm giữ)
bool btnLastState = HIGH;
unsigned long btnPressStart = 0;
bool btnLongHandled = false;
unsigned long lastSingleTapTime = 0;
bool pendingSingleTap = false;

class JpgMemStream : public Stream {
public:
    uint8_t* buf;
    size_t cap;
    size_t len;
    JpgMemStream(size_t maxCap) : cap(maxCap), len(0) { buf = (uint8_t*)malloc(cap); }
    ~JpgMemStream() { if (buf) free(buf); }
    size_t write(uint8_t b) override { if (!buf || len >= cap) return 0; buf[len++] = b; return 1; }
    size_t write(const uint8_t* data, size_t size) override {
        if (!buf) return 0;
        size_t avail = cap - len;
        size_t toCopy = size < avail ? size : avail;
        if (toCopy > 0) { memcpy(buf + len, data, toCopy); len += toCopy; }
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
    ArduinoOTA.onStart([]() { drawStatusScreen("DANG CAP NHAT", "Vui long khong tat nguon...", TFT_YELLOW); });
    ArduinoOTA.onEnd([]() { drawStatusScreen("CAP NHAT XONG", "Dang khoi dong lai...", TFT_GREEN); });
    ArduinoOTA.begin();
}

void setup() {
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    if (BUZZER_PIN >= 0) { pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW); }
    if (HEART_LED_PIN >= 0) { pinMode(HEART_LED_PIN, OUTPUT); digitalWrite(HEART_LED_PIN, LOW); }
    if (BLK_PIN >= 0) {
        ledcSetup(0, 5000, 8);
        ledcAttachPin(BLK_PIN, 0);
        ledcWrite(0, 255);
    }

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(tft_output);

    drawStatusScreen("HOP TINH YEU <3", "Dang ket noi Wi-Fi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }

    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("\nWi-Fi OK! IP: " + WiFi.localIP().toString());
    drawStatusScreen("HOP TINH YEU <3", "Ket noi Cloud OK!\nDang tai du lieu...");

    setupOTA();
    pollServer();
}

void sendHeartToWeb() {
    if (WiFi.status() != WL_CONNECTED) return;
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    String url = String(API_BASE) + "/api/esp/heart?box=" + BOX_ID + "&key=" + DEVICE_KEY;
    if (https.begin(client, url)) {
        https.POST("");
        https.end();
    }
    heartMsg = "Da gui nhip tim len Web <3";
    showHeartOverlay();
}

void showHeartOverlay() {
    heartOverlayUntil = millis() + 5000;
    tft.fillScreen(tft.color565(130, 12, 55));
    tft.setTextColor(TFT_WHITE, tft.color565(130, 12, 55));
    tft.setTextSize(3);
    tft.setCursor(70, 95);
    tft.println("<3 <3");
    tft.setTextSize(2);
    tft.setCursor(18, 155);
    tft.println(heartMsg);
    if (HEART_LED_PIN >= 0) digitalWrite(HEART_LED_PIN, HIGH);
}

void nextPageByButton() {
    if (buttonPinMode == "toggle_clock") {
        activePage = (activePage == 1) ? 0 : 1;
    } else if (buttonPinMode != "alarm_ack") {
        for (int step = 0; step < 5; step++) {
            activePage = (activePage + 1) % 5;
            if (activePage == 3 && !pomoEnabled) continue;
            if (activePage == 4 && (!qrEnabled || qrMatrix == "")) continue;
            break;
        }
    }
    renderActivePage(true);
}

void handlePhysicalButton() {
    bool state = digitalRead(BUTTON_PIN);
    unsigned long now = millis();

    if (btnLastState == HIGH && state == LOW) {
        btnPressStart = now;
        btnLongHandled = false;
    } else if (state == LOW && !btnLongHandled && (now - btnPressStart >= 1100)) {
        btnLongHandled = true;
        pendingSingleTap = false;
        if (alarmRinging) {
            stopAlarm();
        } else if (activePage == 3) {
            pomoRunning = !pomoRunning;
            renderPomodoroPage(false);
        }
    } else if (btnLastState == LOW && state == HIGH) {
        if (!btnLongHandled && (now - btnPressStart >= 35)) {
            if (alarmRinging) {
                stopAlarm();
            } else if (pendingSingleTap && (now - lastSingleTapTime < 380)) {
                // Double click -> Gửi Thả Tim 2 chiều!
                pendingSingleTap = false;
                sendHeartToWeb();
            } else {
                pendingSingleTap = true;
                lastSingleTapTime = now;
            }
        }
    }
    if (pendingSingleTap && (now - lastSingleTapTime >= 390)) {
        pendingSingleTap = false;
        nextPageByButton();
    }
    btnLastState = state;
}

void stopAlarm() {
    alarmRinging = false;
    if (BUZZER_PIN >= 0) digitalWrite(BUZZER_PIN, LOW);
    renderActivePage(true);
}

void checkAlarmsAndDim() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) return;

    if (BLK_PIN >= 0 && nightDim) {
        bool isNight = (timeinfo.tm_hour >= 23 || timeinfo.tm_hour < 6 || (timeinfo.tm_hour == 6 && timeinfo.tm_min < 30));
        ledcWrite(0, isNight ? 38 : 255);
    }

    if (timeinfo.tm_min == lastAlarmMinute) return;
    lastAlarmMinute = timeinfo.tm_min;

    char hhmm[6];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    String nowStr = String(hhmm);
    int wday = timeinfo.tm_wday;

    for (int i = 0; i < alarmCount; i++) {
        if (alarms[i].enabled && alarms[i].days[wday] && alarms[i].time == nowStr) {
            alarmRinging = true;
            activeAlarmLabel = alarms[i].label;
            tft.fillScreen(TFT_RED);
            tft.setTextColor(TFT_WHITE, TFT_RED);
            tft.setTextSize(3); tft.setCursor(25, 90); tft.println("BAO THUC!");
            tft.setTextSize(2); tft.setCursor(20, 150); tft.println(activeAlarmLabel);
            tft.setTextSize(1); tft.setCursor(20, 250); tft.println("Bam nut de tat chuong");
            break;
        }
    }
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
        tft.setCursor(16, 18);
        tft.println("DONG HO & NGAY");
    }

    if (!gotTime) {
        tft.setTextColor(TFT_WHITE, bg); tft.setTextSize(2); tft.setCursor(20, 120); tft.println("Dang dong bo gio...");
        return;
    }

    int hr = timeinfo.tm_hour;
    String suffix = "";
    if (!clock24h) { suffix = hr >= 12 ? " PM" : " AM"; hr = hr % 12; if (hr == 0) hr = 12; }

    char timeBuf[16];
    if (clockShowSec) snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", hr, timeinfo.tm_min, timeinfo.tm_sec);
    else snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", hr, timeinfo.tm_min);

    tft.setTextColor(TFT_WHITE, bg);
    tft.setTextSize(clockShowSec ? 3 : 4);
    tft.setCursor(clockShowSec ? 38 : 48, 75);
    tft.print(timeBuf);
    if (suffix != "") { tft.setTextSize(2); tft.print(suffix); }

    if (clockShowDate) {
        const char* daysVi[] = {"Chu Nhat", "Thu Hai", "Thu Ba", "Thu Tu", "Thu Nam", "Thu Sau", "Thu Bay"};
        char dateBuf[32];
        snprintf(dateBuf, sizeof(dateBuf), "%s, %02d/%02d/%04d",
                 daysVi[timeinfo.tm_wday], timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
        tft.setTextColor(accent, bg); tft.setTextSize(2); tft.setCursor(16, 125); tft.print(dateBuf);
    }

    if (weatherText != "") {
        tft.setTextColor(TFT_CYAN, bg);
        tft.setTextSize(1);
        tft.setCursor(16, 162);
        tft.print(weatherText.substring(0, 36));
        if (weatherTip != "") {
            tft.setTextColor(TFT_YELLOW, bg);
            tft.setCursor(16, 178);
            tft.print(weatherTip.substring(0, 36));
        }
    }

    tft.fillRect(10, 232, 220, 68, tft.color565(30, 15, 35));
    tft.setTextColor(TFT_YELLOW, tft.color565(30, 15, 35));
    tft.setTextSize(1);
    tft.setCursor(16, 244);
    if (dailyQuote != "") {
        tft.print("Quote: " + dailyQuote.substring(0, 28));
    }
    String nextAl = "Bao thuc: Chua bat";
    for (int i = 0; i < alarmCount; i++) {
        if (alarms[i].enabled) { nextAl = "Bao thuc: " + alarms[i].time + " " + alarms[i].label; break; }
    }
    tft.setTextColor(TFT_PINK, tft.color565(30, 15, 35));
    tft.setCursor(16, 272);
    tft.print(nextAl.substring(0, 33));
}

void renderCalendarPage() {
    uint16_t bg = tft.color565(15, 23, 42);
    tft.fillScreen(bg);
    tft.setTextColor(TFT_PINK, bg); tft.setTextSize(2); tft.setCursor(18, 20); tft.println("LICH & SU KIEN");
    tft.drawFastHLine(16, 48, 208, TFT_MAGENTA);
    if (eventCount == 0) {
        tft.setTextColor(TFT_LIGHTGREY, bg); tft.setTextSize(1); tft.setCursor(20, 130);
        tft.println("Chua co su kien nao.");
        return;
    }
    int y = 62;
    for (int i = 0; i < eventCount && i < 6; i++) {
        tft.fillRoundRect(12, y, 216, 36, 6, tft.color565(30, 41, 59));
        tft.setTextColor(TFT_CYAN, tft.color565(30, 41, 59)); tft.setTextSize(1); tft.setCursor(18, y + 6);
        tft.print(events[i].date + (events[i].time != "" ? (" " + events[i].time) : ""));
        tft.setTextColor(TFT_WHITE, tft.color565(30, 41, 59)); tft.setCursor(18, y + 20);
        tft.print(events[i].title.substring(0, 32));
        y += 42;
    }
}

void renderPomodoroPage(bool fullClear) {
    uint16_t bg = pomoIsBreak ? tft.color565(6, 78, 59) : tft.color565(60, 10, 26);
    if (fullClear) {
        tft.fillScreen(bg);
        tft.setTextColor(TFT_PINK, bg); tft.setTextSize(2); tft.setCursor(28, 26);
        tft.println(pomoIsBreak ? "NGHI NGOI" : "POMODORO");
    }
    int mm = pomoRemainSec / 60;
    int ss = pomoRemainSec % 60;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", mm, ss);
    tft.setTextColor(TFT_WHITE, bg); tft.setTextSize(4); tft.setCursor(58, 120); tft.print(buf);
    tft.setTextSize(1); tft.setCursor(20, 195); tft.print(pomoLabel.substring(0, 34));
    tft.setCursor(20, 250);
    tft.print(pomoRunning ? "Dang chay (Bam giu de Tam dung)" : "Tam dung (Bam giu de Bat dau)");
}

void renderQrPage() {
    tft.fillScreen(TFT_WHITE);
    tft.setTextColor(TFT_MAGENTA, TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(16, 18);
    tft.println(qrTitle.substring(0, 17));

    int sep = qrMatrix.indexOf('|');
    if (sep <= 0) {
        tft.setTextColor(TFT_BLACK, TFT_WHITE); tft.setTextSize(1); tft.setCursor(20, 150);
        tft.println("Chua tao ma QR tren Web!");
        return;
    }
    int sz = qrMatrix.substring(0, sep).toInt();
    String bits = qrMatrix.substring(sep + 1);
    if (sz <= 0) return;

    int boxSize = 200;
    int cell = boxSize / sz;
    int offsetX = (240 - cell * sz) / 2;
    int offsetY = 65 + (200 - cell * sz) / 2;
    for (int r = 0; r < sz; r++) {
        for (int c = 0; c < sz; c++) {
            int idx = r * sz + c;
            if (idx < (int)bits.length() && bits[idx] == '1') {
                tft.fillRect(offsetX + c * cell, offsetY + r * cell, cell, cell, TFT_BLACK);
            }
        }
    }
    tft.setTextColor(TFT_DARKGREY, TFT_WHITE); tft.setTextSize(1); tft.setCursor(38, 290);
    tft.print("Quet bang Camera dien thoai");
}

void renderActivePage(bool forceRedraw) {
    if (alarmRinging || millis() < heartOverlayUntil) return;
    if (activePage == 0) {
        if (forceRedraw) displayedPhotoId = "";
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
    } else if (activePage == 3) {
        renderPomodoroPage(forceRedraw);
    } else if (activePage == 4) {
        if (forceRedraw) renderQrPage();
    }
}

void loop() {
    ArduinoOTA.handle();
    handlePhysicalButton();
    checkAlarmsAndDim();

    unsigned long now = millis();
    if (alarmRinging) {
        if (BUZZER_PIN >= 0) digitalWrite(BUZZER_PIN, (now / 300) % 2);
        return;
    }
    if (heartOverlayUntil > 0 && now >= heartOverlayUntil) {
        heartOverlayUntil = 0;
        if (HEART_LED_PIN >= 0) digitalWrite(HEART_LED_PIN, LOW);
        renderActivePage(true);
    }
    if (now < heartOverlayUntil) return;

    if (pomoRunning && (now - lastPomoTick >= 1000)) {
        lastPomoTick = now;
        if (pomoRemainSec > 0) pomoRemainSec--;
        else {
            pomoIsBreak = !pomoIsBreak;
            pomoRemainSec = (pomoIsBreak ? pomoBreakMin : pomoFocusMin) * 60;
            if (activePage == 3) renderPomodoroPage(true);
        }
        if (activePage == 3) renderPomodoroPage(false);
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
    if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); return; }
    WiFiClientSecure client;
    client.setInsecure();

    String pollUrl = String(API_BASE) + "/api/esp/poll?box=" + BOX_ID + "&key=" + DEVICE_KEY;
    HTTPClient https;
    if (!https.begin(client, pollUrl)) return;

    if (https.GET() == HTTP_CODE_OK) {
        String payload = https.getString();
        https.end();

        DynamicJsonDocument doc(20480);
        if (deserializeJson(doc, payload)) return;

        currentMode     = doc["mode"] | "current";
        targetPhotoId   = doc["currentId"] | "";
        slideIntervalMs = (doc["slideInterval"] | 30) * 1000UL;

        JsonArray arr = doc["photos"].as<JsonArray>();
        photoCount = 0;
        for (JsonObject obj : arr) {
            if (photoCount >= MAX_PHOTOS) break;
            photoIds[photoCount++] = obj["id"] | "";
        }

        JsonObject wNow = doc["weatherNow"].as<JsonObject>();
        if (!wNow.isNull()) {
            float t = wNow["temp"] | 0.0;
            weatherText = String(wNow["city"] | "") + ": " + String(t, 1) + "C - " + String(wNow["desc"] | "");
            weatherTip  = (wNow["isRain"] | false) ? String(wNow["tip"] | "") : "";
        } else {
            weatherText = ""; weatherTip = "";
        }

        JsonObject util = doc["utilities"].as<JsonObject>();
        if (!util.isNull()) {
            String newScreenMode = util["screenMode"] | "auto_button";
            buttonPinMode        = util["buttonPinMode"] | "cycle_pages";

            if (newScreenMode != defaultScreenMode) {
                defaultScreenMode = newScreenMode;
                if (defaultScreenMode == "photo") activePage = 0;
                else if (defaultScreenMode == "clock") activePage = 1;
                else if (defaultScreenMode == "calendar") activePage = 2;
                else if (defaultScreenMode == "pomodoro") activePage = 3;
                else if (defaultScreenMode == "qr") activePage = 4;
                renderActivePage(true);
            }

            JsonObject clk = util["clock"].as<JsonObject>();
            if (!clk.isNull()) {
                clock24h      = clk["format24h"] | true;
                clockShowSec  = clk["showSeconds"] | true;
                clockShowDate = clk["showDate"] | true;
                nightDim      = clk["nightDim"] | true;
                clockTheme    = clk["theme"] | "love";
            }

            JsonObject ht = util["heart"].as<JsonObject>();
            if (!ht.isNull()) {
                long pid = ht["pulseId"] | 0;
                if (lastHeartPulseId == -1) lastHeartPulseId = pid;
                else if (pid > lastHeartPulseId) {
                    lastHeartPulseId = pid;
                    heartMsg = ht["msg"] | "Nho em <3";
                    showHeartOverlay();
                }
            }

            JsonObject pm = util["pomodoro"].as<JsonObject>();
            if (!pm.isNull()) {
                pomoEnabled = pm["enabled"] | true;
                int nf = pm["focusMin"] | 25;
                if (nf != pomoFocusMin && !pomoRunning) { pomoFocusMin = nf; pomoRemainSec = nf * 60; }
                pomoBreakMin = pm["breakMin"] | 5;
                pomoLabel = pm["label"] | "Tap trung";
            }

            JsonObject qt = util["morningQuote"].as<JsonObject>();
            if (!qt.isNull() && (qt["enabled"] | true)) {
                JsonArray qArr = qt["quotes"].as<JsonArray>();
                if (qArr.size() > 0) {
                    struct tm ti;
                    int dayIdx = getLocalTime(&ti, 5) ? (ti.tm_mday % qArr.size()) : 0;
                    dailyQuote = qArr[dayIdx] | "";
                }
            } else dailyQuote = "";

            JsonObject qrObj = util["qrLink"].as<JsonObject>();
            if (!qrObj.isNull()) {
                qrEnabled = qrObj["enabled"] | false;
                qrTitle   = qrObj["title"] | "Ma QR";
                qrMatrix  = qrObj["matrix"] | "";
            }

            JsonArray alArr = util["alarms"].as<JsonArray>();
            alarmCount = 0;
            for (JsonObject a : alArr) {
                if (alarmCount >= MAX_ALARMS) break;
                alarms[alarmCount].time    = a["time"] | "07:00";
                alarms[alarmCount].label   = a["label"] | "Bao thuc";
                alarms[alarmCount].enabled = a["enabled"] | false;
                for (int d = 0; d < 7; d++) alarms[alarmCount].days[d] = false;
                for (int d : a["days"].as<JsonArray>()) { if (d >= 0 && d < 7) alarms[alarmCount].days[d] = true; }
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

        if (activePage == 0 && currentMode == "current") renderActivePage(false);
        else if (activePage == 0 && displayedPhotoId == "" && photoCount > 0) {
            slideIndex = 0; lastSlideTime = millis(); downloadAndShowPhoto(photoIds[0]);
        } else if (activePage == 2) renderCalendarPage();
    } else {
        https.end();
    }
}

void downloadAndShowPhoto(String id) {
    if (id == "") return;
    String photoUrl = String(API_BASE) + "/api/esp/photo?box=" + BOX_ID + "&key=" + DEVICE_KEY + "&id=" + id;
    WiFiClientSecure client; client.setInsecure();
    HTTPClient https;
    if (!https.begin(client, photoUrl)) return;
    if (https.GET() == HTTP_CODE_OK) {
        JpgMemStream mem(100000);
        if (mem.buf != nullptr) {
            https.writeToStream(&mem);
            if (mem.len > 0) { TJpgDec.drawJpg(0, 0, mem.buf, mem.len); displayedPhotoId = id; }
        }
    }
    https.end();
}
