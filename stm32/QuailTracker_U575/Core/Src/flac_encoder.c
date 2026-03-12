/**
 * Minimal FLAC encoder — fixed predictors + Rice coding
 * Self-contained, integer-only, no dynamic allocation.
 */
#include "flac_encoder.h"
#include <string.h>

/* ================================================================
 * CRC Tables (from FLAC reference — poly 0x07 / 0x8005, MSB-first)
 * ================================================================ */

static const uint8_t crc8_table[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
};

static const uint16_t crc16_table[256] = {
    0x0000, 0x8005, 0x800F, 0x000A, 0x801B, 0x001E, 0x0014, 0x8011,
    0x8033, 0x0036, 0x003C, 0x8039, 0x0028, 0x802D, 0x8027, 0x0022,
    0x8063, 0x0066, 0x006C, 0x8069, 0x0078, 0x807D, 0x8077, 0x0072,
    0x0050, 0x8055, 0x805F, 0x005A, 0x804B, 0x004E, 0x0044, 0x8041,
    0x80C3, 0x00C6, 0x00CC, 0x80C9, 0x00D8, 0x80DD, 0x80D7, 0x00D2,
    0x00F0, 0x80F5, 0x80FF, 0x00FA, 0x80EB, 0x00EE, 0x00E4, 0x80E1,
    0x00A0, 0x80A5, 0x80AF, 0x00AA, 0x80BB, 0x00BE, 0x00B4, 0x80B1,
    0x8093, 0x0096, 0x009C, 0x8099, 0x0088, 0x808D, 0x8087, 0x0082,
    0x8183, 0x0186, 0x018C, 0x8189, 0x0198, 0x819D, 0x8197, 0x0192,
    0x01B0, 0x81B5, 0x81BF, 0x01BA, 0x81AB, 0x01AE, 0x01A4, 0x81A1,
    0x01E0, 0x81E5, 0x81EF, 0x01EA, 0x81FB, 0x01FE, 0x01F4, 0x81F1,
    0x81D3, 0x01D6, 0x01DC, 0x81D9, 0x01C8, 0x81CD, 0x81C7, 0x01C2,
    0x0140, 0x8145, 0x814F, 0x014A, 0x815B, 0x015E, 0x0154, 0x8151,
    0x8173, 0x0176, 0x017C, 0x8179, 0x0168, 0x816D, 0x8167, 0x0162,
    0x8123, 0x0126, 0x012C, 0x8129, 0x0138, 0x813D, 0x8137, 0x0132,
    0x0110, 0x8115, 0x811F, 0x011A, 0x810B, 0x010E, 0x0104, 0x8101,
    0x8303, 0x0306, 0x030C, 0x8309, 0x0318, 0x831D, 0x8317, 0x0312,
    0x0330, 0x8335, 0x833F, 0x033A, 0x832B, 0x032E, 0x0324, 0x8321,
    0x0360, 0x8365, 0x836F, 0x036A, 0x837B, 0x037E, 0x0374, 0x8371,
    0x8353, 0x0356, 0x035C, 0x8359, 0x0348, 0x834D, 0x8347, 0x0342,
    0x03C0, 0x83C5, 0x83CF, 0x03CA, 0x83DB, 0x03DE, 0x03D4, 0x83D1,
    0x83F3, 0x03F6, 0x03FC, 0x83F9, 0x03E8, 0x83ED, 0x83E7, 0x03E2,
    0x83A3, 0x03A6, 0x03AC, 0x83A9, 0x03B8, 0x83BD, 0x83B7, 0x03B2,
    0x0390, 0x8395, 0x839F, 0x039A, 0x838B, 0x038E, 0x0384, 0x8381,
    0x0280, 0x8285, 0x828F, 0x028A, 0x829B, 0x029E, 0x0294, 0x8291,
    0x82B3, 0x02B6, 0x02BC, 0x82B9, 0x02A8, 0x82AD, 0x82A7, 0x02A2,
    0x82E3, 0x02E6, 0x02EC, 0x82E9, 0x02F8, 0x82FD, 0x82F7, 0x02F2,
    0x02D0, 0x82D5, 0x82DF, 0x02DA, 0x82CB, 0x02CE, 0x02C4, 0x82C1,
    0x8243, 0x0246, 0x024C, 0x8249, 0x0258, 0x825D, 0x8257, 0x0252,
    0x0270, 0x8275, 0x827F, 0x027A, 0x826B, 0x026E, 0x0264, 0x8261,
    0x0220, 0x8225, 0x822F, 0x022A, 0x823B, 0x023E, 0x0234, 0x8231,
    0x8213, 0x0216, 0x021C, 0x8219, 0x0208, 0x820D, 0x8207, 0x0202
};

