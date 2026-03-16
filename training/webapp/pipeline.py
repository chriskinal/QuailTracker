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

"""Pipeline orchestrator: wraps training scripts with progress callbacks."""

import json
import os
import sys

import numpy as np

# Add parent directory to path so we can import training modules
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from config import Config
from dataset import load_dataset, split_dataset, Augmenter
from model import build_model
from export import (
    convert_to_tflite_int8,
    generate_model_header,
    generate_mel_config_header,
    generate_mel_filterbank_header,
    generate_model_config_json,
)
from evaluate import (
    evaluate_keras,
    evaluate_tflite,
    plot_confusion_matrix,
)

from webapp.xeno_canto import download_species_dataset


class ProgressCallback:
    """Keras callback that sends progress to the web UI."""

    def __init__(self, total_epochs, callback):
        self.total_epochs = total_epochs
        self.callback = callback

    def make_keras_callback(self):
        import tensorflow as tf

        outer = self

        class _Callback(tf.keras.callbacks.Callback):
            def on_epoch_end(self, epoch, logs=None):
                logs = logs or {}
                outer.callback(json.dumps({
                    "stage": "training",
                    "epoch": epoch + 1,
                    "total": outer.total_epochs,
                    "loss": round(logs.get("loss", 0), 4),
                    "val_loss": round(logs.get("val_loss", 0), 4),
                    "accuracy": round(logs.get("accuracy", 0), 4),
                    "val_accuracy": round(logs.get("val_accuracy", 0), 4),
                    "auc": round(logs.get("auc", 0), 4),
                    "val_auc": round(logs.get("val_auc", 0), 4),
                }))

        return _Callback()


def run_training(data_dir, output_dir, epochs=100, batch_size=32,
                 augment=True, progress_callback=None):
    """Run model training.

    Args:
        data_dir: Path to clips directory with species subdirs.
        output_dir: Output directory for model artifacts.
        epochs: Max training epochs.
        batch_size: Training batch size.
        augment: Whether to enable augmentation.
        progress_callback: Optional callable(message_str).

    Returns:
        Dict with training results (metadata).
    """
    import tensorflow as tf
    from train import AugmentedSequence, normalize_spectrograms, plot_training_curves

    cb = progress_callback or (lambda msg: None)

    config = Config()
    config.training.epochs = epochs
    config.training.batch_size = batch_size
    config.augmentation.enabled = augment

    os.makedirs(output_dir, exist_ok=True)

    tf.random.set_seed(config.training.random_seed)
    np.random.seed(config.training.random_seed)

    # Load dataset
    cb("Loading dataset...")
    X, y, labels = load_dataset(data_dir, config)
    num_classes = len(labels)
    cb(f"Classes ({num_classes}): {labels}")

    if len(X) == 0:
        raise ValueError("No clips loaded. Check data directory.")

    # Normalize
    cb("Normalizing spectrograms...")
    X, mel_min, mel_max = normalize_spectrograms(X)
    cb(f"  mel_min={mel_min:.4f}, mel_max={mel_max:.4f}")

    # Split
    X_train, X_val, y_train, y_val = split_dataset(X, y, config)
    cb(f"Train: {len(X_train)}, Val: {len(X_val)}")

    # Prepare noise spectrograms for augmenter
    noise_specs = None
    if "noise" in labels:
        noise_idx = labels.index("noise")
        noise_mask = y_train[:, noise_idx] == 1.0
        if np.any(noise_mask):
            noise_specs = X_train[noise_mask]
            cb(f"  Using {len(noise_specs)} noise clips for augmentation")

    augmenter = Augmenter(config, noise_spectrograms=noise_specs)

    # Build model
    cb("Building model...")
    model = build_model(config, num_classes)

    # Data sequences
    train_seq = AugmentedSequence(
        X_train, y_train, config.training.batch_size,
        augmenter=augmenter if config.augmentation.enabled else None,
    )
    val_X = X_val[..., np.newaxis]

    # Callbacks — no ModelCheckpoint, rely on EarlyStopping restore_best_weights
    callbacks = [
        tf.keras.callbacks.EarlyStopping(
            monitor=config.training.early_stop_metric,
            patience=config.training.early_stop_patience,
            restore_best_weights=True,
            verbose=0,
        ),
    ]

    if progress_callback:
        progress_cb = ProgressCallback(epochs, progress_callback)
        callbacks.append(progress_cb.make_keras_callback())

    # Train
    cb(f"Training for up to {epochs} epochs...")
    history = model.fit(
        train_seq,
        validation_data=(val_X, y_val),
        epochs=epochs,
        callbacks=callbacks,
        verbose=0,
    )

    # Save weights using numpy (bypasses TF serialization entirely)
    weights = model.get_weights()
    weights_path = os.path.join(output_dir, "model_weights.npz")
    np.savez(weights_path, *weights)

    # Verify save/load immediately
    val_pred_orig = model.predict(val_X[:5], verbose=0)
    model2 = build_model(config, num_classes)
    data = np.load(weights_path)
    model2.set_weights([data[f'arr_{i}'] for i in range(len(data.files))])
    val_pred_check = model2.predict(val_X[:5], verbose=0)
    cb(f"  Weight verification — orig: {val_pred_orig.flatten()[:3]}, "
       f"reloaded: {val_pred_check.flatten()[:3]}")

    # Save history
    history_dict = {k: [float(v) for v in vals]
                    for k, vals in history.history.items()}
    with open(os.path.join(output_dir, "training_history.json"), "w") as f:
        json.dump(history_dict, f, indent=2)

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
    with open(os.path.join(output_dir, "model_metadata.json"), "w") as f:
        json.dump(metadata, f, indent=2)

    # Save calibration data
    cal_size = min(200, len(X_train))
    cal_indices = np.random.choice(len(X_train), cal_size, replace=False)
    np.save(os.path.join(output_dir, "calibration_data.npy"),
            X_train[cal_indices])

    # Plot training curves
    plot_training_curves(
        history_dict,
        os.path.join(output_dir, "training_curves.png"),
    )

    cb(json.dumps({
        "stage": "training",
        "status": "complete",
        "epochs_trained": metadata["epochs_trained"],
        "best_val_auc": metadata["best_val_auc"],
        "best_val_loss": metadata["best_val_loss"],
    }))

    return metadata


