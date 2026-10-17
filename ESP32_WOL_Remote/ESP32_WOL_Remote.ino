#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <time.h>

// Enter these locally. Do not post credentials in screenshots/logs.
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* AIO_USERNAME = "YOUR_ADAFRUIT_USERNAME";
const char* AIO_KEY = "YOUR_ADAFRUIT_IO_KEY";

const char* MQTT_HOST = "io.adafruit.com";
const uint16_t MQTT_PORT = 8883;
const char* MQTT_TOPIC = "YOUR_ADAFRUIT_USERNAME/feeds/ubuntu-wake";

const uint8_t PC_MAC[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
IPAddress broadcastIP(192, 168, 1, 255);
const uint16_t WOL_PORT = 9;

// DigiCert Global Root G2; trust anchor for Adafruit's documented DigiCert chain.
// TLS validation requires accurate NTP time, established below.
static const char MQTT_ROOT_CA[] PROGMEM = R"CERT(-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----)CERT";

WiFiUDP udp;
WebServer server(80);
WiFiClientSecure secureClient;
PubSubClient mqtt(secureClient);
bool udpReady = false;
unsigned long wakeCount = 0;
unsigned long lastWiFiTry = 0;
unsigned long lastMqttTry = 0;

void logLine(const String& s) {
  Serial.printf("[%lus] %s\n", millis() / 1000, s.c_str());
}

bool sendWakePacket(const char* origin) {
  if (WiFi.status() != WL_CONNECTED) {
    logLine("[WOL] Rejected: Wi-Fi disconnected");
    return false;
  }
  if (!udpReady) udpReady = udp.begin(WOL_PORT);
  if (!udpReady) {
    logLine("[WOL] UDP unavailable");
    return false;
  }
  uint8_t packet[102];
  memset(packet, 0xFF, 6);
  for (int i = 0; i < 16; ++i) memcpy(packet + 6 + i * 6, PC_MAC, 6);
  if (!udp.beginPacket(broadcastIP, WOL_PORT)) return false;
  udp.write(packet, sizeof(packet));
  if (!udp.endPacket()) return false;
  ++wakeCount;
  logLine(String("[WOL] Magic packet sent via ") + origin);
  return true;
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, MQTT_TOPIC) != 0 || length > 64) return;
  String command;
  for (unsigned int i = 0; i < length; ++i) command += char(payload[i]);
  command.trim();
  if (command == "WAKE") {
    logLine("[MQTT] WAKE received");
    sendWakePacket("MQTT");
  } else {
    logLine("[MQTT] Ignoring non-WAKE feed value");
  }
}

void handleHome() {
  server.send(200, "text/html", R"HTML(<!doctype html><html lang="en"><head>
<meta name="viewport" content="width=device-width,initial-scale=1"><meta charset="utf-8">
<title>Ubuntu Remote Wake</title><style>
body{background:#121820;color:white;font:18px system-ui;text-align:center;padding:50px 12px}
main{max-width:360px;margin:auto;padding:30px 20px;background:#202c39;border-radius:18px}
button{background:#22a878;color:white;border:0;border-radius:12px;padding:18px 35px;font:bold 21px system-ui}
p{color:#bcc9d5}</style></head><body><main><h2>Ubuntu Remote Wake</h2>
<p>ESP32 Wake-on-LAN Controller</p><form method="POST" action="/wake">
<button>Wake Ubuntu</button></form><p>Local network controller</p></main></body></html>)HTML");
}

void handleStatus() {
  String response = "{\"wifi_connected\":";
  response += (WiFi.status() == WL_CONNECTED ? "true" : "false");
  response += ",\"mqtt_connected\":";
  response += (mqtt.connected() ? "true" : "false");
  response += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  response += ",\"wake_packets_sent\":" + String(wakeCount);
  response += ",\"uptime_seconds\":" + String(millis() / 1000) + "}";
  server.send(200, "application/json", response);
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);
  delay(1200);
  logLine("=== ESP32 WOL Remote V2 ===");
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  server.on("/", HTTP_GET, handleHome);
  server.on("/wake", HTTP_POST, []() {
    server.send(sendWakePacket("LOCAL HTTP") ? 200 : 503,
                "text/plain", "Wake command processed; check controller logs.");
  });
  server.on("/status", HTTP_GET, handleStatus);
  server.begin();

  secureClient.setCACert(MQTT_ROOT_CA);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  logLine("[HTTP] Server started; waiting for Wi-Fi and TLS time");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    udpReady = false;
    if (mqtt.connected()) mqtt.disconnect();
    if (millis() - lastWiFiTry >= 30000) {
      lastWiFiTry = millis();
      logLine("[WiFi] Reconnecting...");
      WiFi.reconnect();
    }
  } else {
    if (!udpReady) {
      udpReady = udp.begin(WOL_PORT);
      if (udpReady) logLine("[WiFi] Ready at " + WiFi.localIP().toString());
    }
    server.handleClient();
    if (mqtt.connected()) {
      mqtt.loop();
    } else if (time(nullptr) > 1700000000 &&
               (lastMqttTry == 0 || millis() - lastMqttTry >= 15000)) {
      lastMqttTry = millis();
      logLine("[MQTT] Connecting securely...");
      String clientId = "esp32-wol-" + WiFi.macAddress();
      clientId.replace(":", "");
      if (mqtt.connect(clientId.c_str(), AIO_USERNAME, AIO_KEY)) {
        if (mqtt.subscribe(MQTT_TOPIC, 0)) {
          logLine("[MQTT] Subscribed: ubuntu-wake");
        } else {
          logLine("[MQTT] Subscribe failed");
          mqtt.disconnect();
        }
      } else {
        logLine("[MQTT] Connect failed, state=" + String(mqtt.state()));
      }
    }
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();
    if (cmd == "WAKE") sendWakePacket("USB SERIAL");
    else if (cmd == "STATUS") {
      logLine("[STATUS] WiFi=" + String(WiFi.status() == WL_CONNECTED ? "up" : "down") +
              " MQTT=" + String(mqtt.connected() ? "up" : "down") +
              " Wakes=" + String(wakeCount));
    }
  }
  delay(10);
}
