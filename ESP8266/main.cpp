#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <stdlib.h>
#include <string.h>
#include "SH1106Wire.h"
#include "secrets.h"

#ifndef OLED_SDA
    #define OLED_SDA SDA
    #define OLED_SCL SCL
#endif

/*************** 屏幕实例 ***************/
SH1106Wire display(0x3c, OLED_SDA, OLED_SCL);

/*************** 数据结构 ***************/
struct SystemInfo {
    String cpuName;
    String cpuTemp;
    String cpuLoad;
    String gpuName;
    String gpuTemp;
    String gpuLoad;
    int    ramLoad = 0;
};

/*************** OLED 布局常量 ***************/
namespace Layout {
    // 指向字体的指针，而非单个字节
    const uint8_t *FontSmall = ArialMT_Plain_10;
    const uint8_t *FontLarge = ArialMT_Plain_16;

    constexpr uint8_t Line1 = 0;   // CPU Name
    constexpr uint8_t Line2 = 11;  // CPU Temp / Load
    constexpr uint8_t Line3 = 27;  // GPU Name
    constexpr uint8_t Line4 = 38;  // GPU Temp / Load
    constexpr uint8_t Line5 = 52;  // RAM text
    constexpr uint8_t Line6 = 56;  // RAM bar
} // namespace Layout

/*************** SSE 配置 ***************/
namespace SseConfig {
    constexpr uint32_t ReadTimeoutMs    = 20000; // 单次读取超时（服务端心跳间隔 15s）
    constexpr uint32_t HeaderTimeoutMs  = 5000;  // 等待 HTTP 响应头超时
    constexpr uint32_t ReconnectDelayMs = 3000;  // 断线重连间隔（与服务端 retry 提示一致）
}

/*************** SSE 地址解析 ***************/
struct SseEndpoint {
    String   host;
    uint16_t port = 80;
    String   path;
};

static bool parseHttpUrl(const char *url, SseEndpoint &ep) {
    String u(url);
    if (!u.startsWith("http://")) {
        Serial.println("SSE URL must start with http://");
        return false;
    }

    String rest = u.substring(7);
    int slash = rest.indexOf('/');
    String authority = (slash < 0) ? rest : rest.substring(0, slash);
    ep.path = (slash < 0) ? String("/") : rest.substring(slash);

    int colon = authority.indexOf(':');
    if (colon < 0) {
        ep.host = authority;
        ep.port = 80;
    } else {
        ep.host = authority.substring(0, colon);
        ep.port = static_cast<uint16_t>(authority.substring(colon + 1).toInt());
    }

    return ep.host.length() > 0 && ep.port > 0;
}

/*************** HTTP chunked 解码 ***************/
// 把 WiFiClient 上的响应体还原成原始字节流（服务端 SendChunked = true）
class SseBody {
public:
    explicit SseBody(WiFiClient &client) : client_(client) {}

    void reset(bool chunked) {
        chunked_        = chunked;
        chunkRemaining_ = 0;
        finished_       = false;
        timedOut_       = false;
    }

    // 返回一个字节；-1 表示流结束、超时或连接断开
    int read() {
        if (finished_)
            return -1;

        if (!chunked_) {
            int c = readRaw();
            if (c < 0)
                finished_ = true;
            return c;
        }

        while (chunkRemaining_ == 0) {
            long size = readChunkSize();
            if (size <= 0) {
                finished_ = true;
                return -1;
            }
            chunkRemaining_ = static_cast<size_t>(size);
        }

        int c = readRaw();
        if (c < 0) {
            finished_ = true;
            return -1;
        }

        if (--chunkRemaining_ == 0)
            skipCrlf();
        return c;
    }

    bool timedOut() const { return timedOut_; }
    bool finished() const { return finished_; }

private:
    int readRaw() {
        uint32_t start = millis();
        while (!client_.available()) {
            if (!client_.connected())
                return -1;
            if (millis() - start > SseConfig::ReadTimeoutMs) {
                timedOut_ = true;
                return -1;
            }
            delay(1);
        }
        return client_.read();
    }

