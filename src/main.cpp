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
 *
 * RTOS-based firmware for ESP32 with:
 *   - ES7243E I2S ADC (audio capture)
 *   - L76K GPS module (time sync)
 *   - MicroSD card (recording storage)
 *   - Battery monitoring
 *   - SHT30 temperature/humidity sensor
 *
 * Architecture:
 *   Core 0: GPS, SD Writer, Battery Monitor, Temp Sensor, Serial Console
 *   Core 1: Audio Capture (dedicated, high priority)
 */

#include <Arduino.h>
#include "config.h"
#if !DISABLE_BLE
#include <NimBLEDevice.h>
#endif
#include "ring_buffer.h"
#include "audio_capture.h"
#include "sd_writer.h"
#include "gps.h"
#include "battery.h"
#include "temperature.h"

#if !DISABLE_BLE
// Nordic UART Service UUIDs
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// BLE objects
static NimBLEServer* pServer = nullptr;
static NimBLECharacteristic* pTxCharacteristic = nullptr;
static bool bleConnected = false;
static String bleRxBuffer = "";

// BLE callbacks
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        bleConnected = true;
        Serial.println("BLE client connected");
    }
    void onDisconnect(NimBLEServer* pServer) {
        bleConnected = false;
        Serial.println("BLE client disconnected");
        // Restart advertising
        NimBLEDevice::startAdvertising();
    }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            bleRxBuffer += rxValue.c_str();
        }
    }
};
#endif // !DISABLE_BLE

// Global ring buffer for audio data
static RingBuffer *audioBuffer = nullptr;

// Recording state
static bool isRecording = false;

#if !DISABLE_BLE
// BLE send helper (splits long messages into 20-byte chunks)
static void bleSend(const char* str)
{
    if (!bleConnected || !pTxCharacteristic) return;

    size_t len = strlen(str);
    size_t offset = 0;
    while (offset < len) {
        size_t chunkLen = min((size_t)20, len - offset);
        pTxCharacteristic->setValue((uint8_t*)(str + offset), chunkLen);
        pTxCharacteristic->notify();
        offset += chunkLen;
        delay(10);  // Give BLE stack time to send
    }
}
#endif // !DISABLE_BLE

// Dual-output print helpers (Serial + BLE)
static void logPrint(const char* str)
{
    Serial.print(str);
#if !DISABLE_BLE
    bleSend(str);
#endif
}

static void logPrintf(const char* format, ...)
{
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Serial.print(buf);
#if !DISABLE_BLE
    bleSend(buf);
#endif
}

static void logPrintln(const char* str = "")
{
    Serial.println(str);
#if !DISABLE_BLE
    char buf[258];
    snprintf(buf, sizeof(buf), "%s\r\n", str);
    bleSend(buf);
#endif
}

// Forward declarations
void printStatus();
void printMenu();
void startRecording();
void stopRecording();

void setup()
{
    Serial.begin(115200);
    delay(1000);

#if !DISABLE_BLE
    // Initialize NimBLE
    NimBLEDevice::init("QuailTracker");
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    NimBLEService* pService = pServer->createService(SERVICE_UUID);

    pTxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_TX,
        NIMBLE_PROPERTY::NOTIFY
    );

    NimBLECharacteristic* pRxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_RX,
        NIMBLE_PROPERTY::WRITE
    );
    pRxCharacteristic->setCallbacks(new RxCallbacks());

    pService->start();

    // Configure advertising
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setName("QuailTracker");
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // For iPhone compatibility
    pAdvertising->setMaxPreferred(0x12);
    pAdvertising->start();
    Serial.println("BLE advertising started");
    Serial.printf("BLE MAC: %s\n", NimBLEDevice::getAddress().toString().c_str());
#endif

    logPrintln("\n\n");
    logPrintln("================================================");
    logPrintln("  QuailTracker - Autonomous Recording Unit");
    logPrintf("  Firmware Version: %s\n", FIRMWARE_VERSION);
    logPrintln("================================================");
    logPrintf("ESP32 CPU Freq: %d MHz\n", ESP.getCpuFreqMHz());
    logPrintf("Free Heap: %d bytes\n", ESP.getFreeHeap());
    logPrintln();
#if !DISABLE_BLE
    logPrintln("BLE: QuailTracker (Nordic UART Service)");
#else
    logPrintln("BLE: DISABLED (noise testing mode)");
