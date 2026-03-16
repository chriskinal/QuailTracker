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

"""Configuration for BirdNET-STM32 training pipeline.

All mel spectrogram parameters must match between Python training
and C inference on STM32U575 (CMSIS-DSP arm_rfft_q15_512).
"""

from dataclasses import dataclass, field


@dataclass
class AudioConfig:
    sample_rate: int = 24000  # 2:1 decimation from 48kHz capture
    duration: float = 3.0  # seconds per clip
    n_fft: int = 512  # CMSIS-DSP arm_rfft_q15_512 compatible
    hop_length: int = 256
    n_mels: int = 40  # standard for embedded audio
    fmin: float = 500.0  # Hz — below bobwhite fundamental
    fmax: float = 10000.0  # Hz — above bobwhite harmonics
    # Derived: 3.0s * 24000 / 256 = 281.25 frames, truncated to 256
    n_frames: int = 256


@dataclass
class ModelConfig:
    # DS-CNN block filters (first is standard Conv2D)
    filters: list = field(default_factory=lambda: [32, 64, 128, 128])
    kernel_size: tuple = (3, 3)
    strides: tuple = (2, 2)
    dropout_rate: float = 0.2


@dataclass
class TrainingConfig:
    batch_size: int = 32
    epochs: int = 200
    learning_rate: float = 1e-3
    min_learning_rate: float = 1e-6
    val_split: float = 0.2
    early_stop_patience: int = 25
    early_stop_metric: str = "val_auc"
    random_seed: int = 42


@dataclass
class AugmentationConfig:
    enabled: bool = True
    time_shift_max: float = 0.3  # seconds
    gain_db_range: float = 6.0  # ±dB
    noise_snr_min: float = 10.0  # dB
    noise_snr_max: float = 30.0  # dB
    spec_freq_mask_max: int = 4  # mel bins
    spec_time_mask_max: int = 20  # frames
    mel_bin_shift_max: int = 2  # pitch shift approximation


@dataclass
class Config:
    audio: AudioConfig = field(default_factory=AudioConfig)
    model: ModelConfig = field(default_factory=ModelConfig)
    training: TrainingConfig = field(default_factory=TrainingConfig)
    augmentation: AugmentationConfig = field(default_factory=AugmentationConfig)