    long readChunkSize() {
        char line[12];
        size_t len = 0;
        bool gotAny = false;

        while (true) {
            int c = readRaw();
            if (c < 0)
                return -1;
            if (c == '\n')
                break;
            if (c == '\r')
                continue;
            gotAny = true;
            if (len < sizeof(line) - 1)
                line[len++] = static_cast<char>(c);
        }

        if (!gotAny)
            return -1; // 分块框架异常，按流结束处理

        line[len] = '\0';
        char *semi = strchr(line, ';'); // 忽略 chunk extension
        if (semi)
            *semi = '\0';
        return strtol(line, nullptr, 16);
    }

    void skipCrlf() {
        for (int i = 0; i < 2; ++i) {
            int c = readRaw();
            if (c < 0 || c == '\n')
                return;
        }
    }

    WiFiClient &client_;
    bool   chunked_        = false;
    size_t chunkRemaining_ = 0;
    bool   finished_       = false;
    bool   timedOut_       = false;
};

/*************** SSE 帧解析（只向 ArduinoJson 输出 data: 的 JSON 内容） ***************/
class SseJsonStream : public Stream {
public:
    explicit SseJsonStream(SseBody &body) : body_(body) {}

    int available() override { return 0; }

    int read() override {
        if (pending_ >= 0) {
            int c = pending_;
            pending_ = -1;
            return c;
        }
        return nextByte();
    }

    int peek() override {
        if (pending_ < 0)
            pending_ = nextByte();
        return pending_;
    }

    size_t readBytes(char *buffer, size_t length) override {
        size_t n = 0;
        while (n < length) {
            int c = read();
            if (c < 0)
                break;
            buffer[n++] = static_cast<char>(c);
        }
        return n;
    }

    size_t write(uint8_t) override { return 0; }
    void   flush() override {}

    // 新建连接后调用，清空上一连接遗留的扫描状态
    void reset() { resetScan(); }

    // 一个 JSON 文档解析完成后调用：丢弃当前事件的剩余内容，对齐到下一个事件
    void finishEvent() {
        pending_ = -1;

        if (state_ == State::Done) {
            resetScan();
            return;
        }

        // 丢弃当前 data: 行的剩余部分
        int c;
        do {
            c = body_.read();
        } while (c >= 0 && c != '\n');
        if (c < 0) {
            state_ = State::Done;
            return;
        }

        // 丢弃后续字段行，直到遇到空行（事件结束）
        while (true) {
            c = body_.read();
            if (c < 0) {
                state_ = State::Done;
                return;
            }
            if (c == '\r')
                continue;
            if (c == '\n') {
                resetScan();
                return;
            }
            do {
                c = body_.read();
            } while (c >= 0 && c != '\n');
            if (c < 0) {
                state_ = State::Done;
                return;
            }
        }
    }

private:
    enum class State { LineStart, Prefix, SkipLine, Emit, Done };

    void resetScan() {
        state_     = State::LineStart;
        prefixLen_ = 0;
        sawData_   = false;
        pending_   = -1;
    }

    int nextByte() {
        while (true) {
            int c = body_.read();
            if (c < 0) {
                state_ = State::Done;
                return -1;
            }

            switch (state_) {
            case State::LineStart:
                if (c == '\r')
                    break;
                if (c == '\n') {
                    // 已经输出过 data 后又遇到空行，说明本事件结束
                    if (sawData_) {
                        state_ = State::Done;
                        return -1;
                    }
                    break; // 事件之间的空行 / 心跳注释后的空行
                }
                prefix_[0] = static_cast<char>(c);
                prefixLen_ = 1;
                state_     = State::Prefix;
                break;

            case State::Prefix:
                if (c == '\n') {
                    prefixLen_ = 0;
                    state_     = State::LineStart;
                    break;
                }
                if (c == '\r')
                    break;
                prefix_[prefixLen_++] = static_cast<char>(c);
                if (prefixLen_ == 5) {
                    if (memcmp(prefix_, "data:", 5) == 0) {
                        bool continuation = sawData_;
                        sawData_ = true;
                        state_   = State::Emit;
                        if (continuation)
                            return '\n'; // 还原多行 data 之间的换行
                    } else {
                        state_ = State::SkipLine; // id: / retry: / 注释等
                    }
                }
                break;

            case State::SkipLine:
                if (c == '\n')
                    state_ = State::LineStart;
                break;

            case State::Emit:
                if (c == '\r')
                    break;
                if (c == '\n') {
                    state_ = State::LineStart;
                    break;
                }
                return c;

            case State::Done:
                return -1;
            }
        }
    }

