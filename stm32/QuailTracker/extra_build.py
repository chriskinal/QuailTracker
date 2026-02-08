Import("env")

# Add FPU flags to linker and assembler (build_flags only applies to C/C++ compiler).
# The _bare.py framework script doesn't inject FPU settings, so we add them here.
env.Append(
    LINKFLAGS=[
        "-mfpu=fpv5-d16",
        "-mfloat-abi=hard",
    ],
    ASFLAGS=[
        "-mfpu=fpv5-d16",
        "-mfloat-abi=hard",
    ],
)
