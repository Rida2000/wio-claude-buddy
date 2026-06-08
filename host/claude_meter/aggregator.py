"""Pure panel math over a list of UsageEvent. No I/O."""
from __future__ import annotations

from datetime import datetime, tzinfo

from .scanner import UsageEvent, total_tokens
from .pricing import cost_usd


def local_midnight(now_ts: float, tz: tzinfo) -> float:
    dt = datetime.fromtimestamp(now_ts, tz)
    return dt.replace(hour=0, minute=0, second=0, microsecond=0).timestamp()


def today_panel(events: "list[UsageEvent]", now_ts: float, tz: tzinfo) -> dict:
    start = local_midnight(now_ts, tz)
    tokens = 0
    cost = 0.0
    for e in events:
        if e.ts >= start:
            tokens += total_tokens(e)
            c = cost_usd(e.model, e.input_tokens, e.output_tokens,
                         e.cache_creation, e.cache_read)
            if c is not None:
                cost += c
    return {"tokens": tokens, "cost_usd": round(cost, 2)}


def live_panel(events: "list[UsageEvent]", now_ts: float) -> dict:
    if not events:
        return {"tokens": 0, "idle_s": 0, "model": ""}
    latest = max(events, key=lambda e: e.ts)
    sess = latest.session_id
    in_sess = [e for e in events if e.session_id == sess]
    tokens = sum(total_tokens(e) for e in in_sess)
    return {
        "tokens": tokens,
        "idle_s": max(0, int(now_ts - latest.ts)),
        "model": latest.model,
    }


def _current_block_start(events: "list[UsageEvent]", now_ts: float, window_s: float) -> "float | None":
    """First event ts of the contiguous block ending at/around now, where a new
    block begins after any gap > window_s. None if no events within the window."""
    in_win = sorted((e.ts for e in events if now_ts - window_s <= e.ts <= now_ts))
    if not in_win:
        return None
    start = in_win[0]
    prev = in_win[0]
    for ts in in_win[1:]:
        if ts - prev > window_s:
            start = ts
        prev = ts
    return start


def estimate_limit(events, now_ts, window_s, cap, tz) -> dict:
    used = sum(total_tokens(e) for e in events if now_ts - window_s <= e.ts <= now_ts)
    start = _current_block_start(events, now_ts, window_s)
    if start is None:
        reset_s = 0
    else:
        reset_s = max(0, int(start + window_s - now_ts))
    pct = min(100, round(100 * used / cap)) if cap > 0 else 0
    reset_at = now_ts + reset_s
    label = datetime.fromtimestamp(reset_at, tz).strftime("%a %H:%M")
    return {
        "used": int(used),
        "cap": int(cap),
        "pct": int(pct),
        "reset_s": int(reset_s),
        "reset_label": label,
        "src": "est",
    }
