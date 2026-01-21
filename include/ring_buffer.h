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

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * Thread-safe ring buffer for audio data.
 *
 * Producer (audio task) writes to the buffer.
 * Consumer (SD writer task) reads from the buffer.
 *
 * Uses a mutex for thread safety and a semaphore to signal data availability.
 */
class RingBuffer {
public:
    RingBuffer(size_t size);
    ~RingBuffer();

    /**
     * Write data to the buffer.
     * @param data Pointer to data to write
     * @param len Number of bytes to write
     * @return Number of bytes actually written (may be less if buffer full)
     */
    size_t write(const uint8_t* data, size_t len);

    /**
     * Read data from the buffer.
     * @param data Pointer to destination buffer
     * @param len Maximum number of bytes to read
     * @param timeout_ms Timeout in milliseconds (0 = no wait, portMAX_DELAY = forever)
     * @return Number of bytes actually read
     */
    size_t read(uint8_t* data, size_t len, uint32_t timeout_ms = portMAX_DELAY);

    /**
     * Get number of bytes available to read.
     */
    size_t available() const;

    /**
     * Get free space in buffer.
     */
    size_t freeSpace() const;

    /**
     * Check if buffer is full.
     */
    bool isFull() const;

    /**
     * Check if buffer is empty.
     */
    bool isEmpty() const;

    /**
     * Clear the buffer.
     */
    void clear();

    /**
     * Get total buffer size.
     */
    size_t capacity() const { return m_size; }

    /**
     * Check if buffer was initialized successfully.
     */
    bool isValid() const { return m_buffer != nullptr && m_mutex != nullptr; }

    /**
     * Get overflow count (number of bytes that couldn't be written).
     */
    uint32_t getOverflowCount() const { return m_overflowCount; }

    /**
     * Reset overflow counter.
     */
    void resetOverflowCount() { m_overflowCount = 0; }

private:
    uint8_t* m_buffer;
    size_t m_size;
    volatile size_t m_head;     // Write position
    volatile size_t m_tail;     // Read position
    volatile size_t m_count;    // Bytes in buffer

    SemaphoreHandle_t m_mutex;
    SemaphoreHandle_t m_dataAvailable;  // Signaled when data is written

    volatile uint32_t m_overflowCount;
};

#endif // RING_BUFFER_H
