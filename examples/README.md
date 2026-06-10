# Examples

| File | Purpose |
|------|---------|
| `decision_sample.json` | The exact JSON shape `ai/planner.py` expects back from the vision LLM each think tick. Useful for testing `robot.controller.decision_to_commands()` offline, without a camera or an API key. |

Example offline test:

```python
from robot.controller import RobotState, decision_to_commands
import json

decision = json.load(open("examples/decision_sample.json"))
print(decision_to_commands(decision, RobotState(), max_speed=0.4))
```
