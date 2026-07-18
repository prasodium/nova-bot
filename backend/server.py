# =====================================================================
#  AI Autonomous Robot — Backend "Brain"
#  - Hosts the WebSocket the ESP32 main controller connects to.
#  - Runs the perception->decision loop (camera -> vision LLM -> command).
#  - Voice OUT: renders TTS and streams PCM16 audio to the robot.
#  - Voice IN : buffers mic audio from the robot, runs STT, replies (LLM+TTS).
#  - Exposes a small HTTP API for the teleop/monitor CLI.
# =====================================================================
import asyncio
import json
import time
import contextlib

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse
import uvicorn

import config
from ai import vision, planner, tts, stt, chat
from robot.controller import RobotState, decision_to_commands
from robot import protocol

app = FastAPI(title="AI Robot Brain")
state = RobotState()
state.autonomous = config.AUTONOMOUS_ON_START
goal = config.ROBOT_GOAL

robot_ws: WebSocket | None = None
last_decision: dict = {}

# voice-in capture buffer
_voice_buf = bytearray()
_capturing_voice = False
_capture_started_at = 0.0
MAX_CAPTURE_S = 6.0   # safety net: force-finalize a stuck capture (e.g. a lost voice_end)

AUDIO_FRAME_BYTES = 8192   # chunk size when streaming TTS down to the robot


async def send_cmd(cmd: dict) -> bool:
    if robot_ws is None:
        return False
    try:
        await robot_ws.send_text(json.dumps(cmd))
        return True
    except Exception as e:
        print(f"[ws] send failed: {e}")
        return False


async def say(text: str):
    """Speak `text` on the robot: stream TTS audio if enabled, else chirp."""
    if not text:
        return
    pcm = await tts.synthesize(text)
    if pcm is None:
        await send_cmd(protocol.speak(text))   # firmware plays a short chirp
        return
    await send_cmd({"type": "cmd", "action": "play_audio", "rate": tts.OUT_RATE})
    if robot_ws is None:
        return
    # Pace frames at ~real-time rate: the firmware buffers ~1 s of audio and
    # drops anything beyond that, so blasting the whole clip at once would
    # truncate long sentences. 0.8x keeps the robot's buffer comfortably fed.
    frame_s = AUDIO_FRAME_BYTES / 2 / tts.OUT_RATE   # PCM16 mono
    for i in range(0, len(pcm), AUDIO_FRAME_BYTES):
        try:
            await robot_ws.send_bytes(pcm[i:i + AUDIO_FRAME_BYTES])
        except Exception as e:
            print(f"[ws] audio send failed: {e}")
            break
        await asyncio.sleep(frame_s * 0.8)


async def on_voice_end():
    """STT -> wake-name gate -> direct command OR conversational reply."""
    global _voice_buf
    audio_bytes = bytes(_voice_buf)
    _voice_buf = bytearray()
    text = await stt.transcribe(audio_bytes, sample_rate=16000)
    if not text:
        return
    print(f"[voice] heard: {text!r}")
    await handle_utterance(text)


async def handle_utterance(text: str):
    """Wake-name gate -> direct command OR conversational reply. Shared by
    the real mic/STT path (on_voice_end) and the /voice_text test endpoint."""
    is_for_me, command = chat.addressed(text)
    if not is_for_me:
        return  # not addressed by name — ignore silently

    # Fast path: a mood word -> just change expression, no movement/LLM call.
    mood = chat.parse_direct_mood(command)
    if mood:
        await send_cmd(protocol.mood(mood))
        await say(chat.MOOD_ACK.get(mood, "Okay."))
        return

    # Fast path: simple movement phrase -> act immediately, short spoken ack.
    action = chat.parse_direct_command(command)
    if action:
        cmds = decision_to_commands({"action": action, "speed": 0.5},
                                    state, config.MAX_SPEED)
        # If a forward request got turned away for safety, say so.
        if action == "forward" and cmds and cmds[0].get("action") != "drive":
            await say("There's something ahead, so I'll turn instead.")
        else:
            await say(f"Okay, {action.replace('_', ' ')}.")
        for cmd in cmds:
            await send_cmd(cmd)
        return

    # Otherwise: conversational reply (LLM, can see the camera).
    jpeg = await vision.grab_frame()
    r = await chat.reply(command, jpeg, state.telemetry)
    if r.get("mood"):
        await send_cmd(protocol.mood(r["mood"]))
    await say(r.get("say", ""))
    act = r.get("action")
    if act:
        for cmd in decision_to_commands({"action": act, "speed": r.get("speed", 0.4)},
                                        state, config.MAX_SPEED):
            await send_cmd(cmd)


