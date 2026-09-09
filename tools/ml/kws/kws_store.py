#!/usr/bin/env python3
"""kws_store — the feature store outside git: location, tiers, archive verification, safe extraction.

`--store DIR` or MUTAP_KWS_STORE, no personal default. Tiers (README.md):
archives/<id>/ (verified inputs + .verified marker), extracted/<id>/,
pcm/<id>/<decoder>-<resampler>/, features/<manifest-hash>/<split>/, holdout/.
`fetch` never downloads; every later stage refuses an archive whose
.verified marker is missing or names a different sha256.

Every tier path is built through `_tier`, which refuses a path that resolves
outside the store root (a source id or archive file name carrying `/` or
`..`), so no `mkdir`, `rmtree` or write in this module can leave the store.
"""
from __future__ import annotations

import hashlib
import os
import pathlib
import shutil
import tarfile
import zipfile

from kws_manifest import Manifest, Source

ENV_VAR = "MUTAP_KWS_STORE"


class StoreError(RuntimeError):
    """A store condition the builder refuses to proceed past."""


def resolve_store(arg: str | None) -> pathlib.Path:
    value = arg or os.environ.get(ENV_VAR)
    if not value:
        raise StoreError(f"no feature store: pass --store DIR or set {ENV_VAR} (there is no default)")
    return pathlib.Path(value).expanduser().resolve()


def sha256_file(path: pathlib.Path, chunk: int = 1 << 20) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


