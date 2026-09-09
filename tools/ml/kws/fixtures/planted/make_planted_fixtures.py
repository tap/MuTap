#!/usr/bin/env python3
"""make_planted_fixtures — the planted-violation locks verify_splits.py must reject by rule name.

One clean synthetic lock (`clean_lock`) exercises every person-key convention
of verify_splits.py's docstring — Speech Commands, MUSAN noise and music,
SLR28, Common Voice + MSWC client ids, Piper voice + speaker ids, AMI
participants, FMA artists and an M4c hold-out with one mixed take — and
passes all five rules. Each planted fixture is that lock with one leak
written into it (`PLANTED`), so a fixture is rejected by exactly one rule.
The locks are hand-built with kws_manifest.Lock and written by write_lock;
no audio exists behind them, and the pcm_sha256 of a clip is the sha256 of
its id (64 hex characters, unique per clip, equal only where a leak is
planted).

    python3 tools/ml/kws/fixtures/planted/make_planted_fixtures.py [--out DIR]

writes `<Rn-name>/lock.json` + `<Rn-name>/expect.json` per fixture (default:
beside this script). Re-running is byte-identical; test_verify_splits.py
checks the committed files against this script.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import pathlib
import sys
from typing import Any

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from kws_manifest import Clip, Lock, eval_set_id, write_lock  # noqa: E402

HERE = pathlib.Path(__file__).resolve().parent

# Clip ids the planting functions refer to.
POS_TRAIN = "sc/marvin/aaaa_nohash_0"
POS_DEV = "sc/marvin/bbbb_nohash_0"
NEG_TRAIN = "sc/house/aaaa_nohash_0"
NEG_DEV = "sc/house/bbbb_nohash_0"
NOISE_TRAIN = "musan_noise/noise/free-sound/noise-free-sound-0001"
NOISE_DEV = "musan_noise/noise/free-sound/noise-free-sound-0002"
NOISE_EVAL = "musan_noise/noise/free-sound/noise-free-sound-0003"
RIR_TRAIN = "slr28/simulated_rirs/smallroom/Room001/Room001-00001"
RIR_DEV = "slr28/simulated_rirs/mediumroom/Room002/Room002-00001"
MUSAN_MUSIC_TRAIN = "musan_music/music/fma/music-fma-0001"
FMA_TRAIN = "fma/fma_full/000/000123"
FMA_EVAL = "fma/fma_full/000/000456"
CV_A0, CV_A1 = "cv/clips/common_voice_en_100", "cv/clips/common_voice_en_101"
MSWC_A = "mswc/en/clips/house/common_voice_en_100__house"
TTS_12_POS, TTS_12_NEG = "piper/libritts/12/pos-0001", "piper/libritts/12/neg-0001"
AMI_TRAIN_A, AMI_TRAIN_B = "ami/ES2002a/seg-0001", "ami/ES2002b/seg-0001"
HOLDOUT_UTT = "holdout/talker-01/room-1m/utt-0001"


def _sha(clip_id: str) -> str:
    return hashlib.sha256(clip_id.encode()).hexdigest()


def _clip(clip_id: str, source: str, material: str, split: str, share: str, label: int | None, key: str,
          length: int = 16000, endpoint: int | None = None, speaker: str | None = None,
          extra: dict[str, Any] | None = None, variant: int = 0, draw: dict[str, Any] | None = None) -> Clip:
    sha = _sha(clip_id) if variant == 0 else ""  # an augmented row is not a decoded source file
    return Clip(id=clip_id, source=source, material=material, split=split, share=share, label=label,
                endpoint_sample=endpoint, length=length, key=key, pcm_sha256=sha, variant=variant, draw=draw,
                speaker=speaker, extra=dict(extra or {}))


def clean_lock() -> Lock:
    """A synthetic lock, every convention filled in, no leak: verify_splits passes all five rules over it."""
    c: list[Clip] = []
    # Speech Commands v2 (speech; key = speaker hash; the official split).
    c.append(_clip(POS_TRAIN, "sc", "speech", "train", "train", 1, "aaaa", endpoint=9600))
    c.append(_clip(POS_DEV, "sc", "speech", "dev", "dev", 1, "bbbb", endpoint=9800))
    c.append(_clip("sc/marvin/cccc_nohash_0", "sc", "speech", "eval", "eval-speech", 1, "cccc",
                   endpoint=9500))
    c.append(_clip(NEG_TRAIN, "sc", "speech", "train", "train", 0, "aaaa"))
    c.append(_clip(NEG_DEV, "sc", "speech", "dev", "dev", 0, "bbbb"))
    c.append(_clip("sc/house/cccc_nohash_0", "sc", "speech", "eval", "eval-speech", 0, "cccc"))
    # MUSAN noise (key = file id): train, dev, and the eval-noise share the hold-out mixes from.
    c.append(_clip(NOISE_TRAIN, "musan_noise", "noise", "train", "train", None, "noise-free-sound-0001",
                   length=48000))
    c.append(_clip(NOISE_DEV, "musan_noise", "noise", "dev", "dev", None, "noise-free-sound-0002",
                   length=48000))
    c.append(_clip(NOISE_EVAL, "musan_noise", "noise", "eval", "eval-noise", None, "noise-free-sound-0003",
                   length=48000))
    # SLR28 simulated RIRs (key = room id): train and dev.
    c.append(_clip(RIR_TRAIN, "slr28", "rir", "train", "train", None, "Room001", length=4000))
    c.append(_clip(RIR_DEV, "slr28", "rir", "dev", "dev", None, "Room002", length=6000))
    # Music, its own split axis keyed by artist: MUSAN music (ANNOTATIONS artist) and FMA (CC BY tracks).
    c.append(_clip(MUSAN_MUSIC_TRAIN, "musan_music", "music", "train", "aug", None, "Ann Artist",
                   length=32000, extra={"artist": "Ann Artist", "track": "Morning"}))
    c.append(_clip("musan_music/music/fma/music-fma-0002", "musan_music", "music", "eval", "eval-music", None,
                   "Bo Band", length=32000, extra={"artist": "Bo Band", "track": "Evening"}))
    c.append(_clip(FMA_TRAIN, "fma", "music", "train", "aug", 0, "Ann Artist", length=32000,
                   extra={"artist": "Ann Artist", "track": "Noon", "licence": "CC BY 4.0"}))
    c.append(_clip(FMA_EVAL, "fma", "music", "eval", "eval-music", 0, "Cy Choir", length=32000,
                   extra={"artist": "Cy Choir", "track": "Night", "licence": "CC0 1.0"}))
    # Common Voice (key = client_id) and MSWC joined to it by filename; one unjoinable MSWC clip in train.
    c.append(_clip(CV_A0, "cv", "speech", "train", "train", 0, "clientA", extra={"client_id": "clientA"}))
    c.append(_clip(CV_A1, "cv", "speech", "train", "train", 0, "clientA", extra={"client_id": "clientA"}))
    c.append(_clip("cv/clips/common_voice_en_200", "cv", "speech", "dev", "dev", 0, "clientB",
                   extra={"client_id": "clientB"}))
    c.append(_clip("cv/clips/common_voice_en_300", "cv", "speech", "eval", "eval-speech", 0, "clientC",
                   extra={"client_id": "clientC"}))
    c.append(_clip(MSWC_A, "mswc", "speech", "train", "train", 0, "clientA",
                   extra={"client_id": "clientA", "keyword": "house"}))
    c.append(_clip("mswc/en/clips/house/common_voice_en_999__house", "mswc", "speech", "train", "train", 0,
                   "common_voice_en_999", extra={"client_id": None, "unjoinable": True, "keyword": "house"}))
    # Piper TTS: (voice, speaker id) is the person key; positives and TTS negatives share it.
    lessac = {"voice": "en_US-lessac-medium", "voice_sha256": _sha("lessac")}
    libritts = {"voice": "en_US-libritts-high", "voice_sha256": _sha("libritts")}
    c.append(_clip("piper/lessac/0/pos-0001", "piper", "tts", "train", "train", 1, "en_US-lessac-medium/0",
                   endpoint=12000, speaker="0", extra=lessac))
    c.append(_clip("piper/lessac/0/neg-0001", "piper", "tts", "train", "train", 0, "en_US-lessac-medium/0",
                   speaker="0", extra=lessac))
    c.append(_clip(TTS_12_POS, "piper", "tts", "dev", "dev", 1, "en_US-libritts-high/12", endpoint=12000,
                   speaker="12", extra=libritts))
    c.append(_clip(TTS_12_NEG, "piper", "tts", "dev", "dev", 0, "en_US-libritts-high/12", speaker="12",
                   extra=libritts))
    c.append(_clip("piper/libritts/30/pos-0001", "piper", "tts", "eval", "eval-tts", 1,
                   "en_US-libritts-high/30", endpoint=12000, speaker="30", extra=libritts))
    c.append(_clip("piper/libritts/30/neg-0001", "piper", "tts", "eval", "eval-tts", 0,
                   "en_US-libritts-high/30", speaker="30", extra=libritts))
    # AMI: participants recur across meetings; a meeting is placed whole.
    c.append(_clip(AMI_TRAIN_A, "ami", "speech", "train", "train", 0, "ES2002a", length=160000,
                   extra={"participants": ["FEE005", "MEE006"], "meeting": "ES2002a"}))
    c.append(_clip(AMI_TRAIN_B, "ami", "speech", "train", "train", 0, "ES2002b", length=160000,
                   extra={"participants": ["FEE005", "MEE006", "MEE007"], "meeting": "ES2002b"}))
    c.append(_clip("ami/IS1000a/seg-0001", "ami", "speech", "dev", "dev", 0, "IS1000a", length=160000,
                   extra={"participants": ["FIE081", "MIE082"], "meeting": "IS1000a"}))
    # The M4c hold-out: a dry take and one mixed take drawn from the eval-noise share.
    c.append(_clip(HOLDOUT_UTT, "holdout", "holdout", "eval", "holdout", 1, "talker-01", endpoint=14000,
                   speaker="talker-01", extra={"distance_m": 1.0}))
    c.append(_clip(HOLDOUT_UTT, "holdout", "holdout", "eval", "holdout", 1, "talker-01", endpoint=14000,
                   speaker="talker-01", extra={"distance_m": 1.0}, variant=1,
                   draw={"noise": NOISE_EVAL, "level_db": -20.0}))
    # Augmented draws: a train positive from the train pools, a dev positive from the dev pools.
    c.append(_clip(POS_TRAIN, "sc", "speech", "train", "train", 1, "aaaa", endpoint=9600 + 1800, variant=1,
                   draw={"gain_db": -2.0, "snr_db": 8.0, "noise": NOISE_TRAIN, "speed": 1.0, "rir": RIR_TRAIN,
                         "rir_delay": 200, "context": [NEG_TRAIN], "offset": 1600}))
    c.append(_clip(POS_DEV, "sc", "speech", "dev", "dev", 1, "bbbb", endpoint=9800 + 1900, variant=1,
                   draw={"gain_db": 1.0, "snr_db": 12.0, "noise": NOISE_DEV, "speed": 1.0, "rir": RIR_DEV,
                         "rir_delay": 300, "context": [NEG_DEV], "offset": 1600}))
    c.sort(key=lambda k: (k.id, k.variant))
    shares = ("eval-speech", "eval-music", "eval-noise", "eval-tts")
    return Lock(manifest_hash=_sha("planted: no manifest stands behind these locks"), clips=c, shards=[],
                summary={}, class_balance=[], eval_set_id={s: eval_set_id(c, s) for s in shares},
                holdout_set_id=None,
                toolchain={"note": "synthetic lock from fixtures/planted/make_planted_fixtures.py; no audio"},
                self_check={})


def _find(lock: Lock, clip_id: str, variant: int = 0) -> Clip:
    for c in lock.clips:
        if c.id == clip_id and c.variant == variant:
            return c
    raise KeyError(clip_id)


def plant_r1(lock: Lock) -> list[str]:
    """Client A has clips in train and one moved to dev."""
    c = _find(lock, CV_A1)
    c.split, c.share = "dev", "dev"
    return [CV_A0, CV_A1, MSWC_A]


def plant_r2_pcm(lock: Lock) -> list[str]:
    """A dev clip whose decoded PCM is byte-identical to a train clip's."""
    _find(lock, NEG_DEV).pcm_sha256 = _find(lock, NEG_TRAIN).pcm_sha256
    return [NEG_TRAIN, NEG_DEV]


