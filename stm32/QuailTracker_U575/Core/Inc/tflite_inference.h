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

/* TensorFlow Lite Micro wrapper for QuailTracker on-device inference.
 *
 * C API wrapping the C++ MicroInterpreter.  Loads a .tflite model
 * from a caller-supplied buffer, runs int8 inference on mel spectrograms,
 * and returns per-class confidence scores.
 */

#ifndef TFLITE_INFERENCE_H
#define TFLITE_INFERENCE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of output classes the model can have */
#define TFLITE_MAX_CLASSES 16

/* Model info populated by tflite_init() */
typedef struct {
    int      input_size;         /* expected input tensor bytes */
    int      output_classes;     /* number of output classes */
    int      arena_used;         /* bytes of tensor arena actually used */
    float    input_scale;        /* input tensor quantization scale */
    int      input_zero_point;   /* input tensor quantization zero point */
    uint8_t  ready;              /* 1 if model loaded and interpreter allocated */
} tflite_info_t;

/* Inference result for one class */
typedef struct {
    uint8_t  class_index;
    int8_t   raw_score;          /* raw int8 output */
    float    confidence;         /* sigmoid(raw_score) mapped to 0.0-1.0 */
} tflite_result_t;

/* tflite_init() — Initialize interpreter with a .tflite model.
 *
 * modelBuf:    pointer to the TFLite flatbuffer (must be 16-byte aligned)
 * modelSize:   size of the model in bytes
 * arenaBuf:    pointer to tensor arena (must be 16-byte aligned)
 * arenaSize:   size of tensor arena in bytes
 *
 * Returns 0 on success, -1 on failure. */
int tflite_init(const uint8_t *modelBuf, size_t modelSize,
                uint8_t *arenaBuf, size_t arenaSize);

/* tflite_infer() — Run inference on a mel spectrogram.
 *
 * input:       pointer to int8 mel spectrogram (MEL_FRAMES * MEL_BINS)
 * inputSize:   total bytes (should match info.input_size)
 * results:     output array, filled with per-class results
 * maxResults:  size of results array
 *
 * Returns number of results written, or -1 on error. */
int tflite_infer(const int8_t *input, size_t inputSize,
                 tflite_result_t *results, int maxResults);

/* tflite_get_info() — Get model info after successful init. */
const tflite_info_t *tflite_get_info(void);

/* tflite_deinit() — Free interpreter resources (for model reload). */
void tflite_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* TFLITE_INFERENCE_H */