# ---------------------------------------------------------------- WebSocket
@app.websocket("/ws/robot")
async def ws_robot(ws: WebSocket):
    global robot_ws, _capturing_voice, _voice_buf, _capture_started_at
    await ws.accept()
    robot_ws = ws
    print("[ws] robot connected")
    await send_cmd(protocol.config(max_speed=config.MAX_SPEED))
    try:
        while True:
            msg = await ws.receive()
            if msg.get("type") == "websocket.disconnect":
                break
            if "bytes" in msg and msg["bytes"] is not None:
                if _capturing_voice:
                    _voice_buf.extend(msg["bytes"])
                continue
            raw = msg.get("text")
            if not raw:
                continue
            try:
                data = json.loads(raw)
            except json.JSONDecodeError:
                continue
            mtype = data.get("type")
            if mtype == "telemetry":
                state.update_telemetry(data)
                # Safety net: a voice_end can be lost (e.g. a WS hiccup right as
                # the firmware sends it), which would otherwise leave capture
                # stuck on forever since nothing else prompts a retry.
                if _capturing_voice and time.time() - _capture_started_at > MAX_CAPTURE_S:
                    print("[voice] capture timed out (lost voice_end?) — finalizing")
                    _capturing_voice = False
                    asyncio.create_task(on_voice_end())
            elif mtype == "event":
                name, val = data.get("name"), data.get("value")
                print(f"[event] {name} = {val}")
                if name == "voice_audio":
                    _capturing_voice = True
                    _capture_started_at = time.time()
                    _voice_buf = bytearray()
                elif name == "voice_end":
                    _capturing_voice = False
                    asyncio.create_task(on_voice_end())
                elif name == "shaken" and val:
                    # Firmware already reacted locally (dizzy face + sound);
                    # only the backend can produce real speech.
                    asyncio.create_task(say("Hey! Leave me alone, put me down!"))
                elif name in ("blocked", "impact", "obstacle") and val:
                    pass  # the think loop / controller already reacts
            elif mtype == "ack":
                pass
    except WebSocketDisconnect:
        pass
    finally:
        print("[ws] robot disconnected")
        if robot_ws is ws:
            robot_ws = None
        # Discard any in-flight capture — a disconnect mid-utterance means we
        # don't have the full clip anyway, and staying "stuck" would otherwise
        # silently swallow every voice command after reconnect.
        if _capturing_voice:
            _capturing_voice = False
            _voice_buf = bytearray()


# ---------------------------------------------------------------- think loop
async def think_loop():
    print("[brain] think loop started")
    while True:
        await asyncio.sleep(config.THINK_INTERVAL_S)
        if robot_ws is None or not state.autonomous:
            continue
        if state.tilt_fault:
            await send_cmd(protocol.stop())
            continue
        jpeg = await vision.grab_frame()
        if jpeg is None:
            continue
        decision = await planner.decide(jpeg, state.telemetry, goal)
        global last_decision
        last_decision = decision
        state.last_scene = decision.get("scene", "")
        print(f"[brain] {decision}")
        if decision.get("mood"):
            await send_cmd(protocol.mood(decision["mood"]))
        for cmd in decision_to_commands(decision, state, config.MAX_SPEED):
            await send_cmd(cmd)
        await say(decision.get("say", ""))


@contextlib.asynccontextmanager
async def _lifespan(application: FastAPI):
    task = asyncio.create_task(think_loop())
    yield
    task.cancel()
    with contextlib.suppress(asyncio.CancelledError):
        await task

app.router.lifespan_context = _lifespan


# ---------------------------------------------------------------- HTTP control API
@app.get("/status")
async def status():
    return {"robot_connected": robot_ws is not None, "autonomous": state.autonomous,
            "goal": goal, "telemetry": state.telemetry, "last_decision": last_decision}

@app.post("/autonomy")
async def set_autonomy(payload: dict):
    state.autonomous = bool(payload.get("on", False))
    if not state.autonomous:
        await send_cmd(protocol.stop())
    return {"autonomous": state.autonomous}

@app.post("/goal")
async def set_goal(payload: dict):
    global goal
    goal = str(payload.get("text", goal))
    return {"goal": goal}

@app.post("/drive")
async def manual_drive(payload: dict):
    state.autonomous = False
    cmd = protocol.drive(float(payload.get("linear", 0.0)),
                         float(payload.get("angular", 0.0)),
                         int(payload.get("duration_ms", 400)))
    return JSONResponse({"sent": await send_cmd(cmd), "cmd": cmd})

@app.post("/stop")
async def manual_stop():
    state.autonomous = False
    return {"sent": await send_cmd(protocol.stop())}

@app.post("/say")
async def say_endpoint(payload: dict):
    await say(str(payload.get("text", "")))
    return {"ok": True}

@app.post("/voice_text")
async def voice_text(payload: dict):
    """Test hook: run the exact wake-name + direct-command + LLM reply pipeline
    that real mic audio goes through, but skip STT — type the transcript instead."""
    text = str(payload.get("text", ""))
    if not text:
        return {"ok": False, "error": "text required"}
    print(f"[voice_text] heard: {text!r}")
    await handle_utterance(text)
    return {"ok": True}


if __name__ == "__main__":
    print(f"[brain] model={config.AI_MODEL}  vision_node={config.VISION_NODE_URL}")
    print(f"[brain] name={config.ROBOT_NAME}  tts={config.TTS_MODE} stt={config.STT_MODE}")
    print(f"[brain] WebSocket: ws://{config.HOST}:{config.PORT}/ws/robot")
    uvicorn.run(app, host=config.HOST, port=config.PORT, log_level="info")
