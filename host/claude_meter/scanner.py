"""Parse Claude Code JSONL transcripts into a usage-event stream."""
from __future__ import annotations

from datetime import datetime


def iso_to_unix(s: str) -> float:
    """ISO-8601 (with trailing 'Z' or offset) → unix seconds (float)."""
    if s.endswith("Z"):
        s = s[:-1] + "+00:00"
    return datetime.fromisoformat(s).timestamp()


import json
from dataclasses import dataclass


@dataclass(frozen=True)
class UsageEvent:
    ts: float
    session_id: str
    model: str
    input_tokens: int
    output_tokens: int
    cache_creation: int
    cache_read: int


def total_tokens(e: "UsageEvent") -> int:
    """ccusage-style total: all four token classes summed."""
    return e.input_tokens + e.output_tokens + e.cache_creation + e.cache_read


def parse_line(line: str) -> "UsageEvent | None":
    """Return a UsageEvent for assistant records with usage, else None."""
    line = line.strip()
    if not line:
        return None
    try:
        d = json.loads(line)
    except (ValueError, TypeError):
        return None
    if not isinstance(d, dict) or d.get("type") != "assistant":
        return None
    msg = d.get("message")
    if not isinstance(msg, dict):
        return None
    u = msg.get("usage")
    ts = d.get("timestamp")
    if not isinstance(u, dict) or not isinstance(ts, str):
        return None
    try:
        return UsageEvent(
            ts=iso_to_unix(ts),
            session_id=str(d.get("sessionId", "")),
            model=str(msg.get("model", "")),
            input_tokens=int(u.get("input_tokens", 0) or 0),
            output_tokens=int(u.get("output_tokens", 0) or 0),
            cache_creation=int(u.get("cache_creation_input_tokens", 0) or 0),
            cache_read=int(u.get("cache_read_input_tokens", 0) or 0),
        )
    except (ValueError, TypeError):
        return None


from pathlib import Path


class IncrementalScanner:
    """Walks *.jsonl under a root, yielding only newly-appended events."""

    def __init__(self, root: "str | Path"):
        self.root = Path(root)
        self._offsets: dict[str, int] = {}

    def scan(self) -> "list[UsageEvent]":
        events: list[UsageEvent] = []
        for path in sorted(self.root.rglob("*.jsonl")):
            key = str(path)
            try:
                size = path.stat().st_size
            except OSError:
                continue
            start = self._offsets.get(key, 0)
            if start > size:            # truncated / rotated → re-read from 0
                start = 0
            if start == size:
                continue
            try:
                with path.open("r", encoding="utf-8", errors="replace") as f:
                    f.seek(start)
                    for line in f:
                        ev = parse_line(line)
                        if ev is not None:
                            events.append(ev)
                    self._offsets[key] = f.tell()
            except OSError:
                continue
        events.sort(key=lambda e: e.ts)
        return events
