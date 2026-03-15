"""Main training entry point for BirdNET-STM32 model.

Usage:
    python train.py --data-dir /path/to/exported_clips --output-dir output
    python train.py --data-dir /path/to/exported_clips --output-dir output --epochs 5 --no-augment
"""

import argparse
import json
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import tensorflow as tf
from tensorflow import keras

from config import Config
from dataset import load_dataset, split_dataset, Augmenter
from model import build_model, print_model_summary


class AugmentedSequence(keras.utils.Sequence):
    """Keras Sequence with per-batch augmentation."""

    def __init__(self, X, y, batch_size, augmenter=None):
        self.X = X
        self.y = y
        self.batch_size = batch_size
        self.augmenter = augmenter
        self.indices = np.arange(len(X))

    def __len__(self):
        return int(np.ceil(len(self.X) / self.batch_size))

    def __getitem__(self, idx):
        start = idx * self.batch_size
        end = min(start + self.batch_size, len(self.X))
        batch_indices = self.indices[start:end]

        batch_X = self.X[batch_indices].copy()
        batch_y = self.y[batch_indices]

        if self.augmenter is not None:
            for i in range(len(batch_X)):
                batch_X[i] = self.augmenter(batch_X[i])

        # Add channel dimension: (batch, frames, mels) → (batch, frames, mels, 1)
        return batch_X[..., np.newaxis], batch_y

    def on_epoch_end(self):
        np.random.shuffle(self.indices)


def normalize_spectrograms(X):
    """Global [0,1] normalization of log-mel spectrograms.

    Returns:
        X_norm: Normalized array.
        mel_min: Global minimum (for C inference).
        mel_max: Global maximum (for C inference).
    """
    mel_min = float(np.min(X))
    mel_max = float(np.max(X))

    # Avoid division by zero
    if mel_max - mel_min < 1e-9:
        return X, mel_min, mel_max

    X_norm = (X - mel_min) / (mel_max - mel_min)
    return X_norm, mel_min, mel_max


