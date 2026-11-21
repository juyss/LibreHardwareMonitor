// 复制此文件为 secrets.h 后填入自己的信息（secrets.h 已被 .gitignore 忽略）
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";

// SSE 事件流地址（旧版轮询的 /data.json 已改为 /events）
// interval=500 与旧版每 500ms 刷新一次保持一致
// format=compact 只推送 OLED 需要的字段，单条约 100 字节
const char *sseUrl = "http://192.168.1.100:10001/events?interval=500&format=compact";
