#!/usr/bin/env python3
"""kws_manifest — the manifest (`sources[]`, `recipe`) and the build-emitted lock.

The manifest is the committed, versioned description of a dataset; its hash
(over `sources[]` and `recipe`, canonical JSON) names the feature directory
in the store. The lock is what a build emitted from it, with its fields
classified as identity (reproduced exactly by every rebuild) or derived
(reproduced exactly only on the M0 Mac in the pinned environment). See
README.md for the field tables.

Every refusal is a `ManifestError` whose message names the source (or the
recipe field) and the rule; a document that does not match the schema —
an unknown or missing field anywhere — is refused the same way, never as a
bare TypeError/KeyError.
"""
from __future__ import annotations

import dataclasses
import hashlib
import json
import math
import pathlib
import re
from typing import Any

from kws_features import KWS_FEATURES_VERSION, Geometry, Pcen, assert_band_support

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
    "keyword_list": "keywords",  # a plain text file, one keyword per line (the toy fixture; M4b: MSWC)
    "mswc_keywords": "keywords",
    "piper": "tts",  # a pinned voice archive (.onnx + .json): no clips to list, `synth` produces them
    "holdout": "holdout",
    # M4b: "common_voice", "ami", "fma"
}
FIXTURE_PREFIX = "tools/ml/kws/fixtures/"  # every in-repository origin lives here (plan §6 M4 "Sources")
MIC_MODELS = ("none",)  # no microphone-path model is implemented at M4a; any other value is refused
MIN_CONTEXT_S = 2.0  # the plan's ">= 2 s of same-split negative material" (a rule, not a measurement)
_SEGMENT = re.compile(r"[A-Za-z0-9._-]+")


class ManifestError(ValueError):
    """A manifest the builder refuses, with the reason."""


def check_keys(cls: type, d: Any, where: str, error: type[ValueError] = ManifestError) -> None:
    """Refuse a dict whose keys are not exactly the dataclass's fields (unknown keys, missing required),
    raising `error` (ManifestError here; kws_holdout and kws_eval pass their own class — M5's additive
    change so the three modules share one schema-key check)."""
    if not isinstance(d, dict):
        raise error(f"{where}: expected an object, got {type(d).__name__}")
    fields = {f.name: f for f in dataclasses.fields(cls)}
    unknown = sorted(set(d) - set(fields))
    if unknown:
        raise error(f"{where}: unknown field(s) {unknown} (known: {sorted(fields)})")
    required = sorted(n for n, f in fields.items()
                      if f.default is dataclasses.MISSING and f.default_factory is dataclasses.MISSING
                      and n not in d)
    if required:
        raise error(f"{where}: missing required field(s) {required}")


_check_keys = check_keys   # the manifest's own call sites


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
        where = f"source {d.get('id', '?')!r}" if isinstance(d, dict) else "source"
        _check_keys(cls, d, where)
        d = dict(d)
        _check_keys(Origin, d["origin"], f"{where}.origin")
        _check_keys(Archive, d["archive"], f"{where}.archive")
        origin = Origin(**d.pop("origin"))
        archive = Archive(**d.pop("archive"))
        roles = d.pop("roles")
        if isinstance(roles, str) or not isinstance(roles, (list, tuple)):
            raise ManifestError(f"{where}.roles: expected a list of roles, got {roles!r}")
        return cls(origin=origin, archive=archive, roles=tuple(roles), **d)


# ---------------------------------------------------------------- recipe


@dataclasses.dataclass(frozen=True)
class LabelRule:
    trim_db: float = 40.0     # X: the last window above peak - X dB ends the keyword (measured at M4b)
    window_ms: float = 10.0
    # T: a target until M4b's build measures the trim rule's spread across the length-scale draws
    # (plan §6 M4 "Label data"); the card prints it as a manifest value, not a measurement
    tolerance_hops: int = 3


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


RANGE_FIELDS = ("snr_db", "gain_db", "speed", "rt60_s")


