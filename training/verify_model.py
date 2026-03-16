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

"""Verify model confidence values and diagnose low-confidence issues.

Shows actual sigmoid outputs for positive/negative examples to determine
if the model can discriminate (and at what confidence level), plus tests
the TFLite int8 model to check quantization accuracy.

Usage:
    python verify_model.py --model-dir output --data-dir /path/to/exported_clips
    python verify_model.py --model-dir output --data-dir /path/to/exported_clips --wav /path/to/device.wav
"""

import argparse
import json
import os

import numpy as np
import tensorflow as tf

from config import Config
from dataset import load_dataset, split_dataset, compute_mel_spectrogram
from model import build_model
from train import normalize_spectrograms


def verify_keras_confidence(model, X_val, y_val, labels):
    """Show actual sigmoid outputs for positive and negative examples."""
    val_X = X_val[..., np.newaxis]
    y_pred = model.predict(val_X, verbose=0)

    # For single-class sigmoid: output is (N, 1)
    if y_pred.shape[1] == 1:
        probs = y_pred[:, 0]
        # True labels: 1.0 for positive, 0.0 for noise
        true_labels = y_val[:, 0]
    else:
        probs = y_pred
        true_labels = y_val

    pos_mask = true_labels > 0.5
    neg_mask = ~pos_mask

    pos_probs = probs[pos_mask]
    neg_probs = probs[neg_mask]

    print("\n=== Keras Float32 — Actual Sigmoid Outputs ===")
    print(f"  Positive examples ({len(pos_probs)}):")
    print(f"    min={pos_probs.min():.4f}  max={pos_probs.max():.4f}  "
          f"mean={pos_probs.mean():.4f}  median={np.median(pos_probs):.4f}")
    print(f"    >70%: {np.sum(pos_probs > 0.7)}/{len(pos_probs)}  "
          f">50%: {np.sum(pos_probs > 0.5)}/{len(pos_probs)}  "
          f">30%: {np.sum(pos_probs > 0.3)}/{len(pos_probs)}")

    print(f"  Negative examples ({len(neg_probs)}):")
    print(f"    min={neg_probs.min():.4f}  max={neg_probs.max():.4f}  "
          f"mean={neg_probs.mean():.4f}  median={np.median(neg_probs):.4f}")
    print(f"    <30%: {np.sum(neg_probs < 0.3)}/{len(neg_probs)}  "
          f"<50%: {np.sum(neg_probs < 0.5)}/{len(neg_probs)}")

    # Find optimal threshold (maximize accuracy)
    best_thresh = 0.5
    best_acc = 0
    for thresh in np.arange(0.01, 1.0, 0.01):
        pred = (probs >= thresh).astype(int)
        true = (true_labels > 0.5).astype(int)
        acc = np.mean(pred == true)
        if acc > best_acc:
            best_acc = acc
            best_thresh = thresh

    # Accuracy at common thresholds
    print(f"\n  Accuracy at threshold 0.50: "
          f"{np.mean(((probs >= 0.5) == (true_labels > 0.5)).astype(int)):.4f}")
    print(f"  Accuracy at threshold 0.30: "
          f"{np.mean(((probs >= 0.3) == (true_labels > 0.5)).astype(int)):.4f}")
    print(f"  Optimal threshold: {best_thresh:.2f} (accuracy={best_acc:.4f})")

    # Show histogram as text
    print("\n  Confidence distribution (positive examples):")
    for lo in range(0, 100, 10):
        hi = lo + 10
        count = np.sum((pos_probs >= lo / 100) & (pos_probs < hi / 100))
        bar = "#" * count
        print(f"    {lo:3d}-{hi:3d}%: {count:4d} {bar}")

    print("  Confidence distribution (negative examples):")
    for lo in range(0, 100, 10):
        hi = lo + 10
        count = np.sum((neg_probs >= lo / 100) & (neg_probs < hi / 100))
        bar = "#" * count
        print(f"    {lo:3d}-{hi:3d}%: {count:4d} {bar}")

    return pos_probs, neg_probs


