#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple
from zipfile import ZIP_DEFLATED, ZipFile


IGNORED_TOP_LEVEL = {
    ".git",
    ".idea",
    ".vs",
    "build",
}

SYNC_TOP_LEVEL = {
    "source",
}

EXTRA_SYNC_FILES = {
    Path("docs/task.md"),
    Path("docs/Структура-и-реализация-переводов.md"),
    Path("tools/sync_multilang_changes.py"),
    Path("tools/sync_multilang_changes.cmd"),
}


def parse_args() -> argparse.Namespace:
    script_path = Path(__file__).resolve()
    tools_dir = script_path.parent
    repo_root = tools_dir.parent
    parent_dir = repo_root.parent

    default_base_root = parent_dir / "endless-sky-master"
    default_translated_root = repo_root
    default_target_root = default_base_root
    default_output_root = parent_dir / "endless-sky-patched"
    default_bundle_path = tools_dir / "multilang_changes_bundle.zip"

    parser = argparse.ArgumentParser(
        description=(
            "Create/apply multilingual patch bundle. Bundle contains ready data so apply "
            "works without modified source tree."
        )
    )
    parser.add_argument(
        "--create-bundle",
        action="store_true",
        help="Create or refresh bundle file and exit.",
    )
    parser.add_argument(
        "--bundle-path",
        type=Path,
        default=default_bundle_path,
        help="Path to bundle zip with prepared patch data.",
    )
    parser.add_argument(
        "--base-root",
        type=Path,
        default=default_base_root,
        help="Original base repository used to produce multilingual changes.",
    )
    parser.add_argument(
        "--translated-root",
        type=Path,
        default=default_translated_root,
        help="Repository with multilingual modifications (used only with --create-bundle).",
    )
    parser.add_argument(
        "--target-root",
        type=Path,
        default=default_target_root,
        help="Original/updated repository where bundle changes should be applied.",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=default_output_root,
        help="Output path for patched result (ignored with --in-place).",
    )
    parser.add_argument(
        "--in-place",
        action="store_true",
        help="Apply changes directly into --target-root.",
    )
    parser.add_argument(
        "--clean-output",
        action="store_true",
        help="Delete --output-root first if it exists.",
    )
    return parser.parse_args()


def list_files(root: Path) -> Dict[Path, Path]:
    result: Dict[Path, Path] = {}
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(root)
        if rel.parts and rel.parts[0] in IGNORED_TOP_LEVEL:
            continue
        result[rel] = path
    return result


def is_translation_path(rel: Path) -> bool:
    if bool(rel.parts) and rel.parts[0] in SYNC_TOP_LEVEL:
        return True
    return rel in EXTRA_SYNC_FILES


def read_bytes(path: Path) -> Optional[bytes]:
    if not path.exists() or not path.is_file():
        return None
    return path.read_bytes()


def looks_binary(data: Optional[bytes]) -> bool:
    if data is None:
        return False
    return b"\x00" in data


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def write_bytes(path: Path, data: bytes) -> None:
    ensure_parent(path)
    path.write_bytes(data)


def changed_files(
    base_files: Dict[Path, Path],
    translated_files: Dict[Path, Path],
) -> Set[Path]:
    all_paths = set(base_files) | set(translated_files)
    changed: Set[Path] = set()
    for rel in all_paths:
        base_data = read_bytes(base_files[rel]) if rel in base_files else None
        translated_data = read_bytes(translated_files[rel]) if rel in translated_files else None
        if base_data != translated_data:
            changed.add(rel)
    return changed


def create_bundle(base_root: Path, translated_root: Path, bundle_path: Path) -> None:
    if not base_root.exists():
        raise RuntimeError(f"Base root does not exist: {base_root}")
    if not translated_root.exists():
        raise RuntimeError(f"Translated root does not exist: {translated_root}")

    base_files = list_files(base_root)
    translated_files = list_files(translated_root)
    to_bundle = sorted(rel for rel in changed_files(base_files, translated_files) if is_translation_path(rel))

    ensure_parent(bundle_path)
    with ZipFile(bundle_path, "w", compression=ZIP_DEFLATED) as zf:
        manifest: List[dict] = []
        for rel in to_bundle:
            rel_posix = rel.as_posix()
            base_data = read_bytes(base_files[rel]) if rel in base_files else None
            translated_data = read_bytes(translated_files[rel]) if rel in translated_files else None

            is_binary = looks_binary(base_data) or looks_binary(translated_data)
            if is_binary:
                # Translation bundle keeps only text files directly related to multilingual logic/data.
                continue

            if base_data is not None:
                zf.writestr(f"base/{rel_posix}", base_data)
            if translated_data is not None:
                zf.writestr(f"translated/{rel_posix}", translated_data)

            manifest.append(
                {
                    "path": rel_posix,
                    "has_base": base_data is not None,
                    "has_translated": translated_data is not None,
                    "is_binary": False,
                }
            )

        meta = {
            "version": 1,
            "base_root": str(base_root),
            "translated_root": str(translated_root),
            "changed_files": len(manifest),
            "entries": manifest,
        }
        zf.writestr("manifest.json", json.dumps(meta, ensure_ascii=False, indent=2).encode("utf-8"))

    print("Bundle created.")
    print(f"Bundle path: {bundle_path}")
    print(f"Changed files in bundle: {len(manifest)}")


