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

#endif /* FLAC_ENCODER_H */