def run_export(model_dir, progress_callback=None):
    """Export trained model to TFLite + configs.

    Args:
        model_dir: Directory containing best_model.weights.h5,
                   calibration_data.npy, and model_metadata.json.
        progress_callback: Optional callable(message_str).

    Returns:
        Dict with export results.
    """
    cb = progress_callback or (lambda msg: None)

    weights_path = os.path.join(model_dir, "model_weights.npz")
    cal_path = os.path.join(model_dir, "calibration_data.npy")
    metadata_path = os.path.join(model_dir, "model_metadata.json")

    for path, name in [(weights_path, "model_weights.npz"),
                       (cal_path, "calibration_data.npy"),
                       (metadata_path, "model_metadata.json")]:
        if not os.path.exists(path):
            raise FileNotFoundError(f"{name} not found at {path}")

    # Reset TF state and rebuild model with numpy weights
    import tensorflow as tf
    tf.keras.backend.clear_session()

    with open(metadata_path, "r") as f:
        metadata = json.load(f)
    config = Config()
    model = build_model(config, metadata["num_classes"])
    data = np.load(weights_path)
    model.set_weights([data[f'arr_{i}'] for i in range(len(data.files))])

    # Convert to int8 TFLite
    cb("Converting to int8 TFLite...")
    tflite_path = os.path.join(model_dir, "quail_model.tflite")
    model_size = convert_to_tflite_int8(model, cal_path, tflite_path)
    cb(f"  TFLite model: {model_size / 1024:.1f} KB")

    if model_size > 200 * 1024:
        cb(f"  WARNING: Model exceeds 200 KB flash budget!")

    # Generate C headers
    cb("Generating C headers...")
    generate_model_header(tflite_path,
                          os.path.join(model_dir, "quail_model.h"))
    generate_mel_config_header(metadata_path,
                               os.path.join(model_dir, "mel_config.h"))
    generate_mel_filterbank_header(metadata_path,
                                   os.path.join(model_dir, "mel_filterbank.h"))

    # Generate JSON config for BLE deployment
    cb("Generating model_config.json...")
    config_json = generate_model_config_json(
        metadata_path, tflite_path,
        os.path.join(model_dir, "model_config.json"),
    )

    cb(json.dumps({
        "stage": "export",
        "status": "complete",
        "model_size_kb": round(model_size / 1024, 1),
    }))

    return {
        "tflite_path": tflite_path,
        "model_size": model_size,
        "config": config_json,
    }


