"""Evaluate trained models: per-class metrics, confusion matrix, TFLite accuracy.

Usage:
    python evaluate.py --model-dir output --data-dir /path/to/exported_clips
    python evaluate.py --model-dir output --data-dir /path/to/exported_clips --tflite
"""

import argparse
import json
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import tensorflow as tf
from sklearn.metrics import classification_report, confusion_matrix

from config import Config
from dataset import load_dataset, split_dataset
from train import normalize_spectrograms


def evaluate_keras(model, X_val, y_val, labels, threshold=0.5):
    """Evaluate Keras float32 model.

    Args:
        model: Loaded Keras model.
        X_val: Validation spectrograms (N, frames, mels).
        y_val: One-hot labels (N, num_classes).
        labels: List of class label strings.
        threshold: Sigmoid threshold for positive prediction.
    """
    # Add channel dimension
    val_X = X_val[..., np.newaxis]
    y_pred_probs = model.predict(val_X, verbose=0)

    if len(labels) == 1:
        # Single-class sigmoid: use threshold for binary classification
        eval_labels = [labels[0], "noise"]
        y_pred_classes = (y_pred_probs[:, 0] >= threshold).astype(int)
        y_true_classes = (y_val[:, 0] > 0.5).astype(int)
    else:
        eval_labels = labels
        y_pred_classes = np.argmax(y_pred_probs, axis=1)
        y_true_classes = np.argmax(y_val, axis=1)

    print("\n=== Keras Float32 Model ===")
    print(classification_report(y_true_classes, y_pred_classes, target_names=eval_labels))

    return y_true_classes, y_pred_classes


def evaluate_tflite(tflite_path, X_val, y_val, labels, mel_min, mel_max):
    """Evaluate int8 TFLite model.

    Args:
        tflite_path: Path to .tflite file.
        X_val: Validation spectrograms (N, frames, mels), normalized [0,1].
        y_val: One-hot labels (N, num_classes).
        labels: List of class label strings.
        mel_min: Normalization minimum from training.
        mel_max: Normalization maximum from training.
    """
    interpreter = tf.lite.Interpreter(model_path=tflite_path)
    interpreter.allocate_tensors()

    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]

    # Print quantization parameters
    input_scale = input_details["quantization_parameters"]["scales"][0]
    input_zp = input_details["quantization_parameters"]["zero_points"][0]
    output_scale = output_details["quantization_parameters"]["scales"][0]
    output_zp = output_details["quantization_parameters"]["zero_points"][0]

    print(f"\nQuantization parameters:")
    print(f"  Input:  scale={input_scale:.6f}, zero_point={input_zp}")
    print(f"  Output: scale={output_scale:.6f}, zero_point={output_zp}")

    y_pred_classes = []
    y_pred_probs_all = []

    for i in range(len(X_val)):
        sample = X_val[i][np.newaxis, ..., np.newaxis].astype(np.float32)

        # Quantize input to int8
        sample_int8 = (sample / input_scale + input_zp).astype(np.int8)

        interpreter.set_tensor(input_details["index"], sample_int8)
        interpreter.invoke()

        output_int8 = interpreter.get_tensor(output_details["index"])[0]

        # Dequantize output
        output_float = (output_int8.astype(np.float32) - output_zp) * output_scale

        y_pred_probs_all.append(output_float)

        if len(labels) == 1:
            y_pred_classes.append(int(output_float[0] >= 0.5))
        else:
            y_pred_classes.append(np.argmax(output_float))

    y_pred_classes = np.array(y_pred_classes)

    if len(labels) == 1:
        eval_labels = [labels[0], "noise"]
        y_true_classes = (y_val[:, 0] > 0.5).astype(int)
    else:
        eval_labels = labels
        y_true_classes = np.argmax(y_val, axis=1)

    print("\n=== TFLite Int8 Model ===")
    print(classification_report(y_true_classes, y_pred_classes, target_names=eval_labels))

    # Accuracy comparison
    tflite_acc = np.mean(y_pred_classes == y_true_classes)
    print(f"TFLite accuracy: {tflite_acc:.4f}")

    return y_true_classes, y_pred_classes


