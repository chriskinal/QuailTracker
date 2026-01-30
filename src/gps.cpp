/*
 * QuailTracker - GPS-synchronized Autonomous Recording Unit
 * Copyright (C) 2026 QuailTracker Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "gps.h"
#include "config.h"

#include <Arduino.h>
#include <HardwareSerial.h>

static HardwareSerial gpsSerial(2);  // UART2
static TaskHandle_t s_gpsTask = nullptr;
static volatile bool s_running = false;

static GPSData s_gpsData = {0};
static volatile uint32_t s_lastPpsMillis = 0;
static GPSPowerMode s_powerMode = GPS_CONTINUOUS;

// NMEA parsing buffer
#define NMEA_BUFFER_SIZE 128
static char s_nmeaBuffer[NMEA_BUFFER_SIZE];
static int s_nmeaIndex = 0;

// PPS interrupt handler
static void IRAM_ATTR ppsInterrupt()
{
    s_lastPpsMillis = millis();
    s_gpsData.ppsValid = true;
    s_gpsData.lastPpsTime = s_lastPpsMillis;
}

// Calculate NMEA checksum
static uint8_t nmeaChecksum(const char* sentence)
{
    uint8_t checksum = 0;
    // Skip leading $ if present
    if (*sentence == '$') sentence++;

    while (*sentence && *sentence != '*') {
        checksum ^= *sentence++;
    }
    return checksum;
}

// Parse degrees + minutes to decimal degrees
static double parseCoordinate(const char* str, char direction)
{
    if (!str || strlen(str) < 4) return 0.0;

    double raw = atof(str);
    int degrees = (int)(raw / 100);
    double minutes = raw - (degrees * 100);
    double decimal = degrees + (minutes / 60.0);

    if (direction == 'S' || direction == 'W') {
        decimal = -decimal;
    }

    return decimal;
}

// Parse GGA sentence (position and fix data)
static void parseGGA(char* sentence)
{
    char* token;
    char* dirToken;
    int field = 0;

    token = strtok(sentence, ",");
    while (token != NULL) {
        switch (field) {
            case 1:  // Time HHMMSS.sss
                if (strlen(token) >= 6) {
                    s_gpsData.hour = (token[0] - '0') * 10 + (token[1] - '0');
                    s_gpsData.minute = (token[2] - '0') * 10 + (token[3] - '0');
                    s_gpsData.second = (token[4] - '0') * 10 + (token[5] - '0');
                    if (strlen(token) > 7) {
                        s_gpsData.millisecond = atoi(token + 7);
                    }
                }
                break;
            case 2:  // Latitude
                dirToken = strtok(NULL, ",");
                if (dirToken != NULL && strlen(dirToken) > 0) {
                    s_gpsData.latitude = parseCoordinate(token, dirToken[0]);
                }
                field++;  // Skip the N/S field
                break;
            case 4:  // Longitude
                dirToken = strtok(NULL, ",");
                if (dirToken != NULL && strlen(dirToken) > 0) {
                    s_gpsData.longitude = parseCoordinate(token, dirToken[0]);
                }
                field++;  // Skip the E/W field
                break;
            case 6:  // Fix quality
                s_gpsData.valid = (atoi(token) > 0);
                break;
            case 7:  // Satellites
                s_gpsData.satellites = atoi(token);
                break;
            case 8:  // HDOP
                s_gpsData.hdop = atof(token);
                break;
            case 9:  // Altitude
                s_gpsData.altitude = atof(token);
                break;
        }
        field++;
        token = strtok(NULL, ",");
    }
}

// Parse RMC sentence (recommended minimum data)
static void parseRMC(char* sentence)
{
    char* token;
    int field = 0;

    token = strtok(sentence, ",");
    while (token != NULL) {
        switch (field) {
            case 1:  // Time
                if (strlen(token) >= 6) {
                    s_gpsData.hour = (token[0] - '0') * 10 + (token[1] - '0');
                    s_gpsData.minute = (token[2] - '0') * 10 + (token[3] - '0');
                    s_gpsData.second = (token[4] - '0') * 10 + (token[5] - '0');
                }
                break;
            case 2:  // Status A=valid, V=invalid
                s_gpsData.valid = (token[0] == 'A');
                break;
            case 9:  // Date DDMMYY
                if (strlen(token) >= 6) {
                    s_gpsData.day = (token[0] - '0') * 10 + (token[1] - '0');
                    s_gpsData.month = (token[2] - '0') * 10 + (token[3] - '0');
                    s_gpsData.year = 2000 + (token[4] - '0') * 10 + (token[5] - '0');
                }
                break;
        }
        field++;
        token = strtok(NULL, ",");
    }
}

// Process a complete NMEA sentence
static void processNMEA(char* sentence)
{
    if (strncmp(sentence, "$GPGGA", 6) == 0 ||
        strncmp(sentence, "$GNGGA", 6) == 0) {
        parseGGA(sentence);
    } else if (strncmp(sentence, "$GPRMC", 6) == 0 ||
               strncmp(sentence, "$GNRMC", 6) == 0) {
        parseRMC(sentence);
    }
}

static void gpsTaskFunc(void* param)
{
    Serial.printf("GPS task started on core %d\n", xPortGetCoreID());

    while (s_running) {
        while (gpsSerial.available()) {
            char c = gpsSerial.read();

            if (c == '$') {
                // Start of new sentence
                s_nmeaIndex = 0;
            }

            if (s_nmeaIndex < NMEA_BUFFER_SIZE - 1) {
                s_nmeaBuffer[s_nmeaIndex++] = c;
            }

            if (c == '\n') {
                s_nmeaBuffer[s_nmeaIndex] = '\0';
                processNMEA(s_nmeaBuffer);
                s_nmeaIndex = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    Serial.println("GPS task stopped");
    vTaskDelete(NULL);
}

bool gpsInit()
{
    Serial.println("Initializing GPS...");

    // Set up power control pins BEFORE enabling GPS
    // PWR_EN: HIGH = VCC off, LOW = VCC on (P-FET via Q2)
    pinMode(PIN_GPS_PWR_EN, OUTPUT);
    digitalWrite(PIN_GPS_PWR_EN, LOW);  // Start with power ON

    // WAKEUP: HIGH = standby mode, LOW = continuous (via Q3)
    pinMode(PIN_GPS_WAKEUP, OUTPUT);
    digitalWrite(PIN_GPS_WAKEUP, LOW);  // Start in continuous mode

    s_powerMode = GPS_CONTINUOUS;

    // Small delay for GPS to power up
    delay(100);

    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

    // Set up PPS interrupt
    pinMode(PIN_GPS_PPS, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS), ppsInterrupt, RISING);

    Serial.printf("  UART: RX=%d, TX=%d, Baud=%d\n",
                  PIN_GPS_RX, PIN_GPS_TX, GPS_BAUD);
    Serial.printf("  PPS: GPIO%d\n", PIN_GPS_PPS);
    Serial.printf("  PWR_EN: GPIO%d, WAKEUP: GPIO%d\n",
                  PIN_GPS_PWR_EN, PIN_GPS_WAKEUP);

    return true;
}

void gpsStart()
{
    if (s_running) return;

    s_running = true;

    xTaskCreatePinnedToCore(
        gpsTaskFunc,
        "gps",
        STACK_GPS,
        NULL,
        PRIORITY_GPS,
        &s_gpsTask,
        CORE_SYSTEM
    );
}

void gpsStop()
{
    s_running = false;
    s_gpsTask = nullptr;
}

GPSData gpsGetData()
{
    return s_gpsData;
}

bool gpsHasFix()
{
    return s_gpsData.valid && s_gpsData.satellites >= 3;
}

uint32_t gpsGetTimestamp()
{
    if (!s_gpsData.valid || s_gpsData.year < 2024) {
        return 0;
    }

    // Simple Unix timestamp calculation
    // Note: This is a simplified version, doesn't account for leap years properly
    struct tm timeinfo = {0};
    timeinfo.tm_year = s_gpsData.year - 1900;
    timeinfo.tm_mon = s_gpsData.month - 1;
    timeinfo.tm_mday = s_gpsData.day;
    timeinfo.tm_hour = s_gpsData.hour;
    timeinfo.tm_min = s_gpsData.minute;
    timeinfo.tm_sec = s_gpsData.second;

    return mktime(&timeinfo);
}

void gpsSendCommand(const char* cmd)
{
    gpsSerial.print('$');
    gpsSerial.print(cmd);

    uint8_t checksum = nmeaChecksum(cmd);
    gpsSerial.printf("*%02X\r\n", checksum);
}

void gpsSetPowerMode(GPSPowerMode mode)
{
    switch (mode) {
        case GPS_CONTINUOUS:
            // VCC on, WAKEUP high (continuous operation)
            digitalWrite(PIN_GPS_PWR_EN, LOW);   // P-FET on
            digitalWrite(PIN_GPS_WAKEUP, LOW);   // Q3 off, WAKEUP floats high
            if (s_powerMode == GPS_BACKUP) {
                // Coming out of backup, wait for GPS to boot
                delay(100);
            }
            Serial.println("GPS: Continuous mode (~25mA)");
            break;

        case GPS_STANDBY:
            // VCC on, WAKEUP low (standby mode)
            digitalWrite(PIN_GPS_PWR_EN, LOW);   // P-FET on
            digitalWrite(PIN_GPS_WAKEUP, HIGH);  // Q3 on, pulls WAKEUP low
            Serial.println("GPS: Standby mode (~1mA)");
            break;

        case GPS_BACKUP:
            // VCC off (V_BCKP still powered for RTC)
            digitalWrite(PIN_GPS_PWR_EN, HIGH);  // P-FET off
            digitalWrite(PIN_GPS_WAKEUP, LOW);   // Don't care, but set low
            Serial.println("GPS: Backup mode (~7uA)");
            break;
    }
    s_powerMode = mode;
}

GPSPowerMode gpsGetPowerMode()
{
    return s_powerMode;
}