def load_bundle(bundle_path: Path) -> Tuple[dict, ZipFile]:
    if not bundle_path.exists():
        raise RuntimeError(f"Bundle does not exist: {bundle_path}")

    zf = ZipFile(bundle_path, "r")
    try:
        manifest = json.loads(zf.read("manifest.json").decode("utf-8"))
    except Exception:
        zf.close()
        raise
    return manifest, zf


def bundle_read_bytes(zf: ZipFile, item_path: str) -> Optional[bytes]:
    try:
        return zf.read(item_path)
    except KeyError:
        return None


def prepare_output(target_root: Path, output_root: Path, clean_output: bool) -> None:
    if output_root.exists():
        if not clean_output:
            raise RuntimeError(
                f"Output path already exists: {output_root}. "
                "Use --clean-output or choose another --output-root."
            )
        shutil.rmtree(output_root)

    shutil.copytree(
        target_root,
        output_root,
        ignore=shutil.ignore_patterns(".git"),
        dirs_exist_ok=False,
    )


def run_git_merge_file(
    current_data: bytes,
    base_data: bytes,
    translated_data: bytes,
    rel_path: Path,
) -> Tuple[bytes, bool]:
    with tempfile.TemporaryDirectory(prefix="es-merge-") as tmp_dir:
        tmp = Path(tmp_dir)
        current_file = tmp / "current"
        base_file = tmp / "base"
        translated_file = tmp / "translated"
        current_file.write_bytes(current_data)
        base_file.write_bytes(base_data)
        translated_file.write_bytes(translated_data)

        command = [
            "git",
            "merge-file",
            "-p",
            "-L",
            f"target:{rel_path.as_posix()}",
            "-L",
            f"base:{rel_path.as_posix()}",
            "-L",
            f"translated:{rel_path.as_posix()}",
            str(current_file),
            str(base_file),
            str(translated_file),
        ]
        completed = subprocess.run(command, capture_output=True)
        if completed.returncode > 1:
            stderr = completed.stderr.decode("utf-8", errors="replace").strip()
            raise RuntimeError(
                f"git merge-file failed for {rel_path.as_posix()}: {stderr or 'unknown error'}"
            )
        return completed.stdout, completed.returncode == 1


def apply_bundle(
    bundle_path: Path,
    target_root: Path,
    output_root: Path,
    in_place: bool,
    clean_output: bool,
) -> int:
    if not target_root.exists():
        raise RuntimeError(f"Target root does not exist: {target_root}")

    if not in_place:
        prepare_output(target_root, output_root, clean_output)
    else:
        output_root = target_root

    manifest, zf = load_bundle(bundle_path)
    try:
        entries = manifest.get("entries", [])
        merged_count = 0
        copied_count = 0
        deleted_count = 0
        skipped_binary_conflicts = 0
        conflicts: List[str] = []

        for entry in entries:
            rel = Path(entry["path"])
            rel_posix = entry["path"]

            base_data = bundle_read_bytes(zf, f"base/{rel_posix}") if entry["has_base"] else None
            translated_data = (
                bundle_read_bytes(zf, f"translated/{rel_posix}") if entry["has_translated"] else None
            )
            target_path = target_root / rel
            out_path = output_root / rel
            target_data = read_bytes(target_path)

            if translated_data is None:
                if out_path.exists():
                    out_path.unlink()
                    deleted_count += 1
                continue

            if base_data is None:
                write_bytes(out_path, translated_data)
                copied_count += 1
                continue

            if entry.get("is_binary", False):
                if target_data is None or target_data == base_data:
                    write_bytes(out_path, translated_data)
                    copied_count += 1
                else:
                    skipped_binary_conflicts += 1
                    conflicts.append(f"{rel_posix} (binary conflict)")
                continue

            if target_data is None:
                write_bytes(out_path, translated_data)
                copied_count += 1
                continue

            merged_data, has_conflict = run_git_merge_file(
                current_data=target_data,
                base_data=base_data,
                translated_data=translated_data,
                rel_path=rel,
            )
            write_bytes(out_path, merged_data)
            merged_count += 1
            if has_conflict:
                conflicts.append(rel_posix)

        print("Done.")
        print(f"Bundle path:     {bundle_path}")
        print(f"Target root:     {target_root}")
        print(f"Output root:     {output_root}")
        print(f"Changed files:   {len(entries)}")
        print(f"Merged files:    {merged_count}")
        print(f"Copied files:    {copied_count}")
        print(f"Deleted files:   {deleted_count}")
        print(f"Binary conflicts skipped: {skipped_binary_conflicts}")

        if conflicts:
            print("\nConflicts:")
            for item in conflicts:
                print(f"  - {item}")
            return 2
        return 0
    finally:
        zf.close()


def main() -> int:
    args = parse_args()

    bundle_path = args.bundle_path.resolve()
    base_root = args.base_root.resolve()
    translated_root = args.translated_root.resolve()
    target_root = args.target_root.resolve()
    output_root = args.output_root.resolve()

    try:
        if args.create_bundle:
            create_bundle(base_root=base_root, translated_root=translated_root, bundle_path=bundle_path)
            return 0

        return apply_bundle(
            bundle_path=bundle_path,
            target_root=target_root,
            output_root=output_root,
            in_place=args.in_place,
            clean_output=args.clean_output,
        )
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
