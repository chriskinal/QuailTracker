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

#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <stdint.h>
#include "ring_buffer.h"

/**
 * Initialize the audio capture subsystem.
 * Sets up I2S and ES7243E ADC.
 *
 * @param buffer Pointer to ring buffer for audio data
 * @return true if initialization successful
 */
bool audioInit(RingBuffer* buffer);

/**
 * Start the audio capture task.
 * Creates a FreeRTOS task pinned to CORE_AUDIO.
 */
void audioStart();

/**
 * Stop the audio capture task.
 */
void audioStop();

/**
 * Check if audio capture is running.
 */
bool audioIsRunning();

/**
 * Get current audio statistics.
 */
struct AudioStats {
    uint32_t samplesCaptures;
    uint32_t bufferOverflows;
    uint32_t dmaErrors;
    int16_t peakLevel;
};

AudioStats audioGetStats();

/**
 * Reset audio statistics.
 */
void audioResetStats();

#endif // AUDIO_CAPTURE_H