/* ================================================================
 * Bit Writer — MSB-first, accumulates into byte buffer
 * ================================================================ */

typedef struct {
    uint8_t  *buf;
    uint32_t  pos;     /* byte position in output */
    uint32_t  limit;   /* max bytes (overflow protection) */
    uint32_t  bits;    /* pending bit accumulator */
    uint8_t   nbits;   /* pending bit count (0-7) */
    uint8_t   overflow;/* set if pos exceeded limit */
} bitwriter_t;

static void bw_init(bitwriter_t *bw, uint8_t *buf, uint32_t limit)
{
    bw->buf = buf;
    bw->pos = 0;
    bw->limit = limit;
    bw->bits = 0;
    bw->nbits = 0;
    bw->overflow = 0;
}

/* Write 1-25 bits (MSB-first). nbits must be < 8 on entry. */
static void bw_write_bits(bitwriter_t *bw, uint32_t n, uint32_t val)
{
    if (bw->overflow) return;
    bw->bits = (bw->bits << n) | (val & ((1u << n) - 1));
    bw->nbits += (uint8_t)n;
    while (bw->nbits >= 8) {
        bw->nbits -= 8;
        if (bw->pos >= bw->limit) { bw->overflow = 1; return; }
        bw->buf[bw->pos++] = (uint8_t)(bw->bits >> bw->nbits);
    }
}

/* Write val zeros followed by a 1-bit (unary code) */
static void bw_write_unary(bitwriter_t *bw, uint32_t val)
{
    while (val >= 24) {
        bw_write_bits(bw, 24, 0);
        val -= 24;
    }
    if (val > 0)
        bw_write_bits(bw, val, 0);
    bw_write_bits(bw, 1, 1);
}

/* Rice-encode a signed residual */
static void bw_write_rice_signed(bitwriter_t *bw, uint32_t k, int32_t val)
{
    /* Zigzag: non-negative x -> 2x, negative x -> -2x-1 */
    uint32_t u = (val >= 0) ? ((uint32_t)val << 1)
                            : (((uint32_t)(-val) << 1) - 1);
    uint32_t q = u >> k;
    bw_write_unary(bw, q);
    if (k > 0)
        bw_write_bits(bw, k, u & ((1u << k) - 1));
}

/* FLAC-style UTF-8 encoding of frame number (up to 2^21-1) */
static void bw_write_utf8_uint32(bitwriter_t *bw, uint32_t val)
{
    if (val < 0x80) {
        bw_write_bits(bw, 8, val);
    } else if (val < 0x800) {
        bw_write_bits(bw, 8, 0xC0 | (val >> 6));
        bw_write_bits(bw, 8, 0x80 | (val & 0x3F));
    } else if (val < 0x10000) {
        bw_write_bits(bw, 8, 0xE0 | (val >> 12));
        bw_write_bits(bw, 8, 0x80 | ((val >> 6) & 0x3F));
        bw_write_bits(bw, 8, 0x80 | (val & 0x3F));
    } else {
        bw_write_bits(bw, 8, 0xF0 | (val >> 18));
        bw_write_bits(bw, 8, 0x80 | ((val >> 12) & 0x3F));
        bw_write_bits(bw, 8, 0x80 | ((val >> 6) & 0x3F));
        bw_write_bits(bw, 8, 0x80 | (val & 0x3F));
    }
}

/* Pad to next byte boundary with zero bits */
static void bw_align_byte(bitwriter_t *bw)
{
    if (bw->nbits > 0)
        bw_write_bits(bw, 8 - bw->nbits, 0);
}

