"""Flask web server for QuailTracker training pipeline."""

import json
import os
import queue
import sys
import threading
import time

from flask import Flask, Response, jsonify, render_template, request, send_from_directory

# Add parent directory to path for training module imports
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

app = Flask(__name__)

# Global state
_job_lock = threading.Lock()
_job_thread = None
_job_status = {"state": "idle", "stage": "", "error": ""}
_progress_queue = queue.Queue()

# Default directories (overridable via Docker volumes)
DATA_DIR = os.environ.get("DATA_DIR", "/data")
OUTPUT_DIR = os.environ.get("OUTPUT_DIR", "/output")


def _push_progress(message):
    """Thread-safe progress push."""
    _progress_queue.put(message)


def _set_status(state, stage="", error=""):
    global _job_status
    _job_status = {"state": state, "stage": stage, "error": error}
    _push_progress(json.dumps({"_status": state, "stage": stage, "error": error}))


def _run_job(job_fn, *args, **kwargs):
    """Run a job function in the background thread."""
    try:
        _set_status("running")
        job_fn(*args, **kwargs)
        _set_status("done")
    except Exception as e:
        _set_status("error", error=str(e))
        _push_progress(f"ERROR: {e}")
    finally:
        _push_progress("[DONE]")


def _start_job(job_fn, *args, **kwargs):
    """Start a background job if none is running."""
    global _job_thread
    with _job_lock:
        if _job_thread is not None and _job_thread.is_alive():
            return False, "A job is already running"
        # Drain old progress messages
        while not _progress_queue.empty():
            try:
                _progress_queue.get_nowait()
            except queue.Empty:
                break
        _job_thread = threading.Thread(
            target=_run_job, args=(job_fn,) + args, kwargs=kwargs,
            daemon=True,
        )
        _job_thread.start()
        return True, "Job started"


# --- Routes ---

@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/status")
def api_status():
    return jsonify(_job_status)


@app.route("/api/progress")
def api_progress():
    """SSE endpoint streaming progress messages."""
    def generate():
        while True:
            try:
                msg = _progress_queue.get(timeout=30)
                if msg == "[DONE]":
                    yield f"data: {json.dumps({'done': True})}\n\n"
                    break
                yield f"data: {msg}\n\n"
            except queue.Empty:
                # Send keepalive
                yield ": keepalive\n\n"

    return Response(generate(), mimetype="text/event-stream",
                    headers={"Cache-Control": "no-cache",
                             "X-Accel-Buffering": "no"})


@app.route("/api/train", methods=["POST"])
def api_train():
    """Start a training job."""
    from webapp.pipeline import run_training, run_export

    data = request.get_json() or {}
    data_dir = data.get("data_dir", os.path.join(DATA_DIR, "clips"))
    output_dir = data.get("output_dir", OUTPUT_DIR)
    epochs = int(data.get("epochs", 100))
    batch_size = int(data.get("batch_size", 32))
    augment = data.get("augment", True)

    if not os.path.isdir(data_dir):
        return jsonify({"error": f"Data directory not found: {data_dir}"}), 400

    def job():
        _set_status("running", "training")
        run_training(data_dir, output_dir, epochs=epochs,
                     batch_size=batch_size, augment=augment,
                     progress_callback=_push_progress)
        _set_status("running", "export")
        run_export(output_dir, progress_callback=_push_progress)

    ok, msg = _start_job(job)
    if not ok:
        return jsonify({"error": msg}), 409
    return jsonify({"message": msg})


@app.route("/api/download-species", methods=["POST"])
def api_download_species():
    """Download xeno-canto clips for a species."""
    from webapp.xeno_canto import download_species_dataset

    data = request.get_json() or {}
    species = data.get("species", "")
    api_key = data.get("api_key", "")
    output_dir = data.get("output_dir", os.path.join(DATA_DIR, "clips"))
    max_recordings = int(data.get("max_recordings", 30))
    quality_min = data.get("quality_min", "B")

    if not species:
        return jsonify({"error": "species is required"}), 400
    if not api_key:
        return jsonify({"error": "api_key is required"}), 400

    def job():
        _set_status("running", "download")
        download_species_dataset(
            species, api_key, output_dir,
            max_recordings=max_recordings,
            quality_min=quality_min,
            progress_callback=_push_progress,
        )

    ok, msg = _start_job(job)
    if not ok:
        return jsonify({"error": msg}), 409
    return jsonify({"message": msg})


@app.route("/api/full-pipeline", methods=["POST"])
def api_full_pipeline():
    """Full pipeline: download → train → export → evaluate."""
    from webapp.pipeline import run_full_pipeline

    data = request.get_json() or {}
    species_list = data.get("species_list", [])
    api_key = data.get("api_key", "")
    skip_download = data.get("skip_download", False)
    output_dir = data.get("output_dir", OUTPUT_DIR)
    noise_dir = data.get("noise_dir", "")
    max_recordings = int(data.get("max_recordings", 30))
    quality_min = data.get("quality_min", "B")
    epochs = int(data.get("epochs", 100))
    batch_size = int(data.get("batch_size", 32))
    augment = data.get("augment", True)
    call_band_low = float(data.get("call_band_low", 1300))
    call_band_high = float(data.get("call_band_high", 2800))

    if not species_list:
        return jsonify({"error": "species_list is required"}), 400
    if not skip_download and not api_key:
        return jsonify({"error": "api_key is required"}), 400

    def job():
        run_full_pipeline(
            species_list=species_list,
            api_key=api_key,
            skip_download=skip_download,
            output_dir=output_dir,
            noise_dir=noise_dir if noise_dir else None,
            max_recordings=max_recordings,
            quality_min=quality_min,
            epochs=epochs,
            batch_size=batch_size,
            augment=augment,
            call_band_low=call_band_low,
            call_band_high=call_band_high,
            progress_callback=_push_progress,
        )

    ok, msg = _start_job(job)
    if not ok:
        return jsonify({"error": msg}), 409
    return jsonify({"message": msg})


@app.route("/api/outputs/<path:filename>")
def api_outputs(filename):
    """Download output files."""
    return send_from_directory(OUTPUT_DIR, filename, as_attachment=True)


@app.route("/api/outputs")
def api_list_outputs():
    """List available output files."""
    if not os.path.isdir(OUTPUT_DIR):
        return jsonify({"files": []})

    files = []
    for f in os.listdir(OUTPUT_DIR):
        filepath = os.path.join(OUTPUT_DIR, f)
        if os.path.isfile(filepath):
            files.append({
                "name": f,
                "size": os.path.getsize(filepath),
            })
    return jsonify({"files": files})


if __name__ == "__main__":
    os.makedirs(DATA_DIR, exist_ok=True)
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    app.run(host="0.0.0.0", port=5000, debug=False)
