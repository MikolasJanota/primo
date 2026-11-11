#!/usr/bin/env python3
"""Repeat the three selected regressions, retaining the original runner unchanged.

The companion manifest orders medium13, Wisa xs-09-06, then Certora _48.
The first two compare all delivery; the last compares fixed delivery.
Each has three further rotated repetitions and separate SAT-model replay.
"""
from __future__ import annotations

import sys

import compare

if __name__ == "__main__":
    slot = int(sys.argv[sys.argv.index("--slot") + 1])
    assert 0 <= slot < 9
    compare.TIMING_PROFILES = (("legacy-all", "model-all") if slot < 6
                               else ("legacy-fixed", "model-fixed"))
    raise SystemExit(compare.main())
