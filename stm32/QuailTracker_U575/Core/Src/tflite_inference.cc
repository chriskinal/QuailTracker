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
#include <cstdio>

#include "tensorflow/lite/micro/cortex_m_generic/debug_log_callback.h"

static void tflite_debug_log(const char *s) { printf("%s", s); }

/* ---- Static state ---- */

static const tflite::Model *s_model = nullptr;
static tflite::MicroInterpreter *s_interpreter = nullptr;
static tflite_info_t s_info;

/* Op resolver with only the ops our DS-CNN model needs.
 * 6 ops keeps the binary small vs. AllOpsResolver. */
static tflite::MicroMutableOpResolver<7> s_resolver;

/* Storage for the interpreter object (avoid dynamic alloc) */
static uint8_t s_interpBuf[sizeof(tflite::MicroInterpreter)] __attribute__((aligned(16)));


extern "C" int tflite_init(const uint8_t *modelBuf, size_t modelSize,
                            uint8_t *arenaBuf, size_t arenaSize)
{
    (void)modelSize;  /* FlatBuffer is self-describing */

    memset(&s_info, 0, sizeof(s_info));

    RegisterDebugLogCallback(tflite_debug_log);

    /* Verify FlatBuffer */
    s_model = tflite::GetModel(modelBuf);
    printf("TFLite: model schema=%lu, expected=%d\r\n",
           (unsigned long)s_model->version(), TFLITE_SCHEMA_VERSION);
    if (s_model->version() != TFLITE_SCHEMA_VERSION) {
        printf("TFLite: schema version MISMATCH\r\n");
        return -1;
    }

    /* Register only the ops our DS-CNN model uses */
    s_resolver = tflite::MicroMutableOpResolver<7>();
    s_resolver.AddConv2D();
    s_resolver.AddDepthwiseConv2D();
    s_resolver.AddMean();              /* GlobalAveragePooling2D → MEAN op */
    s_resolver.AddFullyConnected();
    s_resolver.AddReshape();
    s_resolver.AddLogistic();          /* sigmoid output */
    s_resolver.AddQuantize();          /* int8 requantization between layers */

    /* Construct interpreter in pre-allocated buffer */
    s_interpreter = new (s_interpBuf) tflite::MicroInterpreter(
        s_model, s_resolver, arenaBuf, arenaSize);

    printf("TFLite: AllocateTensors (arena=%u bytes)...\r\n", (unsigned)arenaSize);
    TfLiteStatus alloc_status = s_interpreter->AllocateTensors();
    if (alloc_status != kTfLiteOk) {
        printf("TFLite: AllocateTensors FAILED (status=%d)\r\n", (int)alloc_status);
        s_interpreter = nullptr;
        return -1;
    }
    printf("TFLite: OK, arena used=%u bytes\r\n",
           (unsigned)s_interpreter->arena_used_bytes());

    /* Populate info */
    TfLiteTensor *input = s_interpreter->input(0);
    TfLiteTensor *output = s_interpreter->output(0);

    s_info.input_size = input->bytes;
    s_info.output_classes = output->dims->data[output->dims->size - 1];
    s_info.arena_used = s_interpreter->arena_used_bytes();
    s_info.input_scale = input->params.scale;
    s_info.input_zero_point = input->params.zero_point;
    s_info.ready = 1;

    /* Print input/output tensor quantization for debugging */
    printf("TFLite: input dims=[");
    for (int i = 0; i < input->dims->size; i++)
        printf("%d%s", input->dims->data[i], i < input->dims->size - 1 ? "," : "");
    printf("] bytes=%d scale=%.6f zp=%d\r\n",
           (int)input->bytes, (double)input->params.scale,
           input->params.zero_point);
    printf("TFLite: output dims=[");
    for (int i = 0; i < output->dims->size; i++)
        printf("%d%s", output->dims->data[i], i < output->dims->size - 1 ? "," : "");
    printf("] scale=%.6f zp=%d\r\n",
           (double)output->params.scale, output->params.zero_point);

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

        /* The model already has a Logistic (sigmoid) output layer, so
         * the dequantized value is already a probability in [0,1].
         * Do NOT apply sigmoid again (double-sigmoid was mapping
         * noise at ~0.53 → sigmoid(0.53) ≈ 0.63 false positives). */
        if (real < 0.0f) real = 0.0f;
        if (real > 1.0f) real = 1.0f;
        results[i].confidence = real;
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
