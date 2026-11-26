"""Verify a layout migration using the closed .prepatch.sqlite exported by ProjectJ.Online.BossRewardSettlement."""

import argparse
from contextlib import closing
import json
from pathlib import Path
import runpy
import shutil
import sqlite3
import tempfile


def read_rows(path, query):
    with closing(sqlite3.connect(path.as_uri() + "?mode=ro", uri=True)) as db:
        return db.execute(query).fetchall()


def verify(source, scratch):
    migration = runpy.run_path(str(Path(__file__).resolve().parents[1] / "Maintenance/Migrate-BossSettlementLayout.py"))
    migrate = migration["migrate"]
    digest = migration["digest"]
    settlements = "SELECT * FROM boss_reward_settlements ORDER BY encounter_key"
    deliveries = "SELECT * FROM outbox_entries ORDER BY idempotency_key"
    original = read_rows(source, settlements)
    pending = read_rows(source, deliveries)
    if len(original) != 1 or len(pending) != 201:
        raise ValueError("Expected the native fixture with one settlement and 201 pending loot/quest deliveries")
    old = original[0][0]
    new = "/game/world/patchtest/l_moved|0|test.boss.renamed"
    mapping = [{"old": old, "new": new}]
    before = digest(source)
    migrated = scratch / "migrated.sqlite"
    result = migrate(source, migrated, mapping)
    if read_rows(migrated, settlements) != [(new, *original[0][1:])] or read_rows(migrated, deliveries) != pending:
        raise AssertionError("Migration changed the settlement payload or pending deliveries")
    rejected = []

    def expect_rejection(label, exception_type, database, target, changes):
        previous = digest(target) if target.exists() else None
        try:
            migrate(database, target, changes)
        except exception_type:
            if (digest(target) if target.exists() else None) != previous:
                raise AssertionError(f"Rejected {label} changed the destination")
            rejected.append(label)
        else:
            raise AssertionError(f"Migration accepted {label}")

    output = scratch / "rejected.sqlite"
    expect_rejection("existing destination", FileExistsError, source, migrated, mapping)
    expect_rejection("duplicate mapping", ValueError, source, output, mapping + mapping)
    expect_rejection("missing key after valid first update", ValueError, source, output,
                     mapping + [{"old": old + "x", "new": new + "x"}])
    occupied = scratch / "occupied.sqlite"
    shutil.copyfile(source, occupied)
    with closing(sqlite3.connect(occupied)) as db:
        db.execute("INSERT INTO boss_reward_settlements SELECT ?, defeat_id, expires_utc_ms, manifest_hash "
                   "FROM boss_reward_settlements", (new,))
        db.commit()
    expect_rejection("occupied target key", sqlite3.IntegrityError, occupied, output, mapping)
    Path(str(occupied) + "-wal").touch()
    expect_rejection("active sidecar", ValueError, occupied, output, mapping)
    if digest(source) != before:
        raise AssertionError("Verification changed the native source snapshot")
    return dict(result, identical_delivery_rows=len(pending), rejected=rejected)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native_fixture", type=Path)
    source = parser.parse_args().native_fixture.resolve(strict=True)
    with tempfile.TemporaryDirectory() as directory:
        print(json.dumps(verify(source, Path(directory)), indent=2))
