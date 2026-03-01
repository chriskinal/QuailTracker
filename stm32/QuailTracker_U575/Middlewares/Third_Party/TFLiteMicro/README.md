# TensorFlow Lite Micro for STM32U575

This directory should contain the TFLite Micro library source files.

## Setup

Generate the STM32-compatible source tree from the TFLite Micro repository:

```bash
# Clone TFLite Micro
git clone --depth 1 https://github.com/tensorflow/tflite-micro.git /tmp/tflite-micro

# macOS prerequisites (GNU make 3.82+ required, system make is 3.81)
brew install make wget

# Download third-party dependencies manually (avoids numpy/PIL issues on macOS)
cd /tmp/tflite-micro
DOWNLOADS=tensorflow/lite/micro/tools/make/downloads
bash tensorflow/lite/micro/tools/make/flatbuffers_download.sh $DOWNLOADS
bash tensorflow/lite/micro/tools/make/kissfft_download.sh $DOWNLOADS

# Download gemmlowp, ruy, eyalroz_printf (no dedicated scripts)
wget -qO /tmp/gemmlowp.zip "https://github.com/google/gemmlowp/archive/719139ce755a0f31cbf1c37f7f98adcc7fc9f425.zip"
unzip -q /tmp/gemmlowp.zip -d /tmp/gm && mv /tmp/gm/gemmlowp-* $DOWNLOADS/gemmlowp && rm -rf /tmp/gm /tmp/gemmlowp.zip

wget -qO /tmp/ruy.zip "https://github.com/google/ruy/archive/d37128311b445e758136b8602d1bbd2a755e115d.zip"
unzip -q /tmp/ruy.zip -d /tmp/ry && mv /tmp/ry/ruy-* $DOWNLOADS/ruy && rm -rf /tmp/ry /tmp/ruy.zip

wget -qO /tmp/printf.zip "https://github.com/eyalroz/printf/archive/f8ed5a9bd9fa8384430973465e94aa14c925872d.zip"
unzip -q /tmp/printf.zip -d /tmp/pf && mv /tmp/pf/printf-* $DOWNLOADS/eyalroz_printf && rm -rf /tmp/pf /tmp/printf.zip

# Generate the source tree for Cortex-M33
# Use PlatformIO's ARM GCC to skip the macOS-incompatible GCC download
PATH="/usr/local/opt/make/libexec/gnubin:$PATH" \
python3 tensorflow/lite/micro/tools/project_generation/create_tflm_tree.py \
  --makefile_options="TARGET=cortex_m_generic OPTIMIZED_KERNEL_DIR=cmsis_nn TARGET_ARCH=cortex-m33 TARGET_TOOLCHAIN_ROOT=$HOME/.platformio/packages/toolchain-gccarmnoneeabi/bin/" \
  --rename_cc_to_cpp \
  /path/to/QuailTracker/stm32/QuailTracker_U575/Middlewares/Third_Party/TFLiteMicro

# Clean up
rm -rf /tmp/tflite-micro
```

This will populate the directory with:
- `tensorflow/lite/micro/` — MicroInterpreter, op resolver, etc.
- `tensorflow/lite/micro/kernels/` — Op implementations (CMSIS-NN optimized)
- `third_party/cmsis_nn/` — CMSIS-NN kernel implementations
- `third_party/flatbuffers/` — FlatBuffer headers

## Required Ops

The QuailTracker DS-CNN model uses only 6 ops:
- Conv2D
- DepthwiseConv2D
- AveragePool2D
- FullyConnected
- Reshape
- Logistic (sigmoid)

These are registered in `Core/Src/tflite_inference.cc` via MicroMutableOpResolver<6>.

## Memory Budget

- Model buffer: 20 KB (loaded from SD card at runtime)
- Tensor arena: 48 KB (activations, intermediate tensors)
- Total: ~68 KB for inference
