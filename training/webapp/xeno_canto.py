"""Xeno-canto API v3 client: search, download, and BirdNET-verified clip extraction."""

import os
import subprocess
import tempfile

import librosa
import numpy as np
import requests
import soundfile as sf

API_BASE = "https://xeno-canto.org/api/3/recordings"

# Cached BirdNET analyzer (loads ~100MB model once)
_analyzer = None


def _convert_to_wav(input_path):
    """Convert audio file to 48kHz mono WAV using ffmpeg.

    BirdNET expects 48kHz. Converting up front avoids librosa/audioread
    MP3 decoding issues in the container.

    Returns path to WAV file (same location, .wav extension).
    """
    if input_path.lower().endswith(".wav"):
        return input_path

    wav_path = os.path.splitext(input_path)[0] + ".wav"
    subprocess.run(
        ["ffmpeg", "-y", "-i", input_path,
         "-ar", "48000", "-ac", "1", wav_path],
        capture_output=True, check=True,
    )
    os.remove(input_path)
    return wav_path


def _get_analyzer():
    """Get or create the cached BirdNET analyzer singleton."""
    global _analyzer
    if _analyzer is None:
        from birdnetlib.analyzer import Analyzer
        _analyzer = Analyzer()
    return _analyzer


def _fetch_quality(species_name, api_key, quality_filter, max_per_page=500):
    """Fetch recordings for a single quality grade from xeno-canto."""
    params = {
        "query": f'en:"{species_name}" {quality_filter}',
        "key": api_key,
        "per_page": min(max_per_page, 500),
    }
    resp = requests.get(API_BASE, params=params, timeout=30)
    resp.raise_for_status()
    data = resp.json()

    recordings = data.get("recordings", [])

    # Filter out very short recordings (<5 seconds)
    filtered = []
    for rec in recordings:
        try:
            length_str = rec.get("length", "0:00")
            parts = length_str.split(":")
            seconds = int(parts[0]) * 60 + int(parts[1])
            if seconds >= 5:
                filtered.append(rec)
        except (ValueError, IndexError):
            continue

    return filtered


def search_species(species_name, api_key, quality_min="B", max_recordings=50):
    """Search xeno-canto for recordings of a species with balanced quality.

    Queries each quality grade separately and builds a balanced result set.
    Target distribution: 40% A, 35% B, 25% C (when quality_min includes
    those grades). If a grade has fewer results than its quota, the
    shortfall is redistributed to other grades.

    Args:
        species_name: English common name (e.g., "Northern Bobwhite").
        api_key: xeno-canto API v3 key.
        quality_min: Minimum quality rating (A, B, or C).
        max_recordings: Maximum number of recordings to return.

    Returns:
        List of recording metadata dicts.
    """
    # Build list of grades to query based on quality_min
    grade_filters = {"A": 'q:A', "B": 'q:B', "C": 'q:C'}
    if quality_min == "A":
        grades = ["A"]
        quotas = {"A": 1.0}
    elif quality_min == "B":
        grades = ["A", "B"]
        quotas = {"A": 0.55, "B": 0.45}
    else:  # C
        grades = ["A", "B", "C"]
        quotas = {"A": 0.40, "B": 0.35, "C": 0.25}

    # Fetch each grade separately
    pools = {}
    for grade in grades:
        pools[grade] = _fetch_quality(species_name, api_key,
                                      grade_filters[grade])

    # Allocate quotas, redistributing shortfalls
    targets = {g: int(max_recordings * quotas[g]) for g in grades}
    # Give rounding remainder to first grade
    targets[grades[0]] += max_recordings - sum(targets.values())

    selected = []
    seen_ids = set()
    remaining = 0

    for grade in grades:
        pool = pools[grade]
        target = targets[grade] + remaining
        remaining = 0
        count = 0
        for rec in pool:
            if count >= target:
                break
            rec_id = rec.get("id")
            if rec_id not in seen_ids:
                seen_ids.add(rec_id)
                selected.append(rec)
                count += 1
        # Carry shortfall to next grade
        remaining = target - count

    # If still short after all grades, try backfilling from any grade
    if remaining > 0:
        for grade in grades:
            for rec in pools[grade]:
                if remaining <= 0:
                    break
                rec_id = rec.get("id")
                if rec_id not in seen_ids:
                    seen_ids.add(rec_id)
                    selected.append(rec)
                    remaining -= 1

    return selected[:max_recordings]


