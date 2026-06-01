# 🌱 SmartFarm IoT System

라즈베리파이, 아두이노, STM32를 활용한 스마트팜 IoT 시스템입니다.

---

## 📌 시스템 아키텍처

```
아두이노 (ESP-01 WiFi)
    └─── TCP ───▶ 라즈베리파이 (iot_server)
                        │
                        ├─── MariaDB (센서 데이터 저장)
                        ├─── HTTP API (포트 8080)
                        └─── Bluetooth RFCOMM ───▶ STM32 F411RE (HC-06)
                                                          │
                                                    액추에이터 제어
                                                    (RGB LED, 부저, DC모터, 수중펌프)

웹 브라우저 ───▶ http://라즈베리파이IP ───▶ Apache ───▶ dashboard.html
웹캠 스트리밍 ───▶ http://라즈베리파이IP:8090
```

---

## 🛠 사용 하드웨어

| 장치 | 역할 |
|---|---|
| Raspberry Pi | 메인 서버, DB, HTTP API, BT 통신 |
| Arduino UNO | 센서 데이터 수집 및 WiFi 전송 |
| STM32 F411RE (Nucleo) | 액추에이터 제어 |
| ESP-01 (WiFi) | 아두이노 WiFi 통신 |
| HC-06 (Bluetooth) | STM32 블루투스 통신 |
| DHT11 | 온습도 센서 |
| CDS | 조도 센서 |
| 불꽃 센서 | 화재 감지 |
| RGB LED | 계절/상태 표시 |
| DC 모터 | 가변저항 PWM 제어 |
| 수중 펌프 + 릴레이 | 자동 급수 |
| 부저 (수동) | 화재 경보 |
| 로지텍 C270 | 실시간 웹캠 스트리밍 |

---

## 📡 통신 프로토콜

### 아두이노 → 서버 (TCP)
```
로그인:   [5:PASSWD]
센서전송: [KJH_SQL]SENSOR@조도@온도@습도@불꽃
```

### 서버 → STM32 (Bluetooth)
```
CMD:LED:R / CMD:LED:G / CMD:LED:B
CMD:MOTOR:ON / CMD:MOTOR:OFF
CMD:BUZZER:ON / CMD:BUZZER:OFF
CMD:PUMP:ON / CMD:PUMP:OFF
CMD:LIGHT:HIGH / CMD:LIGHT:MID / CMD:LIGHT:LOW
```

### STM32 → 서버 (Bluetooth)
```
STATUS:MOTOR:ON:SPD:75:PUMP:OFF:LED:G:BUZZ:OFF
```

### HTTP REST API
| 메서드 | 경로 | 설명 |
|---|---|---|
| GET | /api/sensors | 최근 센서 데이터 |
| GET | /api/actuators | 액추에이터 상태 |
| GET | /api/status | 계절 + 최신 센서 |
| POST | /api/control | 액추에이터 제어 |

---

## ⚙️ 주요 기능

### 🌡 센서 모니터링
- 온도 / 습도 / 조도 / 화재 실시간 수집 (2초 간격)
- MariaDB에 데이터 저장

### 🌈 계절별 RGB LED 자동 제어
| 온도 | 계절 | LED 색상 |
|---|---|---|
| 10°C 이하 | 겨울 | 파랑 |
| 10~25°C | 봄/가을 | 초록 |
| 25°C 이상 | 여름 | 빨강 |

### 🔥 화재 감지 자동 대응
- 화재 감지 시 부저 ON + 워터펌프 ON 자동 실행
- 5초 후 자동 OFF
- 화재 해제 시 부저/펌프 자동 종료

### 💧 습도 기반 자동 급수
- 습도 20% 이하 시 워터펌프 자동 5초 가동
- 웹 UI에 가동 상태 표시

### ☀️ 조도 기반 시간대 표시
| 조도 | 시간대 | 메시지 |
|---|---|---|
| 640 이상 | 🌞 낮 | 조도 조절 필요 |
| 620~640 | 🌅 아침/새벽 | - |
| 620 미만 | 🌙 밤 | 조도 조절 필요 |

### 💡 CDS 연동 LED 밝기 제어
- 아두이노 CDS 센서값 → 서버 → STM32 명령 전송
- STM32 TIM3 CH3 PWM으로 LED 밝기 자동 조절