/* ================================================================
 * Fixed Predictor & Rice Parameter
 * ================================================================ */

static int32_t compute_residual(const int32_t *s, uint32_t i, uint32_t order)
{
    switch (order) {
    case 0: return s[i];
    case 1: return s[i] - s[i-1];
    case 2: return s[i] - 2*s[i-1] + s[i-2];
    case 3: return s[i] - 3*s[i-1] + 3*s[i-2] - s[i-3];
    case 4: return s[i] - 4*s[i-1] + 6*s[i-2] - 4*s[i-3] + s[i-4];
    default: return s[i];
    }
}

/* Sum of zigzag-folded residuals — proxy for coding cost */
static uint64_t compute_fixed_cost(const int32_t *s, uint32_t n, uint32_t order)
{
    uint64_t sum = 0;
    for (uint32_t i = order; i < n; i++) {
        int32_t r = compute_residual(s, i, order);
        uint32_t u = (r >= 0) ? ((uint32_t)r << 1) : (((uint32_t)(-r) << 1) - 1);
        sum += u;
    }
    return sum;
}

/* Estimate Rice parameter from mean of folded residuals */
static uint32_t estimate_rice_param(uint64_t cost, uint32_t count)
{
    if (count == 0 || cost == 0)
        return 0;
    uint32_t mean = (uint32_t)(cost / count);
    if (mean == 0)
        return 0;
    uint32_t k = 31 - (uint32_t)__builtin_clz(mean);
    return (k > 14) ? 14 : k;
}

/* ================================================================
 * Frame Encoding
 * ================================================================ */

static void encode_frame_header(bitwriter_t *bw, uint32_t frameNumber,
                                uint32_t blockSize, uint32_t *crc8PosOut)
{
    /* Sync code (14b) + reserved (1b) + blocking strategy fixed (1b) = 0xFFF8 */
    bw_write_bits(bw, 16, 0xFFF8);

    /* Block size code (4 bits) */
    uint8_t bsCode;
    if (blockSize == 4096)
        bsCode = 12;   /* 256 * 2^(12-8) = 4096 */
    else
        bsCode = 7;    /* 16-bit blocksize-1 follows */
    bw_write_bits(bw, 4, bsCode);

    /* Sample rate code (4 bits): 0x0A = 48000 Hz */
    bw_write_bits(bw, 4, 0x0A);

    /* Channel assignment (4 bits): 0 = mono */
    bw_write_bits(bw, 4, 0);

    /* Sample size (3 bits): 6 = 24-bit */
    bw_write_bits(bw, 3, 6);

    /* Reserved (1 bit) */
    bw_write_bits(bw, 1, 0);

    /* Frame number (UTF-8 coded) */
    bw_write_utf8_uint32(bw, frameNumber);

    /* Extra block size bytes for non-standard sizes */
    if (bsCode == 7)
        bw_write_bits(bw, 16, blockSize - 1);

    /* CRC-8 placeholder — remember position, patch later */
    *crc8PosOut = bw->pos;
    bw_write_bits(bw, 8, 0);
}

