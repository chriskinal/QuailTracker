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
#define FLAC_OUT_BUF_SIZE     12288  /* worst-case encoded frame */
#define FLAC_SAMPLE_RATE      48000
#define FLAC_BITS_PER_SAMPLE  16
#define FLAC_CHANNELS         1
#define FLAC_HEADER_SIZE      42     /* "fLaC" + STREAMINFO block */

typedef struct {
    /* PCM accumulation buffer */
    int16_t  blockBuf[FLAC_BLOCK_SIZE];

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
uint32_t flac_enc_process(flac_enc_t *e, const int16_t *pcm, uint32_t count);

/* Encode remaining partial block. Returns bytes in e->outBuf, or 0. */
uint32_t flac_enc_flush(flac_enc_t *e);

/* Write finalized STREAMINFO to out[42]. Seek to file offset 0 and write. */
void flac_enc_finalize_header(flac_enc_t *e, uint8_t *out);

#endif /* FLAC_ENCODER_H */
