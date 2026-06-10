"""Speech-to-text for incoming robot mic audio (PCM16 mono @ 16 kHz).

Engines (set via STT_MODE in .env):
  none    -> disabled.
  whisper -> faster-whisper (offline). pip install faster-whisper; first run
             downloads the model. Set STT_MODEL (default 'base').

Used together with ENABLE_MIC_STREAMING=1 in the firmware: the controller streams
voice audio between 'voice_audio' and 'voice_end' events; the backend buffers it
and calls transcribe() on 'voice_end'.
"""
import os
import asyncio
import config

_model = None

def _load():
    global _model
    if _model is None:
        from faster_whisper import WhisperModel
        name = os.getenv("STT_MODEL", "base")
        _model = WhisperModel(name, device="cpu", compute_type="int8")
    return _model

def _transcribe_sync(pcm16: bytes, sample_rate: int) -> str:
    import numpy as np
    audio = np.frombuffer(pcm16, dtype="<i2").astype("float32") / 32768.0
    if audio.size < sample_rate * 0.3:   # ignore <0.3s blips
        return ""
    # faster-whisper expects 16 kHz float32 mono and accepts no sample-rate
    # argument — resample first if the robot ever streams at another rate.
    if sample_rate != 16000:
        n_out = int(round(audio.size * 16000 / sample_rate))
        audio = np.interp(np.linspace(0, audio.size - 1, n_out),
                          np.arange(audio.size), audio).astype("float32")
    segments, _ = _load().transcribe(audio, language=None, vad_filter=True)
    return " ".join(s.text.strip() for s in segments).strip()

async def transcribe(pcm16: bytes, sample_rate: int = 16000) -> str:
    """Return recognized text, or '' if STT disabled / nothing heard."""
    if config.STT_MODE == "none" or not pcm16:
        return ""
    if config.STT_MODE != "whisper":
        print(f"[stt] unknown STT_MODE={config.STT_MODE}")
        return ""
    try:
        return await asyncio.to_thread(_transcribe_sync, pcm16, sample_rate)
    except Exception as e:
        print(f"[stt] transcription failed: {e}")
        return ""
