"""DS-CNN model architecture for BirdNET-STM32.

Depthwise Separable CNN designed for int8 quantization
and STM32Cube.AI deployment on STM32U575.
"""

import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers

from config import Config


def _ds_conv_block(x, filters, kernel_size, strides):
    """Depthwise separable convolution block with BN + ReLU6."""
    x = layers.DepthwiseConv2D(
        kernel_size=kernel_size,
        strides=strides,
        padding="same",
        use_bias=False,
    )(x)
    x = layers.BatchNormalization()(x)
    x = layers.ReLU(max_value=6.0)(x)

    x = layers.Conv2D(
        filters,
        kernel_size=(1, 1),
        padding="same",
        use_bias=False,
    )(x)
    x = layers.BatchNormalization()(x)
    x = layers.ReLU(max_value=6.0)(x)

    return x


def build_model(config, num_classes):
    """Build and compile DS-CNN model.

    Architecture:
        Conv2D(16) → DS-Conv(32) → DS-Conv(48) → DS-Conv(64)
        → GlobalAveragePooling → Dropout → Dense(sigmoid)

    Args:
        config: Config instance.
        num_classes: Number of output classes.

    Returns:
        Compiled Keras Model.
    """
    mc = config.model
    ac = config.audio
    tc = config.training

    inputs = keras.Input(shape=(ac.n_frames, ac.n_mels, 1))

    # First block: standard Conv2D (depthwise has no benefit when input channels=1)
    x = layers.Conv2D(
        mc.filters[0],
        kernel_size=mc.kernel_size,
        strides=mc.strides,
        padding="same",
        use_bias=False,
    )(inputs)
    x = layers.BatchNormalization()(x)
    x = layers.ReLU(max_value=6.0)(x)

    # DS-Conv blocks
    for filters in mc.filters[1:]:
        x = _ds_conv_block(x, filters, mc.kernel_size, mc.strides)

    x = layers.GlobalAveragePooling2D()(x)
    x = layers.Dropout(mc.dropout_rate)(x)

    # Sigmoid output (multi-label, matching BirdNET's approach)
    outputs = layers.Dense(num_classes, activation="sigmoid")(x)

    model = keras.Model(inputs=inputs, outputs=outputs)

    # Cosine decay LR schedule
    total_steps = tc.epochs  # Updated per-epoch in callbacks
    lr_schedule = keras.optimizers.schedules.CosineDecay(
        initial_learning_rate=tc.learning_rate,
        decay_steps=total_steps,
        alpha=tc.min_learning_rate,
    )

    model.compile(
        optimizer=keras.optimizers.Adam(learning_rate=lr_schedule),
        loss="binary_crossentropy",
        metrics=[
            "accuracy",
            keras.metrics.AUC(name="auc", multi_label=True),
        ],
    )

    return model


def print_model_summary(model):
    """Print model summary with estimated flash/RAM sizes."""
    model.summary()

    # Rough size estimates
    total_params = model.count_params()
    int8_size_kb = total_params / 1024  # 1 byte per param in int8
    print(f"\nEstimated int8 model size: ~{int8_size_kb:.1f} KB")
    print(f"Total parameters: {total_params:,}")
