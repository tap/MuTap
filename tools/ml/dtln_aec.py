# SPDX-License-Identifier: MIT
"""DTLN-aec inference: block-streaming TF-lite, stateful, 16 kHz.

Processing loop adapted from ``run_aec.py`` of the DTLN-aec repository
(https://github.com/breizhn/DTLN-aec), Copyright (c) Nils L. Westhausen,
MIT license — the model of Westhausen & Meyer, "Acoustic echo cancellation
with the dual-signal transformation LSTM network" (ICASSP 2021). The
pretrained models are distributed in that repository under the same MIT
license; note they were trained on the Microsoft AEC-Challenge corpus,
whose components carry mixed licenses (parts academic-only) — fine for a
benchmark, not something to ship. See tools/ml/README.md.

The models are level-sensitive (trained on typical wav-file levels, well
below full scale); the benchmark's unit-RMS material is scaled down by
``in_scale`` on the way in and back up on the way out.
"""
from __future__ import annotations

import numpy as np

try:
    import tflite_runtime.interpreter as _tflite
except ImportError:  # pragma: no cover - fallback for full TF installs
    import tensorflow.lite as _tflite

BLOCK_LEN = 512
BLOCK_SHIFT = 128


class DtlnAec:
    """Stateful streaming wrapper around a DTLN-aec model pair."""

    def __init__(self, model_prefix: str, in_scale: float = 0.05):
        self._i1 = _tflite.Interpreter(model_path=model_prefix + "_1.tflite")
        self._i1.allocate_tensors()
        self._i2 = _tflite.Interpreter(model_path=model_prefix + "_2.tflite")
        self._i2.allocate_tensors()
        self._in1 = self._i1.get_input_details()
        self._out1 = self._i1.get_output_details()
        self._in2 = self._i2.get_input_details()
        self._out2 = self._i2.get_output_details()
        self._scale = in_scale
        self.name = "dtln-aec-" + model_prefix.rsplit("_", 1)[-1]

    def process(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        """x = far-end reference (loopback), y = microphone; returns e."""
        mic = np.asarray(y, dtype=np.float64) * self._scale
        lpb = np.asarray(x, dtype=np.float64) * self._scale
        n = len(mic)

        pad = np.zeros(BLOCK_LEN - BLOCK_SHIFT)
        mic = np.concatenate((pad, mic, pad))
        lpb = np.concatenate((pad, lpb, pad))

        states_1 = np.zeros(self._in1[1]["shape"], dtype=np.float32)
        states_2 = np.zeros(self._in2[1]["shape"], dtype=np.float32)
        out_file = np.zeros(len(mic))
        in_buffer = np.zeros(BLOCK_LEN, dtype=np.float32)
        in_buffer_lpb = np.zeros(BLOCK_LEN, dtype=np.float32)
        out_buffer = np.zeros(BLOCK_LEN, dtype=np.float32)

        num_blocks = (mic.shape[0] - (BLOCK_LEN - BLOCK_SHIFT)) // BLOCK_SHIFT
        for idx in range(num_blocks):
            i0 = idx * BLOCK_SHIFT
            in_buffer[:-BLOCK_SHIFT] = in_buffer[BLOCK_SHIFT:]
            in_buffer[-BLOCK_SHIFT:] = mic[i0 : i0 + BLOCK_SHIFT]
            in_buffer_lpb[:-BLOCK_SHIFT] = in_buffer_lpb[BLOCK_SHIFT:]
            in_buffer_lpb[-BLOCK_SHIFT:] = lpb[i0 : i0 + BLOCK_SHIFT]

            in_block_fft = np.fft.rfft(in_buffer).astype("complex64")
            in_mag = np.abs(in_block_fft).reshape(1, 1, -1).astype("float32")
            lpb_mag = np.abs(np.fft.rfft(in_buffer_lpb)).reshape(1, 1, -1).astype("float32")

            self._i1.set_tensor(self._in1[0]["index"], in_mag)
            self._i1.set_tensor(self._in1[2]["index"], lpb_mag)
            self._i1.set_tensor(self._in1[1]["index"], states_1)
            self._i1.invoke()
            out_mask = self._i1.get_tensor(self._out1[0]["index"])
            states_1 = self._i1.get_tensor(self._out1[1]["index"])

            estimated_block = np.fft.irfft(in_block_fft * out_mask)
            estimated_block = estimated_block.reshape(1, 1, -1).astype("float32")
            in_lpb = in_buffer_lpb.reshape(1, 1, -1).astype("float32")

            self._i2.set_tensor(self._in2[1]["index"], states_2)
            self._i2.set_tensor(self._in2[0]["index"], estimated_block)
            self._i2.set_tensor(self._in2[2]["index"], in_lpb)
            self._i2.invoke()
            out_block = self._i2.get_tensor(self._out2[0]["index"])
            states_2 = self._i2.get_tensor(self._out2[1]["index"])

            out_buffer[:-BLOCK_SHIFT] = out_buffer[BLOCK_SHIFT:]
            out_buffer[-BLOCK_SHIFT:] = 0.0
            out_buffer += np.squeeze(out_block)
            out_file[i0 : i0 + BLOCK_SHIFT] = out_buffer[:BLOCK_SHIFT]

        e = out_file[(BLOCK_LEN - BLOCK_SHIFT) : (BLOCK_LEN - BLOCK_SHIFT) + n]
        return e / self._scale
