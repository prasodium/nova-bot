"""Tracks the single connected robot and its latest telemetry, and turns the
planner's abstract decision into concrete protocol commands."""
from . import protocol

class RobotState:
    def __init__(self):
        self.telemetry: dict = {}
        self.autonomous: bool = False
        self.last_scene: str = ""

    def update_telemetry(self, t: dict):
        self.telemetry = t

    @property
    def heading(self) -> float:
        return float(self.telemetry.get("heading_deg", 0.0))

    @property
    def tilt_fault(self) -> bool:
        return bool(self.telemetry.get("tilt_fault", False))

    @property
    def blocked(self) -> bool:
        return bool(self.telemetry.get("blocked", False))

    @property
    def front_cm(self) -> float:
        # Large default so a missing sensor never blocks motion.
        return float(self.telemetry.get("distance_cm", 999.0))


# Map an LLM "action" decision to motion command(s). Speech is handled separately
# by the backend's audio pipeline (TTS), not here.
def decision_to_commands(decision: dict, state: RobotState, max_speed: float) -> list[dict]:
    import config
    if state.tilt_fault:
        return [protocol.stop()]

    act = (decision.get("action") or "stop").lower()
    speed = min(float(decision.get("speed", 0.5)), 1.0) * max_speed

    # Don't drive forward into something: either the stall/impact latch (blocked)
    # or the Sharp IR sensor reading too close. The firmware also hard-stops, but
    # we avoid even issuing the command. Turn away instead.
    if act == "forward" and (state.blocked or state.front_cm < config.FRONT_STOP_CM):
        act = "turn_around"

    if act == "forward":
        return [protocol.drive(speed, 0.0, duration_ms=800)]
    elif act == "backward":
        return [protocol.drive(-speed, 0.0, duration_ms=800)]
    elif act == "left":
        return [protocol.drive(speed * 0.4, +speed, duration_ms=600)]
    elif act == "right":
        return [protocol.drive(speed * 0.4, -speed, duration_ms=600)]
    elif act == "turn_around":
        return [protocol.turn_to((state.heading + 180) % 360)]
    else:  # stop / unknown
        return [protocol.stop()]
