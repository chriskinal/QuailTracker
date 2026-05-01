"""Post-build script: copy firmware.bin to repo root as
   qt_esp_v<ver>.bin, with the version pulled from
   ESP_FW_VERSION in src/main.c."""

import os
import re
import shutil

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


def _copy_firmware(source, target, env):
    src_bin = str(target[0])
    version = _read_fw_version()
    repo_root = os.path.dirname(env["PROJECT_DIR"])
    dest = os.path.join(repo_root, "qt_esp_v{}.bin".format(version))
    shutil.copy2(src_bin, dest)
    print("Post-build: {} -> {}".format(os.path.basename(src_bin), os.path.basename(dest)))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _copy_firmware)
