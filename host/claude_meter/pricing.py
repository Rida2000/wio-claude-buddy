"""Per-model API-equivalent pricing (USD per token). Approximate; for the
'estimated cost' readout only. Numbers are per 1M tokens, divided below."""
from __future__ import annotations

# (input, output, cache_write, cache_read) USD per 1,000,000 tokens.
_PER_M: dict[str, tuple[float, float, float, float]] = {
    "opus":   (15.0, 75.0, 18.75, 1.50),
    "sonnet": (3.0, 15.0, 3.75, 0.30),
    "haiku":  (0.80, 4.0, 1.00, 0.08),
}


def _rates(model: str) -> "tuple[float, float, float, float] | None":
    m = model.lower()
    for family, rates in _PER_M.items():
        if family in m:
            return rates
    return None


def cost_usd(model: str, inp: int, out: int, cache_write: int, cache_read: int) -> "float | None":
    r = _rates(model)
    if r is None:
        return None
    return (inp * r[0] + out * r[1] + cache_write * r[2] + cache_read * r[3]) / 1_000_000


import json
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class PlanCaps:
    session_cap: int   # total tokens (incl. cache) per rolling 5h window
    weekly_cap: int    # total tokens per rolling 7d window


# Conservative defaults keyed by substrings found in rateLimitTier.
# These are ESTIMATES to be calibrated against `/usage` (see Task C1).
_DEFAULT = PlanCaps(session_cap=20_000_000, weekly_cap=300_000_000)
_BY_TIER: list[tuple[str, PlanCaps]] = [
    ("max_20", PlanCaps(session_cap=80_000_000, weekly_cap=1_200_000_000)),
    ("max_5",  PlanCaps(session_cap=40_000_000, weekly_cap=600_000_000)),
    ("max",    PlanCaps(session_cap=40_000_000, weekly_cap=600_000_000)),
    ("pro",    PlanCaps(session_cap=20_000_000, weekly_cap=300_000_000)),
]


def caps_for(subscription_type: str, rate_limit_tier: str,
             override_path: "str | Path | None" = None) -> PlanCaps:
    if override_path is not None and Path(override_path).exists():
        d = json.loads(Path(override_path).read_text())
        return PlanCaps(session_cap=int(d["session_cap"]), weekly_cap=int(d["weekly_cap"]))
    hay = f"{subscription_type} {rate_limit_tier}".lower()
    for needle, caps in _BY_TIER:
        if needle in hay:
            return caps
    return _DEFAULT
