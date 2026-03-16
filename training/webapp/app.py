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
_cancel_event = threading.Event()

# Default directories (overridable via Docker volumes)
DATA_DIR = os.environ.get("DATA_DIR", "/data")
OUTPUT_DIR = os.environ.get("OUTPUT_DIR", "/output")


def _push_progress(message, check_cancel=True):
    """Thread-safe progress push. Also checks for cancellation."""
    _progress_queue.put(message)
    if check_cancel:
        _check_cancel()


def _set_status(state, stage="", error=""):
    global _job_status
    _job_status = {"state": state, "stage": stage, "error": error}
    _push_progress(json.dumps({"_status": state, "stage": stage, "error": error}),
                   check_cancel=False)


class CancelledError(Exception):
    """Raised when a job is cancelled."""
    pass


def _check_cancel():
    """Check if cancellation was requested. Call from job functions."""
    if _cancel_event.is_set():
        raise CancelledError("Job cancelled by user")


def _run_job(job_fn, *args, **kwargs):
    """Run a job function in the background thread."""
    try:
        _cancel_event.clear()
        _set_status("running")
        job_fn(*args, **kwargs)
        _set_status("done")
    except CancelledError:
        _set_status("cancelled")
        _push_progress("Job cancelled.", check_cancel=False)
    except Exception as e:
        _set_status("error", error=str(e))
        _push_progress(f"ERROR: {e}", check_cancel=False)
    finally:
        _push_progress("[DONE]", check_cancel=False)


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
    status = dict(_job_status)
    with _job_lock:
        status["running"] = _job_thread is not None and _job_thread.is_alive()
    return jsonify(status)


@app.route("/api/cancel", methods=["POST"])
def api_cancel():
    """Request cancellation of the running job."""
    with _job_lock:
        if _job_thread is None or not _job_thread.is_alive():
            return jsonify({"error": "No job running"}), 409
    _cancel_event.set()
    _push_progress("Cancelling...", check_cancel=False)
    return jsonify({"message": "Cancel requested"})


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
    min_conf = float(data.get("min_conf", 0.5))

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
            min_conf=min_conf,
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
    min_conf = float(data.get("min_conf", 0.5))
    epochs = int(data.get("epochs", 100))
    batch_size = int(data.get("batch_size", 32))
    augment = data.get("augment", True)

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
            min_conf=min_conf,
            epochs=epochs,
            batch_size=batch_size,
            augment=augment,
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
