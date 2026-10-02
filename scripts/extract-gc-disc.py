#!/usr/bin/env python3
"""Extracts the files of a GameCube disc image (ISO/GCM) into a folder.

    scripts/extract-gc-disc.py DISC.iso OUTPUT_DIR

Used to set up games/pikmin2/assets for the Pikmin 2 port, which reads the
disc's files from there; Pikmin 1 has its own installer in nectar-launcher.
Only uncompressed images are read: convert RVZ/WIA/GCZ to ISO with Dolphin
first. Names on the disc that are not UTF-8 (Pikmin 2 carries a few in
Shift-JIS) are decoded so that every file system, including macOS's, accepts
them. The image itself is only read.
"""
import os
import struct
import sys

GAMECUBE_MAGIC = 0xC2339F3D


def decode_name(raw):
    for encoding in ("utf-8", "shift_jis"):
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError:
            pass
    return raw.decode("latin-1")


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    image, output = sys.argv[1], sys.argv[2]
    with open(image, "rb") as disc:
        header = disc.read(0x440)
        if len(header) < 0x440 or struct.unpack(">I", header[0x1C:0x20])[0] != GAMECUBE_MAGIC:
            print(f"{image} is not an uncompressed GameCube disc image", file=sys.stderr)
            return 1
        game_id = header[0:6].decode("ascii", "replace")
        fst_offset, fst_size = struct.unpack(">II", header[0x424:0x42C])
        disc.seek(fst_offset)
        fst = disc.read(fst_size)
        count = struct.unpack(">I", fst[8:12])[0]
        strings = fst[count * 12:]

        os.makedirs(output, exist_ok=True)
        directories = [(count, output)]  # (index where the directory ends, path)
        files = 0
        for index in range(1, count):
            while index >= directories[-1][0]:
                directories.pop()
            word, offset, size = struct.unpack(">III", fst[index * 12:index * 12 + 12])
            name_start = word & 0xFFFFFF
            name = decode_name(strings[name_start:strings.index(b"\0", name_start)])
            if name in ("", ".", "..") or "/" in name:
                print(f"unsafe name in the disc's file table: {name!r}", file=sys.stderr)
                return 1
            path = os.path.join(directories[-1][1], name)
            if word >> 24:  # directory: `size` is the index of its end
                os.makedirs(path, exist_ok=True)
                directories.append((size, path))
                continue
            disc.seek(offset)
            remaining = size
            with open(path, "wb") as out:
                while remaining:
                    chunk = disc.read(min(remaining, 1 << 20))
                    if not chunk:
                        print(f"{image} ends before {name}: the image is truncated", file=sys.stderr)
                        return 1
                    out.write(chunk)
                    remaining -= len(chunk)
            files += 1
    print(f"{game_id}: {files} files extracted to {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