def verify_tflite_confidence(tflite_path, X_val, y_val, labels):
    """Show actual TFLite int8 outputs and compare to expected."""
    interpreter = tf.lite.Interpreter(model_path=tflite_path)
    interpreter.allocate_tensors()

    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]

    input_scale = input_details["quantization_parameters"]["scales"][0]
    input_zp = input_details["quantization_parameters"]["zero_points"][0]
    output_scale = output_details["quantization_parameters"]["scales"][0]
    output_zp = output_details["quantization_parameters"]["zero_points"][0]

    print(f"\n=== TFLite Int8 — Quantization Parameters ===")
    print(f"  Input:  scale={input_scale:.6f}, zero_point={input_zp}")
    print(f"  Output: scale={output_scale:.6f}, zero_point={output_zp}")
    print(f"  Input range: [{(-128 - input_zp) * input_scale:.4f}, "
          f"{(127 - input_zp) * input_scale:.4f}]")
    print(f"  Output range: [{(-128 - output_zp) * output_scale:.4f}, "
          f"{(127 - output_zp) * output_scale:.4f}]")

    # Run inference on all validation samples
    raw_outputs = []
    float_outputs = []

    for i in range(len(X_val)):
        sample = X_val[i][np.newaxis, ..., np.newaxis].astype(np.float32)

        # Quantize input: float [0,1] → int8
        sample_int8 = np.clip(
            np.round(sample / input_scale + input_zp), -128, 127
        ).astype(np.int8)

        interpreter.set_tensor(input_details["index"], sample_int8)
        interpreter.invoke()

        output_int8 = interpreter.get_tensor(output_details["index"])[0]
        output_float = (output_int8.astype(np.float32) - output_zp) * output_scale

        raw_outputs.append(output_int8[0])
        float_outputs.append(output_float[0])

    raw_outputs = np.array(raw_outputs)
    float_outputs = np.array(float_outputs)

    # Split by true label
    if y_val.shape[1] == 1:
        true_labels = y_val[:, 0]
    else:
        true_labels = y_val

    pos_mask = true_labels > 0.5
    neg_mask = ~pos_mask

    print(f"\n=== TFLite Int8 — Actual Outputs ===")
    print(f"  Positive examples ({np.sum(pos_mask)}):")
    print(f"    raw int8: min={raw_outputs[pos_mask].min()}  "
          f"max={raw_outputs[pos_mask].max()}  "
          f"mean={raw_outputs[pos_mask].mean():.1f}")
    print(f"    sigmoid:  min={float_outputs[pos_mask].min():.4f}  "
          f"max={float_outputs[pos_mask].max():.4f}  "
          f"mean={float_outputs[pos_mask].mean():.4f}")

    print(f"  Negative examples ({np.sum(neg_mask)}):")
    print(f"    raw int8: min={raw_outputs[neg_mask].min()}  "
          f"max={raw_outputs[neg_mask].max()}  "
          f"mean={raw_outputs[neg_mask].mean():.1f}")
    print(f"    sigmoid:  min={float_outputs[neg_mask].min():.4f}  "
          f"max={float_outputs[neg_mask].max():.4f}  "
          f"mean={float_outputs[neg_mask].mean():.4f}")

    # What would the firmware see?
    print(f"\n  Firmware equivalent (dequant → conf%):")
    pos_conf = float_outputs[pos_mask] * 100
    neg_conf = float_outputs[neg_mask] * 100
    print(f"    Positive: {pos_conf.min():.0f}%-{pos_conf.max():.0f}% "
          f"(mean {pos_conf.mean():.0f}%)")
    print(f"    Negative: {neg_conf.min():.0f}%-{neg_conf.max():.0f}% "
          f"(mean {neg_conf.mean():.0f}%)")

    # Count detections at different thresholds
    for thresh in [70, 50, 40, 30, 20]:
        tp = np.sum(float_outputs[pos_mask] >= thresh / 100)
        fp = np.sum(float_outputs[neg_mask] >= thresh / 100)
        fn = np.sum(float_outputs[pos_mask] < thresh / 100)
        tn = np.sum(float_outputs[neg_mask] < thresh / 100)
        acc = (tp + tn) / len(float_outputs)
        print(f"    @{thresh}%: TP={tp} FP={fp} FN={fn} TN={tn} acc={acc:.3f}")

    return raw_outputs, float_outputs


