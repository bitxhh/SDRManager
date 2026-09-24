#!/usr/bin/env python3
"""
SDRManager — signal classifier service.

Drop-in contract:
  1. Receive binary I/Q frames from SDRManager (see FRAME FORMAT below).
  2. Classify the modulation type.
  3. Send back a JSON result on the same TCP connection.

To plug in a real model, replace the classify() function.
Dependencies for the stub: none (stdlib only).
Dependencies for a real model: torch, numpy (or tflite, onnxruntime, etc.)

FRAME FORMAT, protocol v3 (little-endian; v1 and v2 are still accepted).
Must match DSP/ClassifierHandler.h on the C++ side.
  [4B uint32  payload_length  — length of everything after these 4 bytes]
  --- payload ---
  [2B uint16  version         — protocol version, currently 3]
  [2B uint16  header_len      — header bytes incl. version/header_len
                                (24 in v1, 40 in v2, 44 in v3); I/Q starts at this offset]
  [8B uint64  timestamp       — hardware sample counter from LimeSuite]
  [4B int32   sample_count N  — number of complex samples]
  [8B float64 sample_rate_hz  — samples per second of the I/Q below]
  [8B float64 vfo_offset_hz   — v2: channel offset from RX centre (0 = wideband)]
  [8B float64 bandwidth_hz    — v2: one-sided channel bandwidth (0 = whole band);
                                with bandwidth > 0 the I/Q is already shifted
                                to DC, filtered and decimated to this channel]
  [4B int32   slot            — v3: demodulator slot, echoed in the response]
  [N*2*4B float32 IQ pairs    — interleaved: I0, Q0, I1, Q1, ...]

RESPONSE FORMAT (newline-terminated JSON):
  {"type": "FM", "confidence": 0.95, "timestamp": 12345, "slot": 0}
  On a malformed frame or unsupported version:
  {"error": "unsupported protocol version 4"}

Supported type strings (extend as needed):
  FM, AM, CW, USB, LSB, NFM, Unknown
"""

import array
import collections
import socket
import struct
import json
import random
import sys

HOST = "127.0.0.1"
PORT = 52001

PROTOCOL_VERSION = 3
SUPPORTED_VERSIONS = (1, 2, 3)

# Header inside the payload (after the 4-byte length prefix)
# Format: version (uint16) + header_len (uint16) + timestamp (uint64)
#         + count (int32) + sample_rate (float64)
#         [v2: + vfo_offset (float64) + bandwidth (float64)]
#         [v3: + slot (int32)]
_HDR    = struct.Struct("<HHQid")     # v1: 2 + 2 + 8 + 4 + 8 = 24 bytes
_HDR_V2 = struct.Struct("<HHQiddd")   # v2: 24 + 8 + 8 = 40 bytes
_HDR_V3 = struct.Struct("<HHQidddi")  # v3: 40 + 4 = 44 bytes

Frame = collections.namedtuple(
    "Frame", "timestamp sample_rate iq vfo_offset_hz bandwidth_hz slot")
_KNOWN_TYPES = ["FM", "AM", "CW", "USB", "LSB", "NFM"]


# ---------------------------------------------------------------------------
# Replace this function with real model inference.
# iq_samples: float32 sequence, interleaved I/Q  [I0, Q0, I1, Q1, ...]
#             (array.array('f'); np.frombuffer(iq_samples, dtype=np.float32)
#             gives a zero-copy numpy view)
# sample_rate: float, Hz
# Returns: (type_string, confidence_0_to_1)
# ---------------------------------------------------------------------------
def classify(iq_samples: array.array, sample_rate: float) -> tuple[str, float]:
    """Stub: returns a random result.  Replace with actual model."""
    return random.choice(_KNOWN_TYPES), round(random.uniform(0.5, 0.99), 3)


# ---------------------------------------------------------------------------
# Protocol helpers
# ---------------------------------------------------------------------------
class FrameError(ValueError):
    pass


def parse_frame(payload: bytes) -> Frame:
    """Parse a frame payload (everything after the 4-byte length prefix).

    Returns a Frame; iq is array('f') of 2*N floats. v1 frames get
    vfo_offset_hz = bandwidth_hz = 0 (wideband); v1/v2 frames get slot = 0.
    Raises FrameError on malformed input or an unsupported version.
    """
    if len(payload) < 4:
        raise FrameError(f"frame too short ({len(payload)} bytes)")
    version, header_len = struct.unpack_from("<HH", payload, 0)
    if version not in SUPPORTED_VERSIONS:
        raise FrameError(f"unsupported protocol version {version}")
    hdr = _HDR_V3 if version >= 3 else _HDR_V2 if version == 2 else _HDR
    if header_len < hdr.size or len(payload) < header_len:
        raise FrameError(f"bad header length {header_len}")
    if version >= 3:
        _, _, timestamp, count, sample_rate, vfo_offset, bandwidth, slot = \
            hdr.unpack_from(payload, 0)
    elif version == 2:
        _, _, timestamp, count, sample_rate, vfo_offset, bandwidth = \
            hdr.unpack_from(payload, 0)
        slot = 0
    else:
        _, _, timestamp, count, sample_rate = hdr.unpack_from(payload, 0)
        vfo_offset, bandwidth, slot = 0.0, 0.0, 0
    if count < 0:
        raise FrameError(f"negative sample count {count}")

    expected = count * 2 * 4   # count complex samples x 2 floats x 4 bytes
    iq_bytes = payload[header_len:header_len + expected]
    if len(iq_bytes) < expected:
        raise FrameError(f"IQ data too short ({len(iq_bytes)} < {expected})")
    iq = array.array("f")
    iq.frombytes(iq_bytes)
    if sys.byteorder != "little":
        iq.byteswap()
    return Frame(timestamp, sample_rate, iq, vfo_offset, bandwidth, slot)


def _recv_exact(conn: socket.socket, n: int) -> bytes | None:
    """Read exactly n bytes, return None on EOF."""
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def _handle(conn: socket.socket) -> None:
    print("[classifier] Client connected", flush=True)
    while True:
        # 1. Read 4-byte length prefix
        raw_len = _recv_exact(conn, 4)
        if raw_len is None:
            break
        payload_len = struct.unpack("<I", raw_len)[0]

        # 2. Read payload
        payload = _recv_exact(conn, payload_len)
        if payload is None:
            break

        # 3. Parse header + IQ samples, classify
        try:
            frame = parse_frame(payload)
        except FrameError as e:
            print(f"[classifier] Bad frame: {e}", flush=True)
            result = {"error": str(e)}
        else:
            mod_type, confidence = classify(frame.iq, frame.sample_rate)
            result = {
                "type":       mod_type,
                "confidence": confidence,
                "timestamp":  frame.timestamp,
                "slot":       frame.slot,
            }

        # 4. Send result
        try:
            conn.sendall((json.dumps(result) + "\n").encode())
        except OSError:
            break

    print("[classifier] Client disconnected", flush=True)


# ---------------------------------------------------------------------------
# Server loop
# ---------------------------------------------------------------------------
def main() -> None:
    print(f"[classifier] Listening on {HOST}:{PORT}", flush=True)
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            srv.bind((HOST, PORT))
        except OSError as e:
            print(f"[classifier] bind failed: {e}", flush=True)
            sys.exit(1)
        srv.listen(1)

        while True:
            conn, addr = srv.accept()
            with conn:
                _handle(conn)
            print("[classifier] Waiting for next connection…", flush=True)


if __name__ == "__main__":
    main()
