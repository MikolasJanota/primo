#!/usr/bin/env python3
"""Print one exact fuzz profile, one argument per line, for Bash mapfile."""
import sys

from profiles import FUZZ_PROFILES, flags

if __name__ == "__main__":
    print("\n".join(flags(FUZZ_PROFILES[int(sys.argv[1])])) )
