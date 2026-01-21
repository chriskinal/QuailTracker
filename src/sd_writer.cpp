#include "sd_writer.h"
#include "config.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

static RingBuffer* s_audioBuffer = nullptr;
static TaskHandle_t s_writerTask = nullptr;
static volatile bool s_running = false;
static volatile bool s_recording = false;

static File s_audioFile;
static SDWriterStats s_stats = {0};
static uint32_t s_dataSize = 0;

// WAV header structure
struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t fileSize = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1;  // PCM
    uint16_t numChannels = CHANNELS;
    uint32_t sampleRate = SAMPLE_RATE;
    uint32_t byteRate = SAMPLE_RATE * CHANNELS * BITS_PER_SAMPLE / 8;
    uint16_t blockAlign = CHANNELS * BITS_PER_SAMPLE / 8;
    uint16_t bitsPerSample = BITS_PER_SAMPLE;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize = 0;
};

static void writeWavHeader(File& file, uint32_t dataSize)
{
    WavHeader header;
    header.fileSize = dataSize + sizeof(WavHeader) - 8;
    header.dataSize = dataSize;

    file.seek(0);
    file.write((uint8_t*)&header, sizeof(header));
}

static void writerTaskFunc(void* param)
{
    Serial.printf("SD Writer task started on core %d\n", xPortGetCoreID());

    uint8_t writeBuffer[SD_WRITE_CHUNK_SIZE];

    while (s_running) {
        if (!s_recording) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Read from ring buffer
        size_t bytesRead = s_audioBuffer->read(writeBuffer, SD_WRITE_CHUNK_SIZE,
                                                pdMS_TO_TICKS(100));

        if (bytesRead > 0 && s_audioFile) {
            size_t written = s_audioFile.write(writeBuffer, bytesRead);

            if (written != bytesRead) {
                s_stats.writeErrors++;
                Serial.println("SD write error!");
            } else {
                s_stats.bytesWritten += written;
                s_dataSize += written;
                s_stats.currentFileSize = s_dataSize + WAV_HEADER_SIZE;
            }
        }
    }

    Serial.println("SD Writer task stopped");
    vTaskDelete(NULL);
}

bool sdWriterInit(RingBuffer* buffer)
{
    s_audioBuffer = buffer;

    Serial.println("Initializing SD card...");

    SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

    if (!SD.begin(PIN_SD_CS)) {
        Serial.println("  SD card init failed!");
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("  No SD card!");
        return false;
    }

    const char* typeStr = "UNKNOWN";
    switch (cardType) {
        case CARD_MMC:  typeStr = "MMC"; break;
        case CARD_SD:   typeStr = "SD"; break;
        case CARD_SDHC: typeStr = "SDHC"; break;
    }

    Serial.printf("  SD card type: %s\n", typeStr);
    Serial.printf("  SD card size: %llu MB\n", SD.cardSize() / (1024 * 1024));

    return true;
}

bool sdWriterStartRecording(uint32_t timestamp)
{
    if (s_recording) {
        sdWriterStopRecording();
    }

    // Generate filename
    char filename[MAX_FILENAME_LEN];
    if (timestamp == 0) {
        // Use millis for now - GPS will provide real timestamps later
        snprintf(filename, sizeof(filename), "/%s_%lu.wav",
                 STATION_ID, millis());
    } else {
        // TODO: Format timestamp as YYYYMMDD_HHMMSS
        snprintf(filename, sizeof(filename), "/%s_%lu.wav",
                 STATION_ID, timestamp);
    }

    strncpy(s_stats.currentFilename, filename, sizeof(s_stats.currentFilename));

    Serial.printf("Starting recording: %s\n", filename);

    s_audioFile = SD.open(filename, FILE_WRITE);
    if (!s_audioFile) {
        Serial.println("Failed to create file!");
        return false;
    }

    // Write placeholder header
    s_dataSize = 0;
    writeWavHeader(s_audioFile, 0);
    s_audioFile.flush();

    s_stats.bytesWritten = 0;
    s_stats.writeErrors = 0;
    s_recording = true;

    return true;
}

void sdWriterStopRecording()
{
    if (!s_recording) return;

    s_recording = false;

    // Wait a bit for any pending writes
    vTaskDelay(pdMS_TO_TICKS(200));

    // Update WAV header with actual size
    if (s_audioFile) {
        writeWavHeader(s_audioFile, s_dataSize);
        s_audioFile.close();
    }

    Serial.printf("Recording stopped. Total size: %lu bytes\n",
                  s_dataSize + WAV_HEADER_SIZE);
}

void sdWriterStart()
{
    if (s_running) return;

    s_running = true;

    xTaskCreatePinnedToCore(
        writerTaskFunc,
        "sd_writer",
        STACK_SD_WRITER,
        NULL,
        PRIORITY_SD_WRITER,
        &s_writerTask,
        CORE_SYSTEM
    );
}

void sdWriterStop()
{
    if (s_recording) {
        sdWriterStopRecording();
    }

    s_running = false;
    s_writerTask = nullptr;
}

bool sdWriterIsRecording()
{
    return s_recording;
}

SDWriterStats sdWriterGetStats()
{
    return s_stats;
}

SDCardInfo sdGetCardInfo()
{
    SDCardInfo info = {0};

    if (SD.cardType() != CARD_NONE) {
        info.mounted = true;
        info.totalBytes = SD.totalBytes();
        info.usedBytes = SD.usedBytes();
        info.freeBytes = info.totalBytes - info.usedBytes;
    }

    return info;
}