### 🎛 가변저항 LED 제어
- STM32 ADC(PA0) 가변저항 → TIM3 CH1 PWM → LED 밝기 실시간 조절

### 📷 실시간 웹캠 스트리밍
- 로지텍 C270 웹캠
- mjpg-streamer로 640x480 15fps 스트리밍
- 웹 대시보드에 통합

---

## 🗄 MariaDB 스키마

```sql
-- 센서 데이터
CREATE TABLE sensor (
  id    INT AUTO_INCREMENT PRIMARY KEY,
  name  VARCHAR(20),
  date  DATE,
  time  TIME,
  illu  INT,
  temp  FLOAT,
  humi  FLOAT,
  flame TINYINT(1) DEFAULT 0
);

-- 액추에이터 상태
CREATE TABLE device (
  name  VARCHAR(20) PRIMARY KEY,
  value VARCHAR(20),
  date  DATE,
  time  TIME
);
```

---

## 🔌 STM32 핀 설정

| 핀 | 기능 |
|---|---|
| PA0 | ADC1 CH0 (가변저항) |
| PA6 | TIM3 CH1 (ADC LED) |
| PA8 | TIM1 CH1 (DC모터 PWM) |
| PA9 | TIM1 CH2 (DC모터 PWM) |
| PB0 | TIM3 CH3 (CDS LED) |
| PB5 | PUMP_RELAY |
| PB6 | MOTOR_EN |
| PB9 | TIM4 CH4 (부저 PWM) |
| PC0 | LED_R |
| PC1 | LED_G |
| PC2 | LED_B |
| PC6 | USART6 TX (HC-06) |
| PC7 | USART6 RX (HC-06) |

---

## 🔌 아두이노 핀 설정

| 핀 | 기능 |
|---|---|
| D2 | ESP-01 TX (SoftwareSerial RX) |
| D3 | ESP-01 RX (SoftwareSerial TX) |
| D4 | DHT11 |
| D6 | LED |
| A0 | CDS (조도 센서) |
| A1 | 불꽃 센서 |

---

## 🚀 실행 방법

### 1. MariaDB 초기화
```bash
sudo mysql -u root < schema.sql
```

### 2. 서버 빌드 및 실행
```bash
cd /srv/samba/iot_socket
gcc -o iot_server iot_server.c -lmysqlclient -lbluetooth -lpthread
./iot_server 9000
```

### 3. 웹 대시보드 배포
```bash
sudo cp dashboard.html /var/www/html/index.html
```

### 4. 웹캠 스트리밍 시작
```bash
mjpg_streamer -i "input_uvc.so -d /dev/video0 -r 640x480 -f 15" \
              -o "output_http.so -p 8090 -w /usr/local/share/mjpg-streamer/www" &
```

### 5. Bluetooth 페어링 (최초 1회)
```bash
bluetoothctl
power on
agent KeyboardDisplay
default-agent
scan on
pair [HC-06 MAC]
trust [HC-06 MAC]
exit
```

---

## 🌐 접속 주소

| 서비스 | 주소 |
|---|---|
| 웹 대시보드 | http://라즈베리파이IP |
| 카메라 스트리밍 | http://라즈베리파이IP:8090/?action=stream |
| HTTP API | http://라즈베리파이IP:8080/api/sensors |
| TCP 서버 | 라즈베리파이IP:9000 |

---

## 📁 파일 구조

```
smartfarm/
├── server/
│   ├── iot_server.c      # 라즈베리파이 메인 서버
│   ├── schema.sql        # MariaDB 스키마
│   ├── idpasswd.txt      # 클라이언트 인증 정보
│   └── Makefile
├── arduino/
│   └── arduino_node5.ino # 아두이노 클라이언트
├── stm32/
│   └── main.c            # STM32 FreeRTOS 메인 코드
└── web/
    └── dashboard.html    # 웹 대시보드
```

---

## 👨‍💻 개발 환경

- Raspberry Pi OS (Bullseye)
- STM32CubeIDE 1.x
- Arduino IDE 2.x
- MariaDB 10.x
- Apache2
- mjpg-streamer
