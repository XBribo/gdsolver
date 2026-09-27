"""The package a cold run measures is pinned once -- no GD.

What is pinned: a rebuild that lands after the run has taken its
meta must not change what the workers deploy, and the meta's hash must be the hash of
what they deploy. cold_regress snapshots the .geode into the content-addressed cache
before anything reads it, and measures that copy; the workers snapshot again at every
launch, which for an immutable file is a no-op.

usage: python py/test_mod_pin.py   (exit 1 on the first failure)
"""
import hashlib
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gdtas import worker as W    # noqa: E402


def check(cond, what):
    if not cond:
        print("FAIL:", what)
        sys.exit(1)
    print("ok:", what)


def main():
    tmp = Path(tempfile.mkdtemp(prefix="modpin_"))
    W.MOD_CACHE = tmp / "cache"
    build = tmp / "gdsolver.solver.geode"
    build.write_bytes(b"package A")

    pinned, digest = W.snapshot_mod(build)
    meta_hash = hashlib.sha256(pinned.read_bytes()).hexdigest()
    check(pinned.parent == W.MOD_CACHE and digest in pinned.name,
          "the pin lands in the cache under its content hash")

    # the main line rebuilds while the suite is running
    build.write_bytes(b"package B -- a later build")

    check(pinned.read_bytes() == b"package A", "the pinned copy is untouched by the rebuild")
    again, digest2 = W.snapshot_mod(pinned)
    check(again == pinned and digest2 == digest,
          "snapshotting the pinned copy again returns the same file (what a launch does)")
    check(hashlib.sha256(again.read_bytes()).hexdigest() == meta_hash,
          "the meta's hash is the hash of what a launch would deploy")

    other, digest3 = W.snapshot_mod(build)
    check(other != pinned and digest3 != digest,
          "the later build gets its own name, so the two can never be confused")
    print("all ok")


if __name__ == "__main__":
    main()
