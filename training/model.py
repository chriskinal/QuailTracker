# QuailTracker - GPS-synchronized Autonomous Recording Unit
# Copyright (C) 2026 QuailTracker Project
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""DS-CNN model architecture for BirdNET-STM32.

Depthwise Separable CNN designed for int8 quantization
and STM32Cube.AI deployment on STM32U575.
"""

import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers

from config import Config


def focal_loss(gamma=2.0, alpha=0.25, label_smoothing=0.05):
    """Binary focal loss with label smoothing.

    Focal loss down-weights easy examples so the model focuses on hard ones.
    Label smoothing handles noisy labels from energy-based sorting.
    """
    @tf.keras.utils.register_keras_serializable()
    def loss_fn(y_true, y_pred):
        # Apply label smoothing: 0 → smooth, 1 → 1-smooth
        y_true = y_true * (1 - label_smoothing) + 0.5 * label_smoothing

        # Clip predictions for numerical stability
        y_pred = tf.clip_by_value(y_pred, 1e-7, 1 - 1e-7)

        # Binary cross entropy per element
        bce = -(y_true * tf.math.log(y_pred) +
                (1 - y_true) * tf.math.log(1 - y_pred))

        # Focal modulation: (1-pt)^gamma
        p_t = y_true * y_pred + (1 - y_true) * (1 - y_pred)
        focal_weight = tf.pow(1 - p_t, gamma)

        # Alpha weighting
        alpha_t = y_true * alpha + (1 - y_true) * (1 - alpha)

        return tf.reduce_mean(alpha_t * focal_weight * bce)

    return loss_fn


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

    model.compile(
        optimizer=keras.optimizers.Adam(learning_rate=tc.learning_rate),
        loss=focal_loss(gamma=2.0, alpha=0.5, label_smoothing=0.05),
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
