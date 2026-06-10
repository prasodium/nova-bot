"""Fetch JPEG frames from the ESP32-S3 vision node."""
import base64
import httpx
import config

async def grab_frame() -> bytes | None:
    """Return raw JPEG bytes from the vision node, or None on failure."""
    url = f"{config.VISION_NODE_URL}/capture"
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            r = await client.get(url)
            r.raise_for_status()
            return r.content
    except Exception as e:
        print(f"[vision] could not grab frame from {url}: {e}")
        return None

def to_base64(jpeg: bytes) -> str:
    return base64.standard_b64encode(jpeg).decode("ascii")
