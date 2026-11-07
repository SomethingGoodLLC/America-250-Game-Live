# PROGRESS — Play America 250 vertical slice

**Result: M0–M8 complete. `scripts/run_slice_autoplay.sh` passes (several runs back to back, plus `RUNS=2` with cross-run hash comparison).**
Nothing was committed or stashed; all changes are uncommitted in the working tree.

## Toolchain facts
- Xcode 27.2 (27B52), Apple clang 21.0.0; cmake 3.30.0; Python 3.13.7 (service venv: `services/negotiation/.venv`, has Pillow, numpy, anthropic 0.68).
- Engine: `/Users/Shared/Epic Games/UE_5.8` (prebuilt editor; project module builds take ~1 min).
- git-lfs is **not installed**. `.gitattributes` LFS rules for `*.uasset`/`*.umap` are in place but inert until `brew install git-lfs && git lfs install`.

## Milestones and evidence
| M | Status | Verification evidence |
|---|--------|------------------------|
| M0 | done | `CLAUDE.md`, `.cursorrules` (same text), `PROGRESS.md`, `.gitattributes`, Unreal entries in `.gitignore`, toolchain facts above. |
| M1 | done | `cmake -S unreal/PA250Sim -B unreal/PA250Sim/build && cmake --build unreal/PA250Sim/build && ctest` -> 1/1 pass; 20 test groups, 211 checks, 0 failed (determinism, illegal move, British march, armies cannot pass through each other, ceasefire block + expiry, battle losses/retreat/ownership, cede-Boston rejected, jailbreak no-op, valid ceasefire + prisoner exchange, save/load hash, flow state machine, player orders own armies only, player-faction result is a no-op, int32 range, 29 shared clause vectors, golden DiplomacyContext). |
| M2 | done | Project `unreal/PlayAmerica250` (built headlessly from the TP_ThirdPerson template incl. Variant_Combat). `Build.sh PlayAmerica250Editor Mac Development` -> Succeeded. `UnrealEditor-Cmd ... -ExecCmds="Automation RunTests PA250; Quit" -unattended -nullrhi` -> `PA250.Sim.Core` Success, exit code 0. RF* + Musket* ported (from Cursor history) to `Source/PlayAmerica250/RF`. |
| M3 | done | `UPA250Subsystem` over `pa250::CampaignController`; automation test `PA250.Flow.StateMachineAndSaveLoad` Success (drives Campaign->Battle->Campaign->Diplomacy->Campaign + save/load hash, no levels). |
| M4 | done | `scripts/gen_campaign_map.py` -> `docs/preview/campaign_map.png` and imported `T_CampaignMap`; `L_Campaign`; `-game -nullrhi -PA250Selftest=campaign` -> `Saved/PA250/selftest_campaign_result.json` passed=true, 14 checks (point-in-polygon, illegal move rejected, adjacent order queued, End Turn, pending battle, turn blocked). |
| M5 | done | `L_Lexington`; `-game -nullrhi -PA250Selftest=battle` (24 bots, tickets, musket projectiles, control point): runs of ~18 s end with `battle_result.json` and pass invariants (losses <= pools, winner set, retreat adjacent, result applied, back to campaign flow). |
| M6 | done | `L_DiplomacyRoom`; `UPA250DiplomacyClient` (HTTP + WebSockets) vs `main:app` JSON control socket. `-PA250Selftest=diplomacy` with MockLocal: 10 checks pass. With the service stopped (`-PA250ExpectUnavailable`): unavailable path returns cleanly, state hash unchanged. |
| M7 | done | `cd services/negotiation && .venv/bin/python -m pytest tests/diplomacy -q --no-cov` -> 53 passed (29 shared vectors, game-session socket tests, adversarial suite vs MockLocal honest + compromised **and the real Claude provider**, provider/avatar-chain unit tests). `ruff` and `mypy` clean on all new/touched-for-this-feature modules. |
| M8 | done | `scripts/run_slice_autoplay.sh`: PASSED on every invocation (6 runs across 5 invocations, incl. `RUNS=2`: "state hashes identical at every turn boundary before the battle (5 boundaries)"). Each run ~20–22 s: new campaign -> 3 scripted turns -> Lexington battle (autoplay bot replaces the player, 10x time dilation) -> BattleResult applied -> diplomacy room: jailbreak line changes nothing, ceasefire(3) accepted -> British AI respects it -> save/load hash identical. 33 in-engine checks + 10 script assertions. A tampered trace makes the assert script fail (verified). |

