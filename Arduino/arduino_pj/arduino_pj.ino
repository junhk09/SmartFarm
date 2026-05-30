/*
 * SmartFarm Arduino Client v2
 * - ID: 5  (idpasswd.txt 기준)
 * - 기존 iot_server.c 프로토콜 사용
 *
 * 통신 프로토콜:
 *   로그인:   [5:PASSWD]
 *   센서전송: [KJH_SQL]SENSOR@조도@온도@습도@불꽃
 *   서버응답: [5]New connected! ...
 *
 * 핀 연결:
 *   ESP-01 TX  → D2 (SoftwareSerial RX)
 *   ESP-01 RX  → D3 (SoftwareSerial TX, 분압회로)
 *   ESP-01 VCC → 3.3V
 *   ESP-01 GND → GND
 *   DHT11      → D4
 *   불꽃센서   → D5 (DO, LOW=감지)
 *   LED        → D6
 *   CDS        → A0
 */

#include <SoftwareSerial.h>
#include <DHT.h>

/* ─── 수정 필요 ─── */
#define SERVER_IP    "10.10.16.65"   /* 라즈베리파이 IP */
#define SERVER_PORT  9000
#define WIFI_SSID    "KCCI601"
#define WIFI_PASS    "@kcci601@"

/* ─── 고정 설정 ─── */
#define MY_ID        "5"
#define MY_PASS      "PASSWD"
#define TARGET_SQL   "KJH_SQL"       /* sensor_device 클라이언트 ID */

#define PIN_ESP_RX   2
#define PIN_ESP_TX   3
#define PIN_DHT      4
#define PIN_FLAME    A1
#define PIN_LED      6
#define PIN_CDS      A0

#define DHT_TYPE     DHT11
#define SEND_MS      5000

/* ─── 객체 ─── */
SoftwareSerial espSerial(PIN_ESP_RX, PIN_ESP_TX);
DHT dht(PIN_DHT, DHT_TYPE);

/* ─── 전역 ─── */
static bool     g_wifi_ok   = false;
static bool     g_tcp_ok    = false;
static uint32_t g_last_send = 0;

/* ════════════════════════════════
 *  AT 헬퍼
 * ════════════════════════════════ */
static bool at_wait(const char *expect, uint32_t ms)
{
    String resp = "";
    uint32_t start = millis();
    while (millis() - start < ms) {
        if (espSerial.available()) {
            char c = espSerial.read();
            resp += c;
            if (resp.indexOf(expect) >= 0) return true;
            if (resp.indexOf("ERROR")  >= 0) return false;
        }
    }
    return false;
}

static bool at_cmd(const char *cmd, const char *expect,
                   uint32_t ms = 3000)
{
    espSerial.println(cmd);
    return at_wait(expect, ms);
}

/* ════════════════════════════════
 *  WiFi 초기화
 * ════════════════════════════════ */
static bool wifi_init(void)
{
    espSerial.begin(9600);
    delay(500);

    Serial.println("[ESP] 리셋 중...");
    if (!at_cmd("AT+RST", "ready", 5000)) {
        Serial.println("[ESP] 리셋 실패");
        return false;
    }
    delay(1000);

    at_cmd("ATE0",        "OK");
    at_cmd("AT+CWMODE=1", "OK");

    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASS);

    Serial.println("[ESP] WiFi 연결 중...");
    if (!at_cmd(cmd, "WIFI GOT IP", 15000)) {
        Serial.println("[ESP] WiFi 실패");
        return false;
    }
    at_cmd("AT+CIPMUX=0", "OK");
    Serial.println("[ESP] WiFi 연결 성공!");
    return true;
}

/* ════════════════════════════════
 *  TCP 연결 + 로그인
 * ════════════════════════════════ */
static bool tcp_connect(void)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "AT+CIPSTART=\"TCP\",\"%s\",%d",
             SERVER_IP, SERVER_PORT);

    Serial.println("[TCP] 서버 연결 중...");
    if (!at_cmd(cmd, "CONNECT", 10000)) {
        Serial.println("[TCP] 연결 실패");
        return false;
    }
    delay(300);

    /* 로그인: [5:PASSWD] */
    char login[32];
    snprintf(login, sizeof(login), "[%s:%s]", MY_ID, MY_PASS);

    char cipsend[32];
    snprintf(cipsend, sizeof(cipsend),
             "AT+CIPSEND=%d", (int)strlen(login));
    espSerial.println(cipsend);
    if (!at_wait(">", 3000)) {
        Serial.println("[TCP] CIPSEND 실패");
        return false;
    }
    espSerial.print(login);
    if (!at_wait("SEND OK", 5000)) {
        Serial.println("[TCP] 로그인 전송 실패");
        return false;
    }

    /* 서버 응답 대기: "New connected" */
    if (!at_wait("connected", 5000)) {
        Serial.println("[TCP] 서버 응답 없음");
        return false;
    }

    Serial.println("[TCP] 로그인 성공!");
    return true;
}

