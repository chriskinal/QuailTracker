"""Post-build script: drop two flashable images at repo root:

   qt_esp_v<ver>.bin     - merged bootloader + partitions + app, for full-flash
                           sideloading. Starts at offset 0x0 — flash with:
                              esptool.py --chip esp32c3 write_flash 0x0 qt_esp_v<ver>.bin
                           or via the Espressif Web Flasher.
   qt_esp_app_v<ver>.bin - app-only image, for OTA upload through the web UI.

   Version is pulled from ESP_FW_VERSION in src/main.c."""

import glob
import os
import re
import shutil
import subprocess

Import("env")


def _read_fw_version():
    main_c = os.path.join(env["PROJECT_DIR"], "src", "main.c")
    try:
        with open(main_c) as f:
            for line in f:
                m = re.search(r'#define\s+ESP_FW_VERSION\s+"([^"]+)"', line)
                if m:
                    return m.group(1)
    except FileNotFoundError:
        pass
    return "unknown"


def _merge_firmware(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware = str(target[0])

    version = _read_fw_version()
    repo_root = os.path.dirname(env["PROJECT_DIR"])
    dest = os.path.join(repo_root, "qt_esp_v{}.bin".format(version))
    app_dest = os.path.join(repo_root, "qt_esp_app_v{}.bin".format(version))

    esptool_dir = env.PioPlatform().get_package_dir("tool-esptoolpy")
    esptool = os.path.join(esptool_dir, "esptool.py")
    python = env.subst("$PYTHONEXE")

    cmd = [
        python, esptool, "--chip", "esp32c3",
        "merge_bin",
        "--output", dest,
        "0x0", bootloader,
        "0x8000", partitions,
        "0x10000", firmware,
    ]
    subprocess.check_call(cmd)
    print("Post-build: merged image -> {}".format(os.path.basename(dest)))

    shutil.copyfile(firmware, app_dest)
    print("Post-build: app image  -> {}".format(os.path.basename(app_dest)))

    # Keep only the two most recently built ESP images at repo root —
    # stops accumulation across version bumps.
    _prune_old_bins(repo_root, "qt_esp_v[0-9]*.bin", keep=2)
    _prune_old_bins(repo_root, "qt_esp_app_v[0-9]*.bin", keep=2)


def _prune_old_bins(repo_root, glob_pattern, keep):
    """Delete older versioned bins matching glob_pattern in repo_root,
    keeping the `keep` most recently modified."""
    paths = glob.glob(os.path.join(repo_root, glob_pattern))
    paths.sort(key=os.path.getmtime, reverse=True)
    for p in paths[keep:]:
        try:
            os.remove(p)
            print("Post-build: pruned {}".format(os.path.basename(p)))
        except OSError:
            pass


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _merge_firmware)
