"""Build the transport-agnostic v:1 wire dict from events + caps."""
from __future__ import annotations

from datetime import tzinfo

from .pricing import PlanCaps
from .aggregator import today_panel, live_panel, estimate_limit
from .limits import merge, ServerLimits

WEEK_S = 7 * 24 * 3600
SESSION_S = 5 * 3600


def build_snapshot(events, now_ts: float, caps: PlanCaps, tz: tzinfo,
                   server: "ServerLimits | None", conn: str = "ok") -> dict:
    estimate = {
        "session": estimate_limit(events, now_ts, SESSION_S, caps.session_cap, tz),
        "week": estimate_limit(events, now_ts, WEEK_S, caps.weekly_cap, tz),
    }
    limits = merge(estimate, server)
    # Session never needs a wall-clock label (always a short countdown).
    limits["session"].pop("reset_label", None)
    return {
        "v": 1,
        "ts": int(now_ts),
        "conn": conn,
        "session": limits["session"],
        "week": limits["week"],
        "today": today_panel(events, now_ts, tz),
        "live": live_panel(events, now_ts),
    }
