"""Helpers to build the JSON commands defined in docs/PROTOCOL.md."""
from itertools import count

_ids = count(1)

def _id() -> int:
    return next(_ids)

def drive(linear: float, angular: float, duration_ms: int = 0) -> dict:
    return {"type": "cmd", "cmd_id": _id(), "action": "drive",
            "linear": round(float(linear), 3),
            "angular": round(float(angular), 3),
            "duration_ms": int(duration_ms)}

def turn_to(heading_deg: float) -> dict:
    return {"type": "cmd", "cmd_id": _id(), "action": "turn_to",
            "heading_deg": float(heading_deg)}

def stop() -> dict:
    return {"type": "cmd", "cmd_id": _id(), "action": "stop"}

def speak(text: str) -> dict:
    return {"type": "cmd", "cmd_id": _id(), "action": "speak", "text": text}

def mood(name: str) -> dict:
    return {"type": "cmd", "cmd_id": _id(), "action": "mood", "mood": name}

def config(**kwargs) -> dict:
    return {"type": "cmd", "action": "config", **kwargs}
