"""Central config loaded from environment / .env."""
import os
from dotenv import load_dotenv

load_dotenv()

def _f(key, default): return float(os.getenv(key, default))
def _i(key, default): return int(os.getenv(key, default))
def _b(key, default): return os.getenv(key, str(default)).lower() in ("1", "true", "yes", "on")

ANTHROPIC_API_KEY = os.getenv("ANTHROPIC_API_KEY", "")
AI_MODEL          = os.getenv("AI_MODEL", "claude-sonnet-4-6")
AI_MAX_TOKENS     = _i("AI_MAX_TOKENS", 600)

VISION_NODE_URL   = os.getenv("VISION_NODE_URL", "http://192.168.1.50").rstrip("/")
HOST              = os.getenv("HOST", "0.0.0.0")
PORT              = _i("PORT", 8000)

THINK_INTERVAL_S  = _f("THINK_INTERVAL_S", 2.0)
ROBOT_GOAL        = os.getenv("ROBOT_GOAL", "Explore safely and describe what you see.")
MAX_SPEED         = _f("MAX_SPEED", 0.4)
AUTONOMOUS_ON_START = _b("AUTONOMOUS_ON_START", False)

TTS_MODE          = os.getenv("TTS_MODE", "none")
STT_MODE          = os.getenv("STT_MODE", "none")

ROBOT_NAME        = os.getenv("ROBOT_NAME", "Nova")
WAKE_REQUIRED     = _b("WAKE_REQUIRED", True)
FRONT_STOP_CM     = _f("FRONT_STOP_CM", 12)
