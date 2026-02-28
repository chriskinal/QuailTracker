"""Dataset loading, mel spectrogram computation, and augmentation."""

import os
import numpy as np
import librosa
import soundfile as sf
from sklearn.model_selection import train_test_split

from config import Config


def load_labels(export_dir):
    """Read class labels from labels.txt, add 'noise' if directory exists."""
    labels_file = os.path.join(export_dir, "labels.txt")
    if os.path.exists(labels_file):
        with open(labels_file, "r") as f:
            labels = [line.strip() for line in f if line.strip()]
    else:
        # Infer from subdirectory names
        labels = sorted([
            d for d in os.listdir(export_dir)
            if os.path.isdir(os.path.join(export_dir, d))
        ])

    # Add noise class if directory exists but not in labels
    noise_dir = os.path.join(export_dir, "noise")
    if os.path.isdir(noise_dir) and "noise" not in labels:
        labels.append("noise")

    return labels


def compute_mel_spectrogram(audio, config):
    """Compute log-mel spectrogram, pad/truncate to (n_frames, n_mels).

    Parameters must match STM32 C inference exactly.
    """
    ac = config.audio

    mel = librosa.feature.melspectrogram(
        y=audio,
        sr=ac.sample_rate,
        n_fft=ac.n_fft,
        hop_length=ac.hop_length,
        n_mels=ac.n_mels,
        fmin=ac.fmin,
        fmax=ac.fmax,
    )

    # Log scale (add small epsilon to avoid log(0))
    log_mel = np.log(mel + 1e-9)

    # Transpose to (time, mels)
    log_mel = log_mel.T

    # Pad or truncate to exactly n_frames
    if log_mel.shape[0] < ac.n_frames:
        pad_width = ac.n_frames - log_mel.shape[0]
        log_mel = np.pad(log_mel, ((0, pad_width), (0, 0)), mode="constant")
    elif log_mel.shape[0] > ac.n_frames:
        log_mel = log_mel[:ac.n_frames]

    return log_mel


def load_dataset(export_dir, config):
    """Load all WAV clips and compute mel spectrograms.

    Args:
        export_dir: Path containing <species>/ subdirectories with WAV files.
        config: Config instance.

    Returns:
        X: np.ndarray of shape (N, n_frames, n_mels)
        y: np.ndarray of shape (N, num_classes) — one-hot encoded
        labels: list of class label strings
    """
    labels = load_labels(export_dir)
    label_to_idx = {label: i for i, label in enumerate(labels)}

    spectrograms = []
    targets = []

    for label in labels:
        class_dir = os.path.join(export_dir, label)
        if not os.path.isdir(class_dir):
            print(f"Warning: no directory for label '{label}', skipping")
            continue

        wav_files = [f for f in os.listdir(class_dir) if f.lower().endswith(".wav")]
        print(f"  {label}: {len(wav_files)} clips")

        for wav_file in wav_files:
            wav_path = os.path.join(class_dir, wav_file)
            try:
                audio, sr = sf.read(wav_path, dtype="float32")

                # Resample if needed
                if sr != config.audio.sample_rate:
                    audio = librosa.resample(
                        audio, orig_sr=sr, target_sr=config.audio.sample_rate
                    )

                # Convert stereo to mono
                if audio.ndim > 1:
                    audio = audio.mean(axis=1)

                mel = compute_mel_spectrogram(audio, config)
                spectrograms.append(mel)

                # One-hot encode
                one_hot = np.zeros(len(labels), dtype=np.float32)
                one_hot[label_to_idx[label]] = 1.0
                targets.append(one_hot)

            except Exception as e:
                print(f"  Warning: failed to load {wav_path}: {e}")

    X = np.array(spectrograms, dtype=np.float32)
    y = np.array(targets, dtype=np.float32)

    print(f"Loaded {len(X)} clips across {len(labels)} classes")
    return X, y, labels


def split_dataset(X, y, config):
    """Stratified train/val split.

    Uses argmax of one-hot labels for stratification.
    """
    # Convert one-hot to class indices for stratification
    y_indices = np.argmax(y, axis=1)

    X_train, X_val, y_train, y_val = train_test_split(
        X, y,
        test_size=config.training.val_split,
        random_state=config.training.random_seed,
        stratify=y_indices,
    )

    return X_train, X_val, y_train, y_val


class Augmenter:
    """Apply random augmentations to a mel spectrogram."""

    def __init__(self, config, noise_spectrograms=None):
        """
        Args:
            config: Config instance.
            noise_spectrograms: Optional array of noise mel spectrograms for mixing.
        """
        self.cfg = config.augmentation
        self.audio_cfg = config.audio
        self.noise_specs = noise_spectrograms
        self.rng = np.random.default_rng(config.training.random_seed)

    def __call__(self, mel):
        """Apply random augmentations to a single mel spectrogram.

        Args:
            mel: np.ndarray of shape (n_frames, n_mels)

        Returns:
            Augmented mel spectrogram of same shape.
        """
        if not self.cfg.enabled:
            return mel

        mel = mel.copy()

        # Time shift (circular)
        if self.cfg.time_shift_max > 0:
            max_shift_frames = int(
                self.cfg.time_shift_max * self.audio_cfg.sample_rate
                / self.audio_cfg.hop_length
            )
            shift = self.rng.integers(-max_shift_frames, max_shift_frames + 1)
            mel = np.roll(mel, shift, axis=0)

        # Gain variation
        if self.cfg.gain_db_range > 0:
            gain_db = self.rng.uniform(
                -self.cfg.gain_db_range, self.cfg.gain_db_range
            )
            # In log domain, adding dB is adding a constant
            mel = mel + gain_db * np.log(10) / 20.0

        # Noise mixing (if noise clips available)
        if self.noise_specs is not None and len(self.noise_specs) > 0:
            snr_db = self.rng.uniform(self.cfg.noise_snr_min, self.cfg.noise_snr_max)
            noise_idx = self.rng.integers(len(self.noise_specs))
            noise = self.noise_specs[noise_idx]

            # Mix in log domain: approximate by weighted sum
            signal_power = np.mean(mel)
            noise_power = np.mean(noise)
            scale = 10 ** ((signal_power - noise_power - snr_db) / 20.0)
            mel = mel + scale * noise

        # SpecAugment: frequency masking
        if self.cfg.spec_freq_mask_max > 0:
            mask_width = self.rng.integers(1, self.cfg.spec_freq_mask_max + 1)
            mask_start = self.rng.integers(0, self.audio_cfg.n_mels - mask_width + 1)
            mel[:, mask_start:mask_start + mask_width] = 0.0

        # SpecAugment: time masking
        if self.cfg.spec_time_mask_max > 0:
            mask_width = self.rng.integers(1, self.cfg.spec_time_mask_max + 1)
            mask_start = self.rng.integers(0, self.audio_cfg.n_frames - mask_width + 1)
            mel[mask_start:mask_start + mask_width, :] = 0.0

        # Pitch shift approximation (mel bin shift)
        if self.cfg.mel_bin_shift_max > 0:
            shift = self.rng.integers(
                -self.cfg.mel_bin_shift_max, self.cfg.mel_bin_shift_max + 1
            )
            if shift != 0:
                mel = np.roll(mel, shift, axis=1)
                if shift > 0:
                    mel[:, :shift] = 0.0
                else:
                    mel[:, shift:] = 0.0

        return mel
