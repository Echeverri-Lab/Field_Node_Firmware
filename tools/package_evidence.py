#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
from datetime import datetime, timezone
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
EVIDENCE_ROOT = REPO_ROOT / "docs" / "evidence"
CAPTURES_DIR = REPO_ROOT / "captures"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def classify(path: Path) -> str:
    suffix = path.suffix.lower()
    if suffix in {".jpg", ".jpeg"}:
        return "image"
    if suffix == ".wav":
        return "audio"
    if suffix == ".csv":
        return "log"
    if suffix == ".log":
        return "serial_log"
    return "artifact"


def latest_matching(pattern: str) -> Path | None:
    matches = sorted(CAPTURES_DIR.glob(pattern), key=lambda p: p.stat().st_mtime, reverse=True)
    return matches[0] if matches else None


def main() -> int:
    parser = argparse.ArgumentParser(description="Copy selected artifacts into docs/evidence and emit a manifest.")
    parser.add_argument("label", help="Destination folder name under docs/evidence/")
    parser.add_argument("--file", action="append", default=[], help="Artifact file to include. Repeatable.")
    parser.add_argument("--latest-captures", action="store_true",
                        help="Include the newest JPG and WAV from ./captures if present.")
    parser.add_argument("--source", default="manual",
                        help="Short provenance label, e.g. usb_monitor, sd_dump, serial_log.")
    parser.add_argument("--note", default="", help="Short note stored in the manifest.")
    args = parser.parse_args()

    selected: list[Path] = []
    for raw in args.file:
        path = Path(raw).expanduser().resolve()
        if not path.is_file():
            raise SystemExit(f"missing artifact file: {path}")
        selected.append(path)

    if args.latest_captures:
        latest_jpg = latest_matching("*.jpg")
        latest_wav = latest_matching("*.wav")
        if latest_jpg:
            selected.append(latest_jpg.resolve())
        if latest_wav:
            selected.append(latest_wav.resolve())

    deduped: list[Path] = []
    seen: set[Path] = set()
    for path in selected:
        if path not in seen:
            seen.add(path)
            deduped.append(path)

    if not deduped:
        raise SystemExit("no artifacts selected")

    out_dir = EVIDENCE_ROOT / args.label
    out_dir.mkdir(parents=True, exist_ok=True)

    manifest: dict[str, object] = {
        "label": args.label,
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "source": args.source,
        "note": args.note,
        "files": [],
    }

    for src in deduped:
        dest = out_dir / src.name
        shutil.copy2(src, dest)
        stat = dest.stat()
        manifest["files"].append({
            "name": dest.name,
            "kind": classify(dest),
            "bytes": stat.st_size,
            "sha256": sha256_file(dest),
            "copied_from": os.path.relpath(src, REPO_ROOT),
        })

    manifest_path = out_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="ascii")
    print(f"wrote evidence bundle: {out_dir}")
    print(f"manifest: {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
