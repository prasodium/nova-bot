"""Voice interaction: wake-name gating, a fast direct-command parser for simple
phrases, and an LLM fallback for anything conversational.

Flow (see server.on_voice_end):
  transcript --addressed()--> stripped --parse_direct_command()--> action?
                                              |no
                                              v
                                          reply()  (LLM, can see the camera)
"""
import re
import json
import config
from anthropic import AsyncAnthropic
from ai import vision

_client: AsyncAnthropic | None = None
def _c() -> AsyncAnthropic:
    global _client
    if _client is None:
        _client = AsyncAnthropic(api_key=config.ANTHROPIC_API_KEY)
    return _client


def addressed(transcript: str) -> tuple[bool, str]:
    """Was the robot addressed by name? Returns (addressed, remainder_text).
    If WAKE_REQUIRED is false, every utterance counts as addressed."""
    text = transcript.strip()
    if not config.WAKE_REQUIRED:
        return True, text
    # Whole-word match only, so "Nova" inside "innovation" does not trigger.
    m = re.search(rf"\b{re.escape(config.ROBOT_NAME)}\b", text, re.IGNORECASE)
    if not m:
        return False, ""
    # Strip everything up to and including the name (+ trailing comma/space).
    rest = text[m.end():].lstrip(" ,.:!?")
    return True, rest


# Direct phrases -> action. Checked before calling the LLM (fast + free + offline-ish).
_DIRECT = [
    (r"\b(stop|halt|freeze|wait)\b",                         "stop"),
    (r"\b(turn around|about face|spin around|go back)\b",    "turn_around"),
    (r"\b(back(ward)?|reverse)\b",                            "backward"),
    (r"\b(left)\b",                                           "left"),
    (r"\b(right)\b",                                          "right"),
    (r"\b(forward|ahead|straight|go)\b",                      "forward"),
]

def parse_direct_command(text: str) -> str | None:
    """Map a simple movement phrase to an action, or None if it's not one."""
    low = text.lower()
    for pat, action in _DIRECT:
        if re.search(pat, low):
            return action
    return None


SYSTEM = """You are {name}, the voice of a small two-wheeled robot. A human just spoke to you.
You may also be given your current camera view. Answer briefly and naturally (one or two
sentences), as if speaking aloud, and you may refer to yourself as {name}. If the human
asked you to move, set an action. Safety first: never drive forward if something is close
ahead (check distance_cm in telemetry).

Reply with ONLY JSON, no markdown:
{{"say": string, "action": "forward"|"backward"|"left"|"right"|"turn_around"|"stop"|null, "speed": number 0..1}}"""

async def reply(transcript: str, jpeg: bytes | None, telemetry: dict) -> dict:
    fallback = {"say": "Sorry, I didn't catch that.", "action": None, "speed": 0.0}
    if not config.ANTHROPIC_API_KEY:
        return fallback
    content = []
    if jpeg:
        content.append({"type": "image", "source": {
            "type": "base64", "media_type": "image/jpeg", "data": vision.to_base64(jpeg)}})
    content.append({"type": "text",
                    "text": f'The human said: "{transcript}". Telemetry: {json.dumps(telemetry)}'})
    try:
        msg = await _c().messages.create(
            model=config.AI_MODEL, max_tokens=config.AI_MAX_TOKENS,
            system=SYSTEM.format(name=config.ROBOT_NAME),
            messages=[{"role": "user", "content": content}])
        text = "".join(b.text for b in msg.content if getattr(b, "type", "") == "text").strip()
        if text.startswith("```"):
            text = text.strip("`")
            if text.lower().startswith("json"): text = text[4:]
        s, e = text.find("{"), text.rfind("}")
        d = json.loads(text[s:e + 1])
        return {"say": str(d.get("say", ""))[:200],
                "action": (d.get("action") or None),
                "speed": float(d.get("speed", 0.4))}
    except Exception as ex:
        print(f"[chat] reply failed: {ex}")
        return fallback
