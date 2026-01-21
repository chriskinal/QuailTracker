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
