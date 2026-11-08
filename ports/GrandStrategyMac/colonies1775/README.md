# East Coast / Thirteen Colonies prototype

Date represented: 19 April 1775. One selectable region per colony, IDs 1–13; water is 0 and surrounding lands are 14. All files belong to the disposable TestHost project.

## Assets

- `output/Colonies1775_preview.png`: colored review sheet and legend.
- `output/T_Colonies1775_Lookup.png`: exact RGB lookup texture, 2048 × 2048; never filter, compress lossily, or add labels to it.
- `output/T_Colonies1775_Display.png`: matching display texture with border lines.
- `output/manifest.json`: stable IDs, names, source relation IDs/tags and pixel counts.
- `output/Lookup.json` and `output/MapData.json`: plugin-compatible input files.
- Unreal assets are imported under `/Game/Colonies1775`.

Rebuild source textures with `python3 scripts/build_map.py`. `scripts/import_unreal.py` imports the assets through Unreal's Python commandlet with PythonScriptPlugin and EditorScriptingUtilities enabled.

## Geography and scope

This is a playable-map prototype, not a surveyed reconstruction. Coordinates span 85°W–66°W and 29°N–48°N, with north at texture top. The texture is equirectangular; display it narrower east–west by approximately cos(38.5°) to reduce distortion.

Historical boundaries are from OpenHistoricalMap, downloaded 8 November 2026, using the archived relations and dates in `source/ohm-boundaries.json`. The response identifies the data as CC0. Massachusetts includes Maine; New York includes the contested New Hampshire Grants (later Vermont). Overlapping source boundaries are resolved by explicit ordering, prioritizing the smaller colonies; Delaware is separated from the Pennsylvania polygon. Western claims are cropped at the view boundary. These claims do not imply settlement or negate Indigenous sovereignty. Surrounding lands are context, with no asserted political ownership.

Delaware uses OHM relation 2685030, whose validity begins in 1783, as an explicitly approximate outline; it must be replaced or historically reviewed before claiming strict 1775 boundary accuracy. Source versions of other colonial boundaries also need historical review, especially Maryland's western extent and overlapping western claims. Coastlines are modern Natural Earth 1:10m land geometry and are not a reconstruction of 1775 shorelines.

Sources:
- OpenHistoricalMap: https://www.openhistoricalmap.org/ ; individual relations: `https://www.openhistoricalmap.org/relation/<relation ID>`.
- Natural Earth public-domain land data: https://www.naturalearthdata.com/about/terms-of-use/ ; archived from https://github.com/nvkelso/natural-earth-vector/blob/master/geojson/ne_10m_land.geojson .
- Historical reference: https://www.loc.gov/item/74693178/ .

No Maps Gallery artwork is included. This map is generated independently from the underlying public geography data.

The original plugin's unfinished FromMask path is not used. Creating the lookup textures and importing a MapObject does not validate every editor generation mode or the large-image GPU recoloring path.

## Using the map on this Mac

Open `TestHost/TestHost.uproject` with UE 5.8.4, then open `/Game/Colonies1775/L_Colonies1775`. To launch it as an editor-hosted game window from the repository root:

```sh
bash ports/GrandStrategyMac/colonies1775/open-map.sh
```

This level is separate from the previously packaged sample app; the existing staged app has not been recooked with this new map.

## Alignment with the author's documentation

Reviewed https://github.com/Miguel-Anastacio/GrandStrategyMapUnreal/blob/main/PLUGIN_DOCUMENTATION.md . The host enables hit-result UV support. The map uses a lookup texture, lookup JSON, data JSON with a struct ID, a MapObject, a StringMapVisualProperty named `Name`, and a matching start mode. The level retains the sample flat-map controller/game mode and uses a configured native MapPawn. Wraparound overlap volumes are disabled for this finite region.

The lookup texture uses the plugin's required settings (UserInterface/`TC_EditorIcon`, no mipmaps, sRGB off, nearest, never stream). MapData keys match the struct fields exactly (`ID`, `Name`, `Area`, `Region`). The display texture is a normal sRGB, mipmapped color texture with 3 px borders.

The map material is project-local, `M_ColoniesProvinces` (unlit). It shows the display texture and lightens the colony under the cursor by matching the plugin's `ProvinceHighlighted` color against the exact lookup texture. It does not use the author's political materials or `DynamicTexture`. On this Mac the plugin's GPU recolor (`ReplaceColorComputeShader`) reaches the GPU as an all-black dynamic texture, and the author's political materials sample whole-Earth mask/terrain textures. So this map shows only its baked colony colors; runtime recoloring (for example ownership changes) does not display. The level adds a navy frame of non-colliding planes that covers the plugin's runtime wraparound copies, and an unbound post-process volume with manual exposure, no tone curve and no bloom for true colors. The camera distance is fitted to the map bounds for a 1280 × 900 window under the engine's `AspectRatio_MaintainYFOV`.

The sample controller's HUD (Change Data, Country/Population/Terrain, debug keys Q/M/L) still appears. Its map modes do not exist on this map. Pressing Q (Show LookupTexture) crashed the game in testing; do not use it on this level.

`TestHost/Source/PortHost/PortHost.cpp` preloads the colony lookup texture after engine init, in editor builds outside commandlets only. Without it, loading the level crashes in the plugin's editor-only `UMapObject::PostLoad`: that reads the lookup texture on the CPU before the texture has platform data. The plugin source is unchanged.

`TestHost/Source/PortHost/ColoniesImportLibrary` is a small host-only automation bridge for the plugin's existing import setters, because the asset's lookup property is read-only to Python. It follows the data-loading sequence of `Upload Map Files`; the purchased plugin source is not modified. The host editor target must be built before running the Python importer. The helper uses the plugin's existing BaseMapStruct for every region, so land/ocean editor filtering by struct type is not distinguished in this prototype.

## Verification, 8 November 2026

Reimported with `scripts/import_unreal.py` (log `logs/colonies-import.log`): `COLONIES_IMPORT_SUCCESS` and `COLONIES_LEVEL_SUCCESS`; all 15 `COLONIES_TILE` lines carry the colony names (none `NO NAME`).

Launch result: the editor binary ran `/Game/Colonies1775/L_Colonies1775` in `-game -windowed` at 1280 × 900 on Metal (SF_METAL_SM6), log `logs/colonies-play.log`. `HighResShot` at frame 669 (12 s) shows the colored East Coast with colony borders inside the frame. No `wrong compression settings`, `Get Color From UV not Valid Texture`, critical or fatal lines; clean `LogExit`.

Click result: one click on central Pennsylvania logged `Clicked on Tile ID: 7`, `Color searched: (R=210,G=193,B=125,A=255)` (`#D2C17DFF` in `Lookup.json`), `UVs: X=0.398 Y=0.373`. A hover screenshot shows Pennsylvania highlighted.
