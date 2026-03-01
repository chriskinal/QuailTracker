"""Sort training clips into call vs. noise with no audio overlap between classes.

The clips were created with 1.5s stride on 3s windows, so adjacent clips share
50% audio. If a "call" clip and "noise" clip overlap, the model gets contradictory
labels for similar spectrograms and can't learn.

This script:
1. Groups clips by source recording (XC ID)
2. Computes call-band energy for each clip
3. Classifies each clip as call/noise using Otsu's threshold
4. Ensures a GAP between call and noise regions — noise clips must have
   NO overlap with ANY call clip (requires >=3s separation)

Usage:
    python sort_clips.py --clips-dir output/clips --dry-run
    python sort_clips.py --clips-dir output/clips
"""

import argparse
import os
import shutil
from collections import defaultdict

import librosa
import numpy as np
import soundfile as sf


CALL_FREQ_LOW = 1300.0
CALL_FREQ_HIGH = 2800.0
SAMPLE_RATE = 24000
N_FFT = 512
HOP_LENGTH = 256
CLIP_DURATION_MS = 3000
STRIDE_MS = 1500


def compute_call_band_energy(audio, sr=SAMPLE_RATE):
    """Log energy in the bobwhite call frequency band."""
    S = np.abs(librosa.stft(audio, n_fft=N_FFT, hop_length=HOP_LENGTH)) ** 2
    freqs = librosa.fft_frequencies(sr=sr, n_fft=N_FFT)
    call_mask = (freqs >= CALL_FREQ_LOW) & (freqs <= CALL_FREQ_HIGH)
    call_energy = np.sum(S[call_mask, :])
    return np.log10(call_energy + 1e-12)


def find_otsu_threshold(values):
    """Otsu's method to split values into two classes."""
    hist, bin_edges = np.histogram(values, bins=100)
    bin_centers = (bin_edges[:-1] + bin_edges[1:]) / 2
    total = hist.sum()
    best_thresh = bin_centers[0]
    best_score = -1
    cumsum = 0
    cum_mean = 0
    global_mean = np.sum(hist * bin_centers) / total

    for i in range(len(hist)):
        cumsum += hist[i]
        if cumsum == 0 or cumsum == total:
            continue
        cum_mean += hist[i] * bin_centers[i]
        w0 = cumsum / total
        w1 = 1 - w0
        m0 = cum_mean / cumsum
        m1 = (global_mean * total - cum_mean) / (total - cumsum)
        score = w0 * w1 * (m0 - m1) ** 2
        if score > best_score:
            best_score = score
            best_thresh = bin_centers[i]

    return best_thresh


def main():
    parser = argparse.ArgumentParser(description="Sort clips with overlap protection")
    parser.add_argument("--clips-dir", required=True, help="Path to clips directory")
    parser.add_argument("--species", default="Northern Bobwhite")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    species_dir = os.path.join(args.clips_dir, args.species)
    noise_dir = os.path.join(args.clips_dir, "noise")

    # Parse clips into per-recording groups
    recordings = defaultdict(list)
    wav_files = sorted(f for f in os.listdir(species_dir) if f.endswith(".wav"))

    print(f"Analyzing {len(wav_files)} clips...")
    for i, fname in enumerate(wav_files):
        parts = fname.replace(".wav", "").split("_")
        xc_id = parts[0]
        offset_ms = int(parts[1])

        audio, sr = sf.read(os.path.join(species_dir, fname), dtype="float32")
        if sr != SAMPLE_RATE:
            audio = librosa.resample(audio, orig_sr=sr, target_sr=SAMPLE_RATE)

        energy = compute_call_band_energy(audio, SAMPLE_RATE)
        recordings[xc_id].append({
            "filename": fname,
            "offset_ms": offset_ms,
            "energy": energy,
        })

        if (i + 1) % 200 == 0:
            print(f"  {i + 1}/{len(wav_files)}...")

    # Global threshold
    all_energies = np.array([c["energy"] for clips in recordings.values() for c in clips])
    threshold = find_otsu_threshold(all_energies)
    print(f"\nOtsu threshold: {threshold:.2f}")

    # For each recording: classify clips, then enforce gap
    total_call = 0
    total_noise = 0
    total_discarded = 0
    noise_files = []

    for xc_id in sorted(recordings.keys()):
        clips = sorted(recordings[xc_id], key=lambda c: c["offset_ms"])

        # Step 1: raw classification
        for c in clips:
            c["is_call"] = c["energy"] >= threshold

        # Step 2: find call offsets
        call_offsets = set()
        for c in clips:
            if c["is_call"]:
                call_offsets.add(c["offset_ms"])

        # Step 3: noise clips must not overlap with ANY call clip.
        # Two clips overlap if their offsets differ by less than CLIP_DURATION_MS.
        for c in clips:
            if c["is_call"]:
                c["label"] = "call"
            else:
                too_close = any(
                    abs(c["offset_ms"] - co) < CLIP_DURATION_MS
                    for co in call_offsets
                )
                c["label"] = "discard" if too_close else "noise"

        n_call = sum(1 for c in clips if c["label"] == "call")
        n_noise = sum(1 for c in clips if c["label"] == "noise")
        n_disc = sum(1 for c in clips if c["label"] == "discard")
        total_call += n_call
        total_noise += n_noise
        total_discarded += n_disc

        noise_files.extend(c["filename"] for c in clips if c["label"] == "noise")

        print(f"  {xc_id}: {n_call:3d} call, {n_noise:3d} noise, "
              f"{n_disc:3d} discarded ({len(clips)} total)")

    print(f"\nSummary:")
    print(f"  Call clips (positive): {total_call}")
    print(f"  Noise clips (negative): {total_noise}")
    print(f"  Discarded (too close to calls): {total_discarded}")
    print(f"  Total: {total_call + total_noise + total_discarded}")

    if args.dry_run:
        print("\n[DRY RUN] No files moved.")
        return

    os.makedirs(noise_dir, exist_ok=True)
    for fname in noise_files:
        shutil.move(
            os.path.join(species_dir, fname),
            os.path.join(noise_dir, fname),
        )

    # Keep discarded clips in species dir as positive examples
    # (better to have borderline positives than lose training data)

    remaining = len([f for f in os.listdir(species_dir) if f.endswith(".wav")])
    noise_count = len([f for f in os.listdir(noise_dir) if f.endswith(".wav")])

    print(f"\nDone:")
    print(f"  {species_dir}: {remaining} call clips (includes {total_discarded} borderline)")
    print(f"  {noise_dir}: {noise_count} noise clips")


if __name__ == "__main__":
    main()
