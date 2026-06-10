# Backend — the Robot's Brain

A FastAPI service that:

1. Hosts the WebSocket the ESP32 main controller connects to (`/ws/robot`).
2. Pulls camera frames from the vision node and asks a vision LLM what to do.
3. Translates the LLM's JSON decision into motion commands, with a redundant
   safety check that refuses `forward` when the robot reports `blocked` or a
   close forward distance.
4. Renders TTS and streams PCM16 audio to the robot **paced at real-time
   rate** (the firmware buffers ~1 s), and transcribes incoming voice audio
   (wake-name gated) when STT is enabled.
5. Exposes an HTTP control API used by `tools/monitor.py`.

## Module Map

| Module | Responsibility |
|--------|----------------|
| `server.py` | WebSocket hub, think loop, HTTP API |
| `config.py` | Environment-driven configuration (`.env`) |
| `ai/planner.py` | Vision-LLM decision step (strict JSON) |
| `ai/chat.py` | Wake-name gating, direct-command parsing, conversational LLM replies |
| `ai/vision.py` | Frame fetching from the vision node |
| `ai/stt.py` | Speech-to-text (faster-whisper, offline) |
| `ai/tts.py` | Text-to-speech (pyttsx3 / piper, offline) |
| `robot/protocol.py` | Command builders matching `docs/PROTOCOL.md` |
| `robot/controller.py` | Robot state + decision-to-command translation |

## Run
```bash
python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env     # add your ANTHROPIC_API_KEY + node IPs
python server.py
```

## HTTP control API
| Method | Path | Body | Effect |
|--------|------|------|--------|
| GET  | `/status`   | — | connection, autonomy, telemetry, last decision |
| POST | `/autonomy` | `{"on": true}` | enable/disable the think loop |
| POST | `/goal`     | `{"text": "..."}` | change the mission |
| POST | `/drive`    | `{"linear":0.3,"angular":0,"duration_ms":400}` | manual move (disables autonomy) |
| POST | `/stop`     | — | stop now |
| POST | `/say`      | `{"text":"hi"}` | speak |

## Swapping AI providers
The LLM call lives entirely in `ai/planner.py`. To use a different provider,
reimplement `decide()` to call your API with the image + telemetry and return the
same decision dict. Everything else stays the same.
