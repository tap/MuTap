#!/usr/bin/env python3
"""kws_manifest — the manifest (`sources[]`, `recipe`) and the build-emitted lock.

The manifest is the committed, versioned description of a dataset; its hash
(over `sources[]` and `recipe`, canonical JSON) names the feature directory
in the store. The lock is what a build emitted from it, with its fields
classified as identity (reproduced exactly by every rebuild) or derived
(reproduced exactly only on the M0 Mac in the pinned environment). See
README.md for the field tables.
"""
from __future__ import annotations

import dataclasses
import hashlib
import json
import pathlib
from typing import Any

from kws_features import KWS_FEATURES_VERSION, Geometry, assert_band_support

MANIFEST_VERSION = 1
LOCK_VERSION = 1

ROLES = ("train", "aug", "dev", "eval-speech", "eval-music", "eval-noise", "eval-tts", "holdout")
SPLITS = ("train", "dev", "eval")
SHARES = ("train", "aug", "dev", "eval-speech", "eval-music", "eval-noise", "eval-tts", "holdout")
MATERIALS = ("speech", "noise", "music", "rir", "tts", "keywords", "holdout")
KINDS = {  # ingestion adapter -> the material it yields
    "speech_commands_v2": "speech",
    "musan": None,  # per options.partition: noise | speech | music
    "openslr_28_simulated": "rir",
    "mswc_keywords": "keywords",
    "holdout": "holdout",
    # M4b: "common_voice", "ami", "fma", "piper"
}


class ManifestError(ValueError):
    """A manifest the builder refuses, with the reason."""


# ---------------------------------------------------------------- sources


@dataclasses.dataclass(frozen=True)
class Origin:
    url: str | None = None
    obtain: str | None = None  # the human step for a store-only source
    path: str | None = None    # repository-relative, for a committed source

    @property
    def in_repository(self) -> bool:
        return self.path is not None


@dataclasses.dataclass(frozen=True)
class Archive:
    file: str
    sha256: str
    size: int


@dataclasses.dataclass(frozen=True)
class Source:
    id: str
    kind: str
    release: str
    origin: Origin
    archive: Archive
    licence: str
    attribution: str
    terms_accepted: str
    terms_verified: str
    redistributable: bool
    roles: tuple[str, ...]
    pcm_exact: bool = False
    options: dict[str, Any] = dataclasses.field(default_factory=dict)

    @property
    def material(self) -> str:
        m = KINDS[self.kind]
        if m is None:
            m = self.options.get("partition")
        if m not in MATERIALS:
            raise ManifestError(f"source {self.id!r}: cannot determine its material (kind {self.kind}, options {self.options})")
        return m

    def to_dict(self) -> dict[str, Any]:
        d = dataclasses.asdict(self)
        d["origin"] = {k: v for k, v in d["origin"].items() if v is not None}
        d["roles"] = list(self.roles)
        return d

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Source":
        d = dict(d)
        origin = Origin(**d.pop("origin"))
        archive = Archive(**d.pop("archive"))
        roles = tuple(d.pop("roles"))
        return cls(origin=origin, archive=archive, roles=roles, **d)


# ---------------------------------------------------------------- recipe


@dataclasses.dataclass(frozen=True)
class LabelRule:
    trim_db: float = 40.0     # X: the last window above peak - X dB ends the keyword (measured at M4b)
    window_ms: float = 10.0
    tolerance_hops: int = 3   # T (measured at build from the trim rule's spread)


@dataclasses.dataclass(frozen=True)
class Resampler:
    name: str = "scipy.signal.resample_poly"
    window: tuple[str, float] = ("kaiser", 5.0)


@dataclasses.dataclass(frozen=True)
class SplitRule:
    salt: str = ""
    fractions: dict[str, float] = dataclasses.field(default_factory=lambda: {"train": 0.8, "dev": 0.1, "eval": 0.1})


@dataclasses.dataclass(frozen=True)
class AugmentPolicy:
    seed: int = 1
    draws_per_positive: int = 1     # K
    noisy_per_negative: int = 1     # M (plus one clean)
    dry_share: float = 0.2
    snr_db: tuple[float, float] = (0.0, 20.0)
    gain_db: tuple[float, float] = (-6.0, 6.0)
    speed: tuple[float, float] = (0.9, 1.1)
    rt60_s: tuple[float, float] = (0.2, 1.0)
    mic_model: str = "none"