def plot_training_curves(history, output_path):
    """Save training curves plot."""
    fig, axes = plt.subplots(1, 3, figsize=(15, 4))

    # Loss
    axes[0].plot(history["loss"], label="Train")
    axes[0].plot(history["val_loss"], label="Val")
    axes[0].set_title("Loss")
    axes[0].set_xlabel("Epoch")
    axes[0].legend()

    # Accuracy
    axes[1].plot(history["accuracy"], label="Train")
    axes[1].plot(history["val_accuracy"], label="Val")
    axes[1].set_title("Accuracy")
    axes[1].set_xlabel("Epoch")
    axes[1].legend()

    # AUC
    axes[2].plot(history["auc"], label="Train")
    axes[2].plot(history["val_auc"], label="Val")
    axes[2].set_title("AUC")
    axes[2].set_xlabel("Epoch")
    axes[2].legend()

    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"Saved training curves: {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Train BirdNET-STM32 model")
    parser.add_argument(
        "--data-dir", required=True,
        help="Path to exported clips directory (with <species>/ subdirs)",
    )
    parser.add_argument(
        "--output-dir", default="output",
        help="Output directory for model and artifacts",
    )
    parser.add_argument("--epochs", type=int, default=None, help="Override max epochs")
    parser.add_argument("--batch-size", type=int, default=None, help="Override batch size")
    parser.add_argument("--no-augment", action="store_true", help="Disable augmentation")
    args = parser.parse_args()

    config = Config()
    if args.epochs is not None:
        config.training.epochs = args.epochs
    if args.batch_size is not None:
        config.training.batch_size = args.batch_size
    if args.no_augment:
        config.augmentation.enabled = False

    os.makedirs(args.output_dir, exist_ok=True)

    # Set random seeds
    tf.random.set_seed(config.training.random_seed)
    np.random.seed(config.training.random_seed)

    # Load dataset
    print("Loading dataset...")
    X, y, labels = load_dataset(args.data_dir, config)
    num_classes = len(labels)
    print(f"Classes ({num_classes}): {labels}")

    if len(X) == 0:
        print("Error: no clips loaded. Check --data-dir path.")
        return

    # Normalize
    print("Normalizing spectrograms...")
    X, mel_min, mel_max = normalize_spectrograms(X)
    print(f"  mel_min={mel_min:.4f}, mel_max={mel_max:.4f}")

    # Split
    X_train, X_val, y_train, y_val = split_dataset(X, y, config)
    print(f"Train: {len(X_train)}, Val: {len(X_val)}")

    # Prepare noise spectrograms for augmenter
    noise_specs = None
    if "noise" in labels:
        noise_idx = labels.index("noise")
        noise_mask = y_train[:, noise_idx] == 1.0
        if np.any(noise_mask):
            noise_specs = X_train[noise_mask]
            print(f"  Using {len(noise_specs)} noise clips for augmentation")

    # Augmenter
    augmenter = Augmenter(config, noise_spectrograms=noise_specs)

    # Build model
    print("\nBuilding model...")
    model = build_model(config, num_classes)
    print_model_summary(model)

    # Data sequences
    train_seq = AugmentedSequence(
        X_train, y_train, config.training.batch_size,
        augmenter=augmenter if config.augmentation.enabled else None,
    )
    # Validation: no augmentation, add channel dim
    val_X = X_val[..., np.newaxis]

    # Callbacks — save weights only to avoid focal_loss serialization bug
    callbacks = [
        keras.callbacks.EarlyStopping(
            monitor=config.training.early_stop_metric,
            patience=config.training.early_stop_patience,
            restore_best_weights=True,
            verbose=1,
        ),
        keras.callbacks.ModelCheckpoint(
            os.path.join(args.output_dir, "best_model.weights.h5"),
            monitor=config.training.early_stop_metric,
            save_best_only=True,
            save_weights_only=True,
            verbose=1,
        ),
    ]

    # Train
    print(f"\nTraining for up to {config.training.epochs} epochs...")
    history = model.fit(
        train_seq,
        validation_data=(val_X, y_val),
        epochs=config.training.epochs,
        callbacks=callbacks,
        verbose=1,
    )

    # Save history
    history_dict = {k: [float(v) for v in vals] for k, vals in history.history.items()}
    history_path = os.path.join(args.output_dir, "training_history.json")
    with open(history_path, "w") as f:
        json.dump(history_dict, f, indent=2)
    print(f"Saved training history: {history_path}")

    # Save metadata
    metadata = {
        "labels": labels,
        "num_classes": num_classes,
        "mel_min": mel_min,
        "mel_max": mel_max,
        "audio_config": {
            "sample_rate": config.audio.sample_rate,
            "n_fft": config.audio.n_fft,
            "hop_length": config.audio.hop_length,
            "n_mels": config.audio.n_mels,
            "n_frames": config.audio.n_frames,
            "fmin": config.audio.fmin,
            "fmax": config.audio.fmax,
        },
        "total_clips": len(X),
        "train_clips": len(X_train),
        "val_clips": len(X_val),
        "epochs_trained": len(history.history["loss"]),
        "best_val_auc": float(max(history.history.get("val_auc", [0]))),
        "best_val_loss": float(min(history.history.get("val_loss", [999]))),
    }
    metadata_path = os.path.join(args.output_dir, "model_metadata.json")
    with open(metadata_path, "w") as f:
        json.dump(metadata, f, indent=2)
    print(f"Saved metadata: {metadata_path}")

    # Save calibration data for TFLite quantization
    cal_size = min(200, len(X_train))
    cal_indices = np.random.choice(len(X_train), cal_size, replace=False)
    cal_data = X_train[cal_indices]
    cal_path = os.path.join(args.output_dir, "calibration_data.npy")
    np.save(cal_path, cal_data)
    print(f"Saved calibration data ({cal_size} samples): {cal_path}")

    # Plot training curves
    plot_training_curves(
        history_dict,
        os.path.join(args.output_dir, "training_curves.png"),
    )

    print("\nTraining complete!")
    print(f"  Best val AUC: {metadata['best_val_auc']:.4f}")
    print(f"  Best val loss: {metadata['best_val_loss']:.4f}")
    print(f"  Output: {args.output_dir}/")


if __name__ == "__main__":
    main()
