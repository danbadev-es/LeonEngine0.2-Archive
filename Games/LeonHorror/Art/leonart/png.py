"""PNG files from numpy arrays, with nothing but the standard library."""

import os
import struct
import zlib

import numpy as np


def _chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def write(path, pixels):
    """
    pixels: uint8 array of shape (height, width, channels) with 1, 3 or 4 channels, or (height, width).
    Row 0 is written first, so it is the top row of the file.
    """
    pixels = np.asarray(pixels)
    if pixels.dtype != np.uint8:
        raise ValueError("png.write wants uint8, got %s" % pixels.dtype)
    if pixels.ndim == 2:
        pixels = pixels[:, :, None]
    height, width, channels = pixels.shape
    color_type = {1: 0, 3: 2, 4: 6}[channels]

    # Each row is prefixed with its filter type (0: none).
    rows = np.zeros((height, 1 + width * channels), dtype=np.uint8)
    rows[:, 1:] = pixels.reshape(height, width * channels)

    data = b"\x89PNG\r\n\x1a\n"
    data += _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0))
    data += _chunk(b"IDAT", zlib.compress(rows.tobytes(), 6))
    data += _chunk(b"IEND", b"")

    folder = os.path.dirname(os.path.abspath(path))
    os.makedirs(folder, exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data)


def to_bytes(values):
    """Floats in 0..1 to uint8, rounded."""
    return np.clip(np.asarray(values, dtype=np.float32) * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8)


def srgb_encode(linear):
    """Linear colour to the sRGB curve."""
    linear = np.clip(np.asarray(linear, dtype=np.float32), 0.0, 1.0)
    return np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.power(linear, 1.0 / 2.4) - 0.055)


def srgb_decode(encoded):
    """An sRGB value to linear; handy for writing colours as they look in a colour picker."""
    encoded = np.asarray(encoded, dtype=np.float32)
    return np.where(encoded <= 0.04045, encoded / 12.92, np.power((encoded + 0.055) / 1.055, 2.4))
