#!/usr/bin/env python3
"""
Decode a frame dumped by the 02_StillCaptureSerial example into an image.

The sketch prints the frame as base64 between marker lines:

    ---BEGIN FRAME 160 120 RGB565---
    <base64...>
    ---END FRAME---

Save the serial output to a file and run:

    python decode_frame.py frame.txt -o frame.png

Pillow is used when available, which gives you a PNG. Without it the script
falls back to writing a PPM/PGM, which every image viewer can open and which
needs no dependencies at all.

Part of the OV7670-ESP32-NonFIFO library. MIT licensed.
"""

import argparse
import base64
import re
import sys

BEGIN_RE = re.compile(r"---BEGIN FRAME (\d+) (\d+) (\w+)---")
END_MARK = "---END FRAME---"


def extract(path):
    """Pull (width, height, format, raw_bytes) out of a captured serial log."""
    width = height = None
    fmt = None
    chunks = []
    collecting = False

    with open(path, "r", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not collecting:
                m = BEGIN_RE.search(line)
                if m:
                    width, height, fmt = int(m.group(1)), int(m.group(2)), m.group(3)
                    collecting = True
                continue
            if END_MARK in line:
                break
            # Ignore anything that is not plausible base64, so stray log lines
            # interleaved by the serial monitor do not corrupt the frame.
            if re.fullmatch(r"[A-Za-z0-9+/=]+", line):
                chunks.append(line)

    if width is None:
        sys.exit("no '---BEGIN FRAME ...---' marker found in %s" % path)
    if not chunks:
        sys.exit("found the begin marker but no base64 data")

    raw = base64.b64decode("".join(chunks))
    return width, height, fmt, raw


def rgb565_to_rgb(raw, width, height, swap):
    """Expand big-endian RGB565 to a flat RGB888 bytearray."""
    expected = width * height * 2
    if len(raw) < expected:
        print("warning: short frame, %d of %d bytes - padding with black"
              % (len(raw), expected), file=sys.stderr)
        raw = raw + bytes(expected - len(raw))

    out = bytearray(width * height * 3)
    for i in range(width * height):
        hi, lo = raw[i * 2], raw[i * 2 + 1]
        if swap:
            hi, lo = lo, hi
        v = (hi << 8) | lo
        r5 = (v >> 11) & 0x1F
        g6 = (v >> 5) & 0x3F
        b5 = v & 0x1F
        # Replicate high bits downwards so 0x1F maps to 0xFF, not 0xF8.
        out[i * 3 + 0] = (r5 << 3) | (r5 >> 2)
        out[i * 3 + 1] = (g6 << 2) | (g6 >> 4)
        out[i * 3 + 2] = (b5 << 3) | (b5 >> 2)
    return out


def yuyv_to_rgb(raw, width, height):
    expected = width * height * 2
    if len(raw) < expected:
        raw = raw + bytes(expected - len(raw))

    out = bytearray(width * height * 3)
    for i in range(0, width * height, 2):
        y0, u, y1, v = raw[i * 2:i * 2 + 4]
        u -= 128
        v -= 128
        for j, y in enumerate((y0, y1)):
            r = y + ((91881 * v) >> 16)
            g = y - ((22554 * u + 46802 * v) >> 16)
            b = y + ((116130 * u) >> 16)
            p = (i + j) * 3
            out[p + 0] = max(0, min(255, r))
            out[p + 1] = max(0, min(255, g))
            out[p + 2] = max(0, min(255, b))
    return out


def write_image(path, width, height, pixels, gray):
    try:
        from PIL import Image
        mode = "L" if gray else "RGB"
        img = Image.frombytes(mode, (width, height), bytes(pixels))
        img.save(path)
        print("wrote %s (%dx%d, via Pillow)" % (path, width, height))
        return
    except ImportError:
        pass

    # No Pillow: emit a netpbm file instead and adjust the extension.
    magic, ext = (b"P5", ".pgm") if gray else (b"P6", ".ppm")
    if not path.lower().endswith(ext):
        path = path.rsplit(".", 1)[0] + ext
    with open(path, "wb") as fh:
        fh.write(b"%s\n%d %d\n255\n" % (magic, width, height))
        fh.write(bytes(pixels))
    print("wrote %s (%dx%d, netpbm - install Pillow for PNG output)"
          % (path, width, height))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="serial capture containing the frame markers")
    ap.add_argument("-o", "--output", default="frame.png", help="output image")
    ap.add_argument("--swap", action="store_true",
                    help="swap byte order (use if colours look wrong and the "
                         "image is otherwise correct)")
    ap.add_argument("--raw", metavar="FILE",
                    help="also write the undecoded sensor bytes here")
    args = ap.parse_args()

    width, height, fmt, raw = extract(args.input)
    print("frame: %dx%d %s, %d bytes" % (width, height, fmt, len(raw)))

    if args.raw:
        with open(args.raw, "wb") as fh:
            fh.write(raw)
        print("wrote raw bytes to %s" % args.raw)

    fmt = fmt.upper()
    gray = False
    if fmt == "RGB565":
        pixels = rgb565_to_rgb(raw, width, height, args.swap)
    elif fmt == "YUV422":
        pixels = yuyv_to_rgb(raw, width, height)
    elif fmt in ("GRAYSCALE", "BAYER"):
        expected = width * height
        pixels = raw[:expected] + bytes(max(0, expected - len(raw)))
        gray = True
    else:
        sys.exit("unsupported format %s" % fmt)

    write_image(args.output, width, height, pixels, gray)


if __name__ == "__main__":
    main()