#endif

    // Create audio ring buffer
    logPrintf("Creating ring buffer (%d bytes)...\n", RING_BUFFER_SIZE);
    logPrintf("Free heap before: %d bytes\n", ESP.getFreeHeap());

    audioBuffer = new RingBuffer(RING_BUFFER_SIZE);

    if (!audioBuffer->isValid())
    {
        logPrintln("FATAL: Ring buffer allocation failed!");
        logPrintln("Halting...");
        while (1)
        {
            delay(1000);
        }
    }
    logPrintf("Free heap after: %d bytes\n", ESP.getFreeHeap());
    logPrintln("Ring buffer OK");

    // Initialize subsystems
    logPrintln("\n--- Initializing Subsystems ---\n");

    // Battery monitor
    batteryInit();

    // SD Card
    bool sdOk = sdWriterInit(audioBuffer);
    if (!sdOk)
    {
        logPrintln("WARNING: SD card not available!");
    }

    // GPS
    bool gpsOk = gpsInit();
    if (!gpsOk)
    {
        logPrintln("WARNING: GPS init failed!");
    }

    // Audio (I2S + ES7243E)
    bool audioOk = audioInit(audioBuffer);
    if (!audioOk)
    {
        logPrintln("WARNING: Audio init failed!");
    }

    // Temperature/Humidity sensor
    bool tempOk = tempInit();
    if (!tempOk)
    {
        logPrintln("WARNING: Temperature sensor not found!");
    }

    // Start background tasks
    logPrintln("\n--- Starting Tasks ---\n");

    batteryStart();
    gpsStart();
    sdWriterStart();
    audioStart();
    if (tempIsPresent())
    {
        tempStart();
    }

    logPrintln("\n--- System Ready ---\n");
    printMenu();
}

// Get command character from Serial or BLE
static int getCommand()
{
    if (Serial.available()) {
        int c = Serial.read();
        while (Serial.available()) Serial.read();  // Flush
        return c;
    }
#if !DISABLE_BLE
    if (bleRxBuffer.length() > 0) {
        int c = bleRxBuffer[0];
        bleRxBuffer.remove(0, 1);
        return c;
    }
#endif
    return -1;
}

void loop()
{
    int c = getCommand();
    if (c >= 0)
    {

        switch (c)
        {
        case '1':
            printStatus();
            printMenu();
            break;

        case '2':
            if (!isRecording)
            {
                startRecording();
            }
            else
            {
                logPrintln("Already recording!");
            }
            printMenu();
            break;

        case '3':
            if (isRecording)
            {
                stopRecording();
            }
            else
            {
                logPrintln("Not recording!");
            }
            printMenu();
            break;

        case '4':
        {
            SDCardInfo info = sdGetCardInfo();
            if (info.mounted)
            {
                logPrintln("\n=== SD Card Info ===");
                logPrintf("Total: %llu MB\n", info.totalBytes / (1024 * 1024));
                logPrintf("Used:  %llu MB\n", info.usedBytes / (1024 * 1024));
                logPrintf("Free:  %llu MB\n", info.freeBytes / (1024 * 1024));
            }
            else
            {
                logPrintln("SD card not mounted!");
            }
            printMenu();
            break;
        }

        case '5':
        {
            GPSData gps = gpsGetData();
            uint32_t validSentences, checksumErrors;
            gpsGetStats(&validSentences, &checksumErrors);

            logPrintln("\n=== GPS Data ===");
            logPrintf("Valid: %s\n", gps.valid ? "Yes" : "No");
            logPrintf("Satellites: %d\n", gps.satellites);
            logPrintf("Position: %.6f, %.6f\n", gps.latitude, gps.longitude);
            logPrintf("Altitude: %.1f m\n", gps.altitude);
            logPrintf("Time: %02d:%02d:%02d UTC\n",
                          gps.hour, gps.minute, gps.second);
            logPrintf("Date: %04d-%02d-%02d\n",
                          gps.year, gps.month, gps.day);
            logPrintf("PPS: %s (last: %lu ms ago)\n",
                          gps.ppsValid ? "OK" : "No signal",
                          millis() - gps.lastPpsTime);
            logPrintf("NMEA: %lu valid, %lu checksum errors\n",
                          validSentences, checksumErrors);
            printMenu();
            break;
        }

        case '6':
        {
            BatteryData batt = batteryGetData();
            logPrintln("\n=== Battery ===");
            logPrintf("Voltage: %.2f V\n", batt.voltage);
            logPrintf("Level: %d%%\n", batt.percentage);
            logPrintf("Status: %s\n",
                          batt.level == BATTERY_OK ? "OK" : batt.level == BATTERY_LOW ? "LOW"
                                                                                      : "CRITICAL");
            printMenu();
            break;
        }

        case '7':
        {
            if (tempIsPresent())
            {
                TempHumidityData temp = tempGetData();
                logPrintln("\n=== Temperature/Humidity ===");
                if (temp.valid)
                {
                    logPrintf("Temperature: %.1f C (%.1f F)\n",
                                  temp.temperature,
                                  temp.temperature * 9.0f / 5.0f + 32.0f);
                    logPrintf("Humidity: %.1f%%\n", temp.humidity);
                    logPrintf("Last read: %lu ms ago\n",
                                  millis() - temp.lastReadTime);
                }
                else
                {
                    logPrintln("No valid reading yet");
                }
            }
            else
            {
                logPrintln("\nTemperature sensor not present!");
            }
            printMenu();
            break;
        }

        case '8':
            gpsSetPowerMode(GPS_CONTINUOUS);
            printMenu();
            break;

        case '9':
            gpsSetPowerMode(GPS_STANDBY);
            printMenu();
            break;

        case '0':
            gpsSetPowerMode(GPS_BACKUP);
            printMenu();
            break;

        case 'r':
        case 'R':
            // Quick record toggle
            if (isRecording)
            {
                stopRecording();
            }
            else
            {
                startRecording();
            }
            break;

        case 'd':
        case 'D':
            gpsDebugDump(5000);
            printMenu();
            break;
        }
    }

    // Check for low battery during recording
    if (isRecording)
    {
        BatteryData batt = batteryGetData();
        if (batt.level == BATTERY_CRITICAL)
        {
            logPrintln("\n!!! CRITICAL BATTERY - STOPPING RECORDING !!!\n");
            stopRecording();
        }
    }

    delay(100);
}

