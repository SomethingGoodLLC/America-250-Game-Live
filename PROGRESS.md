# PROGRESS — Play America 250

**Vertical slice M0–M8 complete. Battle Feel B1–B4 complete.** Nothing committed or stashed; all changes are uncommitted in the working tree.

## Toolchain facts
- Xcode 27.2 (27B52), Apple clang 21.0.0; cmake 3.30.0; Python 3.13.7 (service venv: `services/negotiation/.venv`).
- Engine: `/Users/Shared/Epic Games/UE_5.8` (prebuilt editor).
- git-lfs is **not installed**.

## Milestones and evidence
| M | Status | Verification evidence |
|---|--------|------------------------|
| M0–M8 | done | See prior slice table; `scripts/run_slice_autoplay.sh` still green. |
| B1 Conquest | done | Three posts on `L_Lexington` (`green` neutral, `meetinghouse`/`buckman` continental) verified via `Scripts/verify_lexington_posts.py`. Capture = attacker-rate, contested pauses, enemy→neutral→owned. Spawn-spend tickets; bleed; spawn screen (skipped in autoplay). `BattleResult.objectives_held` + plurality `objective_holder`. Autoplay traces: `post_flips≥1`, `bleed_ticks≥1` on every successful run sampled. Module build Succeeded. |
| B2 Active reload | done | Plain `FActiveReloadSequencer` (no UObject); `AMusketWeapon` owns it; `IA_Reload` commit / hold-clear jam; reload vulnerability 1.5×. `-run=PA250Combat` commandlet: perfect/sweet/miss/jam/clear + damage bonus once. Autoplay `trace.reload` counters present (e.g. perfect/sweet/miss/jam ≈ 100/180/110/12). |
| B3 Spread / volley | done | Musket spread cone from tuning; `FormationQuery` line detection; `UPA250VolleyCoordinator` squads + suppression. Commandlet line/volley tests pass. Autoplay: `volleys.count` 30–59, `avg_hits` 3.0; lines in_line≈engagements when squads fire (e.g. 30/31). |
| B4 Balance + wrap | done | Five successful autoplay battles recorded below. `RUNS=2 scripts/run_slice_autoplay.sh` PASSED (cross-run pre-battle hash identity). Tuning in `DA_MusketTuning` / `FMusketTuningValues` nested structs. |

### B4 autoplay battle sample (10× dilation; game-time lengths)
| # | winner | reason | elapsed_s | flips | bleed | volleys | jam | notes |
|---|--------|--------|-----------|-------|-------|---------|-----|-------|
| 1 | continental | time_limit | 720 | 1 | 3 | 46 | 11 | lines 12/12 |
| 2 | british | time_limit | 720 | 3 | 3 | 44 | 16 | |
| 3 | continental | time_limit | 720 | 1 | 4 | 59 | 24 | lines 30/31 |
| 4 | british | time_limit | 720 | 3 | 4 | 30 | 11 | |
| 5 | british | tickets_exhausted | 47 | 1 | 3 | 43 | 12 | short outlier |

Winners 2 continental / 3 british (neither >4/5). Typical length 720 s game ≈ **12 min** at 1×. Jams uncommon (~3–6% of phase attempts).

## Commands
```
# sim
cmake -S unreal/PA250Sim -B unreal/PA250Sim/build && cmake --build unreal/PA250Sim/build && ctest --test-dir unreal/PA250Sim/build
# unreal module
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" PlayAmerica250Editor Mac Development -project="$PWD/unreal/PlayAmerica250/PlayAmerica250.uproject"
# regenerate Lexington
PA250_LEVELS=lexington UnrealEditor-Cmd ... -run=pythonscript -script=unreal/PlayAmerica250/Scripts/build_levels.py -unattended -nullrhi
# combat unit tests (commandlet; preferred over Automation on this Mac)
UnrealEditor-Cmd ... -run=PA250Combat -unattended -nullrhi
UnrealEditor-Cmd ... -run=PA250Battle -unattended -nullrhi
# Lexington stills / FPS (rendered; editor binary in -game, 1280x720 windowed)
UnrealEditor.app/Contents/MacOS/UnrealEditor <uproject> /Game/PA250/Maps/L_Lexington -game -windowed -ResX=1280 -ResY=720 -PA250Preview=after   # -> docs/preview/lexington_after_*.png
UnrealEditor.app/Contents/MacOS/UnrealEditor <uproject> /Game/PA250/Maps/L_Lexington -game -windowed -ResX=1280 -ResY=720 -PA250FpsSample      # -> Saved/PA250/lexington_fps.json
# whole slice
scripts/run_slice_autoplay.sh            # RUNS=2 for determinism
```