def plant_r2_holdout(lock: Lock) -> list[str]:
    """The hold-out mixed take draws a train noise file (R2's hold-out half, planted as a lock row)."""
    _find(lock, HOLDOUT_UTT, variant=1).draw["noise"] = NOISE_TRAIN
    return [HOLDOUT_UTT, NOISE_TRAIN]


def plant_r3(lock: Lock) -> list[str]:
    """libritts speaker 12's negative lands in train while its positive stays in dev."""
    c = _find(lock, TTS_12_NEG)
    c.split, c.share = "train", "train"
    return [TTS_12_POS, TTS_12_NEG]


def plant_r4(lock: Lock) -> list[str]:
    """A dev meeting whose participant MEE006 also speaks in two train meetings."""
    extra = {"participants": ["MEE006", "FIE081"], "meeting": "IS1000b"}
    lock.clips.append(_clip("ami/IS1000b/seg-0001", "ami", "speech", "dev", "dev", 0, "IS1000b",
                            length=160000, extra=extra))
    lock.clips.sort(key=lambda k: (k.id, k.variant))
    return [AMI_TRAIN_A, AMI_TRAIN_B, "ami/IS1000b/seg-0001"]


def plant_r5(lock: Lock) -> list[str]:
    """An eval-music track by the train artist, spelled differently (the artist key is normalized)."""
    c = _find(lock, FMA_EVAL)
    c.extra["artist"] = "ANN  artist"
    c.key = "ANN  artist"
    return [FMA_TRAIN, FMA_EVAL, MUSAN_MUSIC_TRAIN]


