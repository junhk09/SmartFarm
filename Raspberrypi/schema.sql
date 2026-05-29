-- SmartFarm iotdb 스키마
-- 실행: mysql -u root -p < schema.sql

CREATE DATABASE IF NOT EXISTS iotdb
  CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE iotdb;

CREATE USER IF NOT EXISTS 'iot'@'localhost' IDENTIFIED BY 'pwiot';
GRANT ALL PRIVILEGES ON iotdb.* TO 'iot'@'localhost';
FLUSH PRIVILEGES;

-- 센서 로그 (기존 + flame 컬럼 추가)
CREATE TABLE IF NOT EXISTS sensor (
    id     BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name   VARCHAR(20)  NOT NULL,
    date   DATETIME     NOT NULL,
    time   DATETIME     NOT NULL,
    illu   INT          NOT NULL DEFAULT 0,
    temp   FLOAT        NOT NULL DEFAULT 0,
    humi   FLOAT        NOT NULL DEFAULT 0,
    flame  TINYINT(1)   NOT NULL DEFAULT 0,
    INDEX idx_name (name, date)
) ENGINE=InnoDB;

-- 액추에이터 상태 테이블
CREATE TABLE IF NOT EXISTS device (
    id     INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name   VARCHAR(20)  NOT NULL UNIQUE,
    value  VARCHAR(20)  NOT NULL DEFAULT 'OFF',
    date   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    time   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 기본 액추에이터 등록
INSERT IGNORE INTO device (name, value) VALUES
  ('MOTOR',  'OFF'),
  ('BUZZER', 'OFF'),
  ('LED',    'OFF'),
  ('PUMP',   'OFF');

-- STM32 수신 로그
CREATE TABLE IF NOT EXISTS actuator_log (
    id         INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    raw_msg    VARCHAR(256) NOT NULL,
    created_at DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;