@dataclasses.dataclass(frozen=True)
class Recipe:
    phrase: str
    near_miss: tuple[str, ...]
    geometry: Geometry
    stored_path: str = "log"                    # log | pcen
    log_mel_contract_version: int = 1
    kws_features_version: int = KWS_FEATURES_VERSION
    label: LabelRule = LabelRule()
    context_s: float = 2.0
    resampler: Resampler = Resampler()
    split: SplitRule = SplitRule()
    augment: AugmentPolicy = AugmentPolicy()
    exclusions: dict[str, Any] = dataclasses.field(default_factory=lambda: {"transcript_contains_phrase": True,
                                                                             "mswc_distance_0": True})
    mining: dict[str, Any] = dataclasses.field(default_factory=lambda: {"g2p": "espeak-ng", "max_distance": 2})
    tts: dict[str, Any] | None = None
    subsets: dict[str, Any] = dataclasses.field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        d = dataclasses.asdict(self)
        d["geometry"] = self.geometry.to_dict()
        d["near_miss"] = list(self.near_miss)
        d["resampler"] = {"name": self.resampler.name, "window": list(self.resampler.window)}
        for k in ("snr_db", "gain_db", "speed", "rt60_s"):
            d["augment"][k] = list(d["augment"][k])
        return d

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Recipe":
        d = dict(d)
        geometry = Geometry.from_dict(d.pop("geometry"))
        label = LabelRule(**d.pop("label", {}))
        r = d.pop("resampler", {})
        resampler = Resampler(name=r.get("name", Resampler.name), window=tuple(r.get("window", Resampler.window)))
        split = SplitRule(**d.pop("split", {}))
        a = dict(d.pop("augment", {}))
        for k in ("snr_db", "gain_db", "speed", "rt60_s"):
            if k in a:
                a[k] = tuple(a[k])
        augment = AugmentPolicy(**a)
        near_miss = tuple(d.pop("near_miss", ()))
        return cls(geometry=geometry, label=label, resampler=resampler, split=split, augment=augment,
                   near_miss=near_miss, **d)


# ---------------------------------------------------------------- manifest


@dataclasses.dataclass(frozen=True)
class Manifest:
    name: str
    sources: tuple[Source, ...]
    recipe: Recipe
    manifest_version: int = MANIFEST_VERSION
    path: pathlib.Path | None = None  # where it was loaded from (not part of the hash)

    def source(self, source_id: str) -> Source:
        for s in self.sources:
            if s.id == source_id:
                return s
        raise KeyError(source_id)

    def to_dict(self) -> dict[str, Any]:
        return {"manifest_version": self.manifest_version, "name": self.name,
                "sources": [s.to_dict() for s in self.sources], "recipe": self.recipe.to_dict()}

    @classmethod
    def from_dict(cls, d: dict[str, Any], path: pathlib.Path | None = None) -> "Manifest":
        return cls(name=d["name"], manifest_version=int(d.get("manifest_version", MANIFEST_VERSION)),
                   sources=tuple(Source.from_dict(s) for s in d["sources"]),
                   recipe=Recipe.from_dict(d["recipe"]), path=path)


def canonical_json(obj: Any) -> str:
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def manifest_hash(m: Manifest) -> str:
    """sha256 over the canonical JSON of sources[] and recipe — the feature directory's name."""
    return hashlib.sha256(canonical_json({"sources": [s.to_dict() for s in m.sources],
                                          "recipe": m.recipe.to_dict()}).encode()).hexdigest()


