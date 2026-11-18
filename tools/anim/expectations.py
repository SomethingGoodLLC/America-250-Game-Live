"""What a clip must do to count as the motion its name says (clip_specs.yaml `expect`).

  travel_min_m / travel_max_m   farthest the hips get from where they start, on the ground plane
  direction                     fwd | back | left | right: which way the hips end up (cosine at least 0.6)
  start_hip_max_m               hips this low at the first frame (lying or kneeling)
  end_hip_max_m / end_hip_min_m hips this low / this high at the last frame (lying / standing)
  start_hip_min_m               hips this high at the first frame (he starts on his feet)
From the joints (when given):
  start_lean_max_m              head no further than this from above the hips at the first frame (he starts upright)
  stance_max_m                  ankles no further apart sideways than this, median over the clip
  hands_apart_max_m             the hands never further apart than this (no arms flung wide in an idle)
  hand_high_max_m               neither hand ever higher than this
  head_low_min_m                the head never lower than this (a flinch that does not bend double)
  lean_max_m                    the head never further than this from above the hips (he stands upright throughout)
  hand_out_max_m                neither hand ever further than this from the hips, on the ground plane
Kimodo space: X is the character's left, Y up, Z forward; he starts facing +Z.
"""
import numpy as np

DIRS = {"fwd": (0.0, 1.0), "back": (0.0, -1.0), "left": (1.0, 0.0), "right": (-1.0, 0.0)}


def failures(root_positions, expect, joints=None, index=None) -> list:
    if not expect:
        return []
    out_j = []
    if joints is not None and index is not None:
        pj = np.asarray(joints, dtype=float)
        head, hips = pj[:, index["Head"]], pj[:, index["Hips"]]
        lh, rh = pj[:, index["LeftHand"]], pj[:, index["RightHand"]]
        if "start_lean_max_m" in expect:
            lean = float(np.linalg.norm((head[0] - hips[0])[[0, 2]]))
            if lean > expect["start_lean_max_m"]:
                out_j.append("start lean %.2f m > %.2f" % (lean, expect["start_lean_max_m"]))
        if "stance_max_m" in expect:
            right = pj[:, index["RightLeg"]] - pj[:, index["LeftLeg"]]
            right[:, 1] = 0.0
            right /= np.maximum(np.linalg.norm(right, axis=1, keepdims=True), 1e-6)
            apart = float(np.median(np.abs(((pj[:, index["RightFoot"]] - pj[:, index["LeftFoot"]]) * right).sum(axis=1))))
            if apart > expect["stance_max_m"]:
                out_j.append("stance %.2f m > %.2f" % (apart, expect["stance_max_m"]))
        if "hands_apart_max_m" in expect:
            d = float(np.linalg.norm(lh - rh, axis=1).max())
            if d > expect["hands_apart_max_m"]:
                out_j.append("hands %.2f m apart > %.2f" % (d, expect["hands_apart_max_m"]))
        if "hand_high_max_m" in expect:
            h = float(max(lh[:, 1].max(), rh[:, 1].max()))
            if h > expect["hand_high_max_m"]:
                out_j.append("hand at %.2f m > %.2f" % (h, expect["hand_high_max_m"]))
        if "head_low_min_m" in expect:
            h = float(head[:, 1].min())
            if h < expect["head_low_min_m"]:
                out_j.append("head down to %.2f m < %.2f" % (h, expect["head_low_min_m"]))
        if "lean_max_m" in expect:
            lean = float(np.linalg.norm((head - hips)[:, [0, 2]], axis=1).max())
            if lean > expect["lean_max_m"]:
                out_j.append("lean %.2f m > %.2f" % (lean, expect["lean_max_m"]))
        if "hand_out_max_m" in expect:
            far = float(max(np.linalg.norm((h - hips)[:, [0, 2]], axis=1).max() for h in (lh, rh)))
            if far > expect["hand_out_max_m"]:
                out_j.append("hand %.2f m out > %.2f" % (far, expect["hand_out_max_m"]))
    r = np.asarray(root_positions, dtype=float)
    d = r[:, [0, 2]] - r[0, [0, 2]]
    travel = float(np.linalg.norm(d, axis=1).max())
    out = out_j
    if "start_hip_min_m" in expect and r[0, 1] < expect["start_hip_min_m"]:
        out.append("start hips %.2f m < %.2f" % (r[0, 1], expect["start_hip_min_m"]))
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
