import json
import os
import unreal

# PA250_POSTS_MAP=L_Concord checks the Concord level instead (same three post ids).
map_name = os.environ.get("PA250_POSTS_MAP", "L_Lexington")
path = "/Game/PA250/Maps/" + map_name
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level(path)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
posts = []
for a in actors:
    if "PA250ControlPoint" in a.get_class().get_name():
        posts.append(
            {
                "label": a.get_actor_label(),
                "post_id": str(a.get_editor_property("post_id")),
                "initial_owner": str(a.get_editor_property("initial_owner")),
            }
        )
posts.sort(key=lambda p: p["post_id"])
out = {
    "count": len(posts),
    "posts": posts,
    "passed": len(posts) == 3
    and {p["post_id"] for p in posts} >= {"green", "meetinghouse", "buckman"},
}
saved = os.path.join(
    unreal.Paths.project_saved_dir(), "PA250", map_name[2:].lower() + "_posts_verify.json"
)
os.makedirs(os.path.dirname(saved), exist_ok=True)
with open(saved, "w") as f:
    json.dump(out, f, indent=2)
unreal.log("PA250 verify_lexington_posts: %s" % out)
if not out["passed"]:
    raise RuntimeError("lexington posts verify failed: %s" % out)