static uint32_t encode_frame(flac_enc_t *e, uint32_t blockSize)
{
    bitwriter_t bw;
    bw_init(&bw, e->outBuf, FLAC_OUT_BUF_SIZE);

    /* --- Select best predictor --- */
    uint32_t bestOrder = 0;
    uint64_t bestCost = UINT64_MAX;
    for (uint32_t ord = 0; ord <= 4 && ord <= blockSize; ord++) {
        uint64_t cost = compute_fixed_cost(e->blockBuf, blockSize, ord);
        if (cost < bestCost) {
            bestCost = cost;
            bestOrder = ord;
        }
    }

    uint32_t residualCount = blockSize - bestOrder;
    uint32_t k = estimate_rice_param(bestCost, residualCount);

    /* Check if verbatim would be more compact.
     * Approximate Rice bits: each residual averages ~(2 + k) bits when k fits. */
    uint64_t estRiceBits = (uint64_t)residualCount * (2 + k)
                         + (uint64_t)bestOrder * FLAC_BITS_PER_SAMPLE + 20;
    uint64_t verbBits = (uint64_t)blockSize * FLAC_BITS_PER_SAMPLE + 8;
    int useVerbatim = (estRiceBits >= verbBits);

    /* --- Frame header --- */
    uint32_t crc8Pos;
    encode_frame_header(&bw, e->frameNumber, blockSize, &crc8Pos);

    /* --- Subframe --- */

    if (useVerbatim) {
        /* Subframe header: pad(1) + type VERBATIM=000001(6) + wasted=0(1) */
        bw_write_bits(&bw, 1, 0);
        bw_write_bits(&bw, 6, 1);     /* VERBATIM */
        bw_write_bits(&bw, 1, 0);

        for (uint32_t i = 0; i < blockSize; i++)
            bw_write_bits(&bw, 24, (uint32_t)(e->blockBuf[i] & 0xFFFFFF));
    } else {
        /* Subframe header: pad(1) + type FIXED order N = 001|N(6) + wasted=0(1) */
        bw_write_bits(&bw, 1, 0);
        bw_write_bits(&bw, 6, 0x08 | bestOrder);  /* FIXED: 001xxx */
        bw_write_bits(&bw, 1, 0);

        /* Warm-up samples (unencoded, 24 bits each) */
        for (uint32_t i = 0; i < bestOrder; i++)
            bw_write_bits(&bw, 24, (uint32_t)(e->blockBuf[i] & 0xFFFFFF));

        /* Residual coding: method 0 (4-bit Rice params), partition order 0 */
        bw_write_bits(&bw, 2, 0);     /* coding method */
        bw_write_bits(&bw, 4, 0);     /* partition order */
        bw_write_bits(&bw, 4, k);     /* Rice parameter */

        /* Rice-encode residuals */
        for (uint32_t i = bestOrder; i < blockSize; i++) {
            int32_t r = compute_residual(e->blockBuf, i, bestOrder);
            bw_write_rice_signed(&bw, k, r);
        }

        /* If Rice coding overflowed the output buffer, re-encode as verbatim.
         * Verbatim is bounded: blockSize*3 + ~20 bytes overhead < 16KB. */
        if (bw.overflow) {
            bw_init(&bw, e->outBuf, FLAC_OUT_BUF_SIZE);
            encode_frame_header(&bw, e->frameNumber, blockSize, &crc8Pos);

            bw_write_bits(&bw, 1, 0);
            bw_write_bits(&bw, 6, 1);     /* VERBATIM */
            bw_write_bits(&bw, 1, 0);

            for (uint32_t i = 0; i < blockSize; i++)
                bw_write_bits(&bw, 24, (uint32_t)(e->blockBuf[i] & 0xFFFFFF));
        }
    }

    /* Byte-align */
    bw_align_byte(&bw);

    /* --- Compute and patch CRC-8 over frame header --- */
    {
        uint8_t crc = 0;
        for (uint32_t i = 0; i < crc8Pos; i++)
            crc = crc8_table[crc ^ e->outBuf[i]];
        e->outBuf[crc8Pos] = crc;
    }

    /* --- Frame footer: CRC-16 over entire frame --- */
    uint32_t crc16Pos = bw.pos;
    bw_write_bits(&bw, 16, 0);  /* placeholder */

    {
        uint16_t crc = 0;
        for (uint32_t i = 0; i < crc16Pos; i++)
            crc = (uint16_t)((crc << 8) ^ crc16_table[(crc >> 8) ^ e->outBuf[i]]);
        e->outBuf[crc16Pos]     = (uint8_t)(crc >> 8);
        e->outBuf[crc16Pos + 1] = (uint8_t)(crc & 0xFF);
    }

    uint32_t frameSize = bw.pos;

    /* Update min/max frame size for STREAMINFO */
    if (frameSize < e->minFrameSize || e->minFrameSize == 0)
        e->minFrameSize = frameSize;
    if (frameSize > e->maxFrameSize)
        e->maxFrameSize = frameSize;

    e->frameNumber++;
    e->totalSamples += blockSize;
    e->blockPos = 0;

    return frameSize;
}

