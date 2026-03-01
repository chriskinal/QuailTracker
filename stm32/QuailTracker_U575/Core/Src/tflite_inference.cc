/* TensorFlow Lite Micro wrapper for QuailTracker.
 *
 * Wraps the C++ MicroInterpreter behind a C API so app_freertos.c
 * (compiled as C) can call tflite_init() / tflite_infer().
 *
 * Uses the CMSIS-NN optimized op resolver for Conv2D, DepthwiseConv2D,
 * AveragePool2D, FullyConnected, Reshape, and Softmax/Logistic.
 *
 * Prerequisites:
 *   - TFLite Micro source in Middlewares/Third_Party/TFLiteMicro/
 *   - CMSIS-NN kernels (included with CMSIS-DSP or TFLite Micro)
 */

#include "tflite_inference.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include <cmath>
#include <cstring>

/* ---- Static state ---- */

static const tflite::Model *s_model = nullptr;
static tflite::MicroInterpreter *s_interpreter = nullptr;
static tflite_info_t s_info;

/* Op resolver with only the ops our DS-CNN model needs.
 * 6 ops keeps the binary small vs. AllOpsResolver. */
static tflite::MicroMutableOpResolver<6> s_resolver;

/* Storage for the interpreter object (avoid dynamic alloc) */
static uint8_t s_interpBuf[sizeof(tflite::MicroInterpreter)] __attribute__((aligned(16)));


extern "C" int tflite_init(const uint8_t *modelBuf, size_t modelSize,
                            uint8_t *arenaBuf, size_t arenaSize)
{
    (void)modelSize;  /* FlatBuffer is self-describing */

    memset(&s_info, 0, sizeof(s_info));

    /* Verify FlatBuffer */
    s_model = tflite::GetModel(modelBuf);
    if (s_model->version() != TFLITE_SCHEMA_VERSION) {
        return -1;
    }

    /* Register only the ops our DS-CNN model uses */
    s_resolver = tflite::MicroMutableOpResolver<6>();
    s_resolver.AddConv2D();
    s_resolver.AddDepthwiseConv2D();
    s_resolver.AddAveragePool2D();
    s_resolver.AddFullyConnected();
    s_resolver.AddReshape();
    s_resolver.AddLogistic();  /* sigmoid output */

    /* Construct interpreter in pre-allocated buffer */
    s_interpreter = new (s_interpBuf) tflite::MicroInterpreter(
        s_model, s_resolver, arenaBuf, arenaSize);

    if (s_interpreter->AllocateTensors() != kTfLiteOk) {
        s_interpreter = nullptr;
        return -1;
    }

    /* Populate info */
    TfLiteTensor *input = s_interpreter->input(0);
    TfLiteTensor *output = s_interpreter->output(0);

    s_info.input_size = input->bytes;
    s_info.output_classes = output->dims->data[output->dims->size - 1];
    s_info.arena_used = s_interpreter->arena_used_bytes();
    s_info.ready = 1;

    return 0;
}

extern "C" int tflite_infer(const int8_t *input, size_t inputSize,
                             tflite_result_t *results, int maxResults)
{
    if (!s_info.ready || s_interpreter == nullptr)
        return -1;

    TfLiteTensor *inputTensor = s_interpreter->input(0);

    /* Verify size matches */
    if (inputSize != inputTensor->bytes)
        return -1;

    /* Copy input data */
    memcpy(inputTensor->data.int8, input, inputSize);

    /* Run inference */
    if (s_interpreter->Invoke() != kTfLiteOk)
        return -1;

    /* Extract results */
    TfLiteTensor *outputTensor = s_interpreter->output(0);
    int nClasses = s_info.output_classes;
    int nResults = (nClasses < maxResults) ? nClasses : maxResults;

    for (int i = 0; i < nResults; i++) {
        int8_t raw = outputTensor->data.int8[i];
        results[i].class_index = (uint8_t)i;
        results[i].raw_score = raw;

        /* Dequantize: real = (raw - zero_point) * scale */
        float real = ((float)raw - outputTensor->params.zero_point) *
                     outputTensor->params.scale;

        /* Sigmoid activation (model outputs logits if Logistic op is fused,
         * but apply sigmoid defensively for robustness) */
        results[i].confidence = 1.0f / (1.0f + expf(-real));
    }

    return nResults;
}

extern "C" const tflite_info_t *tflite_get_info(void)
{
    return &s_info;
}

extern "C" void tflite_deinit(void)
{
    if (s_interpreter != nullptr) {
        s_interpreter->~MicroInterpreter();
        s_interpreter = nullptr;
    }
    s_model = nullptr;
    memset(&s_info, 0, sizeof(s_info));
}
