"""pytest setup: repeatable hypothesis runs that write nothing to the tree."""

import os
import sys
from pathlib import Path

from hypothesis import HealthCheck, Phase, settings

# The package imports itself as swap_tier_model from its parent directory.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

settings.register_profile(
    "gate", derandomize=True, database=None, deadline=None,
    max_examples=150, stateful_step_count=60,
    suppress_health_check=[HealthCheck.too_slow])
# A mutant run needs the first failure, not its shrunk form.
settings.register_profile(
    "mutant", parent=settings.get_profile("gate"),
    phases=[Phase.explicit, Phase.reuse, Phase.generate])
settings.load_profile("mutant" if os.environ.get("SWAP_TIER_MODEL_OVERRIDES")
                      else "gate")
