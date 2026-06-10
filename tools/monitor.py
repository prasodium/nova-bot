#!/usr/bin/env python3
"""Terminal teleop + telemetry monitor for the AI robot.

Talks to the backend's HTTP control API (NOT directly to the ESP32).
Run the backend first, then:  python tools/monitor.py

Keys:  W/A/S/D drive   SPACE stop   E toggle autonomy   Q quit
"""
import sys, time, threading
import httpx

BASE = "http://127.0.0.1:8000"
SPEED = 0.4

def post(path, **json):
    try:
        httpx.post(BASE + path, json=json, timeout=3.0)
    except Exception as e:
        print(f"\n[err] {path}: {e}")

def poll_status():
    while True:
        try:
            s = httpx.get(BASE + "/status", timeout=3.0).json()
            t = s.get("telemetry", {})
            d = s.get("last_decision", {})
            line = (f"conn={s['robot_connected']} auto={s['autonomous']} "
                    f"hdg={t.get('heading_deg','?')} tilt={t.get('tilt_fault','?')} "
                    f"scene={d.get('scene','')[:40]!r}        ")
            sys.stdout.write("\r" + line)
            sys.stdout.flush()
        except Exception:
            pass
        time.sleep(0.5)

def get_key():
    """Single-keypress reader (POSIX)."""
    try:
        import termios, tty
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
        try:
            tty.setraw(fd)
            return sys.stdin.read(1)
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
    except Exception:
        return sys.stdin.read(1)

def main():
    print(__doc__)
    threading.Thread(target=poll_status, daemon=True).start()
    while True:
        k = get_key().lower()
        if k == "q": post("/stop"); print("\nbye"); return
        elif k == "w": post("/drive", linear=SPEED, angular=0.0, duration_ms=400)
        elif k == "s": post("/drive", linear=-SPEED, angular=0.0, duration_ms=400)
        elif k == "a": post("/drive", linear=SPEED*0.4, angular=SPEED, duration_ms=400)
        elif k == "d": post("/drive", linear=SPEED*0.4, angular=-SPEED, duration_ms=400)
        elif k == " ": post("/stop")
        elif k == "e":
            # naive toggle: read status then flip
            try:
                cur = httpx.get(BASE + "/status", timeout=3.0).json()["autonomous"]
            except Exception:
                cur = False
            post("/autonomy", on=not cur)

if __name__ == "__main__":
    main()