    SseBody &body_;
    State    state_     = State::LineStart;
    char     prefix_[5] = {0};
    uint8_t  prefixLen_ = 0;
    bool     sawData_   = false;
    int      pending_   = -1;
};

/*************** SSE 客户端 ***************/
class SseClient {
public:
    bool isOpen() { return bodyReady_ && client_.connected(); }

    bool connect(const SseEndpoint &ep) {
        stop();
        Serial.printf("SSE connect %s:%u%s\n", ep.host.c_str(),
                      static_cast<unsigned>(ep.port), ep.path.c_str());

        if (!client_.connect(ep.host.c_str(), ep.port)) {
            Serial.println("SSE connect failed");
            return false;
        }

        char request[256];
        int len = snprintf(request, sizeof(request),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s:%u\r\n"
                           "Accept: text/event-stream\r\n"
                           "Cache-Control: no-cache\r\n"
                           "Connection: keep-alive\r\n"
                           "\r\n",
                           ep.path.c_str(), ep.host.c_str(),
                           static_cast<unsigned>(ep.port));
        if (len <= 0 || len >= static_cast<int>(sizeof(request))) {
            stop();
            return false;
        }
        client_.write(reinterpret_cast<const uint8_t *>(request), len);

        if (!readResponseHeader()) {
            stop();
            return false;
        }

        body_.reset(chunked_);
        stream_.reset();
        bodyReady_ = true;
        return true;
    }

    void stop() {
        client_.stop();
        bodyReady_ = false;
    }

    // 读取并解析下一个 SSE 事件；返回 false 时连接可能仍可用（仅本事件异常）
    bool readEvent(SystemInfo &out) {
        if (!bodyReady_)
            return false;

        bool ok = parseEvent(out);

        if (!client_.connected() || body_.timedOut() || body_.finished())
            bodyReady_ = false;

        return ok;
    }

private:
    bool readResponseHeader() {
        chunked_ = false;
        bool statusOk  = false;
        int  lineCount = 0;

        while (true) {
            String line = readLine();
            if (line.length() == 0) {
                if (lineCount == 0)
                    return false; // 没有收到任何响应
                break;
            }

            if (lineCount++ == 0) {
                statusOk = line.indexOf(" 200") > 0;
                Serial.println(line);
            } else {
                String lower = line;
                lower.toLowerCase();
                if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0)
                    chunked_ = true;
            }
        }

        if (!statusOk)
            Serial.println("SSE bad HTTP status");
        return statusOk;
    }

    String readLine() {
        String line;
        uint32_t start = millis();

        while (true) {
            while (!client_.available()) {
                if (!client_.connected())
                    return line;
                if (millis() - start > SseConfig::HeaderTimeoutMs)
                    return line;
                delay(1);
            }

            int c = client_.read();
            if (c < 0)
                return line;
            if (c == '\n')
                return line;
            if (c != '\r' && line.length() < 200)
                line += static_cast<char>(c);
        }
    }

    bool parseEvent(SystemInfo &out) {
        // /events?format=compact 只包含 OLED 需要的字段
        StaticJsonDocument<512> doc;
        DeserializationError err = deserializeJson(doc, stream_);
        stream_.finishEvent();

        if (err) {
            Serial.printf("SSE JSON error: %s\n", err.c_str());
            return false;
        }
        if (doc.overflowed()) {
            Serial.println("SSE JSON overflowed");
            return false;
        }

        out.cpuName = doc["cpu"].as<String>();
        out.cpuTemp = String(doc["ct"].as<int>());
        out.cpuLoad = String(doc["cl"].as<int>());
        out.gpuName = doc["gpu"].as<String>();
        out.gpuTemp = String(doc["gt"].as<int>());
        out.gpuLoad = String(doc["gl"].as<int>());
        out.ramLoad = doc["ram"].as<int>();
        return true;
    }

    WiFiClient     client_;
    SseBody        body_{client_};
    SseJsonStream  stream_{body_};
    bool           chunked_   = false;
    bool           bodyReady_ = false;
};

