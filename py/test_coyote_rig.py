"""The upside-down linger against the level setting fixGravityBug, against GD's own numbers (no game
needed).

A cube upside down under a block row (undersides at 150, the body at y 135) walks off the row's end.
Where the level keeps the gravity bug (fixGravityBug=0) GD leaves m_isOnGround set while the body
starts to fall -- playerIsFallingBugged's flipped arm tests vy > 2g without negating g -- so a fresh
press a few ticks later still jumps from mid-air (-11.18 on its tick, y moved by that tick's gravity
step). Where the level fixes it (fixGravityBug=1) og drops on the first free tick and the press does
nothing. dp models this as --upsidecoyote, on by default and gated on fixGravityBug (speed.hpp).

The rig (data/rigs/calib_coyote_fgb0.lvl and ..._fgb1.lvl, the same level with kA32 = 0 or 1): a
cube flips at a yellow gravity portal on the ground, falls up onto a row of 30-wide blocks from x 300
to 660 and walks off it; it leaves on tick 521 (x 675.09). A second row far above the exit (undersides at
390) gives the falling body somewhere to go; it is 250 px from every row tested. GD's rows below are
from the game's per-tick dump of each cell. The model starts from GD's state ten ticks earlier (t 511, x 662.110291,
y 135, upside down, grounded, speed 0.9) and has to reproduce every row. The control arm
(--no-upsidecoyote) is the model without the linger: it must NOT match the fixGravityBug=0 press.
The cells separate the three models: without the linger the fixGravityBug=0 press fails (+3..+5);
with the linger ungated (the build before it was gated) the fixGravityBug=1 press fails (+3..+5);
only the gated linger passes all of them.

GDSOLVER_LEVELDP points this at another build.
"""

from __future__ import annotations

import csv
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gdtas.paths import LEVELDP_EXE
from test_growth_rig import OBJ_HEADER

# (id, type, cx, cy, w, h, uid, w0, h0); every other column is 0
ROWS = [(11, 3, 300, 105, 25, 75, 12, 25, 75)] \
    + [(1, 0, 315 + 30 * i, 165, 30, 30, 13 + i, 30, 30) for i in range(12)] \
    + [(1, 0, 615 + 30 * i, 405, 30, 30, 26 + i, 30, 30) for i in range(21)] \
    + [(1, 0, 3600, 105, 30, 30, 25, 30, 30)]
START = "511,662.110291,135,0,0,1,0,1,0,0,0,0,0,0,0.899999976,0,0,0,-1,0,0,0,0,0,0,-1,-1"
OFF = 521          # the first tick off the row

# (fixGravityBug, press tick or None) -> GD's (y, vy) on rows OFF .. OFF+5
GD = {
    (0, None): [(135.0486, 0.216), (135.1458, 0.432), (135.2916, 0.648), (135.4860, 0.864),
                (135.7290, 1.08), (136.0206, 1.296)],
    (0, OFF + 2): [(135.0486, 0.216), (135.1458, 0.432), (135.2916, 0.648), (135.4860, -11.18),
                   (133.0191, -10.964), (130.6008, -10.748)],
    (1, None): [(135.0486, 0.216), (135.1458, 0.432), (135.2916, 0.648), (135.4860, 0.864),
                (135.7290, 1.08), (136.0206, 1.296)],
    (1, OFF + 2): [(135.0486, 0.216), (135.1458, 0.432), (135.2916, 0.648), (135.4860, 0.864),
                   (135.7290, 1.08), (136.0206, 1.296)],
}
TOL = 0.0006


def replay(fgb: int, press: int | None, extra: list[str]) -> dict[int, tuple[float, float]]:
    with tempfile.TemporaryDirectory() as t:
        d = Path(t)
        rows = []
        for oid, typ, cx, cy, w, h, uid, w0, h0 in ROWS:
            rows.append(",".join(str(c) for c in [oid, typ, cx, cy, w, h, 0, uid, 0, 0, 0, 0, 0, 0, 0,
                                                  w0, h0] + [0] * 33))
        (d / "objrects.txt").write_text(OBJ_HEADER + "\n" + "\n".join(rows) + "\n")
        (d / "levelsettings.txt").write_text(f"fixGravityBug={fgb}\n")
        plan = "input=1,0\n" if press is None else f"input={press},1\ninput={press + 6},0\n"
        (d / "plan.txt").write_text(plan)
        subprocess.run([str(LEVELDP_EXE), str(d / "objrects.txt"), "--replay", str(d / "plan.txt"),
                        "--start", START, "--levelsettings", str(d / "levelsettings.txt"),
                        "--out", str(d / "m"), "--cap", "50"] + extra,
                       capture_output=True, text=True, timeout=120, check=True)
        with open(d / "m.trace.csv", encoding="utf-8-sig") as f:
            return {int(r["tick"]): (float(r["y"]), float(r["vy"])) for r in csv.DictReader(f)}


class CoyoteRig(unittest.TestCase):
    def test_every_cell_matches_gd(self):
        extra = sys.argv[1:] if __name__ == "__main__" else []
        for (fgb, press), want in GD.items():
            got = replay(fgb, press, extra)
            for k, (gy, gvy) in enumerate(want):
                t = OFF + k
                with self.subTest(fgb=fgb, press=press, row=f"+{k}"):
                    self.assertIn(t, got)
                    self.assertAlmostEqual(got[t][0], gy, delta=TOL)
                    self.assertAlmostEqual(got[t][1], gvy, delta=TOL)

    def test_control_without_the_linger_misses_the_jump(self):
        got = replay(0, OFF + 2, ["--no-upsidecoyote"])
        self.assertGreater(abs(got[OFF + 3][1] - GD[(0, OFF + 2)][3][1]), 1.0)


if __name__ == "__main__":
    # extra leveldp flags may follow, for running an older build with its flags
    unittest.main(argv=sys.argv[:1])