def run_evaluate(model_dir, data_dir, progress_callback=None):
    """Evaluate model and generate confusion matrices.

    Args:
        model_dir: Directory with model artifacts.
        data_dir: Path to clips directory.
        progress_callback: Optional callable(message_str).

    Returns:
        Dict with evaluation metrics.
    """
    cb = progress_callback or (lambda msg: None)

    metadata_path = os.path.join(model_dir, "model_metadata.json")
    with open(metadata_path, "r") as f:
        metadata = json.load(f)

    labels = metadata["labels"]
    mel_min = metadata["mel_min"]
    mel_max = metadata["mel_max"]
    config = Config()

    cb("Loading dataset for evaluation...")
    X, y, _ = load_dataset(data_dir, config)

    if mel_max - mel_min > 1e-9:
        X = (X - mel_min) / (mel_max - mel_min)
    X = np.clip(X, 0.0, 1.0)

    _, X_val, _, y_val = split_dataset(X, y, config)
    cb(f"Validation set: {len(X_val)} clips")

    # Keras evaluation — rebuild model and load numpy weights
    import tensorflow as tf
    tf.keras.backend.clear_session()

    cb("Evaluating Keras model...")
    model = build_model(config, len(labels))
    weights_path = os.path.join(model_dir, "model_weights.npz")
    data = np.load(weights_path)
    model.set_weights([data[f'arr_{i}'] for i in range(len(data.files))])

    # Quick sanity check: compute predictions and AUC before full evaluation
    val_X_check = X_val[..., np.newaxis]
    raw_preds = model.predict(val_X_check, verbose=0)
    if len(labels) == 1:
        probs_check = raw_preds[:, 0]
        y_true_check = (y_val[:, 0] > 0.5).astype(int)
        pos_p = probs_check[y_true_check == 1]
        neg_p = probs_check[y_true_check == 0]
        from sklearn.metrics import roc_auc_score
        eval_auc = roc_auc_score(y_true_check, probs_check)
        cb(f"  Eval AUC: {eval_auc:.4f} (training reported: {metadata.get('best_val_auc', '?')})")
        cb(f"  Positive preds: min={pos_p.min():.6f}, max={pos_p.max():.6f}, "
           f"mean={pos_p.mean():.6f}")
        cb(f"  Negative preds: min={neg_p.min():.6f}, max={neg_p.max():.6f}, "
           f"mean={neg_p.mean():.6f}")

    y_true, y_pred_keras, opt_threshold = evaluate_keras(
        model, X_val, y_val, labels)
    cb(f"  Optimal threshold: {opt_threshold:.6f}")

    plot_confusion_matrix(
        y_true, y_pred_keras, labels,
        os.path.join(model_dir, "confusion_matrix_keras.png"),
        title="Keras Float32",
    )

    keras_acc = float(np.mean(y_pred_keras == y_true))

    # TFLite evaluation
    tflite_path = os.path.join(model_dir, "quail_model.tflite")
    tflite_acc = None
    if os.path.exists(tflite_path):
        cb("Evaluating TFLite model...")
        y_true_tfl, y_pred_tfl, tfl_threshold = evaluate_tflite(
            tflite_path, X_val, y_val, labels, mel_min, mel_max)
        plot_confusion_matrix(
            y_true_tfl, y_pred_tfl, labels,
            os.path.join(model_dir, "confusion_matrix_tflite.png"),
            title="TFLite Int8",
        )
        tflite_acc = float(np.mean(y_pred_tfl == y_true_tfl))
        cb(f"  TFLite optimal threshold: {tfl_threshold:.6f}")

    results = {
        "keras_accuracy": keras_acc,
        "tflite_accuracy": tflite_acc,
        "val_samples": len(X_val),
        "optimal_threshold": opt_threshold,
        "tflite_threshold": tfl_threshold if tflite_acc is not None else None,
    }

    cb(json.dumps({"stage": "evaluate", "status": "complete", **results}))
    return results