def test_wav_file(wav_path, model, tflite_path, config, mel_min, mel_max):
    """Test a WAV file through the full pipeline."""
    import librosa
    import soundfile as sf

    print(f"\n=== Testing WAV file: {wav_path} ===")

    audio, sr = sf.read(wav_path, dtype="float32")
    if audio.ndim > 1:
        audio = audio.mean(axis=1)
    if sr != config.audio.sample_rate:
        audio = librosa.resample(audio, orig_sr=sr, target_sr=config.audio.sample_rate)

    duration = len(audio) / config.audio.sample_rate
    print(f"  Duration: {duration:.1f}s, samples: {len(audio)}, sr: {config.audio.sample_rate}")
    print(f"  Peak: {np.max(np.abs(audio)):.6f} ({20 * np.log10(np.max(np.abs(audio)) + 1e-9):.1f} dBFS)")

    # Segment into 3-second windows with 1-second step
    ac = config.audio
    window_samples = int(ac.duration * ac.sample_rate)
    step_samples = ac.sample_rate  # 1-second step
    n_windows = max(1, int((len(audio) - window_samples) / step_samples) + 1)

    print(f"  Windows: {n_windows} ({ac.duration}s window, 1s step)")

    # TFLite interpreter
    interpreter = tf.lite.Interpreter(model_path=tflite_path)
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]
    input_scale = input_details["quantization_parameters"]["scales"][0]
    input_zp = input_details["quantization_parameters"]["zero_points"][0]
    output_scale = output_details["quantization_parameters"]["scales"][0]
    output_zp = output_details["quantization_parameters"]["zero_points"][0]

    for w in range(min(n_windows, 30)):  # Limit to 30 windows
        start = w * step_samples
        end = start + window_samples
        if end > len(audio):
            break

        chunk = audio[start:end]

        # Compute mel spectrogram (same as training)
        mel = compute_mel_spectrogram(chunk, config)

        # Log mel values before normalization
        mel_raw_min = np.min(mel)
        mel_raw_max = np.max(mel)

        # Normalize with training min/max
        mel_norm = (mel - mel_min) / (mel_max - mel_min)
        mel_norm = np.clip(mel_norm, 0.0, 1.0)

        # Keras inference
        keras_input = mel_norm[np.newaxis, ..., np.newaxis]
        keras_prob = model.predict(keras_input, verbose=0)[0, 0]

        # TFLite inference
        tflite_input = np.clip(
            np.round(keras_input / input_scale + input_zp), -128, 127
        ).astype(np.int8)
        interpreter.set_tensor(input_details["index"], tflite_input)
        interpreter.invoke()
        output_int8 = interpreter.get_tensor(output_details["index"])[0]
        tflite_prob = (float(output_int8[0]) - output_zp) * output_scale

        # Show int8 input statistics
        in_min = tflite_input.min()
        in_max = tflite_input.max()
        in_mean = tflite_input.mean()

        time_s = start / ac.sample_rate
        print(f"  [{time_s:5.1f}s] logmel=[{mel_raw_min:.1f},{mel_raw_max:.1f}] "
              f"int8=[{in_min},{in_max}] mean={in_mean:.0f} "
              f"| keras={keras_prob:.3f} tflite={tflite_prob:.3f} raw={output_int8[0]}")


def main():
    parser = argparse.ArgumentParser(description="Verify model confidence values")
    parser.add_argument("--model-dir", required=True, help="Directory with model files")
    parser.add_argument("--data-dir", required=True, help="Path to exported clips")
    parser.add_argument("--wav", default=None, help="Optional WAV file to test")
    args = parser.parse_args()

    # Load metadata
    metadata_path = os.path.join(args.model_dir, "model_metadata.json")
    with open(metadata_path, "r") as f:
        metadata = json.load(f)

    labels = metadata["labels"]
    mel_min = metadata["mel_min"]
    mel_max = metadata["mel_max"]
    print(f"Labels: {labels}")
    print(f"Normalization: mel_min={mel_min:.4f}, mel_max={mel_max:.4f}")

    config = Config()

    # Load and normalize dataset
    print("\nLoading dataset...")
    X, y, loaded_labels = load_dataset(args.data_dir, config)

    X_raw = X.copy()  # Keep unnormalized copy
    X_norm = (X - mel_min) / (mel_max - mel_min)
    X_norm = np.clip(X_norm, 0.0, 1.0)

    _, X_val, _, y_val = split_dataset(X_norm, y, config)
    print(f"Validation set: {len(X_val)} samples")

    # Rebuild model and load weights
    model = build_model(config, len(labels))
    model.load_weights(os.path.join(args.model_dir, "best_model.weights.h5"))

    # Verify Keras confidence
    pos_probs, neg_probs = verify_keras_confidence(model, X_val, y_val, labels)

    # Verify TFLite confidence
    tflite_path = os.path.join(args.model_dir, "quail_model.tflite")
    if os.path.exists(tflite_path):
        verify_tflite_confidence(tflite_path, X_val, y_val, labels)
    else:
        print(f"\nSkipping TFLite verification: {tflite_path} not found")

    # Test WAV file if provided
    if args.wav:
        test_wav_file(args.wav, model, tflite_path, config, mel_min, mel_max)

    # Summary and recommendations
    median_pos = np.median(pos_probs)
    median_neg = np.median(neg_probs)
    gap = median_pos - median_neg

    print(f"\n=== Summary ===")
    print(f"  Median positive confidence: {median_pos:.3f} ({median_pos*100:.0f}%)")
    print(f"  Median negative confidence: {median_neg:.3f} ({median_neg*100:.0f}%)")
    print(f"  Discrimination gap: {gap:.3f}")

    if median_pos < 0.7:
        print(f"\n  WARNING: Model produces low confidence even on training data!")
        print(f"  The model can rank (AUC=0.974) but sigmoid outputs are compressed.")
        print(f"  Recommended firmware threshold: {max(0.15, (median_pos + median_neg) / 2):.2f} "
              f"({max(15, int((median_pos + median_neg) / 2 * 100))}%)")
        print(f"  Consider retraining with:")
        print(f"    - Label smoothing (0.1) to improve calibration")
        print(f"    - Temperature scaling on the final layer")
        print(f"    - Larger model or more training data")


if __name__ == "__main__":
    main()
