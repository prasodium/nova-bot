"""Text-to-speech. Renders text to PCM16 mono at the robot amp's sample rate
(default 16 kHz) so the backend can stream it to the ESP32 over WebSocket.

Engines (set via TTS_MODE in .env):
  none      -> disabled; robot just chirps locally on a `speak` command.
  pyttsx3   -> offline OS voices (espeak on Linux, NSSpeech on macOS, SAPI5 on
               Windows). No model download, no API key. Good default.
  piper     -> neural TTS (https://github.com/rhasspy/piper). Best quality,
               offline, but you must download a voice model and set PIPER_MODEL.

All engines return little-endian signed 16-bit PCM, mono, at OUT_RATE.
"""
import io
import os
import wave
import asyncio
import config

OUT_RATE = 16000   # must match AMP_SAMPLE_RATE in firmware config.h


def _resample_pcm16(data: bytes, src_rate: int, dst_rate: int) -> bytes:
    """Linear-interpolation resampler (mono int16). Avoids scipy dependency."""
    if src_rate == dst_rate:
        return data
    import numpy as np
    x = np.frombuffer(data, dtype="<i2").astype("float32")
    if x.size == 0:
        return data
    n_out = int(round(x.size * dst_rate / src_rate))
    xi = np.linspace(0, x.size - 1, n_out)
    y = np.interp(xi, np.arange(x.size), x)
    return y.astype("<i2").tobytes()


def _pyttsx3(text: str) -> bytes | None:
    try:
        import pyttsx3
    except ImportError:
        print("[tts] pyttsx3 not installed (pip install pyttsx3)")
        return None
    tmp = os.path.join(os.path.dirname(__file__), "_tts_tmp.wav")
    engine = pyttsx3.init()
    engine.save_to_file(text, tmp)
    engine.runAndWait()
    try:
        with wave.open(tmp, "rb") as w:
            rate = w.getframerate()
            ch = w.getnchannels()
            frames = w.readframes(w.getnframes())
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)
    if ch == 2:  # downmix stereo -> mono
        import numpy as np
        a = np.frombuffer(frames, dtype="<i2").reshape(-1, 2).mean(axis=1)
        frames = a.astype("<i2").tobytes()
    return _resample_pcm16(frames, rate, OUT_RATE)


def _piper(text: str) -> bytes | None:
    import subprocess
    model = os.getenv("PIPER_MODEL", "")
    if not model or not os.path.exists(model):
        print("[tts] PIPER_MODEL not set / not found")
        return None
    # piper emits raw int16 mono at the model's sample rate (commonly 22050).
    src_rate = int(os.getenv("PIPER_RATE", "22050"))
    p = subprocess.run(["piper", "--model", model, "--output_raw"],
                       input=text.encode(), capture_output=True)
    if p.returncode != 0:
        print(f"[tts] piper failed: {p.stderr.decode()[:200]}")
        return None
    return _resample_pcm16(p.stdout, src_rate, OUT_RATE)


async def synthesize(text: str) -> bytes | None:
    """Return PCM16 mono @ OUT_RATE for `text`, or None if disabled/unavailable."""
    mode = config.TTS_MODE
    if mode == "none" or not text.strip():
        return None
    fn = {"pyttsx3": _pyttsx3, "piper": _piper}.get(mode)
    if fn is None:
        print(f"[tts] unknown TTS_MODE={mode}")
        return None
    # Run the (blocking) engine off the event loop.
    return await asyncio.to_thread(fn, text)