def validate(m: Manifest) -> None:
    """Refuse a manifest the builder cannot honour; every refusal names the source and the rule."""
    if m.manifest_version != MANIFEST_VERSION:
        raise ManifestError(f"manifest_version {m.manifest_version}, expected {MANIFEST_VERSION}")
    if not m.name:
        raise ManifestError("manifest has no name")
    seen: set[str] = set()
    for s in m.sources:
        if s.id in seen:
            raise ManifestError(f"duplicate source id {s.id!r}")
        seen.add(s.id)
        if s.kind not in KINDS:
            raise ManifestError(f"source {s.id!r}: unknown kind {s.kind!r} (known: {', '.join(KINDS)})")
        if (s.origin.url is None) == (s.origin.path is None):
            raise ManifestError(f"source {s.id!r}: origin needs exactly one of url or path")
        if s.origin.path is not None and (pathlib.PurePosixPath(s.origin.path).is_absolute() or ".." in s.origin.path.split("/")):
            raise ManifestError(f"source {s.id!r}: origin.path must be repository-relative, got {s.origin.path!r}")
        if s.origin.in_repository and not s.redistributable:
            raise ManifestError(f"source {s.id!r}: an origin inside the repository requires redistributable: true "
                                "(only redistributable audio may enter git)")
        if len(s.archive.sha256) != 64 or any(c not in "0123456789abcdef" for c in s.archive.sha256):
            raise ManifestError(f"source {s.id!r}: archive.sha256 must be 64 lowercase hex characters")
        if s.archive.size <= 0:
            raise ManifestError(f"source {s.id!r}: archive.size must be positive")
        if not s.licence:
            raise ManifestError(f"source {s.id!r}: licence is empty — every hour of audio is accounted for with a licence")
        if not s.attribution:
            raise ManifestError(f"source {s.id!r}: attribution text is empty")
        if not s.terms_verified:
            raise ManifestError(f"source {s.id!r}: terms_verified date is empty")
        bad = [r for r in s.roles if r not in ROLES]
        if bad or not s.roles:
            raise ManifestError(f"source {s.id!r}: roles {list(s.roles)} must be a non-empty subset of {list(ROLES)}")
        _ = s.material  # raises for an undeterminable material
    r = m.recipe
    if r.stored_path not in ("log", "pcen"):
        raise ManifestError(f"recipe.stored_path {r.stored_path!r} must be log or pcen")
    if r.kws_features_version != KWS_FEATURES_VERSION:
        raise ManifestError(f"recipe.kws_features_version {r.kws_features_version} != this module's {KWS_FEATURES_VERSION}")
    if not r.geometry.valid():
        raise ManifestError("recipe.geometry is not a valid log_mel geometry")
    assert_band_support(r.geometry)
    if abs(sum(r.split.fractions.values()) - 1.0) > 1e-9 or set(r.split.fractions) != set(SPLITS):
        raise ManifestError(f"recipe.split.fractions must cover {list(SPLITS)} and sum to 1, got {r.split.fractions}")
    if r.label.tolerance_hops < 0 or r.label.trim_db <= 0 or r.label.window_ms <= 0:
        raise ManifestError("recipe.label: trim_db and window_ms must be positive, tolerance_hops non-negative")
    a = r.augment
    if a.draws_per_positive < 1 or a.noisy_per_negative < 0 or not 0.0 <= a.dry_share <= 1.0:
        raise ManifestError("recipe.augment: draws_per_positive >= 1, noisy_per_negative >= 0, dry_share in [0, 1]")
    if not r.phrase:
        raise ManifestError("recipe.phrase is empty")


def load_manifest(path: str | pathlib.Path) -> Manifest:
    path = pathlib.Path(path)
    with path.open() as f:
        m = Manifest.from_dict(json.load(f), path=path)
    validate(m)
    return m


def save_manifest(m: Manifest, path: str | pathlib.Path) -> None:
    pathlib.Path(path).write_text(json.dumps(m.to_dict(), indent=1, ensure_ascii=False) + "\n")


# ---------------------------------------------------------------- the lock


@dataclasses.dataclass
class Clip:
    id: str                       # <source-id>/<relative path without extension>
    source: str
    material: str
    split: str                    # train | dev | eval
    share: str                    # one of SHARES
    label: int | None             # 1 positive, 0 negative, None for augmentation material
    endpoint_sample: int | None   # keyword end, 16 kHz sample index (positives)
    length: int                   # samples at 16 kHz
    key: str                      # the split key this clip was assigned by
    pcm_sha256: str               # sha256 of the decoded 16 kHz int16 samples
    variant: int = 0              # 0 = dry; 1.. = augmented draws
    draw: dict[str, Any] | None = None  # the resolved augmentation draw (identity)
    speaker: str | None = None    # TTS speaker id / talker pseudonym where one exists
    extra: dict[str, Any] = dataclasses.field(default_factory=dict)


@dataclasses.dataclass
class Shard:
    split: str
    file: str
    sha256: str
    frames: int
    clips: int