@dataclasses.dataclass(frozen=True)
class Recipe:
    phrase: str
    near_miss: tuple[str, ...]
    geometry: Geometry
    stored_path: str = "log"                    # log | pcen — must agree with geometry.pcen.enabled
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
        for k in RANGE_FIELDS:
            d["augment"][k] = list(d["augment"][k])
        return d

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Recipe":
        _check_keys(cls, d, "recipe")
        d = dict(d)
        geometry_doc = d.pop("geometry")
        _check_keys(Geometry, geometry_doc, "recipe.geometry")
        _check_keys(Pcen, geometry_doc.get("pcen") or {}, "recipe.geometry.pcen")
        try:
            geometry = Geometry.from_dict(geometry_doc)
        except (ValueError, TypeError) as e:
            raise ManifestError(f"recipe.geometry: {e}") from None
        label_doc = d.pop("label", {})
        _check_keys(LabelRule, label_doc, "recipe.label")
        label = LabelRule(**label_doc)
        r = d.pop("resampler", {})
        _check_keys(Resampler, r, "recipe.resampler")
        window = r.get("window", Resampler.window)
        if isinstance(window, str) or not isinstance(window, (list, tuple)):
            raise ManifestError(f"recipe.resampler.window: expected [name, beta], got {window!r}")
        resampler = Resampler(name=r.get("name", Resampler.name), window=tuple(window))
        split_doc = d.pop("split", {})
        _check_keys(SplitRule, split_doc, "recipe.split")
        split = SplitRule(**split_doc)
        a = dict(d.pop("augment", {}))
        _check_keys(AugmentPolicy, a, "recipe.augment")
        for k in RANGE_FIELDS:
            if k in a:
                if isinstance(a[k], str) or not isinstance(a[k], (list, tuple)):
                    raise ManifestError(f"recipe.augment.{k}: expected [lo, hi], got {a[k]!r}")
                a[k] = tuple(a[k])
        augment = AugmentPolicy(**a)
        near_miss = d.pop("near_miss", ())
        if isinstance(near_miss, str) or not isinstance(near_miss, (list, tuple)):
            raise ManifestError(f"recipe.near_miss: expected a list of keywords, got {near_miss!r}")
        return cls(geometry=geometry, label=label, resampler=resampler, split=split, augment=augment,
                   near_miss=tuple(near_miss), **d)


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
        if not isinstance(d, dict):
            raise ManifestError(f"manifest: expected an object, got {type(d).__name__}")
        unknown = sorted(set(d) - {"manifest_version", "name", "sources", "recipe"})
        if unknown:
            raise ManifestError(f"manifest: unknown top-level field(s) {unknown}")
        missing = sorted(k for k in ("name", "sources", "recipe") if k not in d)
        if missing:
            raise ManifestError(f"manifest: missing top-level field(s) {missing}")
        if isinstance(d["sources"], (str, dict)) or not isinstance(d["sources"], (list, tuple)):
            raise ManifestError("manifest.sources: expected a list of sources")
        return cls(name=d["name"], manifest_version=int(d.get("manifest_version", MANIFEST_VERSION)),
                   sources=tuple(Source.from_dict(s) for s in d["sources"]),
                   recipe=Recipe.from_dict(d["recipe"]), path=path)


def canonical_json(obj: Any) -> str:
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def manifest_hash(m: Manifest) -> str:
    """sha256 over the canonical JSON of sources[] and recipe — the feature directory's name."""
    return hashlib.sha256(canonical_json({"sources": [s.to_dict() for s in m.sources],
                                          "recipe": m.recipe.to_dict()}).encode()).hexdigest()


def _is_real(v: Any) -> bool:
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def _is_int(v: Any) -> bool:
    return isinstance(v, int) and not isinstance(v, bool)


def _check_range(name: str, value: Any, lo_min: float | None = None, lo_positive: bool = False) -> None:
    """A range is [lo, hi] of finite real numbers with lo <= hi (a degenerate range is a fixed value)."""
    if not isinstance(value, (list, tuple)) or len(value) != 2 or not all(_is_real(v) for v in value):
        raise ManifestError(f"recipe.augment.{name}: expected [lo, hi] of finite numbers, got {value!r}")
    lo, hi = value
    if lo > hi:
        raise ManifestError(f"recipe.augment.{name}: range {list(value)} is reversed (lo > hi)")
    if lo_positive and lo <= 0.0:
        raise ManifestError(f"recipe.augment.{name}: range {list(value)} must be positive (lo > 0)")
    if lo_min is not None and lo < lo_min:
        raise ManifestError(f"recipe.augment.{name}: range {list(value)} must not go below {lo_min}")