def download_recording(recording, output_dir, progress_callback=None):
    """Download a single recording from xeno-canto.

    Args:
        recording: Recording metadata dict (must have "file" and "id" keys).
        output_dir: Directory to save the downloaded file.
        progress_callback: Optional callable(message_str).

    Returns:
        Path to downloaded file, or None on failure.
    """
    file_url = recording.get("file", "")
    rec_id = recording.get("id", "unknown")

    if not file_url:
        return None

    # xeno-canto URLs may be protocol-relative
    if file_url.startswith("//"):
        file_url = "https:" + file_url

    if progress_callback:
        progress_callback(f"Downloading XC{rec_id}...")

    try:
        resp = requests.get(file_url, timeout=60, stream=True)
        resp.raise_for_status()

        # Determine extension from content-type or URL
        ext = ".mp3"
        if "wav" in file_url.lower():
            ext = ".wav"
        elif "ogg" in file_url.lower():
            ext = ".ogg"

        filename = f"XC{rec_id}{ext}"
        filepath = os.path.join(output_dir, filename)

        with open(filepath, "wb") as f:
            for chunk in resp.iter_content(chunk_size=8192):
                f.write(chunk)

        return filepath

    except requests.RequestException as e:
        if progress_callback:
            progress_callback(f"  Failed to download XC{rec_id}: {e}")
        return None


def _deduplicate_detections(detections, min_gap=3.0):
    """Remove overlapping detections, keeping highest confidence.

    Greedy: pick highest confidence first, skip any within min_gap seconds.
    This ensures no extracted clips share audio.
    """
    if not detections:
        return []

    sorted_dets = sorted(detections, key=lambda d: -d["confidence"])
    kept = []
    for det in sorted_dets:
        if not any(abs(det["start_time"] - k["start_time"]) < min_gap
                   for k in kept):
            kept.append(det)

    return sorted(kept, key=lambda d: d["start_time"])


def segment_with_birdnet(audio_path, output_dir, species_name,
                         min_conf=0.5, sr=24000, progress_callback=None):
    """Use BirdNET to find verified calls and extract clips.

    Runs BirdNET on the full recording, extracts 3s clips centered on
    verified detections of the target species, and extracts noise clips
    from segments with no detections (with 3s gap protection).

    Args:
        audio_path: Path to downloaded audio file.
        output_dir: Root clips directory.
        species_name: Target species English common name.
        min_conf: Minimum BirdNET confidence threshold.
        sr: Target sample rate for extracted clips.
        progress_callback: Optional callable(message_str).

    Returns:
        Tuple of (call_clips, noise_clips) counts.
    """
    from birdnetlib import Recording
    cb = progress_callback or (lambda msg: None)

    analyzer = _get_analyzer()

    # Run BirdNET analysis with overlap for better detection coverage
    recording = Recording(
        analyzer, audio_path,
        min_conf=min_conf,
        overlap=1.5,
    )
    recording.analyze()

    # Filter for target species
    detections = [d for d in recording.detections
                  if d["common_name"] == species_name]

    # Deduplicate overlapping detections (3s gap = no shared audio)
    detections = _deduplicate_detections(detections, min_gap=3.0)

    # Load audio at target sample rate for clip extraction
    try:
        audio, _ = librosa.load(audio_path, sr=sr, mono=True)
    except Exception:
        return 0, 0

    clip_samples = int(3.0 * sr)
    duration = len(audio) / sr

    if len(audio) < clip_samples:
        return 0, 0

    rec_id = os.path.splitext(os.path.basename(audio_path))[0]

    # Extract call clips
    species_dir = os.path.join(output_dir, species_name)
    os.makedirs(species_dir, exist_ok=True)

    call_clips = 0
    detection_starts = []

    for det in detections:
        start_sample = int(det["start_time"] * sr)
        end_sample = start_sample + clip_samples
        if end_sample > len(audio):
            continue

        clip = audio[start_sample:end_sample]
        offset_ms = int(det["start_time"] * 1000)
        clip_path = os.path.join(
            species_dir, f"{rec_id}_{offset_ms:06d}.wav")
        sf.write(clip_path, clip, sr)
        call_clips += 1
        detection_starts.append(det["start_time"])

    # Extract noise clips (segments far from any detection)
    noise_dir = os.path.join(output_dir, "noise")
    os.makedirs(noise_dir, exist_ok=True)

    noise_clips = 0
    t = 0.0
    while t + 3.0 <= duration:
        # Require at least 3s gap from any detection
        too_close = any(abs(t - ds) < 3.0 for ds in detection_starts)
        if not too_close:
            start_sample = int(t * sr)
            clip = audio[start_sample:start_sample + clip_samples]
            offset_ms = int(t * 1000)
            clip_path = os.path.join(
                noise_dir, f"{rec_id}_{offset_ms:06d}.wav")
            sf.write(clip_path, clip, sr)
            noise_clips += 1
            t += 3.0  # non-overlapping noise clips
        else:
            t += 1.5  # stride past detection region

    return call_clips, noise_clips