def plot_confusion_matrix(y_true, y_pred, labels, output_path, title="Confusion Matrix"):
    """Save confusion matrix plot."""
    cm = confusion_matrix(y_true, y_pred)
    fig, ax = plt.subplots(figsize=(max(6, len(labels)), max(5, len(labels) - 1)))
    im = ax.imshow(cm, interpolation="nearest", cmap=plt.cm.Blues)
    ax.figure.colorbar(im, ax=ax)
    ax.set(
        xticks=np.arange(len(labels)),
        yticks=np.arange(len(labels)),
        xticklabels=labels,
        yticklabels=labels,
        ylabel="True label",
        xlabel="Predicted label",
        title=title,
    )
    plt.setp(ax.get_xticklabels(), rotation=45, ha="right", rotation_mode="anchor")

    # Annotate cells
    thresh = cm.max() / 2.0
    for i in range(len(labels)):
        for j in range(len(labels)):
            ax.text(
                j, i, format(cm[i, j], "d"),
                ha="center", va="center",
                color="white" if cm[i, j] > thresh else "black",
            )

    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"Saved confusion matrix: {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Evaluate BirdNET-STM32 model")
    parser.add_argument(
        "--model-dir", required=True,
        help="Directory containing best_model.keras and model_metadata.json",
    )
    parser.add_argument(
        "--data-dir", required=True,
        help="Path to exported clips directory",
    )
    parser.add_argument(
        "--tflite", action="store_true",
        help="Also evaluate int8 TFLite model",
    )
    args = parser.parse_args()

    # Load metadata
    metadata_path = os.path.join(args.model_dir, "model_metadata.json")
    with open(metadata_path, "r") as f:
        metadata = json.load(f)

    labels = metadata["labels"]
    mel_min = metadata["mel_min"]
    mel_max = metadata["mel_max"]

    config = Config()

    # Load and normalize dataset
    print("Loading dataset...")
    X, y, loaded_labels = load_dataset(args.data_dir, config)

    # Verify labels match
    if loaded_labels != labels:
        print(f"Warning: dataset labels {loaded_labels} differ from model labels {labels}")
        print("Using model labels for evaluation.")

    # Normalize with training min/max
    if mel_max - mel_min > 1e-9:
        X = (X - mel_min) / (mel_max - mel_min)
    X = np.clip(X, 0.0, 1.0)

    # Use same split as training
    _, X_val, _, y_val = split_dataset(X, y, config)
    print(f"Validation set: {len(X_val)} clips")

    # Evaluate Keras model
    model_path = os.path.join(args.model_dir, "best_model.keras")
    model = tf.keras.models.load_model(model_path)

    y_true, y_pred_keras = evaluate_keras(model, X_val, y_val, labels)

    plot_confusion_matrix(
        y_true, y_pred_keras, labels,
        os.path.join(args.model_dir, "confusion_matrix_keras.png"),
        title="Keras Float32",
    )

    # Evaluate TFLite model
    if args.tflite:
        tflite_path = os.path.join(args.model_dir, "quail_model.tflite")
        if not os.path.exists(tflite_path):
            print(f"\nError: {tflite_path} not found. Run export.py first.")
            return

        y_true_tflite, y_pred_tflite = evaluate_tflite(
            tflite_path, X_val, y_val, labels, mel_min, mel_max,
        )

        plot_confusion_matrix(
            y_true_tflite, y_pred_tflite, labels,
            os.path.join(args.model_dir, "confusion_matrix_tflite.png"),
            title="TFLite Int8",
        )

        # Compare accuracy
        keras_acc = np.mean(y_pred_keras == y_true)
        tflite_acc = np.mean(y_pred_tflite == y_true_tflite)
        diff = abs(keras_acc - tflite_acc) * 100
        print(f"\nAccuracy comparison:")
        print(f"  Keras:  {keras_acc:.4f}")
        print(f"  TFLite: {tflite_acc:.4f}")
        print(f"  Delta:  {diff:.1f}%")

        if diff > 3.0:
            print("  WARNING: >3% accuracy drop from quantization!")
        else:
            print("  OK: quantization accuracy within acceptable range")


if __name__ == "__main__":
    main()