/*************** OLED 辅助 ***************/
void startSSD1306() {
    display.init();
    display.clear();
    display.display();
}

void bootLogoSSD1306() {
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(Layout::FontLarge);
    display.drawString(0, 0,  "Hardware Monitor");
    display.drawString(0, 14, "By JinPeng");
    display.setFont(Layout::FontSmall);
    display.drawString(0, 45, "Connecting to Wi-Fi...");
    display.display();
}

/*************** 显示名称修正 ***************/
// ArialMT_Plain_10 中 "i5/i7/i9" 的 i 与数字贴得太近，显示时插入一个空格
static String formatCpuName(const String &name) {
    String out;
    out.reserve(name.length() + 1);
    for (size_t i = 0; i < name.length(); ++i) {
        char c = name[i];
        out += c;
        if (c == 'i' && i + 1 < name.length() && name[i + 1] >= '0' && name[i + 1] <= '9')
            out += ' ';
    }
    return out;
}

/*************** 2. 显示到 OLED ***************/
void showSystemInfo(const SystemInfo &in) {
    display.clear();

    /* CPU */
    display.setFont(Layout::FontSmall);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, Layout::Line1, formatCpuName(in.cpuName));

    display.setFont(Layout::FontLarge);
    uint16_t cpuW = display.getStringWidth(in.cpuTemp);
    display.drawString(0, Layout::Line2, in.cpuTemp);
    display.drawString(static_cast<int16_t>(cpuW), Layout::Line2, "°C");
    display.drawString(64, Layout::Line2, in.cpuLoad + "%");

    /* GPU */
    display.setFont(Layout::FontSmall);
    display.drawString(0, Layout::Line3, in.gpuName);

    display.setFont(Layout::FontLarge);
    uint16_t gpuW = display.getStringWidth(in.gpuTemp);
    display.drawString(0, Layout::Line4, in.gpuTemp);
    display.drawString(static_cast<int16_t>(gpuW), Layout::Line4, "°C");
    display.drawString(64, Layout::Line4, in.gpuLoad + "%");

    /* RAM */
    display.setFont(Layout::FontSmall);
    display.drawString(0, Layout::Line5, "RAM");
    display.drawProgressBar(29, Layout::Line6, 95, 6, in.ramLoad);

    display.display();
}

/*************** 全局状态 ***************/
SseClient   sse;
SseEndpoint endpoint;
bool        endpointReady = false;

/*************** Wi-Fi 保活 ***************/
void ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED)
        return;

    Serial.println("WiFi lost, reconnecting...");
    sse.stop();
    display.displayOff();

    WiFi.disconnect();
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        bootLogoSSD1306();
        delay(500);
    }

    Serial.println("WiFi reconnected: " + WiFi.SSID());
    display.displayOn();
}

/*************** 初始化 ***************/
void setup() {
    Serial.begin(115200);
    Serial.println(ssid);

    WiFi.begin(ssid, password);
    startSSD1306();

    while (WiFi.status() != WL_CONNECTED) {
        bootLogoSSD1306();
        delay(100); // 避免启动冲突
        Serial.println("waiting");
        delay(1000);
    }

    Serial.println("WiFi connected: " + WiFi.SSID());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    endpointReady = parseHttpUrl(sseUrl, endpoint);
    if (!endpointReady)
        Serial.println("Invalid SSE URL in secrets.h");
}

/*************** 主循环 ***************/
void loop() {
    if (!endpointReady) {
        delay(1000);
        return;
    }

    ensureWiFi();

    if (!sse.isOpen()) {
        if (!sse.connect(endpoint)) {
            display.displayOff();
            delay(SseConfig::ReconnectDelayMs);
            return;
        }
        display.displayOn();
    }

    SystemInfo info;
    if (sse.readEvent(info)) {
        showSystemInfo(info);
    } else if (!sse.isOpen()) {
        display.displayOff();
        sse.stop();
        delay(SseConfig::ReconnectDelayMs);
    }
}