## Commands
```
# sim
cmake -S unreal/PA250Sim -B unreal/PA250Sim/build && cmake --build unreal/PA250Sim/build && (cd unreal/PA250Sim/build && ctest)
# unreal (project module)
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" PlayAmerica250Editor Mac Development -project="$PWD/unreal/PlayAmerica250/PlayAmerica250.uproject"
# rebuild levels/assets (idempotent; PA250_LEVELS=campaign,lexington,diplomacy)
UnrealEditor-Cmd unreal/PlayAmerica250/PlayAmerica250.uproject -run=pythonscript -script=unreal/PlayAmerica250/Scripts/build_levels.py -unattended -nullrhi
# python
cd services/negotiation && .venv/bin/python -m pytest tests/diplomacy -q --no-cov
# whole slice
scripts/run_slice_autoplay.sh            # RUNS=2 for the cross-run determinism comparison
```
Selftest flags (game exe): `-PA250Selftest=campaign|battle|diplomacy`, `-PA250Autoplay`, `-PA250Service=host:port`, `-PA250Bots=N`, `-PA250BattleTime=sec`, `-PA250ExpectUnavailable`.

## Decisions (made without stopping)
- **No standalone Combat template** ships with UE 5.8; the C++ Third Person template contains `Variant_Combat`, which I kept (code + content). Its classes are unused by the slice. Other variants were removed from the copy.
- **Module rename**: template classes `TP_ThirdPerson*` became `PlayAmerica250*`; `[CoreRedirects]` added to `DefaultEngine.ini` so template assets still resolve.
- **Shared content**: Characters/Input/LevelPrototyping packs copied from `Templates/TemplateResources/High` into `Content/`.
- **Sim**: `apply_diplomacy` is all-or-nothing (any invalid clause => strict no-op, state/log/hash untouched). `safe_passage` = envoy grants the counterpart army entry to an envoy-owned province (overrides ceasefire blocking, suppresses the battle). `prisoner_exchange` counts = prisoners each side releases; returned troops rejoin that faction's first army. `resolve_turn` is a no-op while a battle is pending. Retreat prefers the loser's previous province, then loser-owned adjacent, then any adjacent.
- **Sim/engine seam**: `CampaignController` (flow, orders, pending setup/context, save/load, transition trace) lives in the core so ctest covers it; `UPA250Subsystem` is a thin wrapper. `PA250SimCore.cpp` includes the canonical `.cpp` files (no copies) under `#pragma GCC visibility push(default)` because Unreal builds hidden-by-default and the core has no export macros. Module builds with exceptions on (nlohmann/json).
- **Scenario path** is the dev layout (`<repo>/unreal/PA250Sim/data/boston1775.json`). Packaging would need the data file staged.
- **Blueprints**: `BP_RFCharacter`, `BP_Musket`, `BP_MusketBall` were **not** created; the C++ classes are fully functional with defaults (basic-shape musket/ball, mannequin mesh, runtime-built Enhanced Input mapping, runtime `UMusketTuning` asset `DA_MusketTuning`). Create BPs in the editor only if you want to tune visually.
- **Bots** use a code-driven brain in an `AAIController` (advance / engage / reload / hold; navmesh `MoveTo` with straight-line fallback) instead of a StateTree asset — authoring StateTree assets headlessly wasn't practical. Bots are replaced by a fresh pawn+controller after death (re-possessing an AIController triggered a path-following ensure).
- **Tickets**: death spends one ticket of the victim's class (or the first class with tickets left); losses reported = tickets spent. Ending: pool 0, objective held 75 s, or 240 s time limit (more remaining ticket share wins; tie -> objective holder -> defender).
- **Hitscan retired**: `RFCharacter` fires `AMusketWeapon` projectiles with gravity compensation; no line-trace damage.
- **JSON control socket**: `?format=json` on the existing control socket; default YAML unchanged. Game sessions are `model: "game"` with a `diplomacy_context` body, no `RTCPeerConnection`.
- **Claude provider**: `claude-sonnet-5-5` rejects forced `tool_choice`; uses a single tool with `tool_choice: auto` and fails over to MockLocal if no tool call returns. Real-provider adversarial tests ran against the live API (key read from `.env`, never printed).
- **Content safety** on provider output blocks only high-severity flags (the legacy filter flags "war"/"attack", which are on-topic in 1775).
- **Selftests/autoplay run as `-game -nullrhi`** rather than as editor automation tests (UMG/Slate and level travel need a game world). Time dilation (10x) fast-forwards battles.
- **Python venv**: used `services/negotiation/.venv` (root `.venv` lacks `anthropic`/Pillow).
- **Cloud/offline**: autoplay sets `PA250_LLM=mock PA250_TTS=say PA250_DID=0 MUSETALK_DISABLE=1`: no cloud LLM, TTS or D-ID calls; TTS is macOS `say`.
- **Review pass**: `protocol/test_vectors/clauses/*.json` and `unreal/PA250Sim/data/boston1775.json` were hidden by the repo-wide `*.json` and `data/` gitignore rules; exceptions now keep them as source. A mismatched `BattleResult` is a strict no-op (hash unchanged), same as an invalid diplomacy result. Autoplay and the battle selftest spectate: `SpawnBots` already places `BotsPerSide` on each side, so `RestartPlayer` no longer adds a 13th soldier. The post-ceasefire autoplay check allows movement on friendly ground and fails only when an army enters an enemy province. Typed `player_utterance` lines are logged with the structlog timestamp.
- **Black battle view, verified**: a `-game` screenshot of L_Lexington was still black (camera pitched into an empty sky, exposure crushed). Manual exposure and a downward look then showed the mannequin, musket, and ground. Screenshot: `Saved/PA250/battle_view.png`.
- **Black battle view**: Continental player starts were inside the Buckman Tavern volume, so the player pawn failed to spawn and the camera sat in solid geometry. Player spawn now searches for a clear capsule. Graybox lighting no longer uses Lumen.
- **Map paint crash**: closing a province outline with `Line.Add(Line[0])` asserted in `TArray` on the first Play frame (Boston's 10-point outline). The first point is copied before it is appended.
- **Second review pass** (all verification re-run afterwards: ctest, pytest incl. the live Claude adversarial lines, `Automation RunTests PA250`, `RUNS=2 scripts/run_slice_autoplay.sh`):
  - Hostile armies swapping provinces along one road now meet instead of passing through each other (militia Lexington->Cambridge while the British go Cambridge->Lexington used to hand the British Lexington with no battle). The army entering enemy-owned ground attacks and the defender holds (tie: lower army id).
  - Clause integers must fit int32 in both parsers. C++ previously narrowed `turns: 4294967299` to 3 and accepted what Python rejects. New vector `reject_turns_int_overflow.json`.
  - `CampaignController::queue_move` rejects armies that are not the player's (`not_player_army`). `resolve_turn` still accepts injected orders for tests.
  - `apply_diplomacy` is a no-op for a result whose faction is the player's or unknown (matches `begin_diplomacy`).
  - The C++ vector runner matches error codes exactly (`code` or `code: ...`), like pytest, not by bare prefix.
  - Claude tool is `strict: true`. Strict mode does not support min/max/length/size constraints, so those were dropped from the schema; the prompt and both validators enforce them. A `refusal` stop reason raises, which falls back to MockLocal. The per-reply `AsyncAnthropic` client is now closed.
  - MuseTalk: the subprocess is reaped after a timeout and killed if the client leaves mid-render.

## Open issues
1. **MuseTalk 1.5 is not installed or exercised.** `scripts/install_musetalk.sh` and the adapter (`providers/video_sources/musetalk_source.py`) are untested against real weights; upstream targets CUDA, so Apple Silicon speed is unknown. Current chain in practice: MuseTalk (skipped, logged) -> D-ID (not exercised; keyed in `.env` but disabled in tests) -> **audio-only** (macOS `say` WAV).
2. **Never rendered.** Everything ran `-nullrhi`; layout, colours, portrait orientation, lighting and Slate painting are unverified visually (see list below).
3. Battle balance: Continentals reach the Green first and usually win by holding it (~95 game-seconds, 2–8 casualties). British win rarely; 20 s reloads keep losses low.
4. 32 legacy negotiation tests fail identically on the untouched baseline (`test_providers`, `test_provider_edge_cases`, `test_session_manager`, `test_main`); `main.py`/`mock_local.py` carry pre-existing ruff/mypy debt. Only the new code was brought clean.
5. `Content/` now holds ~140 MB of binary template assets; git-lfs not installed (see top).
6. `Saved/PA250/campaign.json` is the only save slot; load resets flow to Campaign.
7. D-ID path passes a local portrait path as `image_url`; `DIDVideoProvider` may need a hosted URL. Unexercised (see 1).
8. The real-provider adversarial test asserts on the *finalized* proposal, so it proves the validators hold the line, not that the model refuses. Model behaviour is visible in the service log (`Diplomacy reply ... rejected=[...]`).
9. Stretch items (push-to-talk, volley formations, Concord bridge, MetaHuman spike) were **not started**.

## Look at these in the editor first
1. `Content/PA250/Maps/L_Campaign` — Play: does the parchment map fill the view, are province tints/army banners/battle X/treaty badge readable, do clicks select armies and queue arrows?
2. `L_Lexington` — Green layout (triangle roads, meetinghouse + belfry, Buckman Tavern, stone walls, spawn clusters), `Objective_Green` radius, NavBounds covering the field, and that the musket sits sensibly in `hand_r` on the mannequin. Team marker cones should be red/blue (material param `Color` on `BasicShapeMaterial`).
3. `L_DiplomacyRoom` — `EnvoyPortrait` plane orientation on the north wall (material is two-sided unlit), `RoomCamera` framing, candle light intensity (60 cd), and MP4/WAV playback through `MP_Envoy` + `MediaSoundComponent` (AvfMedia).
4. `Content/PA250/Data/DA_MusketTuning` — reload (20 s), speed (3000), damage (40).
5. `Source/PlayAmerica250/RF/*` and `Battle/*` for the combat code; `Scripts/build_levels.py` regenerates all three levels.
