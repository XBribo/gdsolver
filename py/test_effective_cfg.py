"""The generated effective-cfg headers are current, and the manifest reads the lines they print.

tools/gen_effective_cfg.py writes src/mod/cfg_effective.gen.hpp and
dp/src/dp/defaults_profile.gen.hpp from the mod's cfg parsers and dp's per-call reset. A key or a
default added there and not regenerated would silently be missing from every manifest, so this
fails while the headers are behind the code. The rest pins how cold_manifest.py reads the three
lines the mod prints at solve start (no game needed).
"""

from __future__ import annotations

import subprocess
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))

import cold_manifest  # noqa: E402

LOG = """session_start
dpsolve: start level=7 csv=1 bytes horizon=3000
cfgeff: level=7 dpsolve=1 dpspentpad=1 coins=0 dparg=- presstrace=0,-1
cfgdiff: level=7(0) dpsolve=1(0)
dpdefaults: core=dp_core_built_Sep_27_2026_04:00:00 fnv=00000000deadbeef g_upsideCoyote=1 g_aliveCap=16000
dpsolve: solver args: --out x --cap 2000
"""


class Generated(unittest.TestCase):
    def test_headers_are_current(self):
        r = subprocess.run([sys.executable, str(ROOT / "tools" / "gen_effective_cfg.py"), "--check"],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


class ManifestReadsTheLines(unittest.TestCase):
    def test_effective(self):
        e = cold_manifest.effective(LOG)
        self.assertEqual(e["cfg"]["dpspentpad"], "1")
        self.assertEqual(e["cfg"]["presstrace"], "0,-1")
        self.assertEqual(e["changed"], {"level": "7", "dpsolve": "1"})
        self.assertEqual(e["dp"]["g_upsideCoyote"], "1")
        self.assertEqual(e["dp_fnv"], "00000000deadbeef")

    def test_absent(self):
        self.assertIsNone(cold_manifest.effective("session_start\n"))

    def test_levelsource(self):
        got = cold_manifest.SOURCE_LINE.findall(
            "levelsource: id=4001 kind=file sig=0123456789abcdef bytes=1297899\n")
        self.assertEqual(got, [("4001", "file", "0123456789abcdef", "1297899")])


if __name__ == "__main__":
    unittest.main()