@dataclasses.dataclass
class Lock:
    manifest_hash: str
    clips: list[Clip]
    shards: list[Shard]
    summary: dict[str, Any]
    class_balance: list[dict[str, Any]]
    eval_set_id: dict[str, str]
    holdout_set_id: str | None
    toolchain: dict[str, Any]
    self_check: dict[str, Any]
    lock_version: int = LOCK_VERSION

    def to_dict(self) -> dict[str, Any]:
        return {"lock_version": self.lock_version, "manifest_hash": self.manifest_hash,
                "clips": [dataclasses.asdict(c) for c in self.clips],
                "shards": [dataclasses.asdict(s) for s in self.shards],
                "summary": self.summary, "class_balance": self.class_balance, "eval_set_id": self.eval_set_id,
                "holdout_set_id": self.holdout_set_id, "toolchain": self.toolchain, "self_check": self.self_check}

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Lock":
        return cls(lock_version=int(d["lock_version"]), manifest_hash=d["manifest_hash"],
                   clips=[Clip(**c) for c in d["clips"]], shards=[Shard(**s) for s in d["shards"]],
                   summary=d["summary"], class_balance=d["class_balance"], eval_set_id=d["eval_set_id"],
                   holdout_set_id=d.get("holdout_set_id"), toolchain=d["toolchain"], self_check=d["self_check"])


def write_lock(lock: Lock, path: str | pathlib.Path) -> None:
    pathlib.Path(path).write_text(json.dumps(lock.to_dict(), indent=1, sort_keys=True, ensure_ascii=False) + "\n")


def read_lock(path: str | pathlib.Path) -> Lock:
    with pathlib.Path(path).open() as f:
        return Lock.from_dict(json.load(f))


def eval_set_id(clips: list[Clip], share: str) -> str:
    """sha256 of the sorted clip ids of one eval share: a pure function of sources[] and recipe."""
    ids = sorted(c.id for c in clips if c.share == share and c.variant == 0)
    return hashlib.sha256("\n".join(ids).encode()).hexdigest()


# Identity fields reproduce exactly on every rebuild; everything else in the
# lock is derived. pcm_sha256 is identity only for sources flagged pcm_exact.
CLIP_IDENTITY_FIELDS = ("id", "source", "material", "split", "share", "label", "endpoint_sample", "length", "key",
                        "variant", "draw", "speaker", "extra")
SHARD_IDENTITY_FIELDS = ("split", "file", "frames", "clips")
LOCK_IDENTITY_FIELDS = ("manifest_hash", "summary", "class_balance", "eval_set_id", "holdout_set_id")


def compare_locks(expected: Lock, actual: Lock, pcm_exact_sources: set[str]) -> list[str]:
    """Every identity difference between two locks, as messages; empty means they agree."""
    diffs: list[str] = []
    for f in LOCK_IDENTITY_FIELDS:
        if getattr(expected, f) != getattr(actual, f):
            diffs.append(f"lock.{f}: expected {getattr(expected, f)!r}, got {getattr(actual, f)!r}")
    exp = {(c.id, c.variant): c for c in expected.clips}
    act = {(c.id, c.variant): c for c in actual.clips}
    for key in sorted(set(exp) - set(act)):
        diffs.append(f"clip {key[0]} variant {key[1]}: missing from the rebuild")
    for key in sorted(set(act) - set(exp)):
        diffs.append(f"clip {key[0]} variant {key[1]}: not in the expected lock")
    for key in sorted(set(exp) & set(act)):
        a, b = exp[key], act[key]
        for f in CLIP_IDENTITY_FIELDS:
            if getattr(a, f) != getattr(b, f):
                diffs.append(f"clip {key[0]} variant {key[1]}.{f}: expected {getattr(a, f)!r}, got {getattr(b, f)!r}")
        if a.source in pcm_exact_sources and a.pcm_sha256 != b.pcm_sha256:
            diffs.append(f"clip {key[0]} variant {key[1]}.pcm_sha256 (pcm_exact source): expected {a.pcm_sha256}, got {b.pcm_sha256}")
    exp_s = {(s.split, s.file): s for s in expected.shards}
    act_s = {(s.split, s.file): s for s in actual.shards}
    for key in sorted(set(exp_s) ^ set(act_s)):
        diffs.append(f"shard {key}: present on one side only")
    for key in sorted(set(exp_s) & set(act_s)):
        for f in SHARD_IDENTITY_FIELDS:
            if getattr(exp_s[key], f) != getattr(act_s[key], f):
                diffs.append(f"shard {key}.{f}: expected {getattr(exp_s[key], f)!r}, got {getattr(act_s[key], f)!r}")
    return diffs
