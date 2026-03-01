/* Incremental mel spectrogram for on-device inference.
 *
 * Processes one hop (256 samples) of 24kHz audio at a time,
 * producing one mel frame per call.  Uses CMSIS-DSP arm_rfft_q15
 * for the 512-point FFT and a sparse mel filterbank exported
 * from the training pipeline.
 *
 * Memory: ~2 KB working buffers + 20 KB double-buffered output.
 */

#ifndef MEL_SPECTROGRAM_H
#define MEL_SPECTROGRAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Spectrogram dimensions (must match training config) */
#define MEL_HOP        256
#define MEL_FFT_SIZE   512
#define MEL_BINS       40
#define MEL_FRAMES     256   /* 3-second window at 24kHz / 256 hop */

/* mel_init() — call once at startup.
 * normMin/normMax from model_config.json (mel_min / mel_max). */
void mel_init(float normMin, float normMax);

/* mel_process_frame() — feed 256 decimated (24kHz) int16 samples.
 * Maintains 256-sample overlap internally.
 * Writes one mel frame (MEL_BINS int8 values) to the active buffer.
 * Returns the frame index written (0..MEL_FRAMES-1), or -1 if buffer full. */
int mel_process_frame(const int16_t *samples);

/* mel_get_buffer() — returns pointer to the completed spectrogram buffer.
 * The buffer contains MEL_FRAMES * MEL_BINS int8 values, row-major
 * (frame 0 first, MEL_BINS values per frame).
 * After calling this, the active buffer flips to the other half. */
const int8_t *mel_get_buffer(void);

/* mel_reset() — reset frame counter and overlap buffer.
 * Call when starting a new detection window. */
void mel_reset(void);

/* mel_get_frame_count() — returns number of frames written to active buffer. */
int mel_get_frame_count(void);

#ifdef __cplusplus
}
#endif

#endif /* MEL_SPECTROGRAM_H */
