#!/usr/bin/env python3
"""Repair QuailTracker FLAC metadata for strict decoders.

Firmware <= 0.10.6 reserves a fixed-size second metadata block (FLAC_META2_SIZE,
512 bytes) so PPS_* values can be backfilled at recording stop without shifting
the audio. But it declared the whole 508-byte payload as VORBIS_COMMENT while
only ~371 bytes are real tags, leaving the slack as zeros *inside* the comment
block. Lenient tools (metaflac, NAudio.Flac) ignore it, but strict decoders
(flac 1.5+, libFLAC, libsndfile/soundfile, BirdNET-Analyzer) reject the file
with "reserved fields in use" (FLAC__STREAM_DECODER_ERROR_STATUS_BAD_METADATA).

This rewrites each file's VORBIS_COMMENT block into a correctly-sized comment
block followed by a separate PADDING block, occupying the exact same byte range
(so audio frame offsets are unchanged). Only metadata bytes are touched; audio
frames and tag content are left intact. Fixed in firmware 0.10.7 — this is for
files already recorded with older firmware.

Usage:
    repair_flac_metadata.py SRC [DST]      # copy SRC dir -> DST, repair copies
    repair_flac_metadata.py file.flac      # repair a single file IN PLACE
    repair_flac_metadata.py SRC --in-place # repair every .flac in SRC IN PLACE

With a DST directory the originals are never modified (recommended for
irreplaceable field data). Pass --verify (default on if `flac` is installed) to
strict-decode each result.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys

VORBIS_COMMENT = 4
PADDING = 1


def _find_vorbis_block(path):
    """Locate the VORBIS_COMMENT block.

    Returns (header_offset, is_last_bit, declared_len, real_content_len) or a
    ('reason', ...) tuple describing why it can't/shouldn't be repaired.
    """
    with open(path, "rb") as f:
        if f.read(4) != b"fLaC":
            return ("not-flac",)
        p = 4
        while True:
            hdr = f.read(4)
            if len(hdr) < 4:
                return ("no-vorbis-comment",)
            last = hdr[0] >> 7
            btype = hdr[0] & 0x7F
            blen = (hdr[1] << 16) | (hdr[2] << 8) | hdr[3]
            if btype == VORBIS_COMMENT:
                data = f.read(blen)
                if len(data) < blen:
                    return ("truncated-block",)
                try:
                    q = 0
                    vlen = struct.unpack_from("<I", data, q)[0]; q += 4 + vlen
                    n = struct.unpack_from("<I", data, q)[0]; q += 4
                    for _ in range(n):
                        cl = struct.unpack_from("<I", data, q)[0]; q += 4 + cl
                except struct.error:
                    return ("unparseable-comments",)
                return (p, last, blen, q)
            f.seek(blen, 1)
            p += 4 + blen
            if last:
                return ("no-vorbis-comment",)


def repair_file(path):
    """Rewrite path's VORBIS_COMMENT in place. Returns a status string."""
    info = _find_vorbis_block(path)
    if len(info) == 1:
        return "skip: " + info[0]
    off, last, decl_len, content_len = info
    if content_len >= decl_len:
        return "ok: already exact (no slack)"
    if content_len > decl_len - 4:
        return f"skip: {decl_len - content_len}B slack, no room for PADDING header"
    pad_len = decl_len - content_len - 4
    with open(path, "r+b") as f:
        # VORBIS_COMMENT header: is_last=0, type=4, length=content_len.
        # The comment payload at [off+4 : off+4+content_len] is left untouched.
        f.seek(off)
        f.write(bytes([VORBIS_COMMENT,
                       (content_len >> 16) & 0xFF,
                       (content_len >> 8) & 0xFF,
                       content_len & 0xFF]))
        # PADDING header right after the comment content; keep the original
        # last-block flag so block ordering is preserved.
        f.seek(off + 4 + content_len)
        f.write(bytes([(last << 7) | PADDING,
                       (pad_len >> 16) & 0xFF,
                       (pad_len >> 8) & 0xFF,
                       pad_len & 0xFF]))
        f.write(b"\x00" * pad_len)
    return f"repaired: VORBIS_COMMENT {decl_len}->{content_len}B + PADDING {pad_len}B"


def _have_flac():
    return shutil.which("flac") is not None


def _verify(path):
    """Strict full-file test-decode. Returns (ok, detail)."""
    r = subprocess.run(["flac", "-ts", path], capture_output=True, text=True)
    if r.returncode == 0:
        return True, "decode OK"
    last = (r.stderr.strip().splitlines() or [""])[-1]
    return False, last[:70]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", help="source .flac file or directory")
    ap.add_argument("dst", nargs="?", help="destination directory (copy, then repair)")
    ap.add_argument("--in-place", action="store_true",
                    help="repair the source file/dir directly (no copy)")
    ap.add_argument("--no-verify", action="store_true", help="skip strict decode check")
    args = ap.parse_args()

    verify = not args.no_verify and _have_flac()
    if not args.no_verify and not _have_flac():
        print("note: `flac` not found — skipping verification", file=sys.stderr)

    # Build the work list: [(target_path, label)]
    if os.path.isfile(args.src):
        if args.dst or not args.in_place:
            print("error: single file requires --in-place", file=sys.stderr)
            return 2
        targets = [(args.src, os.path.basename(args.src))]
    elif os.path.isdir(args.src):
        names = sorted(n for n in os.listdir(args.src) if n.lower().endswith(".flac"))
        if not names:
            print(f"no .flac files in {args.src}", file=sys.stderr)
            return 1
        if args.in_place:
            targets = [(os.path.join(args.src, n), n) for n in names]
        else:
            if not args.dst:
                print("error: directory needs a DST (or --in-place)", file=sys.stderr)
                return 2
            os.makedirs(args.dst, exist_ok=True)
            targets = []
            for n in names:
                d = os.path.join(args.dst, n)
                shutil.copy2(os.path.join(args.src, n), d)
                targets.append((d, n))
    else:
        print(f"error: {args.src} not found", file=sys.stderr)
        return 2

    where = "in place" if args.in_place else f"-> {args.dst}"
    print(f"Repairing {len(targets)} file(s) {where}\n")
    ok_count = 0
    for path, label in targets:
        status = repair_file(path)
        line = f"  {label:34s} {status:44s}"
        if verify:
            ok, detail = _verify(path)
            ok_count += ok
            line += "VERIFY " + detail
        print(line)
    if verify:
        print(f"\n{ok_count}/{len(targets)} decode cleanly in strict flac")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
