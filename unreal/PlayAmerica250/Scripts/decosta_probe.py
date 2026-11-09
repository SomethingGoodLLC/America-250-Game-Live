#!/usr/bin/env python3
import unreal

def _log(msg: str) -> None:
    unreal.log_error(f"DECOSTA_PROBE: {msg}")  # Error level so it always surfaces

def main() -> None:
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
    except Exception:
        world = None
    if world is None:
        _log("no world")
        return
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PA250ProvinceMapActor)
    _log(f"map_actors={len(actors)}")
    if actors:
        a = actors[0]
        _log(f"decosta_mode={a.is_de_costa_mode() if hasattr(a,'is_de_costa_mode') else 'n/a'}")
        _log(f"presentation={a.is_presentation_loaded() if hasattr(a,'is_presentation_loaded') else 'n/a'}")
    pcs = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PA250CampaignPlayerController)
    _log(f"controllers={len(pcs)}")
    if pcs and hasattr(pcs[0], "click_map_at_uv"):
        pcs[0].click_map_at_uv(0.248, 0.175)
        _log("clicked lexington UV")
    unreal.SystemLibrary.execute_console_command(world, "Shot showui", None)
    _log("Shot showui issued")
    unreal.SystemLibrary.execute_console_command(world, "QUIT", None)

try:
    unreal.set_timeout(main, 12.0)
    _log("scheduled")
except Exception as e:
    _log(f"set_timeout failed: {e}; running now")
    main()
