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


def compute_call_band_energy(audio, sr=SAMPLE_RATE,
                              freq_low=CALL_FREQ_LOW, freq_high=CALL_FREQ_HIGH):
    """Log energy in the specified call frequency band."""
    S = np.abs(librosa.stft(audio, n_fft=N_FFT, hop_length=HOP_LENGTH)) ** 2
    freqs = librosa.fft_frequencies(sr=sr, n_fft=N_FFT)
    call_mask = (freqs >= freq_low) & (freqs <= freq_high)
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


def sort_species_clips(clips_dir, species, freq_low=CALL_FREQ_LOW,
                       freq_high=CALL_FREQ_HIGH, dry_run=False,
                       drop_borderline=True, progress_callback=None):
    """Sort clips for a species into call vs. noise using energy analysis.

    Args:
        clips_dir: Root clips directory containing species subdirectories.
        species: Species subdirectory name.
        freq_low: Lower bound of call frequency band (Hz).
        freq_high: Upper bound of call frequency band (Hz).
        dry_run: If True, classify but don't move files.
        progress_callback: Optional callable(message_str).

    Returns:
        Dict with counts: total_call, total_noise, total_discarded.
    """
    cb = progress_callback or print
    species_dir = os.path.join(clips_dir, species)

    if not os.path.isdir(species_dir):
        cb(f"Species directory not found: {species_dir}")
        return {"total_call": 0, "total_noise": 0, "total_discarded": 0}

    # Restore clips from previous sorts back to species dir for clean re-sort
    noise_dir = os.path.join(clips_dir, "noise")
    discard_dir = os.path.join(clips_dir, "discarded")
    manifest_path = os.path.join(species_dir, ".sort_xc_ids")

    # Build set of XC IDs belonging to this species
    species_xc_ids = set()
    # From current species dir
    for f in os.listdir(species_dir):
        if f.endswith(".wav"):
            species_xc_ids.add(f.split("_")[0])
    # From manifest (covers IDs where ALL clips were moved out)
    if os.path.exists(manifest_path):
        with open(manifest_path, "r") as f:
            for line in f:
                xc_id = line.strip()
                if xc_id:
                    species_xc_ids.add(xc_id)

    restored = 0
    for src_dir in [noise_dir, discard_dir]:
        if not os.path.isdir(src_dir):
            continue
        for fname in list(os.listdir(src_dir)):
            if not fname.endswith(".wav"):
                continue
            xc_id = fname.split("_")[0]
            if xc_id in species_xc_ids:
                shutil.move(
                    os.path.join(src_dir, fname),
                    os.path.join(species_dir, fname),
                )
                restored += 1
    if restored > 0:
        cb(f"  Restored {restored} clips from previous sort")

    # Parse clips into per-recording groups
    recordings = defaultdict(list)
    wav_files = sorted(f for f in os.listdir(species_dir) if f.endswith(".wav"))

    if not wav_files:
        cb(f"No WAV clips found in {species_dir}")
        return {"total_call": 0, "total_noise": 0, "total_discarded": 0}

    cb(f"Analyzing {len(wav_files)} clips (call band: {freq_low:.0f}-{freq_high:.0f} Hz)...")
    for i, fname in enumerate(wav_files):
        parts = fname.replace(".wav", "").split("_")
        xc_id = parts[0]
        offset_ms = int(parts[1])

        audio, sr = sf.read(os.path.join(species_dir, fname), dtype="float32")
        if sr != SAMPLE_RATE:
            audio = librosa.resample(audio, orig_sr=sr, target_sr=SAMPLE_RATE)

        energy = compute_call_band_energy(audio, SAMPLE_RATE, freq_low, freq_high)
        recordings[xc_id].append({
            "filename": fname,
            "offset_ms": offset_ms,
            "energy": energy,
        })

        if (i + 1) % 200 == 0:
            cb(f"  {i + 1}/{len(wav_files)}...")

    # Global threshold
    all_energies = np.array([c["energy"] for clips in recordings.values() for c in clips])
    threshold = find_otsu_threshold(all_energies)
    cb(f"Otsu threshold: {threshold:.2f}")

    # For each recording: classify clips, then enforce gap
    total_call = 0
    total_noise = 0
    total_discarded = 0
    noise_files = []
    discard_files = []

    for xc_id in sorted(recordings.keys()):
        clips = sorted(recordings[xc_id], key=lambda c: c["offset_ms"])

        for c in clips:
            c["is_call"] = c["energy"] >= threshold

        call_offsets = set()
        for c in clips:
            if c["is_call"]:
                call_offsets.add(c["offset_ms"])

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
        if drop_borderline:
            discard_files.extend(c["filename"] for c in clips if c["label"] == "discard")

        cb(f"  {xc_id}: {n_call:3d} call, {n_noise:3d} noise, "
           f"{n_disc:3d} discarded ({len(clips)} total)")

    cb(f"Sort summary: {total_call} call, {total_noise} noise, {total_discarded} discarded")

    if not dry_run:
        # Save manifest of XC IDs for this species (enables clean re-sorts)
        with open(manifest_path, "w") as f:
            for xc_id in sorted(recordings.keys()):
                f.write(xc_id + "\n")
        os.makedirs(noise_dir, exist_ok=True)
        for fname in noise_files:
            shutil.move(
                os.path.join(species_dir, fname),
                os.path.join(noise_dir, fname),
            )

        if discard_files:
            discard_dir = os.path.join(clips_dir, "discarded")
            os.makedirs(discard_dir, exist_ok=True)
            for fname in discard_files:
                shutil.move(
                    os.path.join(species_dir, fname),
                    os.path.join(discard_dir, fname),
                )

        remaining = len([f for f in os.listdir(species_dir) if f.endswith(".wav")])
        noise_count = len([f for f in os.listdir(noise_dir) if f.endswith(".wav")])
        if drop_borderline:
            cb(f"  {species_dir}: {remaining} call clips ({total_discarded} borderline dropped)")
        else:
            cb(f"  {species_dir}: {remaining} call clips (includes {total_discarded} borderline)")
        cb(f"  {noise_dir}: {noise_count} noise clips")

    return {
        "total_call": total_call,
        "total_noise": total_noise,
        "total_discarded": total_discarded,
    }


def main():
    parser = argparse.ArgumentParser(description="Sort clips with overlap protection")
    parser.add_argument("--clips-dir", required=True, help="Path to clips directory")
    parser.add_argument("--species", default="Northern Bobwhite")
    parser.add_argument("--freq-low", type=float, default=CALL_FREQ_LOW,
                        help="Lower call-band frequency (Hz)")
    parser.add_argument("--freq-high", type=float, default=CALL_FREQ_HIGH,
                        help="Upper call-band frequency (Hz)")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    sort_species_clips(
        args.clips_dir, args.species,
        freq_low=args.freq_low, freq_high=args.freq_high,
        dry_run=args.dry_run,
    )


if __name__ == "__main__":
    main()
