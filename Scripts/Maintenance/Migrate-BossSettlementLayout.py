"""Write a new offline Project J outbox snapshot with explicitly renamed field-boss settlement keys."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import tempfile


def digest(path):
    with path.open("rb") as data:
        return hashlib.file_digest(data, "sha256").hexdigest()


def parse_key(value):
    # The runtime key is lower-case map package | channel | stable encounter ID.
    pieces = value.split("|") if isinstance(value, str) else []
    if (len(pieces) != 3 or value != value.lower() or value != value.strip()
            or not pieces[0].startswith("/") or not pieces[1].isascii() or not pieces[1].isdecimal()
            or str(int(pieces[1])) != pieces[1] or not pieces[2] or "\x00" in value):
        raise ValueError(f"Invalid settlement key {value!r}")
    return value


def migrate(source, destination, changes):
    source = source.resolve(strict=True)
    destination = destination.absolute()
    if destination.exists() or destination.is_symlink():
        raise FileExistsError(destination)
    if not isinstance(changes, list) or not changes:
        raise ValueError("Supply a JSON array of {old, new} key pairs")
    renames = []
    for row in changes:
        if not isinstance(row, dict) or set(row) != {"old", "new"}:
            raise ValueError("Each row requires old and new")
        renames.append((parse_key(row["old"]), parse_key(row["new"])))
    keys = [key for pair in renames for key in pair]
    if len(set(keys)) != len(keys):
        raise ValueError("Ambiguous mapping: every old and new key must be distinct")

    def check_sidecars():
        for suffix in ("-journal", "-wal", "-shm"):
            if Path(str(source) + suffix).exists():
                raise ValueError("Use a closed, checkpointed snapshot without SQLite sidecars")

    check_sidecars()
    original = digest(source)
    with tempfile.TemporaryDirectory(dir=destination.parent) as scratch:
        output = Path(scratch) / "outbox.sqlite"
        shutil.copyfile(source, output)
        if digest(output) != original:
            raise ValueError("Source snapshot changed during copy")
        db = sqlite3.connect(output)
        try:
            with db:
                if (db.execute("PRAGMA application_id").fetchone() != (0x504A4F42,)
                        or db.execute("PRAGMA user_version").fetchone() != (2,)):
                    raise ValueError("Expected Project J online outbox schema 2")
                if db.execute("PRAGMA journal_mode").fetchone() != ("delete",):
                    raise ValueError("Expected the Project J rollback-journal format")
                for old, new in renames:
                    # The primary key rejects an occupied destination. The transaction covers the entire patch.
                    result = db.execute("UPDATE boss_reward_settlements SET encounter_key=? WHERE encounter_key=?", (new, old))
                    if result.rowcount != 1:
                        raise ValueError(f"No settlement for {old}")
            if db.execute("PRAGMA integrity_check").fetchall() != [("ok",)]:
                raise ValueError("Migrated snapshot failed SQLite integrity check")
        finally:
            db.close()
        check_sidecars()
        if digest(source) != original:
            raise ValueError("Source changed during migration")
        migrated = digest(output)
        os.link(output, destination)  # Publish the completed file without replacing another operator's result.
    return {"source_sha256": original, "output_sha256": migrated, "changed_settlements": len(renames)}


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("source", type=Path, help="closed offline snapshot, never the active server file")
    cli.add_argument("destination", type=Path, help="new filename in an existing directory")
    cli.add_argument("mapping", type=Path, help='JSON array of {"old": "...", "new": "..."}')
    args = cli.parse_args()
    print(json.dumps(migrate(args.source, args.destination, json.loads(args.mapping.read_text(encoding="utf-8-sig")))))
