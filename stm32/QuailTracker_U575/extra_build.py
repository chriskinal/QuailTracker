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
