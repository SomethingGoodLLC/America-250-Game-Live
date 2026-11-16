"""What a clip must do to count as the motion its name says (clip_specs.yaml `expect`).

  travel_min_m / travel_max_m   farthest the hips get from where they start, on the ground plane
  direction                     fwd | back | left | right: which way the hips end up (cosine at least 0.6)
  start_hip_max_m               hips this low at the first frame (lying or kneeling)
  end_hip_max_m / end_hip_min_m hips this low / this high at the last frame (lying / standing)
Kimodo space: X is the character's left, Y up, Z forward; he starts facing +Z.
"""
import numpy as np

DIRS = {"fwd": (0.0, 1.0), "back": (0.0, -1.0), "left": (1.0, 0.0), "right": (-1.0, 0.0)}


def failures(root_positions, expect) -> list:
    if not expect:
        return []
    r = np.asarray(root_positions, dtype=float)
    d = r[:, [0, 2]] - r[0, [0, 2]]
    travel = float(np.linalg.norm(d, axis=1).max())
    out = []
    if "travel_min_m" in expect and travel < expect["travel_min_m"]:
        out.append("travel %.2f m < %.2f" % (travel, expect["travel_min_m"]))
    if "travel_max_m" in expect and travel > expect["travel_max_m"]:
        out.append("travel %.2f m > %.2f" % (travel, expect["travel_max_m"]))
    if "direction" in expect:
        n = float(np.linalg.norm(d[-1]))
        cos = float(np.dot(d[-1], DIRS[expect["direction"]]) / n) if n > 1e-6 else 0.0
        if cos < 0.6:
            out.append("direction %s cos %.2f" % (expect["direction"], cos))
    if "start_hip_max_m" in expect and r[0, 1] > expect["start_hip_max_m"]:
        out.append("start hips %.2f m > %.2f" % (r[0, 1], expect["start_hip_max_m"]))
    if "end_hip_max_m" in expect and r[-1, 1] > expect["end_hip_max_m"]:
        out.append("end hips %.2f m > %.2f" % (r[-1, 1], expect["end_hip_max_m"]))
    if "end_hip_min_m" in expect and r[-1, 1] < expect["end_hip_min_m"]:
        out.append("end hips %.2f m < %.2f" % (r[-1, 1], expect["end_hip_min_m"]))
    return out
