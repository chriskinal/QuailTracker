"""Pre-build script: convert src/web/index.html to src/web_data.h
   using xxd -i style output. Runs automatically before each build."""

import os
Import("env")

html_path = os.path.join("src", "web", "index.html")
header_path = os.path.join("src", "web_data.h")

if os.path.exists(html_path):
    with open(html_path, "rb") as f:
        data = f.read()

    # Build the header content
    lines = []
    lines.append("/* Auto-generated from src/web/index.html — do not edit */\n")
    lines.append("#ifndef WEB_DATA_H\n#define WEB_DATA_H\n\n")
    lines.append("static const unsigned char src_web_index_html[] = {\n")
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
    lines.append("};\n")
    lines.append(f"static const unsigned int src_web_index_html_len = {len(data)};\n\n")
    lines.append("#endif /* WEB_DATA_H */\n")
    new_content = "".join(lines)

    # Only write if content changed (avoids unnecessary recompiles)
    write = True
    if os.path.exists(header_path):
        with open(header_path, "r") as f:
            if f.read() == new_content:
                write = False

    if write:
        with open(header_path, "w") as f:
            f.write(new_content)
        print(f"Generated {header_path} ({len(data)} bytes from {html_path})")
else:
    print(f"WARNING: {html_path} not found, skipping web_data.h generation")
