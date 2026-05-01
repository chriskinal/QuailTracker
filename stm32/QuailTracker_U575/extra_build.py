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


def _copy_firmware(source, target, env):
    """Post-build: copy firmware.bin to repo root as qt_stm_v<ver>.bin."""
    src_bin = str(target[0])
    version = _read_fw_version()
    dest = os.path.join(env["PROJECT_DIR"], "qt_stm_v{}.bin".format(version))
    shutil.copy2(src_bin, dest)
    print("Post-build: {} -> {}".format(os.path.basename(src_bin), os.path.basename(dest)))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _copy_firmware)
