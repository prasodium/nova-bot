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
from openai import AsyncOpenAI
from ai import vision

_client: AsyncOpenAI | None = None
def _c() -> AsyncOpenAI:
    global _client
    if _client is None:
        _client = AsyncOpenAI(api_key=config.OPENAI_API_KEY)
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


# Mood words -> the firmware's display.h Mood enum. Checked before movement
# commands and before the LLM (fast + free + works with WAKE_REQUIRED=false).
_MOOD_DIRECT = [
    (r"\b(smile|happy|be happy|cheer up|joy)\b",       "happy"),
    (r"\b(angry|mad|be angry|furious)\b",               "angry"),
    (r"\b(i love you|love you|show love|love)\b",       "love"),
    (r"\b(shy|embarrassed|be shy)\b",                    "shy"),
    (r"\b(sad|upset|be sad|cry)\b",                      "sad"),
    (r"\b(sleepy|tired|sleep|nap)\b",                    "sleepy"),
    (r"\b(surprised|shocked|wow|surprise)\b",            "surprised"),
    (r"\b(dizzy|spin|spinning)\b",                        "dizzy"),
    (r"\b(normal|neutral|calm down|reset face)\b",       "neutral"),
]

# Short spoken ack per mood for the direct (non-LLM) path.
MOOD_ACK = {
    "happy": "Yay!", "angry": "Hmph.", "love": "Aww, I love you too!",
    "shy": "Oh... stop it.", "sad": "Okay...", "sleepy": "Yaaawn...", "dizzy": "Whoaaa...",
    "surprised": "Whoa!", "neutral": "Okay.",
}

def parse_direct_mood(text: str) -> str | None:
    """Map a simple mood phrase to a mood name, or None if it's not one."""
    low = text.lower()
    for pat, mood in _MOOD_DIRECT:
        if re.search(pat, low):
            return mood
    return None


SYSTEM = """You are {name}, the voice of a small two-wheeled robot. A human just spoke to you.
You may also be given your current camera view. Answer briefly and naturally (one or two
sentences), as if speaking aloud, and you may refer to yourself as {name}. If the human
asked you to move, set an action. Safety first: never drive forward if something is close
ahead (check distance_cm in telemetry). Also pick a mood/expression for your face that
fits the tone of your reply and what the human said.

Reply with ONLY JSON, no markdown:
{{"say": string, "action": "forward"|"backward"|"left"|"right"|"turn_around"|"stop"|null,
  "speed": number 0..1,
  "mood": "neutral"|"happy"|"angry"|"love"|"shy"|"sad"|"sleepy"|"surprised"|"dizzy"}}"""

async def reply(transcript: str, jpeg: bytes | None, telemetry: dict) -> dict:
    fallback = {"say": "Sorry, I didn't catch that.", "action": None, "speed": 0.0, "mood": "neutral"}
    if not config.OPENAI_API_KEY:
        return fallback
    content = [{"type": "text",
               "text": f'The human said: "{transcript}". Telemetry: {json.dumps(telemetry)}'}]
    if jpeg:
        content.append({"type": "image_url", "image_url": {
            "url": f"data:image/jpeg;base64,{vision.to_base64(jpeg)}"}})
    try:
        msg = await _c().chat.completions.create(
            model=config.AI_MODEL, max_tokens=config.AI_MAX_TOKENS,
            response_format={"type": "json_object"},
            messages=[
                {"role": "system", "content": SYSTEM.format(name=config.ROBOT_NAME)},
                {"role": "user", "content": content},
            ])
        text = msg.choices[0].message.content.strip()
        d = json.loads(text)
        return {"say": str(d.get("say", ""))[:200],
                "action": (d.get("action") or None),
                "speed": float(d.get("speed", 0.4)),
                "mood": str(d.get("mood") or "neutral")}
    except Exception as ex:
        print(f"[chat] reply failed: {ex}")
        return fallback
