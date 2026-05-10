import glob
import os
import re
import shutil

Import("env")

# Add FPU flags to linker and assembler (build_flags only applies to C/C++ compiler).
# Cortex-M33 with single-precision FPU (fpv5-sp-d16).
env.Append(
    LINKFLAGS=[
        "-mfpu=fpv5-sp-d16",
        "-mfloat-abi=hard",
    ],
    ASFLAGS=[
        "-mfpu=fpv5-sp-d16",
        "-mfloat-abi=hard",
    ],
)


def _read_fw_version():
    """Pull FW_VERSION string from Core/Inc/main.h."""
    main_h = os.path.join(
        env["PROJECT_DIR"],
        "stm32",
        "QuailTracker_U575",
        "Core",
        "Inc",
        "main.h",
    )
    try:
        with open(main_h) as f:
            for line in f:
                m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', line)
                if m:
                    return m.group(1)
    except FileNotFoundError:
        pass
    return "unknown"


def _prune_old_bins(repo_root, glob_pattern, keep):
    """Delete older versioned bins matching glob_pattern, keeping the
    `keep` most recently modified. Stops the project root from filling
    up with every build's artifact across many version bumps."""
    paths = glob.glob(os.path.join(repo_root, glob_pattern))
    paths.sort(key=os.path.getmtime, reverse=True)
    for p in paths[keep:]:
        try:
            os.remove(p)
            print("Post-build: pruned {}".format(os.path.basename(p)))
        except OSError:
            pass


def _copy_firmware(source, target, env):
    """Post-build: copy firmware.bin to repo root as qt_stm_v<ver>.bin."""
    src_bin = str(target[0])
    version = _read_fw_version()
    repo_root = env["PROJECT_DIR"]
    dest = os.path.join(repo_root, "qt_stm_v{}.bin".format(version))
    shutil.copy2(src_bin, dest)
    print("Post-build: {} -> {}".format(os.path.basename(src_bin), os.path.basename(dest)))
    _prune_old_bins(repo_root, "qt_stm_v*.bin", keep=2)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _copy_firmware)