## Decisions (Battle Feel + earlier)
- **Battle tuning** lives in expanded `DA_MusketTuning` / `FMusketTuningValues` (Conquest, ActiveReload, Accuracy, Volley nested structs). No second data asset.
- **`objectives_held`** map added to `BattleResult`; `objective_holder` = plurality post owner (tie → empty) so `apply_battle_result` ownership flip is unchanged.
- **Tickets spend on spawn**, not death. Losses = `Initial − RemainingPool − AliveOnField`.
- **Dropped 75 s objective-hold win.** Win by tickets left / time limit / more posts if both exhausted. Bleed: fewer posts → −1 ticket / `BleedIntervalSeconds` (default 8).
- **Sweet reload ≈ 7 s** (sum of 2+3+2 phases); perfect scale 0.75 + 15% next-shot damage. Historical 20 s abandoned for feel.
- **Default `BotsPerSide = 16`.** Autoplay at 10× dilation still finishes under the shell timeout with `TimeLimitSeconds=720`.
- **`Conquest.SoldiersPerUnit = 4` (GAME INVENTION).** One campaign unit buys 4 battle respawns. `InitialPool` stays in campaign units; the live pool is in soldiers. `BattleResult.losses` = round(soldiers lost / 4), clamped to the unit pool, so `losses ≤ pools` and the result schema are unchanged. Reason: with 1 ticket per unit, 16 bots spawned from a 22-unit militia left 6 respawns, and battles on the open map ended in 50–93 game-seconds. Override per run with `-PA250SoldiersPerUnit=N`; the value is recorded in `battle_result.json` as `soldiers_per_unit`.
- **`IA_Reload`**: tap = phase commit; hold = clear jam. Bots auto-commit by class skill.
- **Direct `-game L_Lexington` / Automation RunTests** sometimes SIGBUS in `FMacCursor`/ImageIO on this Mac; **commandlets and `run_slice_autoplay.sh` (via L_Campaign) work.** Prefer those paths for CI/headless.
- **`WriteBattleFile`** concatenates setup/result JSON (no `Printf` of embedded JSON) to avoid `%` corruption.
- Prior slice decisions (sim authority, BattleResult-only campaign writes, hitscan retired, MuseTalk chain, etc.) unchanged.

## Visual Pass — Lexington (dawn battlefield)
**Status: done.** The FPS gate passes since the KiteDemo pass (see Verification). Everything comes from `Scripts/build_levels.py` (`PA250_LEVELS=lexington`); gameplay code was not changed, apart from the camera and visual-only asset hooks.

