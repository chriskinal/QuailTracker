#include "ring_buffer.h"
#include <string.h>
#include <Arduino.h>

RingBuffer::RingBuffer(size_t size)
    : m_size(size)
    , m_head(0)
    , m_tail(0)
    , m_count(0)
    , m_overflowCount(0)
    , m_buffer(nullptr)
    , m_mutex(nullptr)
    , m_dataAvailable(nullptr)
{
    m_buffer = (uint8_t*)malloc(size);
    if (!m_buffer) {
        Serial.printf("ERROR: Failed to allocate %d bytes for ring buffer!\n", size);
        return;
    }

    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) {
        Serial.println("ERROR: Failed to create ring buffer mutex!");
        free(m_buffer);
        m_buffer = nullptr;
        return;
    }

    m_dataAvailable = xSemaphoreCreateBinary();
    if (!m_dataAvailable) {
        Serial.println("ERROR: Failed to create ring buffer semaphore!");
        vSemaphoreDelete(m_mutex);
        free(m_buffer);
        m_buffer = nullptr;
        m_mutex = nullptr;
        return;
    }
}

RingBuffer::~RingBuffer()
{
    if (m_buffer) free(m_buffer);
    if (m_mutex) vSemaphoreDelete(m_mutex);
    if (m_dataAvailable) vSemaphoreDelete(m_dataAvailable);
}

size_t RingBuffer::write(const uint8_t* data, size_t len)
{
    if (len == 0 || !m_buffer || !m_mutex) return 0;

    xSemaphoreTake(m_mutex, portMAX_DELAY);

    size_t free = m_size - m_count;
    size_t toWrite = (len < free) ? len : free;

    if (toWrite < len) {
        m_overflowCount += (len - toWrite);
    }

    if (toWrite > 0) {
        // Write in up to two chunks (wrap around)
        size_t firstChunk = m_size - m_head;
        if (firstChunk > toWrite) {
            firstChunk = toWrite;
        }

        memcpy(m_buffer + m_head, data, firstChunk);

        if (toWrite > firstChunk) {
            memcpy(m_buffer, data + firstChunk, toWrite - firstChunk);
        }

        m_head = (m_head + toWrite) % m_size;
        m_count += toWrite;

        // Signal that data is available
        xSemaphoreGive(m_dataAvailable);
    }

    xSemaphoreGive(m_mutex);

    return toWrite;
}

size_t RingBuffer::read(uint8_t* data, size_t len, uint32_t timeout_ms)
{
    if (!m_buffer || !m_mutex) return 0;

    // Wait for data if buffer is empty
    if (m_count == 0) {
        TickType_t ticks = (timeout_ms == portMAX_DELAY)
            ? portMAX_DELAY
            : pdMS_TO_TICKS(timeout_ms);

        if (xSemaphoreTake(m_dataAvailable, ticks) != pdTRUE) {
            return 0;  // Timeout
        }
    }

    xSemaphoreTake(m_mutex, portMAX_DELAY);

    size_t toRead = (len < m_count) ? len : m_count;

    if (toRead > 0) {
        // Read in up to two chunks (wrap around)
        size_t firstChunk = m_size - m_tail;
        if (firstChunk > toRead) {
            firstChunk = toRead;
        }

        memcpy(data, m_buffer + m_tail, firstChunk);

        if (toRead > firstChunk) {
            memcpy(data + firstChunk, m_buffer, toRead - firstChunk);
        }

        m_tail = (m_tail + toRead) % m_size;
        m_count -= toRead;
    }

    // If there's still data, re-signal
    if (m_count > 0) {
        xSemaphoreGive(m_dataAvailable);
    }

    xSemaphoreGive(m_mutex);

    return toRead;
}

size_t RingBuffer::available() const
{
    return m_count;
}

size_t RingBuffer::freeSpace() const
{
    return m_size - m_count;
}

bool RingBuffer::isFull() const
{
    return m_count >= m_size;
}

bool RingBuffer::isEmpty() const
{
    return m_count == 0;
}

void RingBuffer::clear()
{
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_head = 0;
    m_tail = 0;
    m_count = 0;
    xSemaphoreGive(m_mutex);
}
