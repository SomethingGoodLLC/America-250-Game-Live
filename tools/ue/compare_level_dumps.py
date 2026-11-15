#!/usr/bin/env python3
"""Compare two level dumps (Scripts/dump_level_actors.py): the saved level against the same level
built fresh by the generator. An actor in the saved level that the fresh build does not produce
was placed by hand (or by an older generator) and would be lost by a regeneration.

  tools/ue/compare_level_dumps.py <saved.json> <fresh.json>
Exit code 0: the saved level holds nothing the generator does not produce. 1: it does (listed).
"""
import json
import sys
from collections import Counter


def key(a, with_place=True):
    k = (a["class"], a["label"], tuple(a.get("tags", [])), tuple(a.get("meshes", [])), a.get("instances", 0))
    return k + (tuple(a["loc"]), tuple(a["rot"]), tuple(a["scale"])) if with_place else k


def near(a, b, tol_cm=2.0, tol_deg=0.5):
    return (all(abs(x - y) <= tol_cm for x, y in zip(a["loc"], b["loc"]))
            and all(abs((x - y + 180) % 360 - 180) <= tol_deg for x, y in zip(a["rot"], b["rot"]))
            and all(abs(x - y) <= 0.01 for x, y in zip(a["scale"], b["scale"])))


def main():
    saved = json.load(open(sys.argv[1]))["actors"]
    fresh = json.load(open(sys.argv[2]))["actors"]
    print("saved: %d actors, fresh build: %d actors" % (len(saved), len(fresh)))
    pool = list(fresh)
    only_saved, moved = [], []
    for a in saved:
        hit = next((b for b in pool if key(a, False) == key(b, False) and near(a, b)), None)
        if hit is None:
            same = next((b for b in pool if (a["class"], a["label"]) == (b["class"], b["label"])), None)
            if same is not None:
                moved.append((a, same))
                pool.remove(same)
            else:
                only_saved.append(a)
        else:
            pool.remove(hit)
    print("identical: %d" % (len(saved) - len(only_saved) - len(moved)))
    print("same actor, different place, tags, mesh or instance count: %d" % len(moved))
    for a, b in moved[:40]:
        diffs = [f for f in ("loc", "rot", "scale", "tags", "meshes", "instances") if a.get(f) != b.get(f)]
        print("  ~ %s %s: %s" % (a["class"], a["label"], "; ".join("%s %s -> %s" % (f, a.get(f), b.get(f)) for f in diffs)))
    print("ONLY IN THE SAVED LEVEL (would be lost): %d" % len(only_saved))
    for a in only_saved[:80]:
        print("  - %s %s at %s tags %s" % (a["class"], a["label"], a["loc"], a.get("tags")))
    print("only in the fresh build (the generator adds them): %d" % len(pool))
    by_class = Counter(b["class"] for b in pool)
    for cls, n in by_class.most_common(12):
        print("  + %d x %s (e.g. %s)" % (n, cls, next(b["label"] for b in pool if b["class"] == cls)))
    return 1 if only_saved else 0


if __name__ == "__main__":
    sys.exit(main())