class Store:
    def __init__(self, root: pathlib.Path):
        self.root = pathlib.Path(root).resolve()

    # -- tiers ------------------------------------------------------------------
    def _tier(self, *parts: str) -> pathlib.Path:
        """root/parts..., refused unless every part after the tier name is one plain path segment, i.e. the
        resolved path lies strictly inside the tier directory at exactly that depth (parts[1] names the
        source or hash; `..`, `/` and an empty segment all fail)."""
        tier = (self.root / parts[0]).resolve()
        p = self.root.joinpath(*parts)
        r = p.resolve()
        inside = r.is_relative_to(tier) and r != tier and len(r.relative_to(tier).parts) == len(parts) - 1
        if not inside or any(not seg or seg in (".", "..") or "/" in seg or "\\" in seg for seg in parts[1:]):
            who = parts[1] if len(parts) > 1 else "/".join(parts)
            raise StoreError(f"source {who!r}: path {p} escapes the store tier {tier} (ids and file names "
                             "must be single path segments)")
        return p

    def archives(self, source_id: str) -> pathlib.Path:
        return self._tier("archives", source_id)

    def extracted(self, source_id: str) -> pathlib.Path:
        return self._tier("extracted", source_id)

    def pcm(self, source_id: str, decoder_id: str, resampler_id: str) -> pathlib.Path:
        return self._tier("pcm", source_id, f"{decoder_id}-{resampler_id}")

    def features(self, manifest_hash: str, split: str | None = None) -> pathlib.Path:
        p = self._tier("features", manifest_hash)
        return p / split if split else p

    def holdout(self) -> pathlib.Path:
        return self.root / "holdout"

    # -- archives ---------------------------------------------------------------
    def archive_path(self, source: Source) -> pathlib.Path:
        base = self.archives(source.id)
        p = base / source.archive.file
        if p.parent != base or p.name in ("", ".", "..") or p.name != source.archive.file:
            raise StoreError(f"source {source.id!r}: archive.file {source.archive.file!r} must be a bare "
                             f"file name inside {base}")
        return p

    def marker_path(self, source: Source) -> pathlib.Path:
        return self.archives(source.id) / ".verified"

    def fetch(self, source: Source, repo_root: pathlib.Path) -> pathlib.Path:
        """Verify (and for a repository-relative origin, first copy) the source's archive; never download."""
        dest = self.archive_path(source)
        dest.parent.mkdir(parents=True, exist_ok=True)
        if source.origin.in_repository:
            src = repo_root / source.origin.path / source.archive.file
            if not src.exists():
                raise StoreError(f"source {source.id!r}: repository origin {src} does not exist")
            if not dest.exists() or dest.stat().st_size != src.stat().st_size or sha256_file(dest) != sha256_file(src):
                shutil.copyfile(src, dest)
        elif not dest.exists():
            raise StoreError(f"source {source.id!r}: {dest} is missing — the builder never downloads. "
                             f"Obtain it from {source.origin.url} ({source.origin.obtain}) and place it there")
        size = dest.stat().st_size
        if size != source.archive.size:
            raise StoreError(f"source {source.id!r}: archive size {size} != manifest {source.archive.size} ({dest})")
        digest = sha256_file(dest)
        if digest != source.archive.sha256:
            self.marker_path(source).unlink(missing_ok=True)
            raise StoreError(f"source {source.id!r}: archive sha256 {digest} != manifest {source.archive.sha256} ({dest})")
        self.marker_path(source).write_text(digest + "\n", encoding="utf-8")
        return dest

    def require_verified(self, source: Source) -> pathlib.Path:
        """The archive path, provided `fetch` verified exactly the manifest's sha256; otherwise refuse."""
        dest = self.archive_path(source)
        marker = self.marker_path(source)
        if not dest.exists() or not marker.exists() \
                or marker.read_text(encoding="utf-8").strip() != source.archive.sha256:
            raise StoreError(f"source {source.id!r}: archive not verified against the manifest — run `fetch` first")
        if dest.stat().st_size != source.archive.size:
            raise StoreError(f"source {source.id!r}: archive size changed since verification — run `fetch` again")
        return dest

    # -- extraction -------------------------------------------------------------
    def extract(self, source: Source) -> pathlib.Path:
        """Extract the verified archive with member filtering; idempotent per archive sha256."""
        archive = self.require_verified(source)
        out = self.extracted(source.id)
        marker = out / ".extracted"
        if marker.exists() and marker.read_text(encoding="utf-8").strip() == source.archive.sha256:
            return out
        if out.exists():
            shutil.rmtree(out)
        out.mkdir(parents=True)
        name = archive.name.lower()
        if name.endswith((".tar.gz", ".tgz", ".tar", ".tar.bz2", ".tar.xz")):
            with tarfile.open(archive) as tf:
                for m in tf:
                    if not _safe_member(m.name):
                        raise StoreError(f"source {source.id!r}: refusing tar member {m.name!r}")
                    if m.isdir():
                        (out / m.name).mkdir(parents=True, exist_ok=True)
                    elif m.isreg():
                        target = out / m.name
                        target.parent.mkdir(parents=True, exist_ok=True)
                        src = tf.extractfile(m)
                        assert src is not None
                        with target.open("wb") as dst:
                            shutil.copyfileobj(src, dst)
                    else:  # links, devices, fifos: never
                        raise StoreError(f"source {source.id!r}: refusing non-regular tar member {m.name!r}")
        elif name.endswith(".zip"):
            with zipfile.ZipFile(archive) as zf:
                for info in zf.infolist():
                    if not _safe_member(info.filename):
                        raise StoreError(f"source {source.id!r}: refusing zip member {info.filename!r}")
                    if (info.external_attr >> 16) & 0o170000 == 0o120000:
                        raise StoreError(f"source {source.id!r}: refusing zip symlink {info.filename!r}")
                    if info.is_dir():
                        (out / info.filename).mkdir(parents=True, exist_ok=True)
                        continue
                    target = out / info.filename
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with zf.open(info) as src, target.open("wb") as dst:
                        shutil.copyfileobj(src, dst)
        else:
            raise StoreError(f"source {source.id!r}: unknown archive type {archive.name}")
        marker.write_text(source.archive.sha256 + "\n", encoding="utf-8")
        return out


def _safe_member(name: str) -> bool:
    p = pathlib.PurePosixPath(name)
    return bool(name) and not p.is_absolute() and ".." not in p.parts and not name.startswith("/") and "\\" not in name


def safe_member(name: str) -> bool:
    """True when `name` is a relative archive member path with no `..`, `\\` or leading `/`."""
    return _safe_member(name)


def fetch_all(manifest: Manifest, store: Store, repo_root: pathlib.Path) -> None:
    for s in manifest.sources:
        store.fetch(s, repo_root)
