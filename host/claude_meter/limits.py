"""Merge a local estimate with an optional exact server probe.

USAGE_API_URL is None until the real `/usage` endpoint is confirmed; the
ServerProbe parsing is exercised by unit tests with a mocked transport."""
from __future__ import annotations

import json
import urllib.request
from dataclasses import dataclass

USAGE_API_URL: "str | None" = None  # set once the endpoint is confirmed (Task C2)


@dataclass(frozen=True)
class ServerLimits:
    session: dict   # {"pct": int, "reset_s": int}
    week: dict      # {"pct": int, "reset_s": int}


class ServerProbe:
    def __init__(self, api_url: "str | None", token_loader):
        self.api_url = api_url
        self.token_loader = token_loader

    def _http_get(self, url: str, token: str, timeout: float = 5.0) -> bytes:
        req = urllib.request.Request(url, headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/json",
        })
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.read()

    def probe(self) -> "ServerLimits | None":
        if not self.api_url:
            return None
        try:
            token = self.token_loader()
            raw = self._http_get(self.api_url, token)
            d = json.loads(raw)
            fh, sd = d["five_hour"], d["seven_day"]
            return ServerLimits(
                session={"pct": round(float(fh["utilization"]) * 100),
                         "reset_s": int(fh["resets_in_seconds"])},
                week={"pct": round(float(sd["utilization"]) * 100),
                      "reset_s": int(sd["resets_in_seconds"])},
            )
        except (OSError, ValueError, KeyError, TypeError):
            return None


def merge(estimate: dict, server: "ServerLimits | None") -> dict:
    out = {"session": dict(estimate["session"]), "week": dict(estimate["week"])}
    if server is None:
        return out
    for key, srv in (("session", server.session), ("week", server.week)):
        out[key].update(srv)
        out[key]["src"] = "exact"
    return out