PLANTED = {  # fixture directory -> (rule, planting function)
    "R1-client-id": ("R1", plant_r1),
    "R2-pcm-sha256": ("R2", plant_r2_pcm),
    "R2-holdout-noise": ("R2", plant_r2_holdout),
    "R3-tts-speaker": ("R3", plant_r3),
    "R4-ami-participant": ("R4", plant_r4),
    "R5-music-artist": ("R5", plant_r5),
}


def planted_locks() -> dict[str, tuple[str, Lock, list[str]]]:
    """Every planted fixture as (rule, lock, the planted clip ids), built fresh from the clean lock."""
    out = {}
    for name, (rule, plant) in PLANTED.items():
        lock = copy.deepcopy(clean_lock())
        clips = plant(lock)
        out[name] = (rule, lock, sorted(clips))
    return out


def write_fixtures(root: pathlib.Path) -> list[pathlib.Path]:
    written = []
    for name, (rule, lock, clips) in planted_locks().items():
        d = root / name
        d.mkdir(parents=True, exist_ok=True)
        write_lock(lock, d / "lock.json")
        (d / "expect.json").write_text(json.dumps({"rule": rule, "clips": clips}, indent=1) + "\n",
                                      encoding="utf-8")
        written += [d / "lock.json", d / "expect.json"]
    return written


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", type=pathlib.Path, default=HERE,
                    help="where to write the fixtures (default: beside this script)")
    args = ap.parse_args(argv)
    for p in write_fixtures(args.out):
        print(p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
