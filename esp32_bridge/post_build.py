"""Post-build script: merge bootloader + partitions + app into a single
   flashable image at repo root as qt_esp_v<ver>.bin.

   The merged image starts at flash offset 0x0 — flash with one command:

       esptool.py --chip esp32c3 write_flash 0x0 qt_esp_v<ver>.bin

   Or via the Espressif Web Flasher in a browser (no install required).
   Version is pulled from ESP_FW_VERSION in src/main.c."""

import os
import re
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


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _merge_firmware)
