"""The 'brain': sends the latest camera frame + telemetry + mission to a
vision-capable LLM and gets back a STRICT-JSON decision.

Decision schema the model must return:
{
  "scene":  "<one short sentence describing what the robot sees>",
  "action": "forward|backward|left|right|turn_around|stop",
  "speed":  0.0-1.0,
  "say":    "<optional short sentence to speak, or empty string>"
}
"""
import json
import config
from anthropic import AsyncAnthropic
from ai import vision

_client: AsyncAnthropic | None = None

def _client_lazy() -> AsyncAnthropic:
    global _client
    if _client is None:
        _client = AsyncAnthropic(api_key=config.ANTHROPIC_API_KEY)
    return _client

SYSTEM_PROMPT = """You are the navigation brain of a small two-wheeled robot car.
You receive one forward-facing camera image plus sensor telemetry, and a mission.
Decide the single next movement.

SAFETY RULES (highest priority):
- If anything looks close ahead (large object filling the lower/center of the frame,
  a wall, a table/chair leg, a person's feet), do NOT choose "forward" — choose
  "left", "right", or "turn_around".
- If telemetry shows "blocked": true or "tilt_fault": true, never go forward;
  turn or stop. ("blocked" means the robot already bumped or stalled against something.)
- "distance_cm" is a forward-facing IR sensor (≈4-30 cm range; ~35 means clear).
  If it is below ~15, something is right in front — do NOT go forward; turn instead.
- Prefer small, cautious moves and lower speed in cluttered scenes.

Reply with ONLY a JSON object, no prose, no markdown fences:
{"scene": string, "action": "forward"|"backward"|"left"|"right"|"turn_around"|"stop", "speed": number 0..1, "say": string}
Keep "scene" under 15 words. Keep "say" empty unless something is worth announcing."""

_FALLBACK = {"scene": "uncertain", "action": "stop", "speed": 0.0, "say": ""}

def _extract_json(text: str) -> dict:
    text = text.strip()
    if text.startswith("```"):
        text = text.strip("`")
        if text.lower().startswith("json"):
            text = text[4:]
    start, end = text.find("{"), text.rfind("}")
    if start >= 0 and end > start:
        text = text[start:end + 1]
    try:
        d = json.loads(text)
        return {
            "scene": str(d.get("scene", ""))[:120],
            "action": str(d.get("action", "stop")).lower(),
            "speed": float(d.get("speed", 0.4)),
            "say": str(d.get("say", ""))[:200],
        }
    except Exception as e:
        print(f"[planner] bad JSON from model ({e}): {text[:120]!r}")
        return dict(_FALLBACK)

async def decide(jpeg: bytes, telemetry: dict, goal: str) -> dict:
    """Run one perception->decision step. Returns a decision dict."""
    if not config.ANTHROPIC_API_KEY:
        print("[planner] no ANTHROPIC_API_KEY set — returning stop")
        return dict(_FALLBACK)

    tele = {k: telemetry.get(k) for k in
            ("heading_deg", "pitch_deg", "roll_deg", "left_rpm", "right_rpm",
             "tilt_fault", "blocked", "distance_cm")}
    user_text = (f"Mission: {goal}\n"
                 f"Telemetry: {json.dumps(tele)}\n"
                 f"Decide the next move. JSON only.")

    try:
        msg = await _client_lazy().messages.create(
            model=config.AI_MODEL,
            max_tokens=config.AI_MAX_TOKENS,
            system=SYSTEM_PROMPT,
            messages=[{
                "role": "user",
                "content": [
                    {"type": "image",
                     "source": {"type": "base64", "media_type": "image/jpeg",
                                "data": vision.to_base64(jpeg)}},
                    {"type": "text", "text": user_text},
                ],
            }],
        )
        text = "".join(b.text for b in msg.content if getattr(b, "type", "") == "text")
        return _extract_json(text)
    except Exception as e:
        print(f"[planner] LLM call failed: {e}")
        return dict(_FALLBACK)