### Decisions
- **Level owns exposure.** `PP_Dawn` (unbound) is the only exposure source. The per-camera "EV 7" overrides in `PA250BattleGameMode` were removed: they beat the volume, which is why the first after-stills read as night. Manual exposure, physical-camera exposure off, `DAWN_EXPOSURE_EV = 1.6` with `DAWN_SUN_LUX = 10` (a pair at the top of the generator).
- **Dawn light:** sun pitch −15°, yaw 165° (sun in the ENE behind the British road, light heading WSW), colour (255,165,95), movable, atmosphere sun. SkyAtmosphere, real-time-capture SkyLight, VolumetricCloud, ExponentialHeightFog with volumetric fog (`enable_volumetric_fog`, density 0.014, falloff 0.3, warm directional inscattering). Lumen GI + reflections on (`DefaultEngine.ini`).
- **Grade:** bloom 0.4, vignette 0.45, `white_temp` 7200 (higher = warmer in UE), gain (1.16,1.0,0.76), saturation 0.78, contrast 0.95, film toe 0.6.
- **Ground:** the box plus `SM_Ramp` wedges (55–90 cm cliffs) is replaced by a generated heightfield `SM_PA250_Terrain`: 620×510 m, 2.5 m cells, written as glTF and imported through Interchange with complex-as-simple collision. It stays nearly flat (±45 cm) around the Green and rolls up to about ±10 m at the edges. Flat pads sit under the buildings, posts and spawns. Every placed actor uses the same `ground_z(x, y)`. The runtime navmesh (`Dynamic`) builds on it.
- **Meadow material:** grass/dry-grass from texture-based gradient noise at 60 m and 4.5 m scales, trampled grass around the Green and posts, and dirt roads. The road and trampled masks are computed in a Custom HLSL node from world position, generated from the same `ROADS` list as the level. Interchange dropped both glTF `COLOR_0` and a second UV set, so masks no longer depend on the mesh. Computational Simplex noise cost about 13 fps on the M3 and was replaced with the texture-based noise.
- **Buildings:** Meetinghouse moved to (−3400,−5650) and Buckman Tavern to (−5300,1500); both used to contain their capture posts. Each is a clapboard body on a stone foundation with a 45° gable roof (a rolled square prism with eaves and a barge edge at the gable ends), two rows of sash windows with trim, a door and trim, and chimneys on the tavern. The belfry has a tower, a lantern with louvres and a cone spire.
- **Dressing:** dry-stone walls in irregular courses along both sides of the Concord road and an east field wall, split-rail fences (posts every 3.2 m, two rails) around the Common, and 20 trees with three-lobe crowns. Dressing uses a fixed seed (`random.Random(1775)`).
- **Mesh fit by bounds:** `fit_mesh()` scales any mesh by its own bounds and pivot. That fixed the floating roof slabs (SM_Ramp/ChamferCube aren't 100 cm centred) and makes Fab meshes land at the right size with their own materials. Detail pieces under 3 m don't contribute to Lumen or distance fields. Hundreds of tiny meshes were causing about 11 ms/frame of surface-cache card captures.
- **Camera (`PA250BattleGameMode`):** `FrameHumanCamera` (350 cm arm, socket (0,60,65), lag, −15° toward the post with `PostId == green`) runs on every spawn path: RestartPlayer, RequestSpawn and OnControllerRespawned. The autoplay spectator arm ignores the bot's AI control rotation (`bUsePawnControlRotation = false`, −10° relative pitch, 480 cm, low right shoulder, lagged), so it can't go top-down. It re-targets when the viewed soldier dies. Bot aim is unaffected.
- **Preview stills:** `-PA250Preview=<label>` uses the level's tagged `Shot_*` cameras (fallback table in C++ has the same transforms). Stills are HUD-free (`bShowUI=false`), wait 4 s at start and 1.5 s per cut for Lumen, and poll for a fresh file.
- **Before stills** come from the committed `L_Lexington.umap` and committed `DefaultEngine.ini`, swapped in temporarily, at the same cameras. That map's stationary lights render pure black in `-game` with Lumen off and no built lighting, so the before stills are captured with `viewmode unlit` (true geometry and layout of the committed level).
- **FPS sample** (`-PA250FpsSample`) now uses real frame time (`FApp::GetDeltaTime`), not dilated game time. It starts 5 s in and averages 10 s. The JSON records bots per side, soldiers alive, GI/reflection method, time dilation, game/render/GPU ms and resolution.
- `r.VolumetricFog.GridPixelSize=16` in `[SystemSettings]` (fog cost about 3.4 ms at 8).

### Verification
| Check | Result |
|-------|--------|
| Regenerate | `PA250_LEVELS=lexington UnrealEditor-Cmd … -run=pythonscript -script=Scripts/build_levels.py` → `built L_Lexington`; terrain bounds origin (1000,500) confirm the glTF axis mapping |
| Stills | `docs/preview/lexington_{before,after}_{spawn,green,wide}.png`, same three cameras, 1280×720, rendered (no `-nullrhi`) |
| Autoplay | `scripts/run_slice_autoplay.sh` **PASSED 3/3** after the change (all 10 script assertions + 33 in-engine checks) |
| FPS (M3, editor binary `-game`, 1280×720, 16 bots/side, Lumen on) | **Pass: 35.7 and 36.8 fps** (GPU 26 ms, game thread 5 ms) with KiteDemo content, on a quiet machine. Earlier visual pass: 21–29 fps before the Lumen-card, fog-grid and noise fixes. Results swing a lot when other apps load the GPU (10–29 fps seen). |
| Autoplay with KiteDemo map | `scripts/run_slice_autoplay.sh` **PASSED** (`RUNS=2` with identical hour-boundary hashes, plus 3 single runs). See "Slice autoplay on the hourly clock" below. |

### Slice autoplay on the hourly clock (2026-11-08)
- Diplomacy term text reads `days` (ceasefire, safe_passage); it read the old `turns` field and showed "0 turns".
- Restored the autoplay leg after diplomacy that the hourly refactor dropped: ceasefire is 72 hours, 24 hours advance, no army enters enemy ground, no new battle, hours tick down, then save/load with an identical hash.
- `assert_autoplay_trace.py`: hourly events (`advance_hour`/`interrupt`, consecutive `hour_index`), ≥24 hours after diplomacy, adjacency from `edges`, hash compare at every pre-battle hour. ruff + `mypy --strict` clean.
- Exit code: `PA250Autoplay::Finish` now exits 1 on any failed in-engine check. On Mac, UE's `RequestExitWithStatus` drops the code (generic fallback → `RequestExit` → `_Exit(0)`), so Mac calls `_Exit(code)` after the result files and log are flushed. Verified: normal slice exits 0; `-PA250Service=127.0.0.1:1` (no service) exits 1.

### KiteDemo (Open World Demo Collection) in the battle map
`Content/KiteDemo` (6.3 GB, Fab) is used automatically by the generator (`KITE_ASSETS` in `build_levels.py`), after anything in `Content/ThirdParty`:
- **Ground:** `M_PA250_Meadow` samples `T_GDC_Grass01_D_NoisyAlpha` (3.5 m tile), `T_Forest_Path_001A_D_R` for roads (4.5 m), both normal maps (blended by the road mask) and `T_MacroVariation` at 90 m / 7 m. The macro map replaces the noise nodes, which is also cheaper.
- **Trees:** `HillTree_02` / `HillTree_Tall_02` oaks, 11–16 m, sparse leaf as fits 19 April.
- **Dry-stone walls:** 643 instanced fieldstones (`SM_River_Rock_01`, `Medium_Boulder_LowPoly`, `SM_Boulder05a`) in two courses along the Concord road and the east field wall. Collision comes from invisible boxes the size of the old graybox walls, so cover and pathing are unchanged.
- **Field dressing:** 18 boulders (collision on, low cover), 6 stumps, about 2,000 field-grass clumps in patches (culled 22–35 m, no shadows, no Lumen) and 364 buttercup/yarrow patches (culled 18–28 m). Fixed seed.
- **Instancing:** groups are `HierarchicalInstancedStaticMeshComponent`s added to host actors from Python through `SubobjectDataSubsystem` (no new C++).
- **Foliage materials:** KiteDemo's own UE4-era foliage materials (SpeedTree / subsurface / two-sided foliage) render with white base colour on this UE 5.8 Mac setup, even in the pack's own `Showcase` map. Leaves, bark, grass and flowers therefore use small PA250 masked materials built from the pack's textures (`MI_PA250_OakLeaves`, `…OakBark`, `…FieldGrass`, `…Yarrow`, `…Buttercup`): no wind animation or subsurface, cheaper to render. Rocks keep the pack's photoscan materials.
- The pack has no buildings, fences or soldiers; those stay graybox or are resolved from `Content/ThirdParty` as below.
- **Git:** `Content/KiteDemo/` is in `.gitignore` (a Fab pack: each machine downloads it from Fab). On a machine without it, the committed `L_Lexington` has missing references until `build_levels.py` regenerates it, which falls back to placeholders.
- **First load:** opening the map on a cold cache builds KiteDemo mesh and texture data for minutes. A regenerate leaves `L_Lexington` empty until it finishes, so a battle started meanwhile is black.

### Free Fab assets for `Content/ThirdParty` (used automatically on the next regenerate)
The generator tries exact paths first, then a path-sorted keyword search under `/Game/ThirdParty` with a size check, and writes its picks to `Saved/PA250/lexington_assets.json` ("placeholder" = graybox fallback). Soldiers and muzzle smoke are resolved at runtime (`RF/PA250ThirdPartyAssets.h`).

| Role | Exact path (first hit wins) | Keyword fallback | What to look for on Fab |
|------|-----------------------------|------------------|--------------------------|
| Meetinghouse | `Buildings/SM_Meetinghouse`, `Buildings/SM_Church` | meetinghouse, church, chapel | white clapboard New England church / meetinghouse |
| Tavern / houses | `Buildings/SM_Tavern`, `Buildings/SM_ColonialHouse` | tavern, colonial, house, cottage, farmhouse | colonial saltbox / Georgian timber house |
| Stone walls | `Fences/SM_StoneWall`, `Fences/SM_DryStoneWall` | stonewall, drystone, fieldstone, boulder | dry-stone wall kit, fieldstone boulders |
| Split-rail fence | `Fences/SM_SplitRail`, `Fences/SM_WormFence` | fence, rail | split-rail / worm fence modular |
| Trees | `Foliage/SM_Tree`, `Foliage/SM_DeciduousTree` | tree, oak, maple, elm | deciduous oak/maple, early spring (light leaf) |
| Meadow textures | — | grass / dirt, mud, soil (base-colour maps only: `_D`, `_BC`, `BaseColor`, `Albedo`) | tileable meadow grass + dirt path PBR textures |
| Soldiers | `Characters/SKM_British_Regular`, `Characters/SKM_Continental_Militia`, generic `Characters/SKM_Soldier_1775` | — | 18th-century redcoat / colonial militia, **on the UE5 Mannequin skeleton** (otherwise skipped with a log warning, since `ABP_Unarmed` must still drive it) |
| Muzzle smoke | `FX/NS_MuzzleSmoke`, `FX/NS_MusketSmoke` (Niagara) | — | black-powder muzzle smoke / lingering cannon smoke Niagara |

## Open issues
1. MuseTalk 1.5 still not exercised on Apple Silicon (audio-only fallback in practice).
2. Direct `-game` from some shells SIGBUS in `FMacCursor`/ImageIO; `open -na UnrealEditor.app` or commandlets/`run_slice_autoplay.sh` work. Preview stills need the GUI launch path.
3. Occasional autoplay run fails to emit `autoplay_trace.json` (transient); retry succeeds.
4. Squad `lines.engagements` can read 0 when volleys come only from shot-window aggregation; volley counts still populate.
5. `Content/` binary weight + no git-lfs.
6. Single save slot `Saved/PA250/campaign.json`.
7. Lexington still uses primitive placeholders until Fab packs land under `Content/ThirdParty`. Re-check `DAWN_EXPOSURE_EV` when real albedo textures arrive.
8. Lexington FPS is about 36 on a quiet M3 but drops below 30 when other apps load the GPU. If needed: `r.ScreenPercentage` about 75 with TSR, or a lower Lumen scalability on Mac.
9. **Battle balance.** With `SoldiersPerUnit = 4`, battles run 142–176 game-seconds (3/3 `tickets_exhausted`, 1 flip, British win; the Continentals hold 2 of 3 posts, so the British bleed 17–22 soldiers but win on kills). 24 regulars vs 4 regulars + 18 militia favours the British. Balance (unit strength, cover, `SoldiersPerUnit`) is a design call, left as is.

## Look at these in the editor first
1. **`L_Lexington` dawn visual**: low warm sun from the east, haze, rolling meadow with worn roads, meetinghouse/belfry and tavern, walls, fences, trees. Play to check the OTS camera (350 cm arm, right shoulder, lag) after the first spawn **and** after a respawn. Compare `docs/preview/lexington_before_*.png` with `lexington_after_*.png`.
2. **`L_Lexington` conquest** — three post discs (Green / Meetinghouse / Buckman), ownership colours, spawn-on-owned-post after death, ticket + post strip HUD, capture % while inside a radius.
3. **Active reload** — fire, then tap **R** on sweet/perfect zones through Prime Pan → Ram Charge → Shoulder & Aim; force a jam and hold R to clear; confirm damage vulnerability while reloading.
4. **Line + volley** — stand near friendlies facing the same way for “In Line (N)”; watch bot Make Ready / Present / Fire callouts and a “Volley!” confirm when firing in the window.

## 2026-11-07 — M0 Clock/map recon + verify.sh

### Changed
- Recorded pre-clock baseline of sim, campaign map, diplomacy `turns`, battle launch, and UE install paths.
- Added executable `tools/verify.sh` with subcommands `sim|py|mapgen|ue-build|ue-script|ue-test|ue-shot|all`.

### Files
- `PROGRESS.md` (this section)
- `tools/verify.sh`

### Verification
- `tools/verify.sh sim` → PASS (ctest pa250sim)
- `tools/verify.sh py` → PASS (53 diplomacy tests)
- `tools/verify.sh mapgen` → FAIL expected (no `tools/mapgen/.venv` yet)
- `tools/verify.sh all` → baseline run recorded below after remaining subs

### Recon

#### Sim (`resolve_turn` → new clock mapping)
| Per-turn effect today (`unreal/PA250Sim/src/turn.cpp`) | New cadence |
|---|---|
| Order validation + movement (adjacency step) | Hourly (`path` / `hours_into_edge`) |
| Road intercept + co-location → `pending_battle` | Hourly contact → Interrupt + BattleSetup |
| British AI one-step toward `ai_objective` | Daily 00:00 → emitted as commands |
| Supply accrual `supplies[owner] += supply_value` | Monthly day-1 00:00 |
| Treaty `turns_remaining -= 1` | Hourly after days→hours (M3) |
| Attitude drift / recruitment | N/A — not in `resolve_turn` (FACT) |

Pure API today: `resolve_turn`, `apply_battle_result`, `validate_proposal`, `apply_diplomacy` in `include/pa250/sim.hpp`. Controller queues `Orders` outside `CampaignState` (`controller.hpp`).

#### Data and saves
- Scenario: `unreal/PA250Sim/data/boston1775.json` — string slug province IDs (`boston`, `charlestown`, `cambridge`, `lexington`, `concord`, `roxbury`); **no hex/axial IDs**. Graph = `adjacent[]`. Map polygons/labels are UI-only (not loaded into sim adjacency logic beyond load integrity).
- Save: `schema_version: 1` (`kSchemaVersion` in `types.hpp`); fields include `turn`, provinces, armies, treaties, pending_battle, log. Load rejects mismatched version.
- Tests: ctest target `pa250sim` via `tests/sim_cases.hpp`; shared clause vectors `protocol/test_vectors/clauses/` (many reference clause `"turns"`; `_context.json` has `"turn": 0`).

#### Map (L_Campaign today)
- Empty umap + `APA250CampaignGameMode` → `UPA250MapWidget` → Slate `SPA250Map` (not a mesh actor).
- Polygon source: scenario JSON; base image `T_CampaignMap` / `Content/PA250/Source/campaign_map.png` from `scripts/gen_campaign_map.py`.
- Labels: baked into PNG (Georgia/Baskerville); no label actors. Army counters: Slate boxes at label anchors.
- HUD: turn/supplies/relations/treaties; End Turn / Parley / Save / Load; Fight when pending.
- **Gray unowned:** no dynamic unowned path. Overlay is british red or default continental blue (alpha 78). Static parchment land tones live in the PNG only.

#### Battles
- `resolve_turn` sets `pending_battle` → UI Fight → `BeginBattle` → `OpenLevel(L_Lexington)` → live conquest → `ApplyBattleResult`. Autoplay same path via `-PA250Autoplay`.

#### Diplomacy `turns`
- Schema: `protocol/schemas/diplomacy_clauses.yaml` (ceasefire, safe_passage only). Leave `proposal.yaml` alone.
- Python: `services/negotiation/diplomacy/models.py`, `validator.py`, Claude `build_system_prompt`, scripted mock.
- C++: `diplomacy.cpp` `check_turns`; faction `max_ceasefire_turns` / `max_safe_passage_turns` = 3.
- Vectors: `valid_ceasefire_3.json`, `reject_ceasefire_4.json`, etc.

#### UE install
- `UE_ROOT=/Users/Shared/Epic Games/UE_5.8`
- Editor: `$UE_ROOT/Engine/Binaries/Mac/UnrealEditor` (+ `-Cmd`)
- Build: `$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh`

### Decisions
- **FACT:** Charlestown currently `owner: british` in scenario JSON (will change in M2).
- **FACT:** No attitude drift or recruitment in current sim.
- **SIMPLIFICATION:** Pre-1752 Julian dates will not be used; Gregorian throughout (M1).

### Follow-ups
- M1+ hourly clock; M4 mapgen; `-PA250Shot` (M5).

## 2026-11-07 — M1 Hourly clock (sim)

### Changed
- Replaced `resolve_turn` with `advance_hour` / `enqueue_command` / `plan_path`; `GameTime` hour index from 1607-01-01 Gregorian.
- CampaignState schema_version **2**: `now`, edges, pending_commands, army path/hours_into_edge; treaties use `hours_remaining`.
- Golden determinism vector `protocol/test_vectors/clock_golden_1775.json` (1440 idle hours).

### Files
- `unreal/PA250Sim/include/pa250/{types,sim,controller}.hpp`, `src/{time,path,clock,serialize,diplomacy,battle,controller}.cpp`, `tests/sim_cases.hpp`, `CMakeLists.txt`

### Verification
- `tools/verify.sh sim` → PASS

### Decisions
- **SIMPLIFICATION:** Gregorian calendar throughout (no Julian Old Style).
- **GAME INVENTION:** `march_hours_per_day = 10`.
- **FACT:** No attitude drift / recruitment existed; monthly effect is supply only.

### Follow-ups
- Forced march; bridge dismantling.

## 2026-11-07 — M2 Thirteen towns + typed edges

### Changed
- `boston1775.json`: 13 provinces with explicit `map_index` 1..13; typed edges; start **1775-04-18 20:00**.
- **Charlestown owner → continental** (was british).

### Verification
- `tools/verify.sh sim` → PASS (path boston→concord with harbor = 11h via cambridge)

### Decisions
- **FACT (prominent):** Charlestown not British-occupied until after 17 Jun 1775; HMS Somerset nearby. Revertible.
- **FACT:** British `controls_harbor = true`.
- **GAME INVENTION:** terrain tags / march_hours (Battle Road / necks / boat 3h noted as FACT-inspired).
- **SIMPLIFICATION:** Malden folded into chelsea–medford edge.

## 2026-11-07 — M3 Diplomacy days

### Changed
- Wire field `turns` → `days` (ceasefire 1–365, safe_passage 1–90); reject `turns` with `turns_not_allowed`.
- Apply stores `hours_remaining = days * 24`. Schema, Python, Claude prompt, C++, vectors updated. Left `proposal.yaml` alone.

### Verification
- `tools/verify.sh py` → PASS (54); `tools/verify.sh sim` → PASS

### Decisions
- **GAME INVENTION:** day bounds 1–365 / 1–90 (replaces 3-turn red line).

## 2026-11-07 — M4 Mapgen

### Changed
- `tools/mapgen/` SVG → ProvinceID / BorderDist / CoastDist / presentation / lookup; outputs under `Content/Map/Boston1775/`.
- NonUFS staging for `Map/Boston1775/Raw`.

### Verification
- `tools/verify.sh mapgen` → PASS (0 errors, 8 PROVISIONAL warnings)

### Decisions
- **PROVISIONAL:** SVG polygons authored/adjusted vs sim JSON (IoU notes in mapgen warnings); await Inkscape retrace over a period scan.

## 2026-11-07 — M5–M8 UE map, clock, HUD

### Changed
- `PA250Shaders` PostConfigInit module; `Shaders/PA250Map.ush`; `APA250ProvinceMapActor`; province state texture; `-PA250Shot`.
- `UPA250Subsystem` as `FTickableGameObject` with speeds 6..120 hrs/s, pause on interrupt, monthly autosaves (last 3).
- HUD: date strip, pause/speed, removed End Turn button (kept `EndTurn` API as 24h advance for tests/autoplay).
- `APA250ArmyToken`; CPU picking via `ProvinceIdLookup.bin`; clock/picking commandlets.
- `tools/ue/build_map_assets.py` imports textures + `M_ProvinceMap`.

### Verification
- `tools/verify.sh ue-build` → PASS
- `tools/verify.sh ue-test PA250.Campaign.Clock` → PASS (commandlet)
- `tools/verify.sh ue-test PA250.Map.Picking` → PASS (commandlet)
- `tools/verify.sh ue-script tools/ue/build_map_assets.py` → PASS

### Decisions
- **FACT:** Prefer commandlets over Automation on this Mac (SIGBUS).
- **GAME INVENTION:** Provincial pennant color; British token uses blue stand-in for pre-1801 Union.
- **SIMPLIFICATION:** M_ProvinceMap initially samples paper/ProvinceID (Custom `/Project/PA250Map.ush` compositing available for editor polish); Slate map retained as interim HUD canvas alongside plane actor.

## 2026-11-07 — M9 Final pass

### Shipped
- Hourly EU4-style campaign clock; 13-town Boston 1775 graph; days-based diplomacy; mapgen pipeline; UE map actor + live state + clock tick + HUD restyle; verify.sh harness.

### FACT / GAME INVENTION / SIMPLIFICATION
See M1–M8 sections above. Key FACT: Charlestown Provincial at start; British harbor control; Gregorian SIMPLIFICATION; march hours / day caps GAME INVENTION; map shapes PROVISIONAL.

### Follow-ups
- Bridge dismantling; forced march; neck traversal limits; multiplayer lockstep via hour-stamped commands; Inkscape retrace of SVG over period scan; full Custom material wash in editor; wire army tokens to presentation anchors; update `run_slice_autoplay.sh` for pause/speed.

### Commits
None (per instructions).

## 2026-11-07 — Clock review fixes

### Changed
- `plan_path` arrival now follows the same march window as `advance_hour` (order applies next hour; hours 0–9 march).
- Treaty hours decay after contact, so a 1-day ceasefire covers 24 marches. Ceasefire blocks marches into enemy provinces. A path that does not touch the army's province is dropped instead of teleporting the army.
- Contact stops both armies. `build_battle_setup` carries province terrain plus boat `disordered` / neck `chokepoint` from the attacker's previous province.
- Right-click halt cancels a pending or active player march. HUD shows days remaining and an arrival time. Space / 1–5 / +/− control the clock. Province state texture updates when ownership or hover changes. Autosaves on disk stay at the newest three.

### Verification
- `tools/verify.sh sim` → PASS (including the 1440-hour golden hash)
- `tools/verify.sh ue-build` → PASS

## 2026-11-07 — M10 black map fix

### Changed
- Province map plane kept in XY (removed -90° tip that showed the edge to a top-down camera).
- `M_ProvinceMap` rebuilt Unlit with emissive parchment/water so L_Campaign needs no lights.
- Paper fallback uses engine white (not legacy `T_CampaignMap`); missing Paper no longer multiplies black into the color.

### Files
- `Campaign/PA250ProvinceMapActor.cpp`, `PA250CampaignGameMode.cpp`
- `tools/ue/build_map_assets.py`

### Verification
- `tools/verify.sh ue-script tools/ue/build_map_assets.py` → PASS
- `tools/verify.sh ue-build` → PASS

### Decisions
- **SIMPLIFICATION:** Unlit emissive map material; L_Campaign has no baked lighting.

## 2026-11-07 — M10 black map fix (2)

### Changed
- Unlit `M_MapDisplay` samples `T_MapPreview` (mapgen preview.png) as emissive — visible with no lights.
- Ortho `PA250MapCamera` with `bAutoManageActiveCameraTarget=false`; re-frame after 50ms.
- Key + sky lights spawned so lit tokens/labels are also visible.
- Runtime PNG load fallback if the cooked preview texture is missing.

### Verification
- `tools/verify.sh ue-script tools/ue/build_map_assets.py` → PASS (imported T_MapPreview, M_MapDisplay)
- `tools/verify.sh ue-build` → PASS

## 2026-11-07 — M10-1 One map layer

### Changed
- Removed LEGACY Slate map canvas from the HUD. `UPA250MapWidget` is top/bottom strips + battle broadside only (`HasLegacyMapCanvas()==false`).
- `SPA250Map` gutted to an unreferenced stub (no OnPaint drawing).
- `APA250ProvinceMapActor` is the sole map layer (plane + Unlit display material).
- Flat fills: Unlit `M_MapDisplay` shows mapgen preview; `M_ProvinceMap` kept as Unlit parchment stub with wash params for later polish. Unowned land stays parchment; water is the preview’s blue wash (PROVISIONAL SVG).
- Commandlet `PA250.Map.SingleLayer` (worldless; CreateWorld crashed under `-nullrhi` on Mac).

### Classification (map drawing sources)
| Source | Class |
|---|---|
| `APA250ProvinceMapActor` + `M_MapDisplay` / `T_MapPreview` | NEW |
| Labels / cartouche / compass TextRender on map actor | NEW |
| `APA250ArmyToken` | NEW |
| Route debug lines when army selected | NEW |
| HUD strips + battle broadside in `UPA250MapWidget` | NEW (HUD, kept) |
| `SPA250Map` OnPaint (gray fills, dashed borders, adjacency lines, number boxes, old PNG) | LEGACY — removed |
| `T_CampaignMap` / `campaign_map.png` | LEGACY asset — left unreferenced |

### Legacy assets left for user deletion
- `Content/PA250/Textures/T_CampaignMap.uasset`
- `Content/PA250/Source/campaign_map.png` (if present)
- `Source/.../SPA250Map.cpp` + `.h` (stub; safe to delete after confirming no includes)

### PROVISIONAL shape issues (Inkscape retrace)
- Water has 2 components (~6.96M + ~160k px): harbor / enclosed basin pockets → flat light-blue polygons east of Charlestown and Charles strip (not hidden; SVG authoring).
- IoU warns: lincoln, lexington, charlestown, boston, dorchester, chelsea vs sim JSON.
- sim label of menotomy falls in medford after overlap resolution.

### Files
- `Campaign/PA250MapWidget.*`, `SPA250Map.*`, `PA250ProvinceMapActor.*`, `PA250MapSingleLayerCommandlet.*`
- `tools/ue/build_map_assets.py`, `tools/verify.sh`, `Shaders/PA250Map.ush`

### Verification
- `tools/verify.sh ue-build` → PASS
- `tools/verify.sh ue-test PA250.Map.SingleLayer` → PASS
- `tools/verify.sh mapgen` → PASS (0 errors, 8 PROVISIONAL warnings)
- `build_map.py --preview` → wrote `tools/mapgen/out/preview.png`

### Decisions
- **SIMPLIFICATION:** SingleLayer is a commandlet (not Automation); no CreateWorld under `-nullrhi`.
- **SIMPLIFICATION:** Visible map uses Unlit preview texture until Custom wash material is wired in-editor.
- **PROVISIONAL:** Harbor/basin water components from authored SVG.

## 2026-11-07 — M10-2 Labels, tokens, battle marker

### Changed
- Spawn all 13 province labels from `map_presentation.json` `label_anchor_uv` (no hard-coded town positions).
- Water labels kept (Boston Harbor, Massachusetts Bay, Charles River, Back Bay, Charles Basin) at GAME INVENTION UVs for the new geometry.
- `APA250ArmyToken` at `token_anchor_uv` with stacking offset; strength plate shows stored unit classes (`line_infantry N · militia M`), no invented conversion.
- British token: blue pre-1801 Union stand-in (no St Patrick saltire) — FACT. Provincial: buff pennant — GAME INVENTION.
- Battle marker offset from token so it clears the province label; broadside popup restyled (parchment, ink, serif-ish title) on the right so it avoids covering the contested province.
- Routes hidden until an army is selected; then incident edges + planned path (ferry/boat dotted, planned heavier) via debug lines.

### Files
- `PA250ProvinceMapActor.*`, `PA250ArmyToken.*`, `PA250MapWidget.*`, `PA250CampaignPlayerController.*`

### Verification
- `tools/verify.sh ue-build` → PASS
- `tools/verify.sh ue-test PA250.Map.Picking` → PASS

### Decisions
- **FACT:** British pre-1801 Union (no St Patrick).
- **GAME INVENTION:** Provincial pennant; water-label UV placement.
- **SIMPLIFICATION:** Engine TextRender font (IM Fell English not packaged); battle mark uses `++` stand-in for crossed swords; routes are DrawDebugLine in PIE.

## 2026-11-07 — M10-3 Framing

### Changed
- Map plane in XY; ortho camera fits neatline between top (44px) and bottom (52px) HUD strips at 1920×1080 math.
- Cartouche “Boston & Environs, 1775” at UV (0.08, 0.08) inside neatline top-left; compass at (0.07, 0.90) bottom-left.
- `PA250.Map.Framing` commandlet projects every label/token/cartouche/compass anchor and asserts none sit in HUD strips (Roxbury included).

### Files
- `PA250ProvinceMapActor.*`, `PA250MapFramingCommandlet.*`, `PA250MapWidget.h` (strip height constants)

### Verification
- `tools/verify.sh ue-build` → PASS
- `tools/verify.sh ue-test PA250.Map.Framing` → PASS

### Decisions
- **SIMPLIFICATION:** Framing test uses the same projection math as `FrameCamera` (commandlet, not Automation load of L_Campaign).

## 2026-11-07 — M10 Finish

### Legacy sources removed
- Slate `SPA250Map` drawing (gray fills, dashed borders, straight adjacency including Cambridge–Charlestown across water, old coastline/labels, number-box army counters).
- MapWidget no longer embeds any map canvas.

### Legacy assets left for deletion
- `Content/PA250/Textures/T_CampaignMap.uasset`
- `Content/PA250/Source/campaign_map.png` (if present)
- Optional: `SPA250Map.cpp` / `.h` stubs

### PROVISIONAL shape issues
- Second water component (~160k px): harbor east of Charlestown / Charles basin strip (light-blue fills).
- Province IoU / menotomy label overlap warnings from mapgen `--check`.

### Follow-ups
- Wire Custom `/PA250/PA250Map.ush` owner wash into `M_ProvinceMap` and switch display off preview.
- Package IM Fell English (OFL); real crossed-swords mesh/glyph.
- Persistent route meshes instead of DrawDebugLine.
- Inkscape retrace of SVG over a period scan (coastline, necks, Back Bay).
- Confirm PIE framing vs preview after camera/view-target settles (black-map fixes in same day).

### Verification
- `tools/verify.sh all` → ALL PASSED (ue-shot excluded)
- `tools/mapgen/.venv/bin/python tools/mapgen/build_map.py --preview` → PASS
- ue-shot not run (per instructions)

### Commits
None (per instructions).

## 2026-11-08 — M10 review fix: horizontal labels, plane aspect, camera fit

### Changed
- All map TextRenders (province/water/cartouche/compass/battle) and army tokens now use `APA250ProvinceMapActor::FlatMapTextRotation()` = `FRotator(90, 90, 0)`. The old `(90, 0, 0)` laid text flat but with advance along screen Y, so every label read vertically (screenshot). TextRender advance is `-Y`, glyph-up `+Z`; pitch+yaw turns advance to `+X` (east/screen right) and glyph-up to `-Y` (north/screen up). Province positions and the baked preview were already east-right/north-up; only glyph rotation was wrong.
- Plane aspect fixed: `MapHeightUU` 1000 → 1280 so 1600×1280 matches the 4096×3277 presentation texture / 2048×1638 preview (aspect 1.25). The 1.6 plane stretched the map ~28% horizontally.
- `FrameCamera` now uses `FitOrthoWidth()` (fit width, and height into the 44px/52px HUD usable band at the actual viewport size) instead of `MapWidth*1.08`, which cropped the taller plane. Camera centre bias recomputed from the strip heights in world units.
- `PA250.Map.Framing` commandlet synced to `FitOrthoWidth`/CDO plane size and now also asserts plane aspect, east-right/north-up UV orientation, whole-plane fit, and the text-rotation basis (advance → +X, glyph-up → −Y).

### Verification
- `tools/verify.sh ue-build` → PASS
- `tools/verify.sh ue-test PA250.Map.Framing` → PASS (initially failed on the advance-axis assumption; test corrected to TextRender's `-Y` advance, code was correct)
- `tools/verify.sh ue-test PA250.Map.SingleLayer` → PASS
- `tools/verify.sh ue-test PA250.Map.Picking` → PASS
- `tools/verify.sh mapgen` → PASS (8 PROVISIONAL warns unchanged)

### Decisions
- **SIMPLIFICATION (unchanged):** live display is still the Unlit preview texture, not the `PA250Map.ush` owner wash; SingleLayer/Framing remain worldless commandlets.
- Not visually re-verified in PIE in this pass; the rotation basis is asserted numerically in the Framing commandlet. User should confirm labels read horizontally in PIE.

## 2026-11-08 — M10 visual verification (ue-shot)

### Changed
- `make_preview` gained `labels=False`; `--preview` now also writes `tools/mapgen/out/preview_display.png` (roads/rivers/borders, no baked names or token circles). `build_map_assets.py` and the runtime PNG fallback prefer it: the first ue-shot showed every province name printed twice (baked + TextRender). `preview.png` keeps labels for standalone diagnosis.
- `APA250ArmyToken` flag mesh keeps absolute world rotation facing +Z so the pitched token actor cannot turn it face-down.

### Verification
- `tools/verify.sh ue-shot` → `Saved/Screenshots/MacEditor/HighresScreenshot00006.png`, inspected directly: all 13 province labels, water labels, cartouche and compass read horizontally, single-printed; map un-stretched and fully framed; strength plates visible at Lexington and Boston. HighResShot excludes UMG, so HUD strips are not judged from this capture.
- `tools/verify.sh all` → ALL PASSED (ue-shot excluded by harness); `PA250.Map.Framing`, `PA250.Map.SingleLayer`, `PA250.Map.Picking` → PASS.

### Decisions
- Token flag rectangles remain hard to discern at full-map ortho zoom (~16px, centred under the province label); strength plates are the readable token element at this zoom. Noted, not redesigned.

## 2026-11-08 — Army visibility + zoom buttons

### Changed
- Tokens were effectively invisible (22uu grey-checker flag, no name). `APA250ArmyToken` now: flag 65x45uu (85x60 + 1.25x actor scale when selected) on `/Engine/BasicShapes/BasicShapeMaterial` so the faction tint applies (British blue, Continental deep buff/gold), absolute world rotation facing +Z re-asserted in `Configure`, offset north of the anchor; new name plate (`Militia` / `Regulars`, red and larger when selected) and strength plate stacked south of the anchor.
- Selection messages now name the army's current province ("Selected Massachusetts Militia at Lexington. …").
- Zoom: `Zoom -` / `Zoom +` buttons in the bottom bar call `APA250ProvinceMapActor::ZoomBy(1/1.4 | 1.4)` (clamped 1x–5x, centred on the selected province, clamped to the plane; zooming back to 1x recentres). `FrameCamera` uses `ZoomedOrthoWidth()`.
- `PA250.Map.Framing` also asserts zoom halves ortho width at 2x and that the token flag is at least 0.5x0.35 scale with a name plate.

### Verification
- `tools/verify.sh ue-build` → PASS; `PA250.Map.Framing`, `PA250.Map.SingleLayer`, `PA250.Map.Picking` → PASS.
- `tools/verify.sh ue-shot` → `HighresScreenshot00009.png`, inspected: gold flag + "Militia" + `line_infantry 4 · militia 18` at Lexington, blue flag + "Regulars" at Boston, both findable at full-map zoom.
- Zoom buttons are Slate in the bottom bar (not capturable by HighResShot, which excludes UMG); their `ZoomBy` math is covered by the Framing commandlet. Click in PIE to exercise.

### Correction (2026-11-08, later)
- The "Zoom - / Zoom +" buttons above were claimed but the Slate insertion had silently not applied (anchor mismatch) — the user's PIE screenshot showed only Pause/Resume, Speed+, Parley, Save, Load. Army tokens *were* updated in that screenshot. Buttons now inserted in `PA250MapWidget.cpp` (verified: strings present in `libUnrealEditor-PlayAmerica250.dylib`, ue-build PASS). Restart the Editor (or Live Coding) to load the new dylib; an already-open Editor keeps the old code.
