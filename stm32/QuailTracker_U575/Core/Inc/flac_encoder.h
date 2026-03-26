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

/**
 * Minimal FLAC encoder for QuailTracker STM32U575
 *
 * Fixed predictors only (order 0-4), Rice coding, partition order 0.
 * Integer-only, no dynamic allocation. Hardcoded for 48kHz 16-bit mono.
 */
#ifndef FLAC_ENCODER_H
#define FLAC_ENCODER_H

#include <stdint.h>

#define FLAC_BLOCK_SIZE       4096
#define FLAC_OUT_BUF_SIZE     16384  /* worst-case encoded frame (verbatim 24-bit: 4096×3 + overhead) */
#define FLAC_SAMPLE_RATE      48000
#define FLAC_BITS_PER_SAMPLE  24
#define FLAC_CHANNELS         1
#define FLAC_HEADER_SIZE      42     /* "fLaC" + STREAMINFO block */

/* SEEKTABLE: one seek point every 10 seconds.
 * For 15-minute chunks: 90 points × 18 bytes = 1620 bytes + 4 byte header. */
#define FLAC_SEEK_INTERVAL_SAMPLES  (FLAC_SAMPLE_RATE * 10)  /* 10 seconds */
#define FLAC_MAX_SEEK_POINTS        100  /* supports up to ~16.7 min chunks */
#define FLAC_SEEKTABLE_DATA_SIZE    (FLAC_MAX_SEEK_POINTS * 18)
#define FLAC_SEEKTABLE_BLOCK_SIZE   (4 + FLAC_SEEKTABLE_DATA_SIZE)  /* header + data */

typedef struct {
    uint64_t sampleNumber;
    uint64_t byteOffset;    /* from first audio frame */
    uint16_t frameSamples;
} flac_seekpoint_t;

typedef struct {
    /* PCM accumulation buffer */
    int32_t  blockBuf[FLAC_BLOCK_SIZE];

    /* Encoded frame output buffer */
    uint8_t  outBuf[FLAC_OUT_BUF_SIZE];

    /* Accumulation state */
    uint32_t blockPos;

    /* Stream state */
    uint32_t frameNumber;
    uint64_t totalSamples;
    uint32_t minFrameSize;
    uint32_t maxFrameSize;

    /* Seek table (filled during encoding) */
    flac_seekpoint_t seekPoints[FLAC_MAX_SEEK_POINTS];
    uint32_t seekPointCount;
    uint64_t nextSeekSample;   /* next sample number that triggers a seek point */
    uint64_t audioStartOffset; /* file byte offset where audio frames begin */
    uint64_t currentFileOffset;/* running byte offset for seek point tracking */
} flac_enc_t;

/* Initialize encoder state. Call before starting a new file. */
void flac_enc_init(flac_enc_t *e);

/* Write "fLaC" + placeholder STREAMINFO to out[42]. Returns FLAC_HEADER_SIZE. */
uint32_t flac_enc_write_header(flac_enc_t *e, uint8_t *out);

/* Feed PCM samples. Returns encoded bytes in e->outBuf when a block is
 * complete (caller must write to file), or 0 if still accumulating. */
uint32_t flac_enc_process(flac_enc_t *e, const int32_t *pcm, uint32_t count);

/* Encode remaining partial block. Returns bytes in e->outBuf, or 0. */
uint32_t flac_enc_flush(flac_enc_t *e);

/* Write finalized STREAMINFO to out[42]. Seek to file offset 0 and write. */
void flac_enc_finalize_header(flac_enc_t *e, uint8_t *out);

/* Write placeholder SEEKTABLE metadata block. Returns bytes written. */
uint32_t flac_enc_write_seektable_placeholder(flac_enc_t *e, uint8_t *out);

/* Write finalized SEEKTABLE metadata block with real offsets. */
uint32_t flac_enc_finalize_seektable(flac_enc_t *e, uint8_t *out);

/* Notify encoder of bytes written to file (for seek point byte offsets). */
void flac_enc_notify_write(flac_enc_t *e, uint32_t bytesWritten);

/* Set the file offset where audio frames begin (after all metadata blocks). */
void flac_enc_set_audio_start(flac_enc_t *e, uint64_t offset);

#endif /* FLAC_ENCODER_H */
