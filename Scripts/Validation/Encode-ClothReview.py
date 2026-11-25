"""Encode the native fixed-step screenshot sequence as a 30 FPS MJPEG AVI."""

import argparse
import io
from pathlib import Path
import struct

from PIL import Image


FPS = 30


def chunk(tag, payload):
    return tag + struct.pack("<I", len(payload)) + payload + bytes(len(payload) % 2)


def encode(source, destination):
    frames = sorted(source.glob("[0-9][0-9][0-9][0-9].png"))
    if len(frames) < 150 or any(int(path.stem) != index for index, path in enumerate(frames, 1)):
        raise ValueError("Incomplete native motion capture")
    with Image.open(frames[0]) as first:
        width, height = first.size
    buffer_size = width * height * 3
    main = struct.pack(
        "<14I", round(1_000_000 / FPS), buffer_size * FPS, 0, 0x10, len(frames), 0,
        1, buffer_size, width, height, 0, 0, 0, 0,
    )
    stream = struct.pack(
        "<4s4sIHHIIIIIIIIhhhh", b"vids", b"MJPG", 0, 0, 0, 0, 1, FPS, 0,
        len(frames), buffer_size, 0xFFFFFFFF, 0, 0, 0, width, height,
    )
    bitmap = struct.pack("<IiiHH4sIiiII", 40, width, height, 1, 24, b"MJPG", buffer_size, 0, 0, 0, 0)
    headers = chunk(b"LIST", b"strl" + chunk(b"strh", stream) + chunk(b"strf", bitmap))
    with destination.open("xb") as video:
        video.write(b"RIFF\0\0\0\0AVI ")
        video.write(chunk(b"LIST", b"hdrl" + chunk(b"avih", main) + headers))
        movi = video.tell()
        video.write(b"LIST\0\0\0\0movi")
        index = bytearray()
        for path in frames:
            with Image.open(path) as frame:
                if frame.size != (width, height):
                    raise ValueError(f"Frame dimensions changed at {path.name}")
                jpeg = io.BytesIO()
                frame.convert("RGB").save(jpeg, "JPEG", quality=89, subsampling=0)
            encoded = jpeg.getvalue()
            index.extend(struct.pack("<4sIII", b"00dc", 0x10, video.tell() - movi - 8, len(encoded)))
            video.write(chunk(b"00dc", encoded))
        movi_size = video.tell() - movi - 8
        video.write(chunk(b"idx1", index))
        file_size = video.tell()
        video.seek(movi + 4)
        video.write(struct.pack("<I", movi_size))
        video.seek(4)
        video.write(struct.pack("<I", file_size - 8))
    print(f"{destination}: {width}x{height}, {len(frames)} frames, {len(frames) / FPS:.2f}s")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames", type=Path)
    parser.add_argument("video", type=Path)
    args = parser.parse_args()
    encode(args.frames, args.video)