def run_full_pipeline(species_list, api_key, output_dir, noise_dir=None,
                      skip_download=False, max_recordings=30, quality_min="B",
                      min_conf=0.5, epochs=100, batch_size=32, augment=True,
                      progress_callback=None):
    """Full pipeline: download (with BirdNET sorting) → train → export → evaluate.

    Args:
        species_list: List of species English names.
        api_key: xeno-canto API key.
        output_dir: Output directory for all artifacts.
        noise_dir: Optional path to noise clips directory.
        skip_download: If True, skip download and use existing clips.
        max_recordings: Max recordings per species to download.
        quality_min: Minimum xeno-canto quality rating.
        min_conf: BirdNET minimum confidence for verified call detection.
        epochs: Training epochs.
        batch_size: Training batch size.
        augment: Enable augmentation.
        progress_callback: Optional callable(message_str).

    Returns:
        Dict with full pipeline results.
    """
    cb = progress_callback or (lambda msg: None)
    data_dir = os.path.join(output_dir, "clips")
    model_dir = output_dir
    os.makedirs(data_dir, exist_ok=True)

    # Stage 1: Download xeno-canto clips + BirdNET-verified sorting
    total_clips = 0
    if skip_download:
        cb("Skipping download — using existing clips")
        for species in species_list:
            species_dir = os.path.join(data_dir, species)
            if os.path.isdir(species_dir):
                count = len([f for f in os.listdir(species_dir)
                             if f.endswith(".wav")])
                cb(f"  {species}: {count} existing call clips")
                total_clips += count
            else:
                cb(f"  WARNING: No clips found for '{species}' at {species_dir}")
        noise_dest = os.path.join(data_dir, "noise")
        if os.path.isdir(noise_dest):
            noise_count = len([f for f in os.listdir(noise_dest)
                               if f.endswith(".wav")])
            cb(f"  noise: {noise_count} existing noise clips")
    else:
        cb(json.dumps({"stage": "download", "status": "started"}))
        for i, species in enumerate(species_list):
            cb(f"[{i + 1}/{len(species_list)}] Downloading '{species}'...")
            clips = download_species_dataset(
                species, api_key, data_dir,
                max_recordings=max_recordings,
                quality_min=quality_min,
                min_conf=min_conf,
                progress_callback=cb,
            )
            total_clips += clips

        cb(f"Download complete: {total_clips} verified call clips")

    # Copy additional noise directory if provided
    if noise_dir and os.path.isdir(noise_dir):
        import shutil
        noise_dest = os.path.join(data_dir, "noise")
        os.makedirs(noise_dest, exist_ok=True)
        copied = 0
        for f in os.listdir(noise_dir):
            if f.endswith(".wav"):
                shutil.copy2(os.path.join(noise_dir, f),
                             os.path.join(noise_dest, f))
                copied += 1
        cb(f"Copied {copied} additional noise clips from {noise_dir}")

    # Stage 3: Train
    cb(json.dumps({"stage": "training", "status": "started"}))
    metadata = run_training(
        data_dir, model_dir,
        epochs=epochs, batch_size=batch_size, augment=augment,
        progress_callback=cb,
    )

    # Stage 4: Export
    cb(json.dumps({"stage": "export", "status": "started"}))
    export_result = run_export(model_dir, progress_callback=cb)

    # Stage 5: Evaluate
    cb(json.dumps({"stage": "evaluate", "status": "started"}))
    eval_result = run_evaluate(model_dir, data_dir, progress_callback=cb)

    cb(json.dumps({"stage": "complete", "status": "done"}))

    return {
        "total_clips": total_clips,
        "training": metadata,
        "export": export_result,
        "evaluation": eval_result,
    }
