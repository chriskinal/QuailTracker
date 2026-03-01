"""Xeno-canto API v3 client: search, download, and segment recordings."""

import os
import tempfile

import librosa
import numpy as np
import requests
import soundfile as sf

API_BASE = "https://xeno-canto.org/api/3/recordings"


def search_species(species_name, api_key, quality_min="B", max_recordings=50):
    """Search xeno-canto for recordings of a species.

    Args:
        species_name: English common name (e.g., "Northern Bobwhite").
        api_key: xeno-canto API v3 key.
        quality_min: Minimum quality rating (A, B, or C).
        max_recordings: Maximum number of recordings to return.

    Returns:
        List of recording metadata dicts.
    """
    # q:">C" means better than C, i.e. A and B
    quality_map = {"A": 'q:A', "B": 'q:">C"', "C": 'q:">D"'}
    q_filter = quality_map.get(quality_min, 'q:">C"')

    params = {
        "query": f'en:"{species_name}" {q_filter}',
        "key": api_key,
        "per_page": min(max_recordings, 500),
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

    return filtered[:max_recordings]


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


def segment_recording(audio_path, output_dir, species_name, duration=3.0,
                      stride=1.5, sr=24000):
    """Segment a recording into fixed-length clips with overlap.

    Args:
        audio_path: Path to audio file.
        output_dir: Directory to save clips.
        species_name: Species name (used for subdirectory).
        duration: Clip duration in seconds.
        stride: Stride between clips in seconds.
        sr: Target sample rate.

    Returns:
        Number of clips generated.
    """
    try:
        audio, _ = librosa.load(audio_path, sr=sr, mono=True)
    except Exception:
        return 0

    clip_samples = int(duration * sr)
    stride_samples = int(stride * sr)

    if len(audio) < clip_samples:
        return 0

    species_dir = os.path.join(output_dir, species_name)
    os.makedirs(species_dir, exist_ok=True)

    rec_id = os.path.splitext(os.path.basename(audio_path))[0]
    count = 0

    offset = 0
    while offset + clip_samples <= len(audio):
        clip = audio[offset:offset + clip_samples]
        offset_ms = int(offset / sr * 1000)
        clip_path = os.path.join(species_dir, f"{rec_id}_{offset_ms:06d}.wav")
        sf.write(clip_path, clip, sr)
        count += 1
        offset += stride_samples

    return count


def download_species_dataset(species_name, api_key, output_dir,
                             max_recordings=30, quality_min="B",
                             progress_callback=None):
    """Download and segment a full dataset for one species.

    Args:
        species_name: English common name.
        api_key: xeno-canto API key.
        output_dir: Root output directory (clips go into {output_dir}/{species_name}/).
        max_recordings: Max recordings to download.
        quality_min: Minimum quality rating.
        progress_callback: Optional callable(message_str).

    Returns:
        Total number of clips generated.
    """
    if progress_callback:
        progress_callback(f"Searching xeno-canto for '{species_name}'...")

    recordings = search_species(species_name, api_key, quality_min,
                                max_recordings)

    if not recordings:
        if progress_callback:
            progress_callback(f"  No recordings found for '{species_name}'")
        return 0

    if progress_callback:
        progress_callback(f"  Found {len(recordings)} recordings")

    total_clips = 0

    with tempfile.TemporaryDirectory() as tmp_dir:
        for i, rec in enumerate(recordings):
            if progress_callback:
                progress_callback(
                    f"  [{i + 1}/{len(recordings)}] "
                    f"XC{rec.get('id', '?')} "
                    f"({rec.get('length', '?')}, quality {rec.get('q', '?')})"
                )

            filepath = download_recording(rec, tmp_dir, progress_callback)
            if filepath is None:
                continue

            clips = segment_recording(filepath, output_dir, species_name)
            total_clips += clips

            if progress_callback:
                progress_callback(f"    → {clips} clips")

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

    if progress_callback:
        progress_callback(
            f"  Done: {total_clips} clips for '{species_name}'"
        )

    return total_clips