/* ================================================================
 * STREAMINFO helpers
 * ================================================================ */

/* Pack "fLaC" + STREAMINFO metadata block into out[42] */
static void write_streaminfo(uint8_t *out,
                             uint16_t minBlock, uint16_t maxBlock,
                             uint32_t minFrame, uint32_t maxFrame,
                             uint64_t totalSamples)
{
    /* "fLaC" magic */
    out[0] = 'f'; out[1] = 'L'; out[2] = 'a'; out[3] = 'C';

    /* Metadata block header: last=1, type=0 (STREAMINFO), length=34 */
    out[4] = 0x80;
    out[5] = 0x00;
    out[6] = 0x00;
    out[7] = 34;

    /* Min/max block size (16-bit big-endian each) */
    out[8]  = (uint8_t)(minBlock >> 8);
    out[9]  = (uint8_t)(minBlock);
    out[10] = (uint8_t)(maxBlock >> 8);
    out[11] = (uint8_t)(maxBlock);

    /* Min/max frame size (24-bit big-endian each) */
    out[12] = (uint8_t)(minFrame >> 16);
    out[13] = (uint8_t)(minFrame >> 8);
    out[14] = (uint8_t)(minFrame);
    out[15] = (uint8_t)(maxFrame >> 16);
    out[16] = (uint8_t)(maxFrame >> 8);
    out[17] = (uint8_t)(maxFrame);

    /* Packed: sample_rate(20) | channels-1(3) | bps-1(5) | total_samples(36)
     * 48000=0x0BB80, ch-1=0, bps-1=23=0x17 */
    out[18] = 0x0B;                                       /* sr[19:12] */
    out[19] = 0xB8;                                       /* sr[11:4]  */
    out[20] = 0x01;                                       /* sr[3:0]=0 | ch[2:0]=0 | bps-1[4]=1 */
    out[21] = (uint8_t)(0x70 | ((totalSamples >> 32) & 0x0F)); /* bps[3:0]=7 | ts[35:32] */
    out[22] = (uint8_t)(totalSamples >> 24);
    out[23] = (uint8_t)(totalSamples >> 16);
    out[24] = (uint8_t)(totalSamples >> 8);
    out[25] = (uint8_t)(totalSamples);

    /* MD5 signature — all zeros (optional per spec) */
    memset(&out[26], 0, 16);
}

/* ================================================================
 * Public API
 * ================================================================ */

void flac_enc_init(flac_enc_t *e)
{
    e->blockPos = 0;
    e->frameNumber = 0;
    e->totalSamples = 0;
    e->minFrameSize = 0;
    e->maxFrameSize = 0;
}

uint32_t flac_enc_write_header(flac_enc_t *e, uint8_t *out)
{
    (void)e;
    write_streaminfo(out, FLAC_BLOCK_SIZE, FLAC_BLOCK_SIZE, 0, 0, 0);
    return FLAC_HEADER_SIZE;
}

uint32_t flac_enc_process(flac_enc_t *e, const int32_t *pcm, uint32_t count)
{
    memcpy(&e->blockBuf[e->blockPos], pcm, count * sizeof(int32_t));
    e->blockPos += count;

    if (e->blockPos >= FLAC_BLOCK_SIZE)
        return encode_frame(e, FLAC_BLOCK_SIZE);

    return 0;
}

uint32_t flac_enc_flush(flac_enc_t *e)
{
    if (e->blockPos == 0)
        return 0;
    return encode_frame(e, e->blockPos);
}

void flac_enc_finalize_header(flac_enc_t *e, uint8_t *out)
{
    /* FLAC spec: for fixed-blocksize streams, min_block == max_block
     * even if the last block is shorter. */
    uint16_t blockSize = FLAC_BLOCK_SIZE;

    /* Single short recording (< 1 block): use actual sample count */
    if (e->totalSamples > 0 && e->totalSamples < FLAC_BLOCK_SIZE)
        blockSize = (uint16_t)e->totalSamples;

    write_streaminfo(out, blockSize, blockSize,
                     e->minFrameSize, e->maxFrameSize,
                     e->totalSamples);
}