def validate_tts(m: Manifest) -> None:
    """The TTS block: every voice names a `piper` source (its verified archive) and every `piper` source is
    named by a voice; a `piper` source without a TTS block is a dead row and is refused."""
    piper_ids = {s.id for s in m.sources if s.kind == "piper"}
    tts = m.recipe.tts
    if tts is None:
        if piper_ids:
            raise ManifestError(f"sources {sorted(piper_ids)} are of kind 'piper' but recipe.tts is absent — "
                                "a voice archive is synth's input; add the TTS block or drop the source")
        return
    if not isinstance(tts, dict):
        raise ManifestError("recipe.tts: expected an object")
    voices = tts.get("voices")
    texts = tts.get("texts")
    if not isinstance(voices, list) or not voices:
        raise ManifestError("recipe.tts.voices: expected a non-empty list of voices")
    if not isinstance(texts, dict) or not texts.get("positive"):
        raise ManifestError("recipe.tts.texts: expected {positive: [...], negative: [...]} with at least one "
                            "positive text")
    named: set[str] = set()
    seen_ids: set[str] = set()
    for v in voices:
        if not isinstance(v, dict):
            raise ManifestError(f"recipe.tts.voices: expected voice objects, got {v!r}")
        missing = sorted(k for k in ("id", "source", "onnx", "config", "sha256") if not v.get(k))
        if missing:
            raise ManifestError(f"recipe.tts voice {v.get('id', '?')!r}: missing {missing} (id, source — a "
                                "sources[] row of kind piper —, onnx and config members of that archive, the "
                                "onnx sha256)")
        if v["id"] in seen_ids:
            raise ManifestError(f"recipe.tts: duplicate voice id {v['id']!r}")
        seen_ids.add(v["id"])
        if v["source"] not in piper_ids:
            raise ManifestError(f"recipe.tts voice {v['id']!r}: source {v['source']!r} is not a sources[] "
                                f"row of kind 'piper' (have {sorted(piper_ids)})")
        named.add(v["source"])
        for k in ("onnx", "config"):
            p = pathlib.PurePosixPath(v[k])
            if p.is_absolute() or ".." in p.parts:
                raise ManifestError(f"recipe.tts voice {v['id']!r}: {k} {v[k]!r} must be a relative member "
                                    "path of the voice archive")
        if len(v["sha256"]) != 64 or any(c not in "0123456789abcdef" for c in v["sha256"]):
            raise ManifestError(f"recipe.tts voice {v['id']!r}: sha256 must be 64 lowercase hex characters")
        speakers = v.get("speakers")
        if speakers is not None and (isinstance(speakers, str) or not isinstance(speakers, list)):
            raise ManifestError(f"recipe.tts voice {v['id']!r}: speakers must be a list of speaker ids")
    unnamed = sorted(piper_ids - named)
    if unnamed:
        raise ManifestError(f"sources {unnamed} are of kind 'piper' but no recipe.tts voice names them")
    for k in ("length_scale", "noise_scale", "noise_w"):
        if k in tts:
            _check_range(k, tts[k], lo_positive=(k == "length_scale"))