void printStatus()
{
    logPrintln("\n========== STATUS ==========");

    // Audio
    AudioStats audio = audioGetStats();
    logPrintln("Audio:");
    logPrintf("  Running: %s\n", audioIsRunning() ? "Yes" : "No");
    logPrintf("  Samples: %lu\n", audio.samplesCaptures);
    logPrintf("  Overflows: %lu\n", audio.bufferOverflows);
    logPrintf("  Peak Level: %d\n", audio.peakLevel);
    logPrintf("  Buffer: %d / %d bytes\n",
                  audioBuffer->available(), audioBuffer->capacity());

    // Recording
    SDWriterStats sd = sdWriterGetStats();
    logPrintln("Recording:");
    logPrintf("  Active: %s\n", isRecording ? "Yes" : "No");
    if (isRecording)
    {
        logPrintf("  File: %s\n", sd.currentFilename);
        logPrintf("  Size: %lu bytes\n", sd.currentFileSize);
        logPrintf("  Errors: %lu\n", sd.writeErrors);
    }

    // SD Card
    logPrintln("SD Card:");
    logPrintf("  Inserted: %s\n", sdCardInserted() ? "Yes" : "No");
    SDCardInfo cardInfo = sdGetCardInfo();
    logPrintf("  Mounted: %s\n", cardInfo.mounted ? "Yes" : "No");

    // GPS
    GPSData gps = gpsGetData();
    const char* gpsPowerStr = "Unknown";
    switch (gpsGetPowerMode()) {
        case GPS_CONTINUOUS: gpsPowerStr = "Continuous (~25mA)"; break;
        case GPS_STANDBY:    gpsPowerStr = "Standby (~1mA)"; break;
        case GPS_BACKUP:     gpsPowerStr = "Backup (~7uA)"; break;
    }
    logPrintln("GPS:");
    logPrintf("  Power: %s\n", gpsPowerStr);
    logPrintf("  Fix: %s (%d sats)\n",
                  gps.valid ? "Yes" : "No", gps.satellites);
    logPrintf("  PPS: %s\n", gps.ppsValid ? "OK" : "No signal");

    // Battery
    BatteryData batt = batteryGetData();
    logPrintln("Battery:");
    logPrintf("  %.2fV (%d%%)\n", batt.voltage, batt.percentage);

    // Temperature/Humidity
    if (tempIsPresent())
    {
        TempHumidityData temp = tempGetData();
        logPrintln("Environment:");
        if (temp.valid)
        {
            logPrintf("  %.1fC / %.1f%% RH\n", temp.temperature, temp.humidity);
        }
        else
        {
            logPrintln("  (no reading)");
        }
    }

    logPrintln("============================\n");
}

void printMenu()
{
    logPrintln("\n===== MENU =====");
    logPrintln("1. Status");
    logPrintln("2. Start Recording");
    logPrintln("3. Stop Recording");
    logPrintln("4. SD Card Info");
    logPrintln("5. GPS Data");
    logPrintln("6. Battery");
    logPrintln("7. Temperature/Humidity");
    logPrintln("8. GPS Continuous Mode");
    logPrintln("9. GPS Standby Mode");
    logPrintln("0. GPS Backup Mode");
    logPrintln("R. Toggle Recording");
    logPrintln("D. GPS Debug (raw UART)");
    logPrintln("================");
    logPrintf("[%s] > ", isRecording ? "REC" : "IDLE");
}

void startRecording()
{
    logPrintln("\nStarting recording...");

    // Reset audio stats
    audioResetStats();

    // Clear buffer
    audioBuffer->clear();
    audioBuffer->resetOverflowCount();

    // Start SD recording
    uint32_t timestamp = gpsGetTimestamp();
    if (!sdWriterStartRecording(timestamp))
    {
        logPrintln("Failed to start recording!");
        return;
    }

    isRecording = true;
    logPrintln("Recording started!");
}

void stopRecording()
{
    logPrintln("\nStopping recording...");

    sdWriterStopRecording();
    isRecording = false;

    // Print final stats
    AudioStats audio = audioGetStats();
    logPrintf("Total samples captured: %lu\n", audio.samplesCaptures);
    logPrintf("Buffer overflows: %lu\n", audio.bufferOverflows);
    logPrintf("Ring buffer overflows: %lu\n", audioBuffer->getOverflowCount());

    logPrintln("Recording stopped!");
}
