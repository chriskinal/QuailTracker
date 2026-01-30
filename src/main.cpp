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
#include "ring_buffer.h"
#include "audio_capture.h"
#include "sd_writer.h"
#include "gps.h"
#include "battery.h"
#include "temperature.h"

// Global ring buffer for audio data
static RingBuffer *audioBuffer = nullptr;

// Recording state
static bool isRecording = false;

// Forward declarations
void printStatus();
void printMenu();
void startRecording();
void stopRecording();

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n");
    Serial.println("================================================");
    Serial.println("  QuailTracker - Autonomous Recording Unit");
    Serial.printf("  Firmware Version: %s\n", FIRMWARE_VERSION);
    Serial.println("================================================");
    Serial.printf("ESP32 CPU Freq: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
    Serial.println();

    // Create audio ring buffer
    Serial.printf("Creating ring buffer (%d bytes)...\n", RING_BUFFER_SIZE);
    Serial.printf("Free heap before: %d bytes\n", ESP.getFreeHeap());

    audioBuffer = new RingBuffer(RING_BUFFER_SIZE);

    if (!audioBuffer->isValid())
    {
        Serial.println("FATAL: Ring buffer allocation failed!");
        Serial.println("Halting...");
        while (1)
        {
            delay(1000);
        }
    }
    Serial.printf("Free heap after: %d bytes\n", ESP.getFreeHeap());
    Serial.println("Ring buffer OK");

    // Initialize subsystems
    Serial.println("\n--- Initializing Subsystems ---\n");

    // Battery monitor
    batteryInit();

    // SD Card
    bool sdOk = sdWriterInit(audioBuffer);
    if (!sdOk)
    {
        Serial.println("WARNING: SD card not available!");
    }

    // GPS
    bool gpsOk = gpsInit();
    if (!gpsOk)
    {
        Serial.println("WARNING: GPS init failed!");
    }

    // Audio (I2S + ES7243E)
    bool audioOk = audioInit(audioBuffer);
    if (!audioOk)
    {
        Serial.println("WARNING: Audio init failed!");
    }

    // Temperature/Humidity sensor
    bool tempOk = tempInit();
    if (!tempOk)
    {
        Serial.println("WARNING: Temperature sensor not found!");
    }

    // Start background tasks
    Serial.println("\n--- Starting Tasks ---\n");

    batteryStart();
    gpsStart();
    sdWriterStart();
    audioStart();
    if (tempIsPresent())
    {
        tempStart();
    }

    Serial.println("\n--- System Ready ---\n");
    printMenu();
}

void loop()
{
    if (Serial.available())
    {
        char c = Serial.read();
        // Flush extra characters
        while (Serial.available())
            Serial.read();

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
                Serial.println("Already recording!");
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
                Serial.println("Not recording!");
            }
            printMenu();
            break;

        case '4':
        {
            SDCardInfo info = sdGetCardInfo();
            if (info.mounted)
            {
                Serial.println("\n=== SD Card Info ===");
                Serial.printf("Total: %llu MB\n", info.totalBytes / (1024 * 1024));
                Serial.printf("Used:  %llu MB\n", info.usedBytes / (1024 * 1024));
                Serial.printf("Free:  %llu MB\n", info.freeBytes / (1024 * 1024));
            }
            else
            {
                Serial.println("SD card not mounted!");
            }
            printMenu();
            break;
        }

        case '5':
        {
            GPSData gps = gpsGetData();
            Serial.println("\n=== GPS Data ===");
            Serial.printf("Valid: %s\n", gps.valid ? "Yes" : "No");
            Serial.printf("Satellites: %d\n", gps.satellites);
            Serial.printf("Position: %.6f, %.6f\n", gps.latitude, gps.longitude);
            Serial.printf("Altitude: %.1f m\n", gps.altitude);
            Serial.printf("Time: %02d:%02d:%02d UTC\n",
                          gps.hour, gps.minute, gps.second);
            Serial.printf("Date: %04d-%02d-%02d\n",
                          gps.year, gps.month, gps.day);
            Serial.printf("PPS: %s (last: %lu ms ago)\n",
                          gps.ppsValid ? "OK" : "No signal",
                          millis() - gps.lastPpsTime);
            printMenu();
            break;
        }

        case '6':
        {
            BatteryData batt = batteryGetData();
            Serial.println("\n=== Battery ===");
            Serial.printf("Voltage: %.2f V\n", batt.voltage);
            Serial.printf("Level: %d%%\n", batt.percentage);
            Serial.printf("Status: %s\n",
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
                Serial.println("\n=== Temperature/Humidity ===");
                if (temp.valid)
                {
                    Serial.printf("Temperature: %.1f C (%.1f F)\n",
                                  temp.temperature,
                                  temp.temperature * 9.0f / 5.0f + 32.0f);
                    Serial.printf("Humidity: %.1f%%\n", temp.humidity);
                    Serial.printf("Last read: %lu ms ago\n",
                                  millis() - temp.lastReadTime);
                }
                else
                {
                    Serial.println("No valid reading yet");
                }
            }
            else
            {
                Serial.println("\nTemperature sensor not present!");
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
        }
    }

    // Check for low battery during recording
    if (isRecording)
    {
        BatteryData batt = batteryGetData();
        if (batt.level == BATTERY_CRITICAL)
        {
            Serial.println("\n!!! CRITICAL BATTERY - STOPPING RECORDING !!!\n");
            stopRecording();
        }
    }

    delay(100);
}