def validate(m: Manifest) -> None:
    """Refuse a manifest the builder cannot honour; every refusal names the source and the rule."""
    if m.manifest_version != MANIFEST_VERSION:
        raise ManifestError(f"manifest_version {m.manifest_version}, expected {MANIFEST_VERSION}")
    if not m.name:
        raise ManifestError("manifest has no name")
    seen: set[str] = set()
    for s in m.sources:
        if s.id in ("", ".", "..") or "/" in s.id or "\\" in s.id or not _SEGMENT.fullmatch(s.id):
            raise ManifestError(f"source {s.id!r}: id must be a single path segment [A-Za-z0-9._-]+ (it "
                                "names store directories)")
        if s.id in seen:
            raise ManifestError(f"duplicate source id {s.id!r}")
        seen.add(s.id)
        if s.kind not in KINDS:
            raise ManifestError(f"source {s.id!r}: unknown kind {s.kind!r} (known: {', '.join(KINDS)})")
        if (s.origin.url is None) == (s.origin.path is None):
            raise ManifestError(f"source {s.id!r}: origin needs exactly one of url or path")
        if s.origin.path is not None and (pathlib.PurePosixPath(s.origin.path).is_absolute() or ".." in s.origin.path.split("/")):
            raise ManifestError(f"source {s.id!r}: origin.path must be repository-relative, got {s.origin.path!r}")
        if s.origin.path is not None and not s.origin.path.startswith(FIXTURE_PREFIX):
            raise ManifestError(f"source {s.id!r}: an in-repository origin must lie under {FIXTURE_PREFIX}, "
                                f"got {s.origin.path!r} (plan §6 M4: committed sources live in the fixtures "
                                "directory)")
        if s.origin.in_repository and not s.redistributable:
            raise ManifestError(f"source {s.id!r}: an origin inside the repository requires redistributable: true "
                                "(only redistributable audio may enter git)")
        f = s.archive.file
        if not f or f in (".", "..") or "/" in f or "\\" in f:
            raise ManifestError(f"source {s.id!r}: archive.file must be a bare file name, got {f!r}")
        if len(s.archive.sha256) != 64 or any(c not in "0123456789abcdef" for c in s.archive.sha256):
            raise ManifestError(f"source {s.id!r}: archive.sha256 must be 64 lowercase hex characters")
        if not _is_int(s.archive.size) or s.archive.size <= 0:
            raise ManifestError(f"source {s.id!r}: archive.size must be a positive integer")
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
    computed = "pcen" if r.geometry.pcen.enabled else "log"
    if r.stored_path != computed:
        raise ManifestError(f"recipe.stored_path {r.stored_path!r} does not match "
                            f"recipe.geometry.pcen.enabled={r.geometry.pcen.enabled} — the stored path is "
                            f"the one the geometry computes ({computed!r})")
    if r.kws_features_version != KWS_FEATURES_VERSION:
        raise ManifestError(f"recipe.kws_features_version {r.kws_features_version} != this module's {KWS_FEATURES_VERSION}")
    if not r.geometry.valid():
        raise ManifestError("recipe.geometry is not a valid log_mel geometry")
    try:
        assert_band_support(r.geometry)
    except ValueError as e:
        raise ManifestError(f"recipe.geometry: {e}") from None
    if abs(sum(r.split.fractions.values()) - 1.0) > 1e-9 or set(r.split.fractions) != set(SPLITS):
        raise ManifestError(f"recipe.split.fractions must cover {list(SPLITS)} and sum to 1, got {r.split.fractions}")
    if not _is_int(r.label.tolerance_hops):
        raise ManifestError(f"recipe.label.tolerance_hops must be an integer number of hops, got "
                            f"{r.label.tolerance_hops!r}")
    if r.label.tolerance_hops < 0 or not _is_real(r.label.trim_db) or r.label.trim_db <= 0 \
            or not _is_real(r.label.window_ms) or r.label.window_ms <= 0:
        raise ManifestError("recipe.label: trim_db and window_ms must be positive, tolerance_hops non-negative")
    if not _is_real(r.context_s) or r.context_s < MIN_CONTEXT_S:
        raise ManifestError(f"recipe.context_s {r.context_s!r} is below the plan's minimum of "
                            f"{MIN_CONTEXT_S:g} s of same-split negative material around every positive "
                            "(plan §6 M4 \"Positives and augmentation\")")
    a = r.augment
    if not _is_int(a.draws_per_positive) or not _is_int(a.noisy_per_negative):
        raise ManifestError("recipe.augment: draws_per_positive (K) and noisy_per_negative (M) must be "
                            "integers")
    if a.draws_per_positive < 1 or a.noisy_per_negative < 0 or not _is_real(a.dry_share) \
            or not 0.0 <= a.dry_share <= 1.0:
        raise ManifestError("recipe.augment: draws_per_positive >= 1, noisy_per_negative >= 0, dry_share in [0, 1]")
    if not _is_int(a.seed):
        raise ManifestError(f"recipe.augment.seed must be an integer, got {a.seed!r}")
    _check_range("snr_db", a.snr_db)
    _check_range("gain_db", a.gain_db)
    _check_range("speed", a.speed, lo_positive=True)
    _check_range("rt60_s", a.rt60_s, lo_min=0.0)
    if a.mic_model not in MIC_MODELS:
        raise ManifestError(f"recipe.augment.mic_model {a.mic_model!r} is not implemented (allowed: "
                            f"{list(MIC_MODELS)}; no microphone-path model exists at M4a, so any other value "
                            "would be a silent no-op)")
    if not r.phrase:
        raise ManifestError("recipe.phrase is empty")
    validate_tts(m)


def load_manifest(path: str | pathlib.Path) -> Manifest:
    path = pathlib.Path(path)
    with path.open(encoding="utf-8") as f:
        doc = json.load(f)
    try:
        m = Manifest.from_dict(doc, path=path)
    except ManifestError:
        raise
    except (TypeError, KeyError, AttributeError) as e:
        raise ManifestError(f"{path}: manifest does not match schema version {MANIFEST_VERSION}: "
                            f"{e}") from None
    validate(m)
    return m


def save_manifest(m: Manifest, path: str | pathlib.Path) -> None:
    text = json.dumps(m.to_dict(), indent=1, ensure_ascii=False) + "\n"
    pathlib.Path(path).write_text(text, encoding="utf-8")


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
    text = json.dumps(lock.to_dict(), indent=1, sort_keys=True, ensure_ascii=False) + "\n"
    pathlib.Path(path).write_text(text, encoding="utf-8")


def read_lock(path: str | pathlib.Path) -> Lock:
    with pathlib.Path(path).open(encoding="utf-8") as f:
        return Lock.from_dict(json.load(f))


def eval_set_id(clips: list[Clip], share: str) -> str:
    """sha256 of the sorted clip ids of one eval share: a pure function of sources[] and recipe."""
    ids = sorted(c.id for c in clips if c.share == share and c.variant == 0)
    return hashlib.sha256("\n".join(ids).encode()).hexdigest()


# Identity fields reproduce exactly on every rebuild; everything else in the
# lock is derived. pcm_sha256 is identity only for the decoded (variant 0)
# clips of sources flagged pcm_exact: an augmented variant's digest is of
# audio rendered through resample_poly and fftconvolve, so it is derived.
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
        if a.variant == 0 and a.source in pcm_exact_sources and a.pcm_sha256 != b.pcm_sha256:
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
