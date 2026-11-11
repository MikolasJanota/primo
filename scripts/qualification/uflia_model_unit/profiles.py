"""Exact profiles shared by the new producer's qualification workflows."""
from __future__ import annotations

FUZZ_PROFILES = (
    "model-all", "model-rebuild", "model-complete", "model-fixed",
    "model-names", "model-scan", "disabled", "model-strong",
)
TIMING_PROFILES = ("baseline-off", "off", "legacy-all", "model-all", "legacy-fixed", "model-fixed")


def flags(name: str) -> list[str]:
    if name == "baseline-off":
        return ["--quiet", "--stats"]
    producer = "legacy" if name.startswith("legacy") or name == "off" else "model-unit"
    policy = "off" if name in ("off", "disabled") else name.split("-")[-1]
    if policy in ("rebuild", "complete", "strong"):
        policy = "all"
    result = ["--quiet", f"--qf-uflia-lia-equality-producer={producer}",
              f"--qf-uflia-tableau-equalities={policy}"]
    if name == "model-rebuild":
        result.append("--no-qf-uflia-online-components")
    if name == "model-complete":
        result.append("--no-qf-uflia-model-combination")
    if name == "model-strong":
        result.append("--preprocessing-mode=strong")
    return result