void printStatus()
{
    Serial.println("\n========== STATUS ==========");

    // Audio
    AudioStats audio = audioGetStats();
    Serial.println("Audio:");
    Serial.printf("  Running: %s\n", audioIsRunning() ? "Yes" : "No");
    Serial.printf("  Samples: %lu\n", audio.samplesCaptures);
    Serial.printf("  Overflows: %lu\n", audio.bufferOverflows);
    Serial.printf("  Peak Level: %d\n", audio.peakLevel);
    Serial.printf("  Buffer: %d / %d bytes\n",
                  audioBuffer->available(), audioBuffer->capacity());

    // Recording
    SDWriterStats sd = sdWriterGetStats();
    Serial.println("Recording:");
    Serial.printf("  Active: %s\n", isRecording ? "Yes" : "No");
    if (isRecording)
    {
        Serial.printf("  File: %s\n", sd.currentFilename);
        Serial.printf("  Size: %lu bytes\n", sd.currentFileSize);
        Serial.printf("  Errors: %lu\n", sd.writeErrors);
    }

    // SD Card
    Serial.println("SD Card:");
    Serial.printf("  Inserted: %s\n", sdCardInserted() ? "Yes" : "No");
    SDCardInfo cardInfo = sdGetCardInfo();
    Serial.printf("  Mounted: %s\n", cardInfo.mounted ? "Yes" : "No");

    // GPS
    GPSData gps = gpsGetData();
    const char* gpsPowerStr = "Unknown";
    switch (gpsGetPowerMode()) {
        case GPS_CONTINUOUS: gpsPowerStr = "Continuous (~25mA)"; break;
        case GPS_STANDBY:    gpsPowerStr = "Standby (~1mA)"; break;
        case GPS_BACKUP:     gpsPowerStr = "Backup (~7uA)"; break;
    }
    Serial.println("GPS:");
    Serial.printf("  Power: %s\n", gpsPowerStr);
    Serial.printf("  Fix: %s (%d sats)\n",
                  gps.valid ? "Yes" : "No", gps.satellites);
    Serial.printf("  PPS: %s\n", gps.ppsValid ? "OK" : "No signal");

    // Battery
    BatteryData batt = batteryGetData();
    Serial.println("Battery:");
    Serial.printf("  %.2fV (%d%%)\n", batt.voltage, batt.percentage);

    // Temperature/Humidity
    if (tempIsPresent())
    {
        TempHumidityData temp = tempGetData();
        Serial.println("Environment:");
        if (temp.valid)
        {
            Serial.printf("  %.1fC / %.1f%% RH\n", temp.temperature, temp.humidity);
        }
        else
        {
            Serial.println("  (no reading)");
        }
    }

    Serial.println("============================\n");
}

void printMenu()
{
    Serial.println("\n===== MENU =====");
    Serial.println("1. Status");
    Serial.println("2. Start Recording");
    Serial.println("3. Stop Recording");
    Serial.println("4. SD Card Info");
    Serial.println("5. GPS Data");
    Serial.println("6. Battery");
    Serial.println("7. Temperature/Humidity");
    Serial.println("8. GPS Continuous Mode");
    Serial.println("9. GPS Standby Mode");
    Serial.println("0. GPS Backup Mode");
    Serial.println("R. Toggle Recording");
    Serial.println("================");
    Serial.printf("[%s] > ", isRecording ? "REC" : "IDLE");
}

void startRecording()
{
    Serial.println("\nStarting recording...");

    // Reset audio stats
    audioResetStats();

    // Clear buffer
    audioBuffer->clear();
    audioBuffer->resetOverflowCount();

    // Start SD recording
    uint32_t timestamp = gpsGetTimestamp();
    if (!sdWriterStartRecording(timestamp))
    {
        Serial.println("Failed to start recording!");
        return;
    }

    isRecording = true;
    Serial.println("Recording started!");
}

void stopRecording()
{
    Serial.println("\nStopping recording...");

    sdWriterStopRecording();
    isRecording = false;

    // Print final stats
    AudioStats audio = audioGetStats();
    Serial.printf("Total samples captured: %lu\n", audio.samplesCaptures);
    Serial.printf("Buffer overflows: %lu\n", audio.bufferOverflows);
    Serial.printf("Ring buffer overflows: %lu\n", audioBuffer->getOverflowCount());

    Serial.println("Recording stopped!");
}