/* ════════════════════════════════
 *  TCP 전송
 * ════════════════════════════════ */
static bool tcp_send(const char *msg)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd),
             "AT+CIPSEND=%d", (int)strlen(msg));
    espSerial.println(cmd);
    if (!at_wait(">", 3000)) return false;
    espSerial.print(msg);
    return at_wait("SEND OK", 5000);
}

/* ════════════════════════════════
 *  서버 수신 처리
 *  CMD:LED:ON / CMD:PUMP:ON 등
 * ════════════════════════════════ */
static void process_incoming(void)
{
    if (!espSerial.available()) return;

    String line = espSerial.readStringUntil('\n');
    line.trim();

    if (line.indexOf("CLOSED") >= 0) {
        g_tcp_ok = false;
        Serial.println("[TCP] 연결 끊김");
        return;
    }

    if (!line.startsWith("+IPD,")) return;
    int colon = line.indexOf(':');
    if (colon < 0) return;

    String data = line.substring(colon + 1);
    data.trim();
    Serial.print("[RX] "); Serial.println(data);

    /* CMD:LED:ON 형식 처리 */
    if (data.startsWith("CMD:")) {
        String action = data.substring(4);
        if (action.equalsIgnoreCase("LED:ON")) {
            digitalWrite(PIN_LED, HIGH);
        } else if (action.equalsIgnoreCase("LED:OFF")) {
            digitalWrite(PIN_LED, LOW);
        }
        /* 필요 시 추가 명령 여기에 */
    }
}

/* ════════════════════════════════
 *  센서 읽기 + 전송
 *  형식: [KJH_SQL]SENSOR@illu@temp@humi@flame
 * ════════════════════════════════ */
static void read_and_send(void)
{
    float temp = dht.readTemperature();
    float humi = dht.readHumidity();
    if (isnan(temp) || isnan(humi)) {
        Serial.println("[DHT] 읽기 실패");
        temp = 0.0f; humi = 0.0f;
    }

    int light = analogRead(PIN_CDS);
    
  int flame = (analogRead(A1) > 50) ? 1 : 0;

    if (flame) {
        /* 화재 시 LED 점멸 */
        digitalWrite(PIN_LED, HIGH); delay(100);
        digitalWrite(PIN_LED, LOW);
        Serial.println("[경고] 화재 감지!");
        Serial.print("[FLAME RAW] ");
Serial.println(analogRead(A1));
    }

  char pkt[100];
char s_temp[10], s_humi[10];

// float을 문자열로 변환
dtostrf(temp, 4, 2, s_temp);
dtostrf(humi, 4, 2, s_humi);

snprintf(pkt, sizeof(pkt),
         "[%s]SENSOR@%d@%s@%s@%d\n",
         TARGET_SQL, light, s_temp, s_humi, flame);

    Serial.print("[TX] "); Serial.print(pkt);

    if (!g_tcp_ok) {
        g_tcp_ok = tcp_connect();
        if (!g_tcp_ok) return;
    }

    if (!tcp_send(pkt)) {
        Serial.println("[TX] 전송 실패 → 재연결");
        g_tcp_ok = false;
    }
}

/* ════════════════════════════════
 *  setup / loop
 * ════════════════════════════════ */
void setup(void)
{
    Serial.begin(9600);
    pinMode(PIN_FLAME, INPUT);
    pinMode(PIN_LED,   OUTPUT);
    pinMode(PIN_DHT, INPUT_PULLUP);
    digitalWrite(PIN_LED, LOW);
    dht.begin();
    delay(2000);

    Serial.println("=== SmartFarm Arduino [ID:5] ===");
    Serial.print("Server: "); Serial.print(SERVER_IP);
    Serial.print(":"); Serial.println(SERVER_PORT);

    g_wifi_ok = wifi_init();
    while (!g_wifi_ok) {
        Serial.println("[오류] WiFi 실패 → 10초 후 재시도");
        delay(10000);
        g_wifi_ok = wifi_init();
    }
    g_tcp_ok = tcp_connect();
}

void loop(void)
{
    process_incoming();

    uint32_t now = millis();
    if (now - g_last_send >= SEND_MS) {
        g_last_send = now;
        if (g_wifi_ok) read_and_send();
        else           g_wifi_ok = wifi_init();
    }
}
