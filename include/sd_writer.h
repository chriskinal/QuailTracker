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

#ifndef SD_WRITER_H
#define SD_WRITER_H

#include <stdint.h>
#include "ring_buffer.h"

/**
 * Initialize the SD card writer subsystem.
 *
 * @param buffer Pointer to ring buffer containing audio data
 * @return true if SD card initialized successfully
 */
bool sdWriterInit(RingBuffer* buffer);

/**
 * Start a new recording.
 *
 * @param timestamp Unix timestamp for filename (0 = use millis)
 * @return true if file created successfully
 */
bool sdWriterStartRecording(uint32_t timestamp = 0);

/**
 * Stop the current recording.
 * Finalizes the WAV header and closes the file.
 */
void sdWriterStopRecording();

/**
 * Start the SD writer task.
 */
void sdWriterStart();

/**
 * Stop the SD writer task.
 */
void sdWriterStop();

/**
 * Check if currently recording.
 */
bool sdWriterIsRecording();

/**
 * Get current recording statistics.
 */
struct SDWriterStats {
    uint32_t bytesWritten;
    uint32_t writeErrors;
    uint32_t currentFileSize;
    char currentFilename[64];
};

SDWriterStats sdWriterGetStats();

/**
 * Get SD card info.
 */
struct SDCardInfo {
    uint64_t totalBytes;
    uint64_t usedBytes;
    uint64_t freeBytes;
    bool mounted;
};

SDCardInfo sdGetCardInfo();

/**
 * Check if SD card is physically inserted.
 * Uses card detect pin (GPIO34) with external pull-up.
 * @return true if card is inserted (pin LOW)
 */
bool sdCardInserted();

#endif // SD_WRITER_H