def download_species_dataset(species_name, api_key, output_dir,
                             max_recordings=30, quality_min="B",
                             min_conf=0.5, progress_callback=None):
    """Download recordings and extract BirdNET-verified clips.

    Args:
        species_name: English common name.
        api_key: xeno-canto API key.
        output_dir: Root output directory (clips go into subdirs).
        max_recordings: Max recordings to download.
        quality_min: Minimum quality rating.
        min_conf: BirdNET minimum confidence for call detection.
        progress_callback: Optional callable(message_str).

    Returns:
        Total number of call clips generated.
    """
    cb = progress_callback or (lambda msg: None)

    cb(f"Searching xeno-canto for '{species_name}'...")
    recordings = search_species(species_name, api_key, quality_min,
                                max_recordings)

    if not recordings:
        cb(f"  No recordings found for '{species_name}'")
        return 0

    # Show quality distribution
    from collections import Counter
    qdist = Counter(rec.get("q", "?") for rec in recordings)
    dist_str = ", ".join(f"{g}={n}" for g, n in sorted(qdist.items()))
    cb(f"  Found {len(recordings)} recordings ({dist_str})")

    # Pre-load BirdNET analyzer before download loop
    cb("Loading BirdNET analyzer...")
    _get_analyzer()

    total_calls = 0
    total_noise = 0

    with tempfile.TemporaryDirectory() as tmp_dir:
        for i, rec in enumerate(recordings):
            cb(f"  [{i + 1}/{len(recordings)}] "
               f"XC{rec.get('id', '?')} "
               f"({rec.get('length', '?')}, quality {rec.get('q', '?')})")

            filepath = download_recording(rec, tmp_dir, progress_callback)
            if filepath is None:
                continue

            # Convert MP3/OGG to WAV for reliable audio loading
            try:
                filepath = _convert_to_wav(filepath)
            except subprocess.CalledProcessError:
                cb(f"    Failed to convert {os.path.basename(filepath)}")
                continue

            calls, noise = segment_with_birdnet(
                filepath, output_dir, species_name,
                min_conf=min_conf,
                progress_callback=progress_callback,
            )
            total_calls += calls
            total_noise += noise
            cb(f"    → {calls} call clips, {noise} noise clips")

    # Write labels.txt if it doesn't exist
    labels_path = os.path.join(output_dir, "labels.txt")
    existing_labels = set()
    if os.path.exists(labels_path):
        with open(labels_path, "r") as f:
            existing_labels = {line.strip() for line in f if line.strip()}

    existing_labels.add(species_name)
    with open(labels_path, "w") as f:
        for label in sorted(existing_labels):
            f.write(label + "\n")

    cb(f"  Done: {total_calls} call clips, {total_noise} noise clips "
       f"for '{species_name}'")

    return total_calls
