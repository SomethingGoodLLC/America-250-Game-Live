# PROGRESS — Play America 250

**Vertical slice M0–M8 complete. Battle Feel B1–B4 complete.** Nothing committed or stashed; all changes are uncommitted in the working tree.

## 2026-11-14 — Musket carry, reload drill on the move, no loading at a run (user report)
- **Reported:** the left arm looked missing, the musket floated while the player reloaded, and reloading on the run looked wrong.
- **Cause, seen in `-PA250MeleeProbe` stills from the player's own camera before any fix:** the unarmed clips hang the right hand at the thigh, so the left arm folded across the chest to reach the stock and the left shoulder showed as a stump. The barrel followed the camera all the way round, so with the view turned it pointed back through the body. On the move the reload drill parked the musket at a fixed point in front of the body while the stride moved the body under it.
- **Carry (`FPA250MeleePose::Carry`, `ApplyMeleePose`):** on foot the right hand holds the musket in front of the right hip and the muzzle points up and to the left (`CarryPitchDegrees` 22, `CarryYawDegrees` -20, `UPA250MeleeComponent`), so both hands are on it. Every other hold (shoulder, guard, thrust, club, charge, reload) takes the carry's share, so those poses are unchanged. The left elbow pole is further out and back. A plain carry no longer re-solves the legs.
- **Barrel stays with the body:** `AMusketWeapon::UpdateVisuals` keeps the barrel within 60 deg left / 25 deg right of the body's facing. Visual only; the ball still flies to the aim point.
- **Reload drill only standing:** above `ReloadDrillMaxSpeed` (60 cm/s) the musket stays at the two-handed carry and the loading runs on unseen (player and bots). The left hand now uses the parked musket's own spot from the first frame of the blend.
- **GAMEPLAY SETTING (user decision 2026-11-14): the player loads standing or at his ordinary pace, never at a run.** `ARFCharacter::ReloadBlockedByMovement` (player, on foot, faster than `WalkSpeed` + 30 cm/s) sets `AMusketWeapon::SetReloadPaused`: the sequencer stands still, an R press does nothing and is no fumble, the HUD reads "Slow down to reload", and the reload carries on from the same point. Bots and riders are not held to it. `FActiveReloadSequencer` is untouched.
- **Class cards re-shot** (`-PA250ClassPortraits` → `build_class_portraits.py` → `import_class_portraits.py`, 8 textures). The run wrote all 8 stills and logged `PA250 portraits done` but did not exit by itself within 240 s and was killed. The new cards are darker than the committed ones (faces in shade, a post behind the soldier): the stills are taken on the rebuilt Lexington.
- **Verified (this Mac):** editor build Succeeded. `-run=PA250Battle` checks=394 fails=0 (6 new `reload on the move:` checks). `-run=PA250Visuals` failures=0 (4 new `carry:` checks; measured hand_r +12.8 cm forward, 24.1 cm toward the middle, feet unmoved). `-run=PA250Combat` rc=0. Rendered `-PA250MeleeProbe` in a `-game` window, 57 stills in `Saved/PA250/melee_probe/` (new stages 8 and 9: `39_hold_player_view` … `57_hold_front_view_walk`); log: walking `phase=2 progress=0.03`, running `0.13` → `0.13` 2.2 s later, stopped `0.52`; no `Failed to compile Material`, `IsRotationNormalized` or assertion in the log.
- **Not verified:** hands-on play (how the pause feels, the HUD line on screen); bots in a firing line at distance; soldiers on a slope; from directly behind, the arms are still mostly hidden by the coat because both hands are now in front of the body; a faint seam still shows at the left shoulder when walking with the view turned far round.

## 2026-11-13 — Staff letters: no more popups that stop the clock (user report)
- **Rule (user decision 2026-11-13): the clock stops only for a decision needed right then.** `letter_is_timely` (sim) is true only for the detached commander's question; it alone raises `Interrupt::EventPopup`. Answering it starts the clock again (`UPA250Subsystem::bPausedByLetter`); the player's own pause is never overridden.
- **Notices no longer take the screen.** Deliveries, refusals, the requisition follow-up and the chapter briefing are reported on the HUD message line (`UPA250Subsystem::NoteDispatches`, display only). No Acknowledge.
- **Desk letters.** Every other letter waits behind a "Letter on the desk" button (left edge) with the clock running; the panel has a Later button. GAMEPLAY TUNING in `logistics.cpp`: unanswered after 72 h (Knox 7 days) the clerk files it as set aside (`letter_lapse_hour`).
- **Repeats.** The supply letter came back after every Concord/Lexington battle and always said "Concord". It now names the field fought on and returns at most once a week (`kAftermathCooldownHours`). The April columns no longer wait for that letter to be answered.
- **Consequences (replaces the 2026-11-08 "acknowledge only" rule for L1, L4, L6, D8).** These letters now carry three choices each, defined in `opening_war_correspondence.json` (`choices[].effects`: gold, local_support, british_attitude, provisions/powder with delivery_hours, army_morale, army_readiness, clerk_load), applied exactly once by `apply_source_decision`. All amounts are GAMEPLAY TUNING and each relay is labelled fictional. L2 (Crown draft, retrospective exhibit) is filed for reading with no reward and no popup. The panel builds its buttons from the catalog, with the effect written on each button; long letters scroll.
- **Verified:** `pa250sim_tests` 9522 checks, 0 failed (new `desk_letters_do_not_stop_the_clock`; `logistics_loop` / `logistics_edges` updated). Editor build Succeeded. `-PA250DeskProbe` in a `-game` window (log `Saved/Logs/desk_probe.log`, `ScreenShot00204–212.png`): commander's question paused and opened; answering it resumed the clock; the supply letter and L1 arrived as desk buttons with the clock running through a delivery; L1's first choice took 30 gold.
- **Not verified:** hands-on play; the British-player view; old saves that hold an open notice (the panel still shows it with Acknowledge); balance of the new amounts; L4/L6/D8 on screen (same code path as L1, covered only by the catalog parse).

## 2026-11-13 — Concord playtest fixes (user report after playing L_Concord)
- **Spawn camping / men over the water (one cause).** `ConfigureSoldier` moved any bot that spawned more than 70 m from `Posts[0]` to 55 m from it. Posts sort by id, so on L_Concord `Posts[0]` is the Far bridgehead: the militia were dropped beside the bridge and into the river. Rule removed; starts and posts are placed at the intended distances by the layouts. Also: `PickSpawnTransform` no longer uses the contested crossing post or a post with a living enemy within 60 m (`SafeSpawnEnemyCm`, GAMEPLAY SETTING), and puts men on the navmesh; `ChooseRespawnPoint` no longer moves a level `PlayerStart` onto the last post (it uses its own marker). River channel blocks are 24 m tall so nothing projects onto their tops.
- **Shots over the bridge rail.** The rail collision hulls stopped every ball. `SM_NorthBridge1775` now has 10 hulls (deck and abutments); two unseen men-only walls along the deck (`Bridge_Rail_*`, tag `PA250Creek`, narrowed by `SealLevelRiver`) keep men on it.
- **Tickets (user decision 2026-11-13: every man standing is a ticket).** `SideIsOut`: a side is beaten only when its reserve is spent AND no man of it is alive. The two-minute exhaustion guard is gone, so a side wiped early ends the battle at once instead of leaving the player to watch.
- **Damage (GAMEPLAY SETTINGS).** Grenadier health 120 -> 100. Head multiplier 1.5 -> 2.5 (a head hit kills any class at any range). Body damage is full to 60 m (was 40) and 80 at 100 m (was 60). Headshots were already modelled: a hit more than 75 cm above the man's centre.
- **After-battle report.** Opens by itself on the campaign map after a battle (`UPA250Subsystem::HasUnseenBattleReport`, display only), with a Continue button; it was only readable in the town window.
- **Verified:** `-run=PA250Battle` checks=386 fails=0 (weapon-curve, grenadier and exhaustion tests rewritten for the new rules; new damage test). Kit validation and import passed (bridge 10 hulls). `-PA250Autoplay` on L_Concord: start still shows the militia at the Muster ground with the bridge 145 m and the bridgehead 201 m off; 0 fallen at 40 s (22 before); ran to a result, no error lines. On L_Lexington: ran to a result, no error lines. Campaign autoplay: `aftermath: the after-battle report opens by itself on the map` passed and the still shows it.
- **Not verified:** firing over the rail by hand (hull count and the men-only rule are tested, not a live shot); hands-on play of any of this; balance - the British won the one Concord autoplay now that the militia cannot reinforce onto the bridge.

## 2026-11-12 — Lexington Common re-laid to its 1775 plan; Concord gets its own level (user decision)
- **Decision (user, 2026-11-12):** re-lay the Common to the historical plan and give Concord its own level with a carved river and a full-length bridge, accepting that posts and spawns move.
- **Layouts (`build_levels.py`):** a level is now a *layout* (`_lexington_layout`, `_concord_layout`, bound by `use_layout`), built by one `build_battlefield`. `PA250_LEVELS=lexington|concord`. Each layout's docstring separates record, reading and GAME INVENTION. Field trees are picked by a seeded search, not a hand list; `audit_village` also checks walls against roads and the river, and sites against each other.
- **L_Lexington:** true compass (+X east, +Y south) and real size: triangle of the Concord road (165 m), Bedford road (150 m) and Harrington Road; meetinghouse on the Common at its point, facing down the Boston road; Belfry and a schoolhouse near it; Buckman Tavern across the Bedford road; Harrington houses across Harrington Road, Munroe houses across the Concord road; Belfry Hill south. The Common is open ground (no fence, no trees). Posts: `green` on Parker's line, `meetinghouse` behind the meetinghouse, `buckman` before the tavern. Continentals start on the Common, the regulars 60 m down the Boston road. Side lengths and bearings are from the modern street plan from memory, not a survey.
- **L_Concord (new):** bridge at the origin; river (28 m channel, about 32 m of water) cut into the terrain along a smoothed course, one ribbon water mesh, 111 unseen channel blocks; `SM_NorthBridge1775` (33.4 m, six bents of three piles, 26 collision hulls) placed in the level and tagged `PA250LevelCrossing`; 122 m causeway with its north wall, east and west branches, Brown's and Buttrick's farms on the ridge, lane to the Old Manse and Elisha Jones's, village road (not continued north, as in 1775). Mid-morning sun. Posts keep the fixed ids: `green` = North Bridge (mid-span), `meetinghouse` = Muster ground (causeway head, 144 m west), `buckman` = Far bridgehead (57 m east).
- **Kit:** 22 assets (+`NorthBridge1775`, `Schoolhouse`, `OldManse`; the Manse's gambrel roof is not modelled). `validate_battlefield_kit.py`: 22 passed.
- **Game code:** `UPA250Subsystem::BattleLevelForPreset` (concord_bridge -> L_Concord when the map exists; `TravelToFlowLevel` uses it). `ApplyScenarioPreset` skips `SpawnCreekBridge` on a level with `PA250LevelCrossing`. `SealLevelRiver` narrows the channel blocks to men only at level start (as saved they block everything, which stopped shot across the water - found by the new test). A level opened directly now fights its own scenario (`-PA250Scenario=<preset>` overrides); previews and fps files are named by level (`concord_*`). Battle Road walls carry the tag `PA250BattleRoad`; the test counts that instead of an X range. `tag()` in `build_levels.py` now adds a tag instead of replacing the list.
- **Verified on this Mac:** `-run=PA250Battle` checks=385 fails=0 (9 new: level choice, L_Concord contents, river and bridge collision). Post verify exit 0 on both maps (`PA250_POSTS_MAP`). Stills `docs/preview/{lexington,concord}_relay_*.png` from `scripts/capture_preview.sh relay <level>` inspected. `-PA250FpsSample` 1280x720: Lexington 32.76, Concord 35.42 (gate 30). `-PA250Autoplay` through the campaign: `battle level: L_Lexington`, battle_road at Menotomy, `BRITISH victory flips=4`, result applied, no error lines. `-PA250Autoplay` on L_Concord: `river: 111 channel blocks stop men only`, `CONTINENTAL victory flips=1 bleed=8`, no error lines; `concord_relay_fight.png` shows militia charging over the bridge. `DA_MusketTuning` and the campaign save are byte-identical to before (`battle_result.json` restored from backup after autoplay).
- **Not verified:** hands-on play; a campaign battle that actually begins at Concord and travels to L_Concord (the level choice is unit-tested, the autoplay campaign met at Menotomy); whether a man can be pushed into the 1.5 m of shallows outside the channel blocks; 1080p frame rate; packaged builds; balance over many battles (two autoplay battles only).
- **Known limits:** Concord's grass reads straw-yellow in full sun; the capture flag stands mid-bridge; Brown's pasture (the real Muster Field) is 300 m off and is represented by the post at the causeway head.

## 2026-11-12 — Battlefield kit: period buildings, photoreal surfaces, North Bridge (user request: realistic, historically accurate battle level)
- **Surfaces (`tools/art/build_battlefield_textures.py`, Pillow only):** 14 tiling PBR sets (base colour, normal, roughness, 1024 px) from OpenAI `gpt-image-2.6-sunburst` (`PA250_OPENAI_IMAGE_MODEL`; `-flare` is 7x faster and nearly as good): weathered / ochre / red clapboard, painted trim, cedar shingle, riven oak, door plank, fieldstone, handmade brick, lime mortar, April pasture, road dirt, trampled turf, ploughed soil. Each image is a plain surface with no structure; the tool flattens its lighting, cross-blends the edges (wrap step equals interior step in the log), tones the colours and derives normal and roughness. `--process-only` reruns that from `SourceArt/battlefield/raw` without the API. Plus a generated water ripple normal.
- **Kit (`tools/art/battlefield_kit.py`, bpy 4.5.3):** 19 assets as real geometry (lapped clapboards in short boards, individual shingles, bricks, stones, 12-over-12 sash): `Meetinghouse1714` (50 x 40 x 28 ft as voted, three tiers of windows, **no steeple**), `Belfry` (the separate bell tower that stood on the Common in 1775), `BuckmanTavern` (hip roof, two chimneys), `HouseGable`, `HouseSaltbox`, `HouseCape`, `Barn`, `WagonShed`, `StoneWall_A/B/C`, `PostRailFence`, `PostRailFenceEnd`, `WormFence`, `Well`, `HitchingRail`, `Woodpile`, `TavernSign`, `NorthBridge`. Metres, Z up, front −Y, UCX hulls. What is record and what is a reading is in each recipe's docstring. `tools/art/validate_battlefield_kit.py` reopens every blend and re-imports every GLB and FBX.
- **Unreal (`Scripts/import_battlefield_kit.py`):** `/Game/PA250/Battlefield` (Textures, Materials, Meshes), ownership-tagged, `PA250_BATTLEFIELD_VERIFY_ONLY=1` for a fresh-process check; also `M_Water` and `M_CreekBank`. Kit fronts face +Y after import.
- **Level (`build_levels.py`, `PA250_LEVELS=lexington`):** the graybox meetinghouse (with its wrong steeple) and tavern are the kit buildings on the same footprints; the tavern now faces the Common. New `VILLAGE` list: Belfry beside the meetinghouse, three houses and three barns round the Common, the tavern's shed, well, woodpile, sign and hitching rail (`audit_village` raises if a site touches a road, wall, fence, tree, post, spawn or muster point). Walls are kit fieldstone pieces and fences kit post-and-rail panels, on the **same lines and the same invisible cover boxes** as before. Ground material: pasture, trampled turf, road dirt and four ploughed `FIELDS` with furrows, all with normals; 189 woodlot trees on the skyline (no shadows). Grass and flower cards shade with an up normal (half of each clump used to go black). Grade is photographic instead of the desaturated painting look; sun 15 lux.
- **Concord crossing (`APA250BattleGameMode::SpawnCreekBridge`):** the six gameplay boxes keep size, tags and collision but are not drawn when the kit is present; the North Bridge model, river water and a damp margin are drawn instead (tags `PA250BridgeVisual`, `PA250CreekVisual`, no collision). The Common pad is level out to 23 m for it.
- **Two old defects found and fixed.** (1) The terrain was imported with Nanite on and the game drew and collided with its **498-triangle fallback**, up to 30 cm off `ground_z()`, so walls, buildings and posts floated or sank. Nanite is now off for the terrain and the build raises if LOD0 is not the full heightfield (101,184 triangles). (2) `make_tuning()` wrote the retired 5 s bleed into `DA_MusketTuning` on every level rebuild (the asset is untracked); it now writes 12 s.
- **Locked decisions touched:** none in the sim. Posts, roads, spawns, cover lines and muster points are unchanged; the village sites are GAME INVENTION inside the existing layout. Procedural-asset rules followed (generator, blend, GLB, per-mesh FBX with UCX, previews, round-trip validation, fresh-process Unreal verification).
- **Verified (this Mac):** kit validation `passed: true` for 19 assets (`SourceArt/battlefield/kit_validation.txt`); importer `VERIFIED {"assets": 81, "meshes": 19, "problems": []}` and the same in a fresh process (bounds within 0.5 cm, triangle and hull counts equal the manifest). Editor build Succeeded. `-run=PA250Battle` checks=376 fails=0. `verify_lexington_posts.py` passed (3 posts). Rendered `-game -windowed` 1280x720: `scripts/capture_preview.sh kit` → `docs/preview/lexington_kit_{spawn,green,wide}.png` inspected; log `PA250 creek bridge: kit visuals 3 (bridge=1 river=1)`; `-PA250FpsSample` 32.7–35.8 fps over five samples on the final level (gate 30; 36.82 recorded on 7 Nov, not re-measured on today's build before this change); `-PA250Autoplay` ran to `PA250 battle over: BRITISH victory (tickets_exhausted)` with no error lines. `Saved/PA250/campaign.json` and `battle_result.json` byte-identical afterwards.
- **Not verified:** hands-on play (walking the bridge ramps, cover behind the new wall pieces, bots pathing round the new houses beyond one autoplay battle); the `lexington_common` scenario in a window (a debug launch is now the Concord fight); frame rate with a full battle's smoke at 1080p; baked or packaged builds (the crossing loads kit assets by path).
- **Known limits:** the Common's plan is still the game's, not the surveyed one (in 1775 the meetinghouse stood at the east point of the triangle facing the Boston road, with Buckman Tavern across the Bedford road); moving it means moving posts and spawns. Concord is still fought on the Lexington field: the river is a level 9 x 36 m rectangle and the bridge 12 m, not the 30 m Concord River with its causeway. `-PA250Preview` now and then takes one still from the spectator camera instead of the tagged one (seen on `wide`, 2 runs in 7). `SourceArt/battlefield` is large (every blend packs its textures).

## 2026-11-11 — The British keep accounts too (user request)
- **Sim (GAME INVENTION):** `settle_enemy_budget` (run on the 1st after the player's settlement) settles the other side from its own war chest (`progress.enemy_gold`, starts 60, never shown): town levies, depot trade and workshop muskets in; army pay (2 gold a regular) and building upkeep out — the same `monthly_budget` the player uses, now per faction. A side that controls the harbour and holds its HQ town also lands 40 gold and 12 provisions a month by sea (`sea_subsidy_gold`, `sea_provisions`). No credit: an empty chest leaves the army unpaid (−8 morale each month, `enemy_arrears_months`). With gold above a 30-gold reserve it raises its cheapest catalog company once a month in a town it holds that has the building, muskets and provisions for it (Boston: Regular company, 40 gold, 4 muskets, 4 provisions, into the army there). Workshops now produce for whoever holds the town. At the start Boston nets +8 a month (15 levy + 8 trade + 40 by sea − 48 pay − 7 upkeep), so the garrison grows slowly, replaces losses over months, and a force it cannot pay (e.g. the 40-man reprisal column) loses morale.
- **UE:** the enemy-town view gains **Supply and pay**: whether the place is supplied by sea, and whether deserters report the garrison unpaid. Never the chest.
- **Verified:** `pa250sim_tests` 9475 checks, 0 failed (new `enemy_economy`: first month paid with no recruit; a full chest buys 4 regulars and spends muskets/provisions; an empty magazine blocks it; an oversized army goes unpaid without touching the player's purse; losing the harbour town ends the subsidy; save/load). Clock golden regenerated to `6d22e393707e0b41` (the British now settle monthly and reinforce in an idle run). Editor build Succeeded; enemy-town panel inspected in `ScreenShot00157.png`.
- **Not verified:** a long campaign's balance (how fast Boston grows against the player's income); the unpaid-garrison line on screen (ctest only); the British AI still does not build, buy stores or choose targets by its finances.

## 2026-11-11 — Treasury: balance, monthly income and expenses, borrowing (user request)
- **Sim (GAME INVENTION, after Massachusetts's 1775 bills of credit):** `settle_monthly_budget` in `buildings.cpp`, run on the 1st from `buildings_monthly_tick`. **In:** town levies (unchanged, 15 gold per point of supply value; `clock.cpp` now calls `town_levy_gold`), depot trade +8 gold per Depot level, workshops +1 musket per level into their own town's magazine, farms' provisions as before. **Out:** army pay (1 gold per militiaman, 2 per other soldier), building upkeep (Workshop 2, Depot 2, Fort 3, Meeting House 1, Militia Muster 2, Farm 0 — was 0 for all), 5% monthly interest on debt. A shortfall is borrowed automatically up to the credit limit (60 gold per town held); past it the army goes unpaid: every army −8 morale, Support −3, `pay_arrears_months` counts up. `take_loan` / `repay_loan` move 100 gold at a time. All numbers are in `buildings_catalog.json` → `economy`. New `CampaignProgress.treasury_debt`, `pay_arrears_months` (schema 2, soft defaults). `treasury_json` and the settlement read the same `monthly_budget`, so the projection is what happens. Controller records `borrow` / `repay`.
- **UE:** the treasury figure in the top strip is now a button — "Treasury 540 gold  +145 a month" — that opens **The Province Treasury**: balance, debt and limit, every income and expense line, net per month, what arrives in kind, and Borrow / Repay. Esc closes it. Building descriptions name the new yields.
- **Verified:** `pa250sim_tests` green incl. new `treasury` (projection equals settlement; pay scales with men; depot trade; loan steps, limit, interest; automatic borrowing; unpaid army; save/load); clock golden regenerated to `d514b0cc489b0b91` (an idle campaign now pays its militia). Editor build Succeeded; `-PA250CityProbe` borrowed 100 and captured the window (`ScreenShot00151.png`): levies +180, depot trade +16, pay −33, upkeep −13, interest −5, net +145.
- **Not verified:** a month-end settlement watched in a real window (ctest only); balance over a long campaign (the opening nets about +150 a month against 30–50 gold buildings, so gold is plentiful until armies grow); the British have no treasury.

## 2026-11-11 — Town panel follow-up: purchases, towns list, in-window muster check
- **Magazine tab:** priced purchase buttons for the selected town — 20 cartridges, 4 muskets, 6 provisions (prices from the new pure `purchase_cost` in the sim, the same figure `approve_purchase` charges). Regulars need muskets from the town magazine, and until now only cartridges could be bought.
- **Your towns (top right):** one row per town held, alphabetical, showing free plots or what is under way ("building 1d", "muster 2d", "BATTLE"); a click opens that town. Hidden with the forces list during letters and battles.
- **Fix:** the panel's content was replaced from a widget tick (inside Slate's paint pass), which drew one garbled frame — everything stacked at zero size — whenever the content changed with the clock running. Refresh now runs from a core ticker.
- **Probe:** `-PA250CityProbe` now arranges a 30-day ceasefire through the ordinary diplomacy path (otherwise the British reach Concord within hours and the pending battle stops the clock), builds a Militia Muster, raises a company, and marches it.
- **Verified in a `-game` window (1600x900):** log `CITY_PROBE: muster started; Muster Militia company, Lexington: -20 gold, -3 provisions` → `after muster armies: ... raised_lexington_3@lexington=6` → `march order: Lexington Company → Menotomy. Arrives 22 April 1775 · 1 a.m.`; screenshots `ScreenShot00116–121.png`: the company appears in Your forces, as a counter on the map with its route, and in the town's garrison. `pa250sim_tests` green for `recruitment` and `city_view_buildings`.
- **Noted:** in the normal opening the British march at once, so there is no quiet day to build or muster before the first battle. Another session has since added recruit types to `buildings_catalog.json` ("Company officer", "Rifle company"); they render in the Forces tab unchanged.
- **Not verified:** real mouse clicks; a raised company in battle; the towns list on a window narrower than 1600.

## 2026-11-11 — One town window + raising troops (user request)
- **One window:** the right-hand selection panel is gone; the docked town panel is the only window for a selected town. A town you hold shows stat tiles (Development, Supply, Defence, Manpower) and three tabs — **Buildings** (plots + detail/build list), **Forces** (garrison with "Give orders", raise troops, last engagement), **Magazine** (all seven stores; Buy / Make cartridges, Send to depot). A town you do not hold shows only forces reported (strength undisclosed), whether it is fortified, and reports — no plots, stores or stats. "Enter battle" appears at the top when armies are in contact there. Closing the panel (X / Esc) clears the selection; picking an army in the list opens its town on the Forces tab. The panel steps aside while a staff letter is open. Removed from `PA250MapWidget`: `LocationPanelText`, `ProvinceTitleText`, `PressViewTown`, `PressDevelop`, `DevelopTip`; dev diagnostics moved to the panel footer (`DevText`).
- **Raising troops (GAME INVENTION, sim):** `start_recruitment` / `cancel_recruitment` / `tick_recruitment_jobs` in `buildings.cpp`; catalog in `buildings_catalog.json` (`recruits`, `manpower`). One company musters per town at a time; gold and stores are charged on start, the men arrive after the catalog's days. Militia company: 6 men, 20 gold, 3 provisions, 2 days, needs a **Militia Muster** (they bring their own firelocks). Provincial regulars: 4 men, 40 gold, 4 muskets + 4 provisions from the town magazine, 4 days, needs a **Fort**. Monthly manpower per town (6 / 12 / 18 by size at Support 50, scaled by Support 25–100; `Province.manpower_used`, reset on the 1st). The men join the player's idle army in the town, else form `raised_<town>_<seq>` ("<Town> Company", morale 60). A raised company standing idle in a town with another of the player's armies is absorbed by it (`merge_idle_player_armies`); scripted armies are never absorbed (the defected garrison beside the militia stays separate — `defect_battle_participants`). A town lost mid-muster disperses the men; dismissing a muster returns everything. Saved in `progress.recruit_jobs` (schema_version 2, soft defaults). Controller records `muster` / `cancel_muster`.
- **HUD:** "Your forces" lists up to 6 armies (was the first only); a selected army that no longer exists is deselected.
- **Locked decisions touched:** sim stays authoritative and deterministic (jobs tick on sim hours, ids from `next_command_seq`); persistence stays one JSON, schema 2; battles unchanged (a battle is still set up between one army per side). `CLAUDE.md` does not yet list recruitment among the game inventions — not edited here.
- **Verified:** `pa250sim_tests` 9380 checks, 0 failed (new `recruitment` case; clock golden regenerated to `fd7250c055cb5502` because provinces and progress serialise new fields); editor build Succeeded; `-PA250CityProbe` (now five captures) logged all steps, screenshots `ScreenShot00040–44.png` inspected: Buildings, Forces and Magazine tabs and the enemy-town view render as described.
- **Not verified:** a muster completing in a real window (covered by ctest only), a raised company's counter on the map and marching it, two player armies in one battle province, real mouse clicks, the British AI's reaction to extra armies (it is unchanged and does not recruit).

## 2026-11-11 — Campaign HUD skin (user request: make the map and town look better)
- **Style:** new `UI/PA250HudStyle.{h,cpp}`: one palette (Continental navy, walnut, brass, parchment), framed panel / header-strip / chip / parchment brushes, a navy-and-brass button style, and `Font()` — Crimson Text (SIL OFL; `Content/PA250/Fonts/` with `OFL.txt`) with the engine font as fallback when the files are missing. Colours are entered as on-screen sRGB and converted (`SRGB()`); Slate treats brush colours as linear, which is why the first pass looked washed.
- **Map HUD (`PA250MapWidget`):** top strip rebuilt — title plate, treasury / stores / support in inset boxes, Ledger, and a date plaque with one pip per clock speed and a PAUSED/speed label; brass rule under it. All panels use the framed brushes, all buttons the new style, all text the serif. A soft dark vignette is drawn over the map under the panels (generated texture, hit-test invisible). Symbol glyphs that drew as boxes (coin, fleur-de-lis, swords, crown, wagon…) are replaced by words. The right-hand selection panel no longer repeats the town's slots and levels (the town panel shows them); `DevelopmentText` removed.
- **Town panel:** same font and colour handling; level pips are drawn squares, not typed glyphs.
- **Not changed:** the 3D map itself (engraving, town diamonds, washes, captions, army counters) — another session is working in `PA250ProvinceMapActor`/`PA250ArmyToken`.
- **Verified:** editor build Succeeded; `-PA250CityProbe` at 1600x900, screenshots inspected (see the newest three in `Saved/Screenshots/MacEditor`). **Not verified:** hover/pressed button states, the parchment panels (letters, battle broadside, About) and the ledger with the new skin, windows smaller than 1600x900.

## 2026-11-11 — Town panel in the manner of a grand-strategy province view (user request)
- **Look:** `UPA250CityViewWidget` is now Slate (like the rest of the campaign HUD), docked lower left, not modal: painted header with the town's name, size and holder; stat tiles (Development, Supply, Defence, Support); magazine line; one square plot per building slot with its picture, level pips and a progress bar while building; a detail pane (selected building with Upgrade / Demolish / Stop work, or the build list with cost, days, effects and the sim's reason when unavailable); status line + treasury. Fort and Meeting House have no artwork and show an initial.
- **Opening:** a mouse click on a town selects it and shows the panel; with an army selected, a click on another town is only its march order (`UPA250MapWidget::HandleTownClicked`). Double-click is gone. Esc, the X, **View Town** (toggle) and clearing the selection close it. Only the panel's rectangle blocks map clicks (`IsViewportPointOverPanel`). Scripted probe clicks (`HandleProvinceClick`) do not open it.
- **Demolition (user question — why does it pay?):** no longer a refund. Salvage only, 10% of the level-0 cost per level (`demolish_refund_pct` in `buildings_catalog.json`), logged as "gold salvage (timber, nails, brick)"; demolishing a Meeting House also takes back the +5 Support it gave. Stopping work still returns the full purse.
- **Removed:** `UPA250BuildingSlotWidget` and the empty `WBP_BuildingSlot` scaffold (unused once the layout is Slate). `WBP_CityView` stays as an optional place to swap the header/plot pictures.
- **Probe:** `-PA250CityProbe` on `L_Campaign` (`-game -windowed`): three `Shot showui` captures from 12 s (Lexington build list; Farm + Fort under way; Boston), then quits.
- **Verified:** `PlayAmerica250Editor` Mac Development **Succeeded**; `-PA250CityProbe` at 1600x900 logged `CITY_PROBE: lexington build list open=1`, `farm + fort started; Build Fort, Lexington: -45 gold`, `boston open=1`; screenshots `Saved/Screenshots/MacEditor/ScreenShot00019–21.png` inspected (panel docked, 4 plots per row, wrapped text, enemy town without build buttons). `pa250sim_tests` 9326 checks, 0 failed (scratch build dir; includes another session's concurrent cartridge-scavenging changes in `battle.cpp`).
- **Not verified:** real mouse clicks on the plots/buttons (the probe calls the handlers), demolish confirm on screen, a 720p window, save/load from the UI. `Shot showui` came out washed on two of three captures (existing HUD too), so colours were judged from the one good frame. Plot pictures are blank for the first seconds while textures stream. The edit that outlines only the first empty plot is not yet compiled.

## 2026-11-11 — City View review pass (fixes on top of the 2026-11-10 entry)
- **Sim:** one validation (`construction_block` in `buildings.cpp`) now feeds `start_construction`, `building_preview` and `city_view_json`, so a greyed-out reason cannot disagree with what the sim accepts (and the snapshot no longer copies the campaign state six times). A town lost mid-build abandons the job (no refund) instead of finishing the building for the new owner. Finished/cancelled jobs are removed from the save. Demolish refund reads the same cost source as the charge. Completion clamps to max level. Meeting House morale and the Militia Muster flag apply only to a defender on its own ground. `buildings_catalog.json` was missing `battle_morale` (shipped Meeting House gave +0): now 2. `city_view_json` adds `yours`, `battle_pending`, `upgrade_available`, `demolish_refund`.
- **UE:** City View fallback layout is built in `RebuildWidget` (it was built in `NativeConstruct`, after Slate had taken an empty root, so the empty scaffolded `WBP_CityView` would have shown nothing); opaque panel + scroll box + font sizes. Grid/menu are rebuilt only when their content changes (was every sim hour). Upgrade button shows its cost and the sim preview as tooltip, disabled when unavailable; Demolish needs a second click and states the refund; no build/demolish buttons in towns you do not hold; keyboard focus returns to the map HUD after every City View click and on close. Map building icons rebuilt only when buildings change. `LoadSimDataFiles()` replaces six copied try/catch blocks.
- **Verified:** `pa250sim_tests` 9283 checks, 0 failed (scratch build dir), clock golden unchanged (`1fcb1b06b253b281`); `clang -fsyntax-only` from `compile_commands.json` clean on `PA250CityViewWidget.cpp`, `PA250BuildingSlotWidget.cpp`, `PA250Subsystem.cpp`, `PA250MapWidget.cpp`, `PA250ProvinceMapActor.cpp`, `PA250SimCore.cpp`.
- **Not verified:** no UBT build and no PIE/`-game` run (editor and other sessions' commandlets were open). The City View has still never been seen in a rendered window: layout, icons, focus behaviour and the double-click are unverified. Syntax check of `PA250BattleGameMode.cpp` fails on incomplete `UWorld`/`UStaticMesh` in `PA250BattleGameMode.h:98` (file not touched here; may only be a unity-build include order).
- **Open (both closed by the next entry):** double-click march side effect; Meeting House Support farming.

## 2026-11-12 — Dragoons for both sides (user request: campaign unit, ride then fight on foot)
- **Sim (data + defaults only):** new unit class `dragoon` (GAME INVENTION for 1775; neither side had cavalry at Lexington/Concord). Recruits in `PA250Sim/data/buildings_catalog.json` and mirrored in `ensure_default_recruits`: Continental `light_horse` "Troop of light horse" (3 men, 60 gold, 3 muskets, 5 provisions, 6 days, needs a Farm); British `dragoon_troop` "Troop of light dragoons" (3 men, 70 gold, same stores, needs a Fort). No rule changes: tickets, pay (regular rate), losses and `BattleResult` are class-generic. The British AI economy buys the cheapest company, so it never picks the 70-gold troop and the scripted 1775 run is unchanged. Opening armies unchanged (user choice), so dragoons appear only after one is raised.
- **Battle:** Dragoon kit for both sides (carbine, pistol and sabre; no bayonet; aim ×1.1, range ×0.9; Continental round hat, British cap), last in the spawn order, and combat profiles (British Veteran/Light, Continental Trained/Light). `ConfigureSoldier` puts dragoons in the saddle and releases the horse of anyone re-kitted into another class. Dragoons are never in volley squads. Spawned dragoons stay at their side's start (not pulled in to 55 m), as a mounted reserve. The rider wears the soldier's own coat (his per-slot materials are copied onto the horse's rider).
- **Own horse:** a dragoon dismounting pickets his horse where he stands. It stays riderless and can still be shot; it is remounted by walking back to it, and the player uses H within 3 m. If it dies (ridden: the throw; picketed: it just goes down) he fights on foot for the rest of the battle. When his body is cleared, his horse goes too.
- **Bot dragoons (`APA250BotController::TickDragoon`, pure `PA250DragoonDecide` in the header):** ride to the post (gallop over 40 m, canter over 15 m, else trot); get down when an enemy is within 60 m or at the post (25 m); on foot they fight through the normal solo skirmisher path; with no enemy inside 120 m and the post over 60 m away they walk back to the horse (hysteresis: they keep going until the post is within 35 m) and remount. They wait out a reload before mounting and decide nothing while still dropping in at spawn. All GAME INVENTION values.
- **Debug:** `-PA250ExtraUnits=side:class:units,...` adds tickets to a debug battle only (launched straight into the map; never a campaign battle).
- **Verified (this Mac):** `pa250sim_tests` 9485 checks, 0 failed (5 Concord recruits; light horse refused without a Farm, British troop `unknown_unit` to the Continental player, 3 dragoons after 6 days, gold 500→440, muskets 5→2). The golden clock test passed (the golden file had been rewritten at 21:36 by another session; not by this work). Editor build Succeeded. `-run=PA250MountedWeapons` 56 PASS / 0 FAIL (decision table with hysteresis, gait choice, kits and order, spawn in saddle, picket, H refused from 10 m, H remounts the same horse, picketed horse shot dead alone, no remount, horse released on re-kit). `-run=PA250Battle` checks=376 fails=0 (final build). Rendered `L_Lexington -game -windowed -PA250Autoplay -PA250ExtraUnits=continental:dragoon:3,british:dragoon:3`: both sides mount, dismount and picket, fight on foot; one dragoon walked 48 m back, mounted his own horse, rode about 40 m to the next post and got down; horses took musket hits (82, 89). `-PA250ClassPick` shows a Dragoon card (12 men left), and the kill feed shows "Dragoon x Militia". Saved/PA250 result files restored byte-identical after each run.
- **Not verified:** a player choosing the Dragoon card and spawning mounted (same `ConfigureSoldier` path as the bots); the hysteresis fix in a rendered battle; raising light horse in the campaign UI by hand (the town panel lists catalog recruits generically). The Lexington field is compact, so dragoons ride only short distances there.
- **Gaps:** no class portrait for the Dragoon card (the portrait tool covers the existing classes); dragoons never fire or cut from the saddle (user chose ride-then-fight-on-foot); horses use the infantry capsule for navigation, so a horse can clip through walls.

## 2026-11-12 — Riding: gaits, sabre and firearms from the saddle, the horse shot from under him (user request, RDR as inspiration)
- **Gaits:** Shift urges the horse up a gait (walk → trot → canter → gallop) and Ctrl reins it down. Speeds are the pack's authored ground speeds (143/353/550/1029 cm/s, `WarHorse_clips.json`). The clip follows the measured speed (with hysteresis) at play rate speed/clip-speed, clamped 0.6–1.3, so the hooves don't slide. Riding acceleration is 420 cm/s², braking 520, turn rate 170°/s (GAME INVENTION). Off the reins and nearly stopped, he drops back to a walk. `TickMounted` replaces `SetMoving`, which is kept for the test pawn.
- **Weapons:** 1 sword, 2 musket, 3 pistol as before. The sabre now cuts: the blade tip is swept (22 cm sphere, Pawn objects) between 25% and 80% of the slash, once per man. Damage is 55 standing up to 120 at a gallop (GAME INVENTION); his own side is never cut. Firing from the saddle adds an aim cone of 0° standing to 4° at a gallop (GAME INVENTION).
- **Hits:** `APA250MountedSoldier` has a horse body box and a rider capsule (query-only, they block only WorldDynamic, i.e. musket balls). The foot capsule ignores WorldDynamic while mounted. A ball in the rider volume is the man's wound (it is forwarded to his `TakeDamage`, and the projectile treats it as hitting him for friendly fire, marker and kill credit). A ball elsewhere wounds the horse: 250 health (GAME INVENTION), a flinch below a canter, no friendly fire. His own balls ignore his horse.
- **Horse death:** the horse detaches from the rider, plays `A_WarHorse_Death` (×1.3 above a canter), slides on with its momentum for 1.2 s and stays on the field for 120 s. The rider is thrown as a ragdoll: each body part is launched by its height above the pelvis (legs keep 0.3 of the horse's speed, the upper body gets +0.45), which pitches him over the neck at speed or topples him off the right side at a walk. Linear damping 0.9 while loose. Fall damage is 0.02 per cm/s above a trot (GAME INVENTION). When the body settles (≥1.6 s in physics time; at most 3.5 s) the capsule is stood where he lies, and he blends from the physics pose to the standing pose over 0.8 s. No input while thrown. Remounting is blocked for 10 s (GAME INVENTION). New exec functions: `KillHorse`, `MountedGaitUp/Down`, `MountedSlash`. New code is in `Mounted/PA250MountedRiding.cpp`; `RFCharacter.cpp` has only small hooks.
- **Fixed on the way:** (1) `AActor::SetActorRotation` sweeps, so the gun pose now uses `SetWorldRotation` without a sweep. (2) A latent bug: on the first switch to absolute rotation the gun kept the hand-bone rotation whenever the aim equalled the stored relative rotation (a `SetWorldRotation` no-op); the transform is now refreshed. That was the failing check `newly equipped gun aligns before its first tick`.
- **Verified (this Mac):** editor build Succeeded. `-run=PA250MountedWeapons` PASS 40 / FAIL 0 (adds gaits, hysteresis, play rate, sway and sabre tables, gait keys, gait clips, hit routing, sabre faction rule, horse death → detached corpse + death clip, thrown loose body, capsule restored, no fire while thrown, remount blocked, back on his feet and re-attached). `-run=PA250Battle` checks=376 fails=0. Rendered: `L_Lexington?game=/Script/PlayAmerica250.RFGameMode -game -windowed`, Metal, probe `-ExecCmds="py ride_probe.py"` (scratch, frame dt 0.023). Log: gallop at 1029 cm/s, `horse: down at 1029 cm/s`, `thrown ... over the neck`, up after 2.4 s, about 3 m past the fall. Frames show him pitching forward, landing head first and the horse lying on its side in its tack. The slow fall (53 cm/s, slid off) was also seen.
- **Not verified:** a sabre cut actually landing on a man in a rendered run (the rule is tested; the blade sweep needs the animated pose); a ball wounding the rider through the rider volume in a real battle (a bare commandlet world refuses pawn damage without a game mode); bots riding (only the player mounts). The man appears as the engine mannequin under plain `RFGameMode`; the battle mode assigns the soldier mesh.
- **Known limits:** no authored get-up clip (the physics-to-pose blend stands in for one); the rider dying while mounted still removes the horse (`OnDeath` → `Dismount`).

## 2026-11-12 — War horse import corrected; army art 1.5×; campaign map veil fixed
- **War horse:** the Nov 9 import left both mesh slots on `WorldGridMaterial`, both ORM textures sRGB with default compression, and both materials without the skeletal-mesh usage flag. The game therefore drew the engine default material and the horse looked flat grey. `Scripts/import_warhorse.py` now fixes these in place (always saving; legacy FBX selected process-locally) and keeps the mesh, skeleton and 8 clips (checked correct: 39 bones, `rider_seat` z 157.1, durations match). `PA250_WARHORSE_VERIFY_ONLY=1` checks everything in a fresh process. Verified: `WARHORSE_IMPORT_OK`, then `WARHORSE_VERIFY_OK` in a fresh process; a rendered shot shows the dappled coat, blue/gold saddle cloth and brown tack. The stuck Nov 9 commandlet (pid 3972, game module missing during a build) was killed at the user's request.
- **Army art 1.5×:** `FigureHeightUU` 12 → 18, with reveal/hide 22/19 → 33/28.5 px so the art appears at the same zoom, only larger. `PA250MapFraming` Success (militia 30.5×26.5 UU, 26 slots clear). Rendered `-PA250ArtProbe=enlarged`: figures 48.3 px (were 32.2); saves byte-identical.
- **Map tooltip veil (bug from the hover-tooltip work):** `UPA250MapTooltip::HideTip` did nothing on its first call (`bShowing` started false), so the unsized widget stayed visible over the whole viewport as a 96%-opaque cream card. It now always collapses. Verified: art probe contrast std ≈1.5 before, 34–49 after.

## 2026-11-11 — Militia illustration on the campaign map (user request)
- **Art:** user-supplied painting (officer pointing, four men aiming) kept as `Content/PA250/Source/ArmyArt/original/Massachusetts_Militia.png`; `tools/art/prepare_army_sprites.py` now also writes `army_militia.png` (content 589×512 in the 1024×512 canvas, uv 0.57520×1.0, aspect 1.1504). Rerun output: the four existing sprites, oval and wash came out byte-identical; composited on parchment, the new sprite shows no matte fringe.
- **Rule:** new `EPA250ArmyArt::Militia` (appended, so existing indices stay the same). `ChooseArmyArt` uses it for any Continental force that is not at least half line infantry (the slice's opening army 4+18 now shows art instead of the counter). British militia-majority forces still use the counter. FigureFrac 0.68 (rear-rank men ≈350 of 512 px), so the figures are 12 UU like the other art (world ≈20.3×17.6 UU). The Continental slot footprint is now the larger of regulars and militia (regulars stay larger, so slots are unchanged). `static_assert` checks one spec per enum value. `PA250MapFraming` checks the new asset plus 2 rule cases (c1 → Militia; militia-only marching/engaged → Militia). ART_PROBE preloads the texture. The About text now describes it as a painted illustration, not engraved.
- **Importer:** `Scripts/import_army_art.py` adds `army_militia.png` → `/Game/PA250/Textures/Army/T_Army_Militia`. `PA250_ARMY_ART_ONLY=T_Army_Militia` (comma list) imports only the named textures and leaves the other textures and the materials untouched. Use it while other processes have the art loaded. If the texture is missing, the token logs `PA250 army art: ... missing texture` and falls back to the counter.
- **Verified (this Mac):** ruff clean; new paths not git-ignored. Import (militia only, after another session's build finished): `ARMY_ART_IMPORT: texture T_Army_Militia 1024x512 ... TC_BC7 ... never_stream=True`; the other five textures keep their 06:12 timestamps. `-run=PA250MapFraming`: `army art 4 1024x512 world 20.3x17.6 UU`, `art rule cases checked 8`, `army slots checked 26`, `Success - 0 error(s), 0 warning(s)`. Rendered: `L_Campaign -game -windowed -ResX=1600 -ResY=900 -PA250ArtProbe=militia` (editor binary, Metal) logged `T_Army_Militia ... PF_BC7 mips=11 srgb=1`, `continental_militia art=4 lod=illustration figure_px=32.2`, militia selected by clicking its art. Shots in `Saved/Screenshots/ArtProbe/militia/` (02 shows the painting at Concord). Every `Saved/PA250/*.json` byte-identical after the run. A `-game` window started before the import keeps the counter until it is relaunched (the token's textures load once, at class load).
- **Seen:** at the reveal zoom the painting is about 55 px wide, small next to its 15 UU name plate. The British art has the same figure scale (it is wider only because it shows more men).

## 2026-11-10 — Battle look and feel: cleaner screen, wounded view, ambience and footsteps
- **Screen:** engine on-screen debug text (the yellow `[VSM] Non-Nanite Marking Job Queue overflow` warning) is off during a battle and restored in `EndPlay` (`-PA250ScreenMessages` keeps it; the warning itself still goes to the log). Nameplates: `ARFCharacter::ShouldShowNameplate` - an enemy only inside 15 m and in sight (was 50 m), a squadmate inside 25 m, other friends inside 12 m (was 30 m); the label says `Regular` / `Provincial` / `Militia` instead of the raw faction id, in a smaller font. The controls legend shows for 25 s, fades by 30 s, and returns while **F1** is held (H is the mount key); a small `F1 (hold)  controls` hint stays.
- **Player's view (`ARFCharacter::TickViewFeedback`, `ViewStress`):** vignette and desaturation scale with wounds and with being under fire, and snap in on a fresh hit with a brief colour fringe; they ease out as the danger passes. Only the stressed view sets camera post-process overrides (vignette, saturation, fringe); at rest the level's `PP_Dawn` is untouched and exposure is never set here. A musket fired within 6 m of the player gives a small FOV jolt.
- **Sound (`tools/audio/synth_sfx.py`, original synthesis):** `ambience_dawn` (24 s seamless wind-and-birds loop; `FPA250BattleAudio::StartAmbience` at battle start, stopped in `EndPlay`; forced looping at runtime and in `Scripts/import_audio.py`), `footstep_grass_1..4` (`TickFootsteps`: one per stride for the player, and for other soldiers within 18 m of the listener). Imported to `/Game/PA250/Audio/SFX` (5 assets).
- **Tooling:** `-PA250BattleShots` now takes its whole comma list (FParse stopped at the first comma, so only the first still was ever taken); `-PA250HurtProbe=<s>` wounds the player for 60 at that game-second.
- **Verified (this Mac):** editor build Succeeded; `-run=PA250Battle` checks=367 fails=0 (7 new `look:` cases). Loop seam measured on the WAV: step across the loop point 0.008 against a median sample step of 0.004 (p99 0.045). Real window, human player, sound on: `L_Lexington -game -windowed -PA250HurtProbe=2.2 -PA250BattleShots=2,2.35,3.4,40` - `player health now 40`; corner luminance 35.3 before the wound, 21.9 one second after (vignette on), no yellow engine text in any still; at 40 s the legend has gone to the F1 hint and the spawn screen reads `RESPAWN tickets 1` (the held ticket).
- **Not verified:** any of the audio by ear (ambience level against gunfire, footstep rhythm and volume); the fringe and desaturation by eye in motion; nameplate distances in play.
- **Seen, not changed:** launching `L_Lexington` directly drops the player a few metres from British regulars, who bayonet him inside five seconds (already noted on 2026-11-10 as pre-existing). The campaign's own battle entry was not checked for this.

## 2026-11-10 — The player's side cannot lose on tickets while the player is alive (user decision)
- `APA250BattleGameMode::SideIsOut` (pure) now holds the exhaustion rule; `CheckTicketExhaustion` never counts the player's side out while the human's pawn is alive on it (not in autoplay / selftest). The enemy side's rule is unchanged, and the check still runs when the player dies, so falling at 0 tickets past the 120 s gate ends the battle.
- **Not changed:** the battle clock. `time_limit` / `force_preserved` still decide the battle when time runs out, even with the player alive.
- **Verified (this Mac):** editor build Succeeded (second attempt; the first failed at link during another session's build); `-run=PA250Battle` checks=360 fails=0 (4 new `exhaustion:` cases); `scripts/run_slice_autoplay.sh` PASSED. **Not verified:** in a live battle (no rendered run of a side at 0 tickets with the player alive past the gate).

## 2026-11-10 — One ticket held in reserve for the human player (user decision)
- **Rule (`FPA250TicketLedger::Held`, plain C++):** after the opening deployment one ticket of the player's side is held for the player. Deaths and bleed cannot take the pool below it, bots are not reinforced from it (`CanReinforce`; `SpawnBot`, `SpawnBotAt` and the muster wave count use it), and deaths are still counted for `BattleResult.losses`. When the player rejoins on it (`DrawHeld` in `RequestSpawn`) it becomes an ordinary ticket and the banner says `Last reserve - fall again and you fight on as one of the men still standing`. After that the take-over rule below applies. Not set in autoplay / selftest (no human).
- **Consequence to know:** while the hold is undrawn the player's side cannot reach 0 tickets, so it cannot lose on `tickets_exhausted` until the player has died once, rejoined, and the last ticket is spent. The time limit and other end rules are unchanged.
- **Verified (this Mac):** editor build Succeeded; `-run=PA250Battle` checks=356 fails=0 (7 new `reserve:` cases); `scripts/run_slice_autoplay.sh` PASSED. Real window `-PA250TakeoverProbe=held` (pool cut to the held ticket, player killed at 8 s, spawn requested 1 s after the death cam): `after death cam spawn_screen=1 remaining=1 held=1`, then `takeover probe PASSED: pawn=RFCharacter_28 alive=1 player=1 input=1 view=1 cursor=0 spawn_screen=0`. `-PA250TakeoverProbe` (pool 0): still PASSED via take-over.
- **Not verified:** clicking the spawn screen by hand in PIE (the probe calls `RequestSpawn`, what the button calls). Restart the editor to load this build.
- **Build note:** a concurrent build can compile a file just before it is edited, leaving an object newer than the source; the next build then skips it and reports Succeeded. `touch` the file if a change seems missing from the binary.

## 2026-11-10 — Out of reserves: the player takes over a standing soldier
- **Report:** "after dying I can't choose a spawn point." Editor log of that PIE battle (Lincoln, `battle_road`): `PA250 tickets continental: 1 units -> 4 soldiers`. Bot deaths spend the same pool, so it was empty about 20 s in; `CanSpawn` was false when the death cam ended and `CloseSpawnScreenToSpectate` put the player in spectate with no message, while the 120 s exhaustion gate kept the battle running. Not caused by the corpse-lifetime change.
- **Fix (battle layer only; tickets, losses and the sim are unchanged):** `APA250BattleGameMode::TakeOverSoldier` - with no reserves, the player possesses the nearest living bot of his side (bot controller retired, no ticket spent, bot-only state cleared) and the banner says `No reserves left - you take the place of a man still standing (N left)`. With nobody left: `No reserves and no men left standing - you can only watch`. `ShowNotice` puts a timed line in the banner slot. `UPA250VolleyCoordinator::DischargeOne` never fires a player-controlled musket (a taken-over man may still be on a squad's list until the next rebuild).
- **Verified (this Mac):** editor build Succeeded; `-run=PA250Battle` checks=340 fails=0. Real window `L_Lexington -game -windowed -PA250TakeoverProbe` (sets the player's reserves to 0 and kills him at 8 s): `PA250 takeover probe PASSED: pawn=RFCharacter_19 alive=1 player=1 input=1 view=1 cursor=0 spawn_screen=0`; still `Saved/PA250/takeover_probe.png` shows the shoulder camera, the player HUD and the notice.
- **Not verified:** by hand in PIE (moving, firing and dying again as the taken-over man). The editor was open during the build, so it must be restarted to load this.
- **Open:** a 1-unit army gives a 4-soldier reserve shared with 16 bots, so the reserve is gone in seconds; whether small armies should field fewer men or keep a life back for the player is a design decision, not changed here.

## 2026-11-10 — Melee: bots that fight, steel and stock sounds, blood on the bayonet
- Builds on the same day's lunge-pose and guard/parry/riposte passes (other session); the hit rules and poses are unchanged.
- **Bot hand-to-hand (`PA250ChooseMeleeMove`, pure; `APA250BotController::MeleeAttack` replaces the two inline thrust/charge branches):** a bot that reads a raised guard (`ResolveGuard` turned on him) closes to butt-stroke reach and uses the stock, the guard-breaker, instead of thrusting into it; a butt-stroke that lands is followed by a thrust at once; otherwise he needs `MeleeRecoverSeconds` (+/-30 %) between blows. New AI-only training values (GAMEPLAY SETTINGS, `PA250CombatProfiles.cpp`): `MeleeRecoverSeconds` Veteran 0.45 / Trained 0.70 / Recruit 1.00, `MeleeSkill` (chance per exchange to read a guard) 0.85 / 0.60 / 0.35. Before, every bot thrust again on its next think tick and the player could hold X against bots indefinitely.
- **Sounds (`tools/audio/synth_sfx.py`, original synthesis):** `bayonet_clash_1..3` (block / parry, was the flint click), `bayonet_stab_1..2` (thrust / charge hit, was the ball-strike thud), `butt_thud_1..2`, `melee_swing_1..3`. Imported to `/Game/PA250/Audio/SFX` (10 assets; `Scripts/import_audio.py` picks them up on a full re-import). `FPA250BattleAudio::PlayAtOr` falls back to the old bank when a new one is missing.
- **Blood:** an unblocked thrust or charge hit throws the red mist (`UPA250SmokeSubsystem::AddImpact`) from the wound.
- **Verified (this Mac):** editor build Succeeded. `-run=PA250Battle` checks=340 fails=0 (16 new `melee ai:` / `melee audio:` cases, including a real bot against a real guarding soldier: butt-stroke into the guard, thrust at an open man). `-run=PA250Combat` fails=0, `-run=PA250Visuals` failures=0. `scripts/run_slice_autoplay.sh` PASSED. Rendered `L_Lexington -game -windowed -PA250Selftest=battle`: `selftest_battle PASSED`, no asserts.
- **Not verified:** heard the new sounds in game (rendered runs used `-nosound`; the WAVs were not auditioned either); fought bots by hand (whether the pause between blows and the guard-break feel fair with X held); the blood mist on a bayonet hit in a still; melee balance over several battles. `-PA250MeleeProbe` was not re-run.

## 2026-11-10 — Battle feel: fire discipline per troop type, ball strikes, the fallen stay
- **Fire discipline (GAMEPLAY SETTINGS in `DA_CombatProfiles` class defaults, `FPA250TrainingProfile`):** `VolleyRaggedness` (multiplier on `VolleyFireJitterSeconds`: Veteran 0.6 / Trained 2.8 / Recruit 5.5), `RollingFireFileSeconds` (Veteran 0.14: the opening volley of a contact is fired together, later ones run down the line from the right, file by file; `FSquad::VolleysThisFight` resets when contact is lost), `MisfireChance` (AI only, 3 / 6 / 10 %, doubled at full stress, capped 50 %; **the player never misfires**), `AimVariance` (each bot's aim cone scaled by a fixed per-man factor from his profile seed). `FPA250TacticalRole::RestedAimScale` (Light 0.75, Irregular 0.7): fire-at-will from a covered spot is tighter. Pure helpers: `UPA250VolleyCoordinator::VolleyShotDelay`, `UPA250CombatProfiles::AimScaleFor` / `MisfireChanceAt`.
- **Misfire:** `AMusketWeapon::Misfire()` = flint click, pan puff + weak flash, lock animation, no ball, full reload. Rolled in `ARFCharacter::FireMusketAt` (`MisfireChance`, `MisfireStress` published by the bot controller). `PA250 battle: N AI misfires` is logged at battle end.
- **Wounds:** `URFHealthComponent::OnWounded` (non-lethal hit) -> `ARFCharacter::OnWounded`: 0.45 s stagger, 4 s / 3 deg aim penalty, bot loses his aim (`ConsumeFlinch`), player gets an FOV punch + kick.
- **Smoke bank (`UPA250SmokeSubsystem`, visual only):** per-puff `Tint` / `bPowder` / `Rise` / `WindShare`; `AddPanFlash` (every shot, and alone on a misfire), `AddImpact` (dirt where a ball goes to ground, brief red mist on a man; called from `AMusketProjectile::OnHit`). `MaxPuffs` 240 -> 300.
- **The fallen stay:** corpse lifespan 3 s -> `pa250.CorpseSeconds` (45), capped by `pa250.MaxCorpses` (24, oldest cleared first; the player's own body keeps the death-cam rule). The dead man's musket stops tracking his aim and lies flat; team marker hidden. A ball that hits a body only moves it (no damage, no hitmarker).
- **Player feel:** FOV punch on firing and on a near miss.
- **Tooling:** `-PA250BattleShots=40,80` writes HUD stills of a running battle to `Saved/PA250/battle_shot_<n>.png` and logs fallen / puffs / misfires. `-PA250Preview=smoke` now also shows pan puffs and ball strikes.
- **Verified (this Mac):** editor build Succeeded. `-run=PA250Battle` checks=324 fails=0 (21 new `fire:` cases). `-run=PA250Combat` fails=0. `-run=PA250Visuals` failures=0. `scripts/run_slice_autoplay.sh` PASSED (3 misfires in the battle). Real window: `scripts/capture_preview.sh smoke` (stills regenerated in `docs/preview/`, dust visible ahead of the line) and `L_Lexington -game -windowed -PA250Selftest=battle -PA250BattleShots=40,80,110`: `selftest_battle PASSED`, no asserts, `fallen_on_field=18 puffs=103` at game-second 40, a fallen regular visible in the still.
- **Not verified:** played by hand in PIE (rolling fire vs ragged volley by ear, flinch and FOV punch as the player, musket pose on a body up close); frame rate with 24 bodies down (no `-PA250FpsSample` run); battle balance over several runs (misfires and per-man aim shift lethality slightly; B4 numbers not re-sampled).

## 2026-11-10 — City View building slots (hybrid)
- **Sim:** `buildings_catalog.json` + `province_sizes.json`; timed `start_construction` / cancel / demolish (25% refund); `church` + `militia_muster` on `Province`; jobs in `CampaignProgress.construction_jobs`. Tracks (farm/workshop/depot/fort) still 0–3 and occupy one slot each; Lexington = 3 empty slots. Seeded levels unchanged. Clock golden `1fcb1b06b253b281`.
- **UE:** `UPA250Subsystem` City View API + HudEpoch gold sync; UMG `UPA250CityViewWidget` / `UPA250BuildingSlotWidget` (fallback layout; `WBP_CityView` / `WBP_BuildingSlot` scaffolded); Slate **View Town** + double-click open, Esc closes; develop buttons removed from the location panel; map building-icon markers under towns; Militia Muster bumps defender muster cap; fort cover still cubes. Art imported under `/Game/PA250/UI/CityView/`.
- **Verified:** `pa250sim_tests` 9256 checks, 0 failed (`city_view_buildings` now also loads the shipped catalog/size JSON and covers cancel 100% refund, upgrade-in-place at full slots, demolish refund in `resource_changes`, church +5 on complete / +1 monthly); `PlayAmerica250Editor` Mac Development **Succeeded**; city-view art + WBP scaffold commandlets OK. `-run=PA250Battle` checks=324 fails=3 (pre-existing fire/flinch cases from concurrent melee work, not City View). PIE City View / Lexington build–demolish / save-load UI **not** re-run this pass (editor busy with other probes). One follow-up display fix (build-failure status shows the sim preview text, not the raw error code; demolish/church strings read the catalog refund/support values) is **not** UE-compiled yet — editor was open with another session's probe.

## 2026-11-11 — Reload drill animation (body)
- **Before:** reloading had no body pose; the musket stayed in the carry while only its lock and ramrod moved. Now the soldier performs the drill, driven by the active-reload sequencer's phase and progress (so a faster or fumbled reload moves the hands faster or holds them; miss-delay and jam hold the last pose).
- **Prime (phase 1):** musket across the waist in the left hand, muzzle up a little and to the left; right hand to the cartridge box on the hip, up to the mouth (bite), then onto the lock. **Ram (phase 2):** standing still, the butt goes to the ground by the left foot with the musket near upright ("cast about"); right hand charges the muzzle and then follows the musket's own ramrod bone (`b_gun_Ramrod`) up and down. On the move the musket stays at the waist and the hand still works the muzzle. **Recover (phase 3):** hand back to the stock and the hold eases to the carry. Head tips down to watch the hands.
- **How:** `UPA250MeleeComponent::TickReloadPose` / `ReloadHandLocal` (pure) compute the hold and the right-hand point; new `Reload` weight + `ReloadRightHandCS` in `FPA250MeleePose`; the musket is pinned in the world on a body-relative anchor while reloading (`AMusketWeapon::BodyPoseAlpha/BodyPoseRotation`, absolute location) so it does not shake with the working hand; the left-hand target is given in absolute component space then (`FPA250SupportHandTarget::SpotCS`). A swing, the guard, a blow or the post-shot shoulder hold interrupts the pose; the reload itself runs on.
- **Ramrod timing fixed:** measured on the pack's 4.2 s reload clip, all ramrod travel is in the first 75 % and only the lock moves after. `ReloadAnimAlpha` now holds 0 while priming, plays 0→0.75 over the ramming phase and 0.75→1 over the recovery (it was spread evenly over the three phases, so the rod was already out while he primed).
- **Verified (this Mac):** editor build Succeeded; `-run=PA250Battle` checks=367 fails=0; `-run=PA250Visuals` failures=0 on the second run (first failed only the known flaky homespun check), new check: right hand 0.0 cm from the drill's mouth target. Rendered `-PA250MeleeProbe` on the player's soldier with a 10 s Arcade reload left untouched: 8 stills `31_reload_cartridge_box.png` … `38_reload_done.png` (38 stills in all), pose weight 1.00 through phases 1–3 and 0.00 when loaded. Not verified: reloading while walking or ducked behind a wall, the jam hold, bots in a line reloading together, hands-on play.
- Process note: three sessions deadlocked for ~25 min because each wait loop used `pgrep -f` patterns that matched the others' shell command text. This session now waits with `pgrep -x dotnet` / `pgrep -x UnrealEditor` from a script file.

## 2026-11-10 — Musket to the shoulder, hit effect, kill slow-motion
- **Shoulder hold:** new `Shoulder` pose weight brings the trigger hand up in front of the right shoulder (butt in the shoulder, left hand solved onto the stock). On while the player aims (RMB), while a bot's squad is at "Present!" (`APA250BotController`, one call to `UPA250MeleeComponent::Present`), and for 0.55 s after any shot. Before this every musket was aimed and fired from the hip. The muzzle is therefore higher when aiming (shots leave from shoulder height). Reload still has no body pose.
- **Hit effect:** blows that land (not blocks) spawn `/Game/Variant_Combat/VFX/NS_Damage` at the victim. It is the combat template's generic burst - small pale specks - not blood; there is no blood asset in Content.
- **Kill beat:** the player's melee kill slows the whole field to 0.35 for 0.45 s of real time (`KillSlowMoScale`, `KillSlowMoSeconds`; skipped when the battle is already time-scaled, e.g. autoplay) with a small FOV punch. HUD line on a kill is now `Run through` / `Struck him down`.
- **Verified (this Mac):** editor build Succeeded; `-run=PA250Battle` checks=367 fails=0; `-run=PA250Visuals` 0 failures on the re-run (first run failed only the known flaky homespun check). Rendered `-PA250MeleeProbe` (30 stills): `27_shouldered_side.png` musket level at the shoulder; player's kill logged `global time dilation 1.00 → 0.35 → 1.00 (after)`, `30_player_view_kill.png` shows the KILL marker mid slow-motion. Not verified: bots shouldering on "Present!" in a real volley (the probe freezes bot brains), aim accuracy from the higher muzzle, hands-on play.

## 2026-11-10 — Bayonets shown on the muskets; bots break a guard
- **Visible bayonet:** soldiers who have a bayonet now carry the weapon pack's own `SM_HS_WP01_Bayonet_Y_0` on the muzzle (`AMusketWeapon::SetBayonetFixed`, created on first use, attached to the skeletal musket; the mesh is modelled at the origin along the barrel axis, so it is laid on `b_gun_Fire`). Soldiers without one show a bare muzzle, so who thrusts and who clubs can be told by looking. Log: `PA250 bayonet: laid along the barrel on …`.
- **Bots answer a guard:** a bot whose target has his guard up and is within butt-stroke reach goes in with the butt 60 % of the time instead of thrusting into the block (`DoThrust`).
- **Grip checked from more angles:** `25_hold_right_hand.png` (from behind his right shoulder: right hand under the wrist of the stock, left over the lock) and `26_hold_on_guard.png` (port arms, both hands on the musket).
- **Verified (this Mac):** editor build Succeeded; `-run=PA250Battle` checks=360 fails=0; `-run=PA250Visuals` failures=0; rendered `-PA250MeleeProbe` 27 stills, bayonets visible on both men in `04_thrust_flinch.png`. Not verified: the bot guard-break (random, no test and not staged in the probe); hands-on play.

## 2026-11-10 — Hands grip the musket; clubbed rush checked on screen
- **Grip (all soldiers on the period mesh, all the time):** the unarmed clips left both hands open, the left lying on top of the barrel and the musket hanging off the right wrist joint. Now, inside pose evaluation: the right hand is rolled so its palm lies against the right side of the stock (`FPA250SoldierAnimProxy::GripRightHand`), the left takes the stock from the side/underneath instead of from above, and the fingers of both hands close (`CurlFingers`: each joint bends about the axis across the finger toward the palm, worked out from geometry, so it does not depend on the pack's finger bone axes). The musket is seated 90 % of the way from wrist to knuckles instead of on the wrist bone. `FPA250SupportHandTarget` gained `GunForwardCS` and `RightHandBackAxis`.
- **Rush (G without a bayonet), fixes from the stills:** the raise now hands over to the blow on contact (it dropped to nothing first, so the musket snapped level for a frame); on contact the attacker is stood off at striking distance (125 cm clubbed, 150 cm bayonet) if a slow frame carried him onto his man (he was ending nose to nose, 71 cm).
- **Verified (this Mac):** editor build Succeeded; `-run=PA250Battle` checks=356 fails=0; `-run=PA250Visuals` failures=0. Rendered `-PA250MeleeProbe` (26 stills): `21_rush_run.png` musket carried raised; `22_rush_blow.png` raised at contact, gap 125 cm, foe −45, flinch=1, locked=1; `23_hold_from_his_right.png` / `24_hold_from_his_left.png` both hands closed on the stock. The right hand is hidden behind the left forearm in both close-ups, so its grip is not confirmed by eye; `25_hold_on_guard.png` was framed too low to show the hands. Not verified: hands-on play; the grip during reload and aiming poses.

## 2026-11-10 — Clubbed musket for soldiers without bayonets
- **Who has a bayonet** (`UPA250MeleeComponent::IssuedBayonet`, GAME RULE, not sourced per unit): every British soldier; no Patriot `militia`; one Continental `line_infantry` in four, fixed by his clothing seed. `HasBayonet()` is read when he attacks, so no spawn code changed. The musket mesh itself is unchanged (no bayonet is shown or hidden).
- **E without a bayonet** is a clubbed-musket swing: the musket slides back through the hands to the barrel (70 cm), goes up over the right shoulder, and the butt comes down on the man in front. Wind-up 0.45 s (thrust 0.35), reach 1.7 m (2.2), damage 50 (75), stagger 0.6 s (0.35), steps in to 125 cm. Same assist, guard/parry, riposte and no-one-blow rules as the thrust. **G** is a rush with the musket carried raised, ending in the same blow: 45 damage (bayonet charge 70), 1.3x the shove. Q butt-stroke unchanged.
- **Pose:** new `Raise` / `Chop` weights in `FPA250MeleePose`; musket pitch −70° raised → −190° at the blow (turned end for end); left hand is placed on the barrel from the same weights (the published left-hand target lags the musket by a frame, which at ~600°/s left the hand in the air). `AMusketWeapon::PoseSupportHand` now measures the left-hand spot from the grip socket, not the musket root, because the root is no longer at the hand while clubbing.
- **HUD:** `No bayonet - E clubs with the musket, G rushes`; the guard warning now reads `ATTACK COMING`.
- **Balance (unmeasured):** most Patriots now fight hand to hand with a slower, shorter, weaker blow than the British bayonet, so they should lose more close fights than before. No battle statistics taken.
- **Verified (this Mac):** editor build Succeeded; `-run=PA250Battle` checks=356 fails=0 (9 new `club:` checks); `-run=PA250Visuals` failures=0, measured hand_r +57 cm up when raised, +62 cm forward at the blow. Rendered `-PA250MeleeProbe` (21 stills, `19_club_raised.png`, `20_club_hit.png`): swing stepped in 110 cm, foe −50, flinch=1, locked=1. An earlier run of these checks used a binary built before the club code and proved nothing; the figures here are from the rebuilt one. Not verified: hands-on play, the rush (G) on screen, or the mannequin fallback.

## 2026-11-10 — Guard, parry, riposte; no one-blow bayonet kills from the front
- **Why the player always died to a charge:** 100 damage, x2 against a reloading man (melee rule), x1.5 `ReloadVulnerability` in the health component = 300 on 100 health. A thrust was 225. Anyone who had just fired was dead to the first touch.
- **No one-blow kill from the front** (everyone, not only the player): a thrust or charge that is not from behind (>120°) and not a riposte leaves a man who had at least half his health with 15 (`FrontalBlowLeavesHealth`, `FrontalMercyHealthFraction`; `URFHealthComponent::MinHealthAfterNextDamage`, a one-shot floor applied after every multiplier). Charge base damage 100 → 70. Backstabs still kill outright.
- **Guard (hold X):** musket across the body (new `Guard` pose weight, musket pitch/yaw offsets). Frontal blows within 70° are blocked: 15 % of a thrust gets through, 30 % of a charge (he is still shoved back). Raised no more than 0.3 s before the blow lands it is a **parry**: no damage, the attacker is thrown off for 0.9 s, and the defender's next thrust within 1.3 s is a **riposte** (half wind-up, x2, not subject to the 15-health floor). A butt-stroke ignores a guard (guard-breaker). Guarding squares the player to the camera, slows him to 45 %, drops for any swing, and cannot be re-raised for 0.4 s (no flicker-parry).
- **Telegraph:** a bot's thrust at the player winds up 0.6 s (0.35 s otherwise) and the HUD shows `!! BAYONET COMING - hold X to guard !!` for thrusts and charges launched at him; then `Blocked` / `Parried! Strike back - E` / `Wounded! Hold X to guard`.
- **Bots** raise a guard 35 % of the time when a thrust or charge is launched at their front (`BotGuardChance`); they block, never parry. `Blocked - butt-stroke (Q) breaks a guard` tells the player what to do.
- **Lunge mechanics:** thrust presses are buffered 0.3 s (`RequestThrust`); the charge sweeps from last frame's position so a slow frame cannot carry the point past a man.
- **Balance (unmeasured):** frontal melee now takes two blows and some are blocked, so bayonet fights last longer for both sides. No before/after battle statistics were taken.
- **Verified (this Mac):** editor build Succeeded; `-run=PA250Battle` checks=324 fails=0 (new `guard:` and floor checks); `-run=PA250Visuals` failures=0. Rendered `-PA250MeleeProbe` (same command as below; 19 stills): frontal charge on a reloading 100-health man → health 15, alive (was a kill); thrust into a held guard → −11, `blow=3` (blocked); thrust into a guard raised 0.22 s after the thrust began → no damage, `blow=4` (parried), `foe can riposte=1`, riposte dealt 150; charge −70. Not verified: playing it by hand (X key feel, parry timing against real bots), or the HUD prompts on screen.
- Another session was building and editing shared files throughout (it also removed a duplicate declaration in `PA250MeleeComponent.cpp`); several test runs had to wait for its builds.

## 2026-11-10 — Bayonet lunge, charge and hit reactions (procedural body pose)
- **Before:** every melee move was the musket sliding along the facing; the body never moved and a struck man showed nothing. The packs still ship no thrust, charge or hit clips, so the new motion is procedural, not mocap.
- **Pose layer** (`FPA250SoldierAnimProxy::ApplyMeleePose`, inside pose evaluation; game code only publishes `FPA250MeleePose` weights): hips shift/drop, spine turn + lean, head counter-rotation, two-bone leg IK (front foot steps out 42 cm and lifts mid-step, rear foot trails and stays down, reach-clamped along the ground), right arm IK drives the musket (it hangs off `hand_r`), then the existing left-hand solve onto the moved musket. One component-space round trip per frame.
- **Thrust (E)** is now a lunge: turns onto the nearest enemy within 3.8 m / 50° and tracks him, coils for the first 55 % of the 0.35 s wind-up, then steps in up to 110 cm (stops 185 cm short of him) with the point at full extension as the hit resolves; holds 0.16 s in a live man, 0.25 s in a dead one, recovers in 0.3 s. Feet are planted (idle clip + IK) from the step on. New tuning in `FPA250MeleeTuningValues`: `ThrustStepCm`, `ThrustAssistRangeCm`, `ThrustAssistConeDegrees`, `ThrustStandoffCm`, `ThrustHitStaggerSeconds`, `FlinchSeconds`, `HitStopSeconds`.
- **Charge (G):** body low and forward, musket tucked at the hip instead of swinging with the run clip, +7° FOV for the player; on contact it drives home (strike pose) and recovers; a miss is a stumbling catch-step over the existing 0.5 s penalty.
- **Butt-stroke (Q):** right hand punches up/forward and the musket whips to 115° pitch (`AMusketWeapon::MeleePitchOffset`, visual only).
- **Hit reactions:** a man who survives a blow rocks along the thrust over planted feet (`ReceiveHit` → flinch 0.6 s), is stopped 0.35 s by a thrust (0.6 s butt-stroke as before, charge still throws him back), and **loses his own thrust wind-up** (first point home wins). In a fight the player is part of, both men freeze 0.07 s of real time (hit-stop, `CustomTimeDilation`), with a small camera kick and FOV punch. Kills still ragdoll.
- **Frame-rate fixes found by the rendered probe:** the step is a move-to root-motion source (a constant force ran whole frames and carried a 110 cm step 228 cm at ~15 fps); the hit waits for the step to finish (on a slow frame both came due together and the sweep ran from the pre-step position and missed); the melee component now ticks after movement and before the body's animation.
- **Balance note (unmeasured):** bots use the same `DoThrust`, so their thrusts also track and step in. Expect bayonet fights to resolve faster; no before/after kill counts were taken.
- **Verified (this Mac):** editor build Succeeded. `-run=PA250Battle` checks=293 fails=0 (new: lunge curve timing, thrust assist, wind-up broken by a blow, 0.35 s stagger). `-run=PA250Visuals` failures=0, measured on the real skeleton at full lunge: hand_r +69.6 cm forward, foot_l +42 cm on the ground, foot_r −13 cm (leg reach; asked 30), pelvis +20 / −15 cm; flinch from the front: head −35.6 cm, feet 0. Rendered: `UnrealEditor <uproject> /Game/PA250/Maps/L_Lexington -game -windowed -ResX=1280 -ResY=720 -nosound -PA250MeleeProbe -PA250MeleeProbeX=-8500 -PA250MeleeProbeY=3000 -PA250MeleeProbeSide=-1` → 14 stills in `Saved/PA250/melee_probe/` and `PA250 melee probe …` log lines (thrust: advanced 110 cm, foe −75, flinch=1, locked=1; kill: foe DEAD; charge: foe −100, thrown back (before the entry above changed both); butt-stroke −35; player thrust: dilation 0.05 at the hit in two of the runs, back to 1.00 one sample later; in the last run that sample fell a frame before the hit). No bone asserts in the rendered run. Not verified: feel at a steady 60 fps (the probe's own screenshots hitch the frame), and hands-on play.
- **Pre-existing, seen while testing:** `PA250Visuals` "Patriots wear homespun" fails on some runs (2 of 5) — the unseeded soldier's coat seed is `GetTypeHash(GetFName())`, which differs per process. Launching `L_Lexington` directly, a British bot bayoneted the idle player 4.6 s in (both sides start close together).

## 2026-11-09 — Campaign choices review (seams)
- Pre-battle broadside now calls `build_battle_setup` while the battle is still pending. `GetBattleSetup()` stays empty until To arms.
- Map HUD invalidates when `HudEpoch` changes (`NativeTick`). Pause, speed, and commander orders bump the epoch. Reading the epoch inside a text lambda does not subscribe Slate.
- Requisition follow-up stores the support snapshot in `decision_id`. `powder` stays 0, and `deliver_committed` ignores `requisition_aftermath`.
- Workshop cartridge bonus applies only to the province owner’s militia tickets. `province_cartridges` is the physical magazine, so the fight cannot spend the bonus.
- `DA_MusketTuning` bleed was the retired 5s (file offset `0x5bd`); patched to 12. A loaded 5 is still replaced with 12 and logged. Exhaustion gate test: `TicketsMayExhaust`.
- Ledger now also records transfer departure, shipment delivery, cartridge completion, L3/L5/L7 spends, escalate Support, vassalage gold, and defect gold.
- Ragdoll disables the capsule before physics and lifts a body that is already below the floor. Fort cover logs a missing cube and sits walls on the ground trace.
- **Verified:** `pa250sim_tests` **9197 checks, 0 failed**. Clock golden still `19616930d31958dc` (idle 1440h does not take these paths). UE editor was open (`UnrealEditor` pid 18499) plus a warhorse import commandlet, so the editor module was not rebuilt and PIE / `-run=PA250Battle` / a rendered death were **not** run.

## 2026-11-09 — Campaign choices matter (resources, buildings→battle, events, battle length/feel)
- **A — Resource ledger:** `CampaignState.resource_changes` (cap 64, schema 2 soft-default) with `detail::record_resource_change` on develop / purchase / aftermath / capacity / Knox / deliver / monthly supply / battle ammo / defender reward. HUD Ledger shows treasury + recent reasons; `HudEpoch` + `InvalidateLayoutAndVolatility` keep top bar / support / stores in sync.
- **B — Tooltips:** `development_preview` / `aftermath_choice_preview`; Farm/Workshop/Depot/Fort and aftermath buttons expose exact cost/effect/delay.
- **C — Buildings→battle:** Fort seeds `initial_post_holders` + cover walls; workshop adds cartridge budget/militia tickets; farm/depot raise owner readiness and `reinforcement_scale` (bot respawn). `modifier_notes` feed the pre-battle screen.
- **D — Events:** Requisition records Support −12 immediately and schedules 24h `requisition_aftermath` (+4 Support or attitude −5). Pending list in Ledger; staff status uses the last reason line.
- **E/F — Pre/post battle:** Battle broadside lists tickets + modifiers; after-battle report lists ownership reason, gold, Support (incl. defender +5), and change_reasons.
- **G — Length:** Removed hard-coded 5s bleed; DA/default `BleedIntervalSeconds=12`, `MinSecondsBeforeExhaustion=120`.
- **H — Feel:** Ragdoll impulse + WorldStatic collision; walk/run play-rate from speed; damage-direction wedge; spawn screen “Killed by …”.
- **Verified:** `pa250sim_tests` **9166 checks, 0 failed**; clock golden regenerated (`19616930d31958dc`). UE editor build / PIE **not re-run** this pass (editor may be open — restart after building).

## 2026-11-09 — Rifle aim pitch unstuck
- The musket barrel was clamped to ±45°, so looking up past that left the rifle behind the view. The limit is now ±85°, in line with the camera.
- Shoulder-camera rotation lag is off. Its substepped blend was stalling pitch (yaw still moved), which is the same "can't aim higher" report that previously cleared only after an editor restart. `FrameHumanCamera` also resets the arm's relative pitch and the view pitch range to ±89 on spawn.
- Verified: editor build Succeeded; `-run=PA250Visuals` failures=0. Upward-aim measure: barrel.forward=1.000 at a 70° upward aim (both factions). PIE look is not re-checked this pass.

## 2026-11-10 — Army counters redesigned for readability
- **Problem (screenshot):** an army at wide zoom was a small pale-blue disc with a white bar — it read as a minus button, sat visually below the big town diamonds, and its plate ("Militia / Yours") said nothing about strength.
- **New compact counter** ([PA250ArmyToken.cpp](/Users/leone/PycharmProjects/Samson/unreal/PlayAmerica250/Source/PlayAmerica250/Campaign/PA250ArmyToken.cpp:276)): a NATO-style infantry counter — parchment-framed rectangle (gold frame when selected), faction-coloured body, crossed parchment bars. Shape coding is now consistent: **towns are diamonds, armies are crossed rectangles**, at 38 px on screen (was a 30 px disc). `GetDrawnRect`/`HitTest` follow the rectangle, so clicking the counter still selects the army.
- **Plate:** your armies now read `Militia / Yours · 22 fit` on the map itself; British plates still show no strength (unreported by design). Slot sizing uses the same plate string, so anchors don't move.
- **Verified:** editor build **Succeeded**; `PA250MapFraming` passed (26 army slots, 13 town washes, Success 0 errors); `PA250Battle` checks=275 fails=0; sim 9164/0. Counter not yet seen in PIE — restart the editor (it was open during a previous build) and look at Concord at wide zoom.

## 2026-11-10 — Army selection/movement clarity + branding
- **Branding:** the only player-visible "PA250" was the top-bar crest; it now reads **⚜ Play America 250**. Code/asset names keep the PA250 prefix (internal only).
- **Your forces panel** (top-left, under the bar): one clickable button per player army — `⚔ Massachusetts Militia — Concord · 22 fit · holding` (or `→ Lexington` when marching, `▶` when selected). Clicking selects the army, focuses it, and the gold banner under the top bar states what to do next: "selected — at Concord · Click a town to march there · Esc / right-click cancels", with the route (`Concord → Lincoln → Lexington`) once marching. The panel hides while a staff letter or battle owns the screen (the correspondence panel in the screenshot sat where it would collide).
- **Province window:** "⚔ Select the army here — then click a town to march" button when a friendly army is present but not selected; an empty town now lists **Your forces elsewhere** (army, town, fit, orders) instead of a dead-end "No forces known". March orders report destination, arrival and full route in the status line.
- **Panel hiding fixed:** the province window now collapses while a battle is pending, so it can never cover the "Battle of … / To arms!" broadside.
- **Verified:** editor build **Succeeded** (note: built `libUnrealEditor-PlayAmerica250-0001.dylib` because the editor is open — restart it to play the new HUD); `PA250Battle` 275/0; sim 9164/0. New panels not yet seen in PIE.

## 2026-11-10 — HUD redesign after screenshot review
- **Top bar:** compact groups only — crest, date/pause/speed, 🪙 gold, faction-total powder/cartridges/muskets/provisions/in-transit, support/attitude — plus a 📖 Ledger button. Nothing truncates at 1600 px; per-province detail moved out of the bar.
- **Ledger:** off by default (was an always-on panel covering the map's left third, wrapping every row). Opens top-left below the bar in 12 pt, one short line per province (`name · dev · cart/musk/prov`), map-key line at the foot; Esc closes it.
- **Province window:** narrowed 380→336 px; develop and action buttons are now two-per-row fill-width grids, so nothing clips past the right edge (Depot/Fort were cut off in the screenshot).
- **Toolbar:** the 9-row vertical button stack (which collided with the province window) is now one slim horizontal bar bottom-right: ⌂ Overview · ◎ Focus · ＋ · － · Parley · Save · Load · About · Dev. The permanent colour-legend strip is gone (key lives in the ledger/About). `IsPointOverHud` updated for all three.
- **Verified:** editor build **Succeeded**; `PA250Battle` checks=275 fails=0; sim 9164/0. A `-game` screenshot run was blocked by the sandbox (GUI launch denied), so the new layout is **not yet visually verified** — check in PIE.

## 2026-11-10 — Campaign loop review + EU4-style development/HUD
- **Double-check fixes:** Concord broadside now says three posts (was "one crossing"); top bar no longer packs a multi-line stores/transit/report blob into the 72 px strip; purchase/transfer now reject provinces the player does not hold (`not_your_province`) instead of buying into Boston.
- **Province development (sim-authoritative):** each province has Farm/Workshop/Depot/Fort levels 0–3 (seeded: Concord & Cambridge workshop+depot 1, Boston workshop+depot+fort 1, Charlestown fort 1). `develop_province` costs gold from `logistics_tuning.json` (`development` block, +20 g/level), rejects enemy/maxed/battle-pending. Effects: farms add monthly supply income + 3 provisions/level to the magazine; workshops halve cartridge-job time per level divisor; depots cut shipment transit into the province; forts give the holder +3 morale/+2 readiness per level in `build_battle_setup`. Serialized in province JSON (schema stays 2, missing fields default 0); clock golden regenerated (`49294c7284ca863a`) for the expanded state.
- **HUD (EU4-style):** dark top bar with gold ⚜ PA250 crest, date/treasury/stores/support groups; dark province window with gold title (name, owner, terrain, development total), stores, fit/sick/wounded, development pips + next-cost line and 🌾/⚒/⛟/🏰 develop buttons; structured after-battle report (ground/losses/ammo/morale separate) inside the province window; bottom-left ledger of every held province (dev, cartridges, muskets, provisions) plus in-transit shipments and underway cartridge jobs. `IsPointOverHud` covers the taller province window and ledger.
- **Verified (this Mac):** `pa250sim_tests` 9164 checks, 0 failed (new `province_development` case); `PlayAmerica250Editor` Mac Development **Succeeded**; `-run=PA250Battle` checks=275 fails=0. PIE visual check of the new panels **not run** this pass.

## 2026-11-10/11 — Campaign map: camera framing, marching columns, traced roads, ink town marks, hover tooltips
- **Camera framing fixed:** the project default `AspectRatio_MaintainYFOV` made the engine treat the map camera's ortho width as the view height (engine `CameraStackTypes.cpp`: X multiplier = SizeY/SizeX), so the map showed aspect× more than `FrameCamera` framed (1.78× at 16:9): black bands past the plate, clamps not matching the view. The map camera alone now overrides to `AspectRatio_MaintainXFOV` (`bOverrideAspectRatioAxisConstraint`); battle cameras untouched. Rendered overview fills the viewport. Army figure height back to 12 UU for the true scale (compact at overview ≈18 px, illustration from zoom ≈3.1).
- **Armies move along their route:** tokens persist per army and are positioned from sim state only — start-town slot → (traced road) → next-town slot at `hours_into_edge / march_hours`; resting overnight mid-road stays put; drawn position eases to the sim position (τ 0.25 s, snap > 250 UU), never ahead of the sim. Town hotspots take click precedence over a passing column (no mis-targeted march orders). Probe: Regulars glide hour by hour Boston→Cambridge→Menotomy, no jumps (`ART_PROBE: column` samples).
- **Traced roads (4 of 13 land edges):** `decosta_locations.json` `routes`: roxbury-boston (Boston Neck), cambridge-brookline (Great Bridge), cambridge-watertown, brookline-roxbury, read off full-res UV-gridded crops and overlay-checked. Not traced (road not legible on the plate, or no printed road): Concord–Lincoln–Lexington (under the printed battle), Menotomy links, Charlestown/Chelsea/Medford roads, Roxbury–Dorchester; water crossings stay straight. Routes also draw the selected army's route lines. `PA250MapFraming`: each route joins its towns' hotspots and its halfway point is on the road (33–118 image px off the straight chord).
- **Calmer town marks:** diamonds, fill and selection halo use the unlit `M_MapSprite` over a white texture (the lit `BasicShapeMaterial` was washed pastel by the key light); ink colours Continental sRGB 30,52,110 / British 128,30,24 / neutral 70,54,36; hollow diamond = Continental, solid = British (shape cue, not colour alone). Constant screen size (mark ≈14 px, caption ≈15 px), never larger than the sizes reserved for army slots. New `M_MapText` (unlit copy of the engine's lit text material; created by `Scripts/import_army_art.py` only if missing) for captions and army plates; captions get a soft parchment backing (ground-oval texture) so they read over engraved lettering and the wash.
- **Hover tooltips:** new `UPA250MapTooltip` (own widget, hit-test invisible, placed beside the hovered thing's projected footprint — army figures + plate via `GetFootprint`, town mark + caption via `HoverWorldBox` — right, else left/below/above, kept in the viewport; 0.25 s rest delay, hidden over HUD / while dragging; rendered check `-PA250ArtProbe=tooltip`: neither tooltip covers its army plate or town caption); text from `APA250ProvinceMapActor::DescribeHover`: your army = men · morale · readiness · holding/marching to X (next Y)/engaged; enemy = side + "Strength unreported" + "On the march" (no destination); town = holder + forces present + pending battle.
- **Verified (this Mac):** builds Succeeded (before the other session's after-battle `Printf` error in `PA250MapWidget.cpp:386`, not this work); `PA250MapFraming` all of this pass's checks pass (its one failure is the other session's not-yet-imported `T_Army_Militia`), `PA250MapPicking`, `PA250MapSingleLayer` passed; rendered `-PA250ArtProbe=after` 1600×900 screenshots (overview, Boston close, march hours 1–3) and hover texts logged. Probe drag `grab_error` is unreliable (grab read before the cursor reaches the map); drag `reversals=0`. Slice autoplay not re-run this pass.

## 2026-11-09 — Town ownership wash, drag-pan fix, arrow-key pan
- **Ownership wash (no tiles):** the De Costa plate shows no town boundaries, and the only region geometry in the repo (`tools/mapgen/source/boston_1775.svg`) is PROVISIONAL/authored for the stylized map; the sim tracks town control, not countryside. So no territory tiles. Instead each held town gets a soft hand-colour wash (period map convention) in owner colour (Continental sRGB 40,70,150 / British 170,40,30, opacity 0.30; unowned = none), under the diamond and caption, retinted by `RefreshTownOwnership`. Radius = 1.8× click radius, capped at 0.75× the nearest other hotspot; Boston overridden to 300 px (`wash_radius_px`, built-up blocks measured ≈350×460 px). Texture `T_Map_TownWash` generated by `prepare_army_sprites.py`. `PA250MapFraming` checks the asset and all 13 radii.
- **Drag-to-pan fixed (user-confirmed in PIE).** Root cause: the project input defaults for the battle (`DefaultViewportMouseCaptureMode=CapturePermanently_IncludingInitialMouseDown`, `LockOnCapture`) plus `FInputModeGameAndUI`'s default hide-cursor-during-capture captured, locked and hid the cursor on mouse-down, so the map never saw the drag. The campaign PC now sets capture-during-mouse-down, no lock, cursor visible (`ApplyCampaignInputMode`, also used by the game mode) and restores the saved viewport modes in `EndPlay` (log: `PA250 input: campaign end, restored capture=3 lock=0`). Drag is now screen-space (centre at press minus cursor travel at the measured scale), so camera movement cannot feed back (the old per-frame UV delta also caused a shake); middle-button pan uses the same path. `FrameCamera` writes the clamped centre back, so there is no hidden overshoot at the map edge. The first `-game` probe passed only because it bypassed capture; the probe now sends real Slate mouse/key events with viewport focus.
- **Arrow keys pan** at 900 screen px/s at any zoom, read from player input (viewport focused, e.g. PIE) and from the HUD widget (widget focused). Probe: viewport focus 454 px, widget focus 465 px per 0.5 s hold (expect ≈450), no drift after release; drag 0 reversals.
- **Import script:** rebuilding an existing `M_MapSprite` asserts (`!IsRooted`, token CDO holds it) and never saves; the script now keeps an existing material (delete it with the editor closed to rebuild).
- **Verified (this Mac):** editor build Succeeded; `PA250MapFraming` (7 art-rule cases, 13 washes, 26 slots), `PA250MapPicking`, `PA250MapSingleLayer` passed; rendered `-PA250ArtProbe=after` 1600×900 (drag + arrow steps added). PIE screenshot from the user shows the washes. Slice autoplay not re-run this pass.

## 2026-11-08 — Supplied art pack: army illustrations (background replacement rejected)
- **Pack inspected** (`Play_America_250_Complete_Art_Pack.zip`): `Cleaned_Boston_Map.png` 1415×1111 RGB; `Marching_Infantry` 1999×786, `Firing_Infantry` 2172×724, `Provincial_Militia` 1920×819, `Colonial_Town` 1752×897, `Colonial_Farmhouse` 1536×1024 (all RGBA). Sprite body alpha is 249–252 (never 255); 2–11% of pixels are alpha 1–31 dust; transparent and fringe texels carry saturated red/yellow (or, farmhouse, dark-brown glow) RGB that bilinear/mips would turn into halos. No baked lettering or checkerboard in the sprites.
- **Cleaned background NOT integrated.** It is a redrawn reinterpretation, not a cleanup of the De Costa plate: neat-line frame moved and cartouche/references replaced by a larger empty box; Concord ≈0.03 UV west with the river through the town; North Bridge not identifiable; four Charles bridges vs two on the plate; Dorchester Neck, Charlestown, Noddles I. and Castle I. reshaped; illegible pseudo-lettering remains. 1415 px wide vs 6768 (≈3–8 screen px per texel at opening/max zoom). Boston, Cambridge, Watertown, Roxbury, Dorchester within ≈0.01 UV. The De Costa engraving stays the sole background; the pack file was not copied into the repo.
- **Town art not used:** the engraving already draws every town; `Colonial_Farmhouse` also has a baked dark glow. No building icons added.
- **Hotspot fix:** Lexington `marker_uv` (0.2477,0.1749) sat in blank ground NW of the engraved town (despite `verified: true`); moved to (0.300,0.230), radius 120 px, in `decosta_locations.json` (+ framing commandlet, both probes). Cambridge hotspot is in the field west of the label, buildings outside its radius — flagged, unchanged. Other 11 checked on full-res crops.
- **Army art:** `tools/art/prepare_army_sprites.py` (Pillow only, deterministic) drops alpha dust, snaps body alpha, crops padding (marching straggler cut), resizes premultiplied, bleeds edge colour, pads to 1024×512; also generates a soft parchment ground oval. `Scripts/import_army_art.py` → `/Game/PA250/Textures/Army/T_Army_*` (BC7, sRGB, mips, World group, never-stream; runtime log: `PF_BC7 mips=11`, oval `mips=9`, ≈2.2 MB total) and `/Game/PA250/Materials/M_MapSprite` (unlit translucent, `Sprite`/`UVRect`/`Tint`/`Opacity`). Originals of the three used sheets kept in `Content/PA250/Source/ArmyArt/original/`.
- **Tokens** (`PA250ArmyToken`, `PA250ProvinceMapActor`): one token per sim army, art chosen from state only — British `BritishMarching` only while `path` is non-empty (mirrored to face the next province), `BritishFiring` only while party to `pending_battle` (faces the foe); `ProvincialRegulars` (`Provincial_Militia.png` — despite the file name the supplier says it shows provincial regulars, 2026-11-09) only for a Continental force that is at least half line infantry. Massachusetts Militia (4 line + 18 militia) and a British force at rest keep the compact counter at every zoom (no militia or neutral British art supplied). Rule covered by 7 `PA250MapFraming` cases. Ground anchor = feet bottom-centre for every variant and the counter; slot sized for the side's largest art, player armies placed first, so switching art/LOD or an enemy arriving never moves an army. Slot = first of 24 candidates (E/W/S/SE/SW/N/NE/NW × 3 rings) covering no town hotspot, caption or placed army. Compact counter (30 px constant) at wide zoom; illustration when a figure is ≥22 px (hide <19), measured from the real projection. Faction rule, gold selection rule/ring, soft ground oval and the name plate (short name + side word; strength stays in the HUD) are separate elements. Hit-test = drawn rect / disc only. About panel states the art caveats.
- **Verified (this Mac):** Editor build Succeeded (no warnings). `PA250MapFraming` passed (art assets/aspect, LOD thresholds, 26 slot checks: 13 towns × militia + regulars together, 0 overlap), `PA250MapPicking` passed, `PA250MapSingleLayer` passed. `scripts/run_slice_autoplay.sh` PASSED (battle → result → diplomacy → save/load hash identical); pre-existing `Saved/PA250/*.json` backed up and restored byte-identical. Rendered probe `-PA250ArtProbe=<label>` (editor binary, `-game -windowed`, Metal): `Saved/Screenshots/ArtProbe/{before,after}/01–09*.png` at 1600×900 (same views/state), `after_1280x900/` for resize. Probe log: militia selected by clicking its art; Lincoln order queued then cleared; 0/13 town hotspots captured by army hit targets; existing save loads (hash d387c3c4951da836), file unchanged. Sim ctest not re-run (no sim changes).
- **Known issues:** (1) pre-existing camera framing: visible extent ≈1.77× wider than `FrameCamera` computes (zoom 4.5 shows ≈1060 UU at 1600×900, not 596), so opening/clamped views show black off-plane margins; pan/zoom left unchanged, LOD measures px/UU instead. (2) First `-game` launch after a material change compiles `M_MapSprite` on demand; sprites are invisible until "Preparing Shaders" finishes. (3) The engraving's printed soldiers still surround Lexington/Concord; the ground oval separates game armies but a faithful cleaned plate would be better. (4) No militia or neutral British art, so the slice's opening army never shows an illustration. (5) Re-importing must not delete a texture `M_MapSprite` still defaults to: the import commandlet crashed (`!IsRooted`) and the material fell back to the grey default; fixed by deleting and regenerating `M_MapSprite` with `Scripts/import_army_art.py` (run on a current build).

## 2026-11-08 — Campaign map presentation redesign
- **Markers:** TextRender `◇` glyphs (rendered as solid blocks, incl. over Boston) replaced with small diamond mesh markers (muted red/blue/charcoal by owner, shape + caption text, never colour alone) plus a selection halo. Captions: majors only at overview, all at zoom ≥3.2 or when selected. Army counters show only the short name at rest; strength/units on selection. Counters offset south-east of the town diamond.
- **Framing:** opening view is the Boston–Cambridge–Lexington–Concord overview (`CampaignOverviewCenterUV`/`CampaignOverviewZoom`, ≈2.4× at 1080p), not the full sheet. `Campaign overview` + `Focus selected` actions; cursor-centred wheel zoom (`ZoomAtUV`); max zoom 8→6.
- **HUD:** compact grouped top bar (date/pause/speed, Treasury, Stores, Support — each with a tooltip); grouped selection panel (Held by / Selected force / Forces here / Reports; enemy strength stays unreported); bottom bar keeps only status + time controls; secondary utilities moved to a small right-hand stack. Permanent disclaimer replaced by an `About this map` panel (source + July-1775 caveat preserved). Raw diagnostics behind a `Dev` toggle. HUD clicks/drags/wheel no longer reach the map (`IsPointOverHud` guard); Escape closes About, then clears selection, never touches pause.
- **Verified (this Mac):** Editor Mac Development **Succeeded** (one UBA-cache failure required a `-NoUBA` build). Commandlets `PA250.Map.Framing` (updated: de Costa aspect + Boston/Lexington/Concord overview visibility), `PA250.Map.Picking`, `PA250.Map.SingleLayer`, `PA250.Campaign.Clock` passed; sim ctest passed. Screenshots: `Saved/Screenshots/MacEditor/HighresScreenshot00011.png` (1920×1080, no UI) and `ScreenShot00001.png` (1280×720 with UI, Lexington selected via probe).
- **Limits:** hover tooltips on 3D markers not implemented (resource groups have Slate tooltips); unverified hotspot UVs unchanged; Fight→return, save/load UI clicks and battle entry not re-exercised this pass (sim save/load covered by ctest/autoplay paths untouched). A concurrent session was editing/building during this pass; no processes of theirs were touched.

## 2026-11-08 — Opening-war correspondence (L1–L7, D8)
- **Catalog:** [`unreal/PA250Sim/data/opening_war_correspondence.json`](unreal/PA250Sim/data/opening_war_correspondence.json) — eight institutional URLs; historical metadata separate from playable adaptation; `text_kind: paraphrase_from_report` (no claimed verbatim excerpts; no embedded manuscript scans).
- **Sim:** `PersonnelCommitment` (paper units held out of `gate_tickets_by_stores` without touching arms/readiness/morale); disclosed `DonorReserve` + `Shipment` (commit ≠ receive); `CorrespondenceItem` gains `source_id` / `event_id` / `decision_id` / `decision_applied`; schema **2** defaults on load. Eligibility and delivery use existing `advance_hour` / map speed only (no skip-to-date UI).
- **Playable:** L3 (May 4 shelter — no guaranteed `local_support`); L5 (fictional subsequent forage/boats after May 24 + L3 resolved); L7 (Jun 25 logistics relay after `chapter_continental` — transfer depletes `ny_powder` donor, credits continental only on shipment delivery; procurement and conserve distinct).
- **Reference-only:** L1, L2 (retrospective exhibit for Patriots — never live intel), L4, L6, D8 — acknowledge only; source view / LaunchURL never mutates state.
- **UI:** Staff panel dual panes in letter body; L3/L5/L7 choice rows; **View original source**; autoplay answers source letters without stalling.
- **Verified (this Mac):** `pa250sim_tests` **9111 checks, 0 failed** (incl. `opening_war_l3_loop`, `opening_war_l5_l7`); golden `clock_golden_1775.json` regenerated (`9c697d6690de4340`). `PlayAmerica250Editor` Mac Development **Succeeded** (forced recompile of PA250SimCore / MapWidget / Subsystem / CampaignGameMode). `-run=PA250Battle` checks=272 fails=0. PIE staff-panel / slice autoplay **not re-run** this pass.
- **Untouched:** reload, volley, de Costa map actor.
- **Limits:** donor reserve is a disclosed gameplay abstraction (not per-province warehouses); manuscript image rights not cleared (link-out only); Crown L2 live playable deferred; PIE dual-pane not exercised.

## 2026-11-08 — Map redesign continuation: hotspots, texture, fight-return
- **Hotspots verified:** all 9 previously-draft UVs checked on JPG crops and moved off printed troops/camps/water onto the engraved town buildings/labels in [`decosta_locations.json`](unreal/PlayAmerica250/Content/Map/Boston1775/Raw/decosta_locations.json): concord (0.143,0.306), lincoln (0.195,0.238; no engraved town — open road midway), charlestown (0.568,0.398), medford (0.448,0.178), watertown (0.358,0.475), brookline (0.445,0.540), roxbury (0.535,0.592), dorchester (0.558,0.598), chelsea (0.678,0.343). All 13 now `verified: true`.
- **Texture:** asset queried in-editor: 6761×5314, `TC_BC7`, `TMGS_SIMPLE_AVERAGE` (mips), never-stream, sRGB, no size clamp. Decision: keep full resolution (zoom legibility of engraved text); BC7+mips ≈ 48 MB GPU, not the ~1.2 GiB uncompressed estimate, which only applies to the JPEG fallback path if the asset is missing.
- **Fight-return:** `-PA250Autoplay` run repeatedly passed "autoplay battle: BattleResult applied" and "aftermath: at least one battle recorded" (march to contact → battle → return to campaign). The later diplomacy leg fails with "service reachable (state became Unavailable)": with the negotiation service up (`/health` ok), session creation still hit the client's connect timeout, so the autoplay save/load leg after diplomacy was not reached. Sim save/load round-trip remains covered by `pa250sim_tests`.
- **Screenshots:** before [`ScreenShot00000.png`](unreal/PlayAmerica250/Saved/Screenshots/MacEditor/ScreenShot00000.png) (1280×900, pre-redesign probe, Lexington selected) vs after [`ScreenShot00002.png`](unreal/PlayAmerica250/Saved/Screenshots/MacEditor/ScreenShot00002.png) (same viewport/state) and small-window [`ScreenShot00003.png`](unreal/PlayAmerica250/Saved/Screenshots/MacEditor/ScreenShot00003.png) (1280×720). A concurrent session's round-counter/legend work is also present in the current tree and preserved.
- **Verified:** Editor build Succeeded; `PA250.Map.Framing`, `PA250.Map.Picking`, `PA250.Map.SingleLayer` passed; sim ctest passed.
- **Texture-memory (completed):** working JPG padded 6761×5314 → 6768×5316 (multiple of 4, edge-pixel replication; UVs rescaled, `image_size` updated, note in JSON). Re-imported BC7. Runtime log confirms resident texture `6768x5316 format=PF_BC7 mips=13` (new `PA250 display texture resident` log in `PA250ProvinceMapActor`). BC7 vs BC1 compared on Mac: BC1 import logged `PF_DXT1 mips=13`; screenshot diff on a text-dense crop: mean max-channel diff 2.34/255, 0.38% of pixels differing by >16 — near-identical at opening framing. Decision: keep BC7 (≈48 MB GPU with mips vs ≈24 MB BC1) for fine engraved linework; BC7 restored and re-verified after the comparison.
- **Fight-return save (completed):** in-game `-PA250Autoplay` passed ApplyBattleResult + aftermath + return (above). Headless sim check (`/tmp/save_after_battle.cpp`, scratch, not committed): march militia → contact at Cambridge → `begin_battle`/`finish_battle` → flow=campaign, battles_fought=1 → `save` → `load` → identical hash `b12344c06cc01faa`. `PA250.Battle` commandlet: 272 checks, 0 fails.

## 2026-11-08 — De Costa–Hall campaign overview
- **Visual:** `L_Campaign` uses the July 1775 De Costa–Hall engraving as the sole overview (`T_DeCostaBostonConcord` / JPEG fallback). Stylized Boston1775 mapgen stays in-tree but is not the player-facing layer when de Costa loads.
- **Source:** `Content/PA250/Source/M01-de-costa-boston-concord.jpg` — LOC IIIF full JPEG of the same plate (`6761×5314`). Planned NYPL download size `4920×3966` was not on disk; rights note in `decosta_locations.json` (US public domain; not worldwide clearance).
- **Hotspots:** `Content/Map/Boston1775/Raw/decosta_locations.json` — all **13** provinces, pixel hit radii, `verified` flag (runtime always selectable). Opening towns visually checked on crops: Boston, Cambridge, Lexington, Menotomy `verified: true`; others draft.
- **Picking / UI:** plane hit → UV → nearest hotspot (not ProvinceId color). Diamond+caption markers; army tokens from `CampaignState` only. HUD disclaimer + location panel; Enter battle only when `pending_battle` matches selection (`PressFight`). Pan = MMB or LMB-drag (≥5 px); wheel zoom; Reset view. Camera manual exposure / tone curve off in de Costa mode. Max zoom raised to 8×.
- **Import:** `tools/ue/build_map_assets.py` gains `mips` / `bc7` / `never_stream`; `Scripts/import_decosta.py`. Inspected: sRGB, bilinear, `TMGS_SIMPLE_AVERAGE`, `TC_BC7`, never_stream. Non-POT source; Python cannot read mip count via `platform_data`. Runtime build logged ~1.2 GiB estimate while encoding — watch Mac memory; optional later resize.
- **Gitignore:** `!unreal/PlayAmerica250/Content/Map/Boston1775/Raw/*.json` so presentation/hotspot JSON is tracked.
- **Verified this pass:** Editor Mac Development **Succeeded**. `-run=PA250MapSingleLayer` **passed**. `-game L_Campaign` log: `display=T_DeCostaBostonConcord decosta=1`, `locations loaded (13 provinces)`. Probe `py Scripts/...` path must be absolute from `-game` cwd. BC7-vs-BC1 zoom comparison and full Fight→return not re-run in this pass. GrandStrategyMac unused.

## 2026-11-08 — April campaign loop (Stage A–C foundations)
- **Sim:** `FactionStores` (powder/arms/provisions), `StaffState` + correspondence queue (one clerk), `CampaignProgress` (aftermath, Concord follow-up, chapter gates, Knox project, detached commander). Schema stays **2**; new fields default on load.
- **Loop:** battle → `apply_battle_result` expends stores / sets aftermath → `EventPopup` letter → purchase / request_assistance / requisition / capacity_investment → timed delivery (commit ≠ receive) → ticket gating in `build_battle_setup` changes the next fight. Concord follow-up marches seeded after a logistics choice.
- **Player role:** composite Massachusetts field commander (fictionalized); Continental / Washington chapters gated to 14 Jun / 3 Jul 1775. Knox offered on/after 17 Dec 1775 as a campaign project (not automatic Boston).
- **Battle:** `scenario_preset` (`lexington_common` | `concord_bridge` | `battle_road`) and its `terrain_tags` are read by the battle on shared `L_Lexington`. Concord (`bridge_crossing`) contests only the crossing; the magazine and foothold stay locked. Battle Road (`return_march`) does not bleed for ground, and the clock goes to whoever kept more of their starting pool. Campaign morale/readiness adds capped pressure bias (does not merge with squad Steady/Shaken/Rout). Manual reload and volley lane/epoch logic untouched except that one pressure addend.
- **UI:** map header shows Gold | Powder | Arms | Prov | Local support; staff panel with choice buttons.
- **Verified:** `pa250sim_tests` 8980 checks, 0 failed (incl. `logistics_loop`, `commander_and_knox_gates`); golden `clock_golden_1775.json` regenerated. `PlayAmerica250Editor` Mac Development **Succeeded**. `-run=PA250Battle` checks=249 fails=0; `-run=PA250Combat` fails=0. Slice autoplay / PIE staff panel not re-run in this pass.
- **Review fixes (2026-11-08, later pass):** delivered/refused letters are now clearable notices (any choice clears `pending_event_id`, never re-charges/re-delivers — previously the delivery popup could never be dismissed and autoplay's staff loop could spin on it); commander dismiss clears `pending_clarification`; declining Knox sets `declined`; `resolve_commander_request` only consumes the commander's own letter (a June chapter briefing could previously be answered as a commander order) and now moves army morale/readiness, not just strings; `ensure_logistics_defaults` no longer resets real 0 `local_support`/`readiness` to 55/100; battle powder/arms spend uses the stores-gated fielded tickets, not the paper roster (a 5-man gated force previously burned powder for 23); a second capacity investment cannot be charged while one is underway, and a ready repair contact adds +2 arms to later purchases; a full clerk desk slips purchase/request deliveries by a day; the Concord follow-up orders the militia from wherever the first battle left it. Map widget: commander buttons only on the detached commander's letter, chapter/notice letters get a single Acknowledge, choice rows hidden for non-awaiting letters (`IsPendingLetterAwaitingChoice`). Autoplay answers staff popups while advancing the ceasefire day (previously stopped after 1 hour on the seeded commander letter). New `logistics_edges` ctest case (17 checks failed before, 0 after). Re-verified: ctest 100% (golden hash unchanged), editor build **Succeeded**, `PA250Battle` 249/0, `PA250Combat` fails=0, `run_slice_autoplay.sh` **PASSED**.
- **Campaign speed:** the map clock defaults to **1 campaign hour per real second** (it was 6). Speed− / Speed+ and keys 1–7 step through ¼, ½, 1, 2, 6, 24, and 120 h/s. The header shows that rate. Battle time is unchanged.
- **Concord crossing:** the North Bridge fight fields one British company (at most 6, and never more men than the provincials on the crossing). A creek and a plank bridge are built under that post; the magazine and foothold stay locked. The ground is still the Lexington field.
- **Known limits:** campaign-confidence pressure bias is a margin effect (≤0.35 added pressure, cannot shake a full-strength squad by itself); Knox delivery is a state flag, no playable artillery; PIE staff panel still not exercised. Concord and Battle Road still share `L_Lexington` geometry.
- **Review pass (2026-11-08, correctness):** a ceasefire now halts a march already underway before it enters enemy ground (friendly legs and safe passage still complete). Concord (`bridge_crossing`) contests only the crossing post; the magazine and foothold stay locked. Battle Road (`return_march`) does not bleed tickets for ground, and the clock goes to whoever kept the larger share of their starting pool. Escalating Parker's company (`hold_stores`) forfeits the town if his army loses the field without holding the post; Boston stays protected. A staff letter raised in the same hour as contact stays pending after the battle interrupt, so the panel can be answered once the fight ends. Verified: `pa250sim_tests` 9033 checks, 0 failed (incl. `ceasefire_halts_march_in_progress`, `letter_survives_contact`, `escalate_risks_the_ground`, `logistics_loop`, `logistics_edges`); golden `clock_golden_1775.json` unchanged, not regenerated. `PlayAmerica250Editor` Mac Development **Succeeded**. `-run=PA250Battle` checks=264 fails=0; `-run=PA250Combat` fails=0. `scripts/run_slice_autoplay.sh` **PASSED**. PIE staff panel still not exercised.

## 2026-11-08 — Line volleys: two-rank lane safety + cancel-safe stagger
- Kept existing two-rank Line `FormationOffsets` (N≥4). No single-rank collapse.
- Coordinated mode remains `FPA250TacticalRole::bIndividualFire == false` (editable by role, not faction).
- `HasClearFiringLane`: world blocks + living same-faction pawns sit that shooter out of the volley (rear rank through the front).
- Ready threshold uses **safe** ready (loaded + in position + clear lane); `VolleyMaxWaitSeconds` still fires with whoever is safe (never with zero).
- `VolleyFireJitterSeconds` (bot tuning, default 0.1 s) schedules pending shots; `VolleyEpoch` cancels delayed fire on lost contact / morale / charge / size&lt;2.
- `pa250.BotSpacingReport` now logs loaded / formedReady / safeReady / pending / wait / reason; near-camera HUD can show a short wait reason after 2 s.
- Player reload unchanged.
- Verified: `BUILD` Succeeded. `-run=PA250Battle` checks=224 fails=0 (two-rank front/rear, ready/max-wait, epoch cancel, friendly + world lane). `-run=PA250Combat` passes. Not verified in PIE yet — stand near a Line squad and watch Make ready → Present → FIRE!; `pa250.BotSpacingReport` for safeReady/reason.

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

## Envoy persuasion (added after M8)
- Envoy replies: offline fallback is now context-aware (`fallback_reply`, deterministic, no `hash()`); the Claude system prompt tells it to bargain and not simply agree.
- New `defect` clause (British only): the model proposes, the sim disposes. Value = title (colonel 20, baron 35, mayor_of_boston 45, governor 60) + gold/10 + 15 per granted province, must reach `defect_price` = max(10, loyalty - relation/5) (loyalty 70). Must be the only clause. Effects: gold leaves `player_gold` (starts 500), lands recorded in `fiefs`, his army joins the player, `mayor_of_boston` hands over Boston. Afterwards he accepts no more clauses.
- New state (`player_gold`, `fiefs`, envoy loyalty/defected/title) changes the state hash, so `clock_golden_1775.json` was regenerated with `--write-golden`. Save schema stays 2; new fields default on load.
- Evidence: ctest passes (new `defect` case + 6 shared vectors); `tests/diplomacy` 70 passed incl. live Claude (real offer accepted, flattery-only refused); `run_slice_autoplay.sh` PASSED. CLAUDE.md gained one line sanctioning `defect` as the only way Boston can change hands.
- Follow-up done: the campaign status bar shows `Gold N`; the diplomacy header shows your treasury and "HE HAS CHANGED SIDES". A betrayal drops British attitude to -100 and lands a 40-man `british_reprisal` column at Boston, which the daily AI then marches. Golden hash regenerated again.
- **Surrender with honors of war** (after Fort Necessity, 1754): new `surrender{army_id,days}` clause. One of the envoy's armies that is cornered (enemy strength in/beside its province >= 1.5x its own) marches out unharmed with its arms, falls back to an envoy-owned neighbour, yields the ground it held, and a parole ceasefire of 1-30 days starts. Refused in protected provinces (Boston) or with no retreat. Evidence: ctest 8,899 checks incl. `surrender_honors_of_war` + 6 vectors; tests/diplomacy 81 passed incl. live Claude (accepts when cornered, refuses when not); autoplay PASSED. The Claude tool schema is now one `anyOf` variant per clause type with required fields (the flat schema made the model drop `days`). Context gained `army_provinces`, `army_strengths`, `adjacency`.
- **Mood, snubs, vassalage** (after the Washington 1776 letter incident): `take_offense{reason,days<=7}` (envoy's own act; applied at once; refuses all clauses until it ends; attitude -10, grievance +30/40/50), `register_diplomacy_failure` (talked then left without agreement: attitude -2, grievance +8, snub at grievance >= 30; skipped in selftest/autoplay), grievance decays 2/day. `vassalage{title,gold,provinces}`: the Crown tempts the player; applies only when the player accepts; permanent peace treaty, gold/title/lands, attitude >= 60. Gold is uncapped everywhere (linear 1 point per 10 gold) and treasury income is 15 gold per supply point per month. UI header shows Mood / snub hours / Vassal. Evidence: ctest 8,944 checks, tests/diplomacy 103 passed incl. live Claude (insult -> take_offense, wavering -> vassalage, polite -> ceasefire), autoplay PASSED. Golden hash regenerated.
- Limits: as a vassal there is no hostile "patriot" force (only two factions exist), so the "rogue army" is just the player's army under the Crown's peace. The offense path (UI, `ApplyEnvoyActJson`) is built but not seen in the editor.
- Not done: a reprisal landing in a city you now hold is untested in the editor; 

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

## 2026-11-08 — Battlefront Conquest M0 audit

### Commands / paths
```
UE="/Users/Shared/Epic Games/UE_5.8"
PROJ="$HOME/PycharmProjects/Samson/unreal/PlayAmerica250/PlayAmerica250.uproject"
BUILD() { "$UE/Engine/Build/BatchFiles/Mac/Build.sh" PlayAmerica250Editor Mac Development -Project="$PROJ" -WaitMutex; }
TEST() { "$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PROJ" \
  -ExecCmds="Automation RunTests $1; Quit" -unattended -nullrhi -nosplash -NoSound -log \
  -TestExit="Automation Test Queue Empty" 2>&1 | grep -E "Test Completed|Result=|Error" ; }
```

### Battle audit
| Topic | Finding |
|-------|---------|
| Player character | `ARFCharacter` (`Source/PlayAmerica250/RF/RFCharacter.h`) — shared player/bot pawn |
| Camera | `USpringArmComponent` length 350, socket (0,60,65), lag; `APA250BattleGameMode::FrameHumanCamera` re-applies same on spawn/respawn |
| Weapon / fire | `AMusketWeapon` + `AMusketProjectile` (projectile damage). Player `FireMusket` does a camera visibility trace for aim point, then `FireMusketAt` |
| Active reload | `FActiveReloadSequencer` (plain C++) owned by `AMusketWeapon`; `IA_Reload` commit / hold-clear jam |
| Bot pawn | Same `ARFCharacter`; `AIControllerClass = APA250BotController` |
| Bot controller | `APA250BotController::Tick` brain — **no Behavior Tree / StateTree / EQS** on the battle path |
| Bot move targets | `PickGoal` → nearest defend-or-attack post; destination = post actor location |
| Command post | `APA250ControlPoint` — overlap capture, ownership colors; **no ring slots** |
| Ticket HUD | Top strip `BRITISH N / CONTINENTAL N` via `TicketsRemaining` = **remaining reinforcement pool**, not kills. Spend-on-spawn today (B1); M6 will switch to −1/death |
| Enhanced Input | Runtime-built in `ARFCharacter::BuildDefaultInput` (`IA_RF_*`, `IMC_RF_Default`); optional Content refs empty — **no Content IA_*/IMC_* assets** |

### Bot clumping — root cause
Every bot independently picks the same nearest enemy/neutral post (`PA250BotController.cpp` L97–101), navigates to the **post centroid** (L139), halts inside `0.7 × Radius` (~840 uu for default 1200) (L141, L169–173), and only jitters ±400 uu while advancing (L176). No `UCrowdFollowingComponent` / RVO. Volley squad clustering (1600 uu) amplifies piles after they gather.

### Verification
- `BUILD` → Succeeded (Target is up to date, unchanged tree)
- Audit section present (this block)

## 2026-11-08 — Battlefront Conquest M1–M6

### Decisions (overrides)
- **Projectile stays** for damage; M1 dual-trace (`UPA250AimComponent`) resolves camera aim + muzzle blocked flag, then `AMusketWeapon` fires a projectile along the corrected direction.
- **Tickets −1 on death** (+ bleed), not spawn (overrides B1). HUD `BRITISH N` = remaining tickets. `BattleResult.losses` from death counts / `SoldiersPerUnit`.
- **Bleed interval 5 s**. Capture radius **10 m** (1000 uu). Conquest + volley still on `DA_MusketTuning`; weapon/melee/bot/faction on new DataAssets (runtime defaults if Content missing).

### M1 — Shoulder camera, reticle, true aim
- Hip 220 / (0,±60,70) / FOV 80; ADS 120 / Y45 / FOV 55 / 0.15 s blend; shoulder swap `V`; face control yaw while ADS or 1 s after fire.
- `UPA250AimComponent::ResolveAim`; UMG reticle (spread / hostile red / blocked X); hitmarker hook.
- Files: `PA250AimComponent.*`, `RFCharacter.*`, `PA250BattleHUD.*`, `PA250BattleTests.cpp`, `FrameHumanCamera`.

### M2 — Weapon feel + reload presets
- `UPA250WeaponTuning`: hip 4° / ADS 0.6° / +1.5° moving; falloff 100@40m→60@100m; head ×1.5; max 200 m; Arcade (4/2.5/6) + Authentic (15/12/20).
- Recoil kick, Niagara muzzle smoke (existing path), jam blocks `CanFire` until clear.
- Files: `PA250WeaponTuning.*`, `MusketWeapon.*`, `MusketProjectile.*`.

### M3 — Bots spread and fight
- Root cause (audit): `PA250BotController.cpp` shared nearest post + centroid + 0.7×R halt.
- Fix: CP ring slots 6–25 m, squads 6–8, weighted CP assignment, `UCrowdFollowingComponent` separation, engage 40–80 m, volley window, bayonet charge, cover below 50% strength.
- `pa250.BotSpacingReport`; `APA250BotSpacingTest` + `Scripts/build_levels.py` target `ftest_bot_spacing` → `/Game/Tests/FTEST_BotSpacing`.
- Files: `PA250BotController.*`, `PA250ControlPoint.*`, `PA250VolleyCoordinator.*`, `PA250BotTuning.*`, `PA250BotSpacingTest.*`.

### M4 — Faction visuals
- `UPA250FactionVisuals` (British madder/blue/tricorn; Continental dark blue/buff/tricorn; Militia brown/round). MID coat + hat mesh; nameplate widget component. TODO for period meshes on Manny.
- Files: `PA250FactionVisuals.*`, `RFCharacter` ApplyFactionVisuals / UpdateNameplate.

### M5 — Bayonet / melee
- `UPA250MeleeComponent` + `UPA250MeleeTuning`: thrust / butt-stroke (preserves reload) / charge. Authority-only damage. Input E/Q separate from actions. Bots use charge rules from M3.
- Files: `PA250MeleeComponent.*`, `PA250MeleeTuning.*`.

### M6 — Conquest loop
- Death tickets, 5 s bleed, 10 m capture with attacker/defender rate, CP pips + capture ring text, kill feed, 3 s death cam → spawn screen, single `BattleResult` emit counter.
- Files: `PA250BattleGameMode.*`, `PA250BattleHUD.*`, `PA250BattleCommandlet.cpp`.

### Verification
- `BUILD` → **Succeeded**
- `-run=PA250Battle` → **fails=0** (Aim/Weapon/Faction/Melee/Conquest/spacing mirrors; bleed@5s; one BattleResult)
- Automation `PA250.Battle` via GUI binary could not run concurrently with the commandlet (editor instance lock); commandlet is the reliable Mac path per prior PROGRESS.
- `unreal/PA250Sim` `ctest` → **100% passed** (sim untouched)
- Generate FTEST map: `PA250_LEVELS=ftest_bot_spacing UnrealEditor-Cmd … -run=pythonscript -script=Scripts/build_levels.py`

### Playtest checklist (PIE on L_Lexington)
1. **M0** — Confirm bots no longer all freeze on the Green disc centroid; `pa250.BotSpacingReport` prints median NN / crowded / clusters.
2. **M1** — Hip OTS at ~220 cm; hold RMB for ADS blend; press V to swap shoulder; reticle grows with hip spread and turns red on enemies.
3. **M1** — Fire past a fence corner: blocked mark when the muzzle is occluded; clear shot hits near the reticle aim.
4. **M2** — Fire then tap R through Arcade reload; force a jam and hold R to clear; feel kick + muzzle smoke.
5. **M2** — Switch game-mode reload preset to Authentic and confirm much longer reload.
6. **M3** — Watch squads claim different CP sectors and hold a line at 40–80 m before volleying; bayonet when closing.
7. **M4** — British red / Continental blue coats + hats; enemy nameplates red within 50 m LOS.
8. **M5** — E thrust, Q butt-stroke during reload without losing progress, sprint+E charge.
9. **M6** — HUD tickets drop on death and every 5 s when holding fewer CPs; capture ring % inside 10 m; kill feed lines.
10. **M6** — Die → 3 s death cam → spawn screen on a held CP; fight to tickets 0 or time limit and confirm one BattleResult returns to campaign.

## 2026-11-08 — Battlefront review fixes

### Checked, left as specified
- Death spend (`SpendDeathTicket`), spawn allowed only while `TicketPool > 0`, bleed forced to 5 s, capture radius 1000 uu. `BattleResult.losses` = round(deaths / `SoldiersPerUnit`), clamped to the class pool (bleed is not a loss). `EndBattle` emits one result. Jam blocks `CanFire`. Butt-stroke does not touch the reload sequencer. Thrust/charge damage runs only in `SweepAttack` after `HasAuthority`. Arcade 4/2.5/6 and Authentic 15/12/20. Projectile falloff stays on `AMusketProjectile`. Crowd component uses the stock `SetDefaultSubobjectClass` path; `SetCrowd*` no-ops until a crowd manager exists.

### Fixed
- Aim block test used the spread cone, so the reticle X flickered and a clear shot could be treated as blocked. Block trace is now the unspread muzzle ray; spread stays on `FireDirection` only.
- Squad rebuild every 2.5 s (`static` timer) called `FindFreeSlot` past a bot's own claim, then `AssignSlot` dropped it. Bots also re-claimed whatever `PickGoal` liked, and file-index offsets stacked unique slots on top of each other. Claims now stick until the squad's post changes; destination is the slot point; a full ring gets one stable offset instead of a new random centroid every think.
- Tickets at 0 still opened the deploy screen in UI-only mode (`RequestSpawn` then no-ops). That now spectates. Death-cam corpse outlives the 3 s timer. A watched soldier's death retargets the camera; your own corpse stays the death cam.
- Projectile hits pushed a second kill-feed line and flashed the player's hitmarker for bot shots. Marker is the local shooter only; one feed line from `OnCharacterDied`.
- Spawn screen built its post list before `GameMode` was set, so held CPs never appeared. It rebuilds on open.
- Nameplates had no slate widget and `UpdateNameplate` was never called. Each soldier refreshes from the current view target (enemy red ≤50 m with LOS, friendly blue ≤30 m, squadmates always).

### Verification
- `BUILD` → Succeeded
- `-run=PA250Battle` → fails=0

### Still a PIE check on `L_Lexington`
- `pa250.BotSpacingReport` and whether squads sit on the ring rather than the Green centroid. Crowd separation is compiled in but not exercised headlessly.
- Reticle X only when the muzzle is actually in cover; a clear shot lands near the crosshair. If the X is stuck on, the new 8 cm muzzle overlap is the suspect.
- Die with tickets left (deploy lists held posts) and with tickets at 0 (spectate, no locked deploy screen).
- Nameplates, jam, butt-stroke during reload, and one `BattleResult` back to the campaign.

## Envoy review fixes (2026-11-08)

- Battle results now apply morale, retreat and destruction only to the two armies included in the battle setup. A regression covers the original Continental army sharing Boston with the defected army and the reprisal column.
- Offline defect offers preserve signed gold amounts and reject any negative amount, including spaced and Unicode minus signs, before summing the offer.
- Surrender context strength totals, JSON parsing and ratio arithmetic now use int64. Three shared vectors cover large strengths and summed enemy strengths; a sim regression covers totals above int32 and context JSON round-trip.
- Verification: rebuilt PA250Sim; 8,914 checks passed, including the existing clock golden without regeneration. Python shared vectors and offline defect tests: 57 passed, 3 live-provider tests deselected. Ruff and mypy passed for the touched Python implementation. UE build/editor and UE automation were not rerun for these fixes.

## 2026-11-08 — Battlefront review fixes (2)

### Fixed
- **Reload presets did not match the brief.** An un-pressed phase counted as a miss and restarted, so a reload never finished without input, and a sweet press at mid-phase beat "perfect". `AMusketWeapon::PresetToSequencer` now maps a preset so untouched = Base, a commit at the window centre on every phase = Perfect (+15%), and an early miss on every phase = Failed. Measured: Arcade 4.01 / 2.51 / 6.03 s, Authentic 15.01 / 12.01 / 20.03 s. Opt-in sequencer fields (`WindowCenter`, `bTimeoutCompletesPhase`); the legacy `DA_MusketTuning` path and the `PA250.Combat` tests are unchanged. Bots commit at the window centre instead of a fixed 45%.
- **Recoil drifted.** Pitch recovery pushed the same way as the kick (camera climbed to twice the kick and stayed there). Yaw recovery used a different random sample from the kick. Both now return to the original aim.
- **Bots only fired under 40 m.** Inside the 40–80 m band they kept advancing to the band's midpoint. Now: halt and fire anywhere inside 80 m with line of sight; reload on the move until the 40 m edge. Melee ranges were inverted (charge under 2.5 m, thrust at 3.5 m, beyond 2.2 m reach); they now come from `UPA250MeleeTuning` (thrust ≤ reach + radius, charge ≤ 4 m when off cooldown). Bots face their focus target while engaging.
- **"Squad strength < 50%" read corpses that last 3 s.** Squads now persist across the 2.5 s rebuild with stable ids, so sectors and slot claims also stay put. Strength = live / peak members. New and respawned bots join a nearby squad with room. A shattered squad (cut below half the minimum size) disbands so survivors regroup instead of hiding all battle. Stale `SquadId`s are cleared. The player rides with the nearest friendly squad for squadmate nameplates.
- **CP ring sectors were interleaved.** `Sector = i % 8` spread one "sector" around the whole ring, and sector distance did not wrap. Sectors are now angular octants with wrap-around distance; rings sit at 6 / 15.5 / 25 m (the 6 m ring was never used before); 48 slots (6 per octant); off-navmesh points are dropped (raw ring kept if no navmesh). Same-side squads on one post use sectors three octants apart. Post assignment spreads squads per side, so one side no longer pushes the other off a post.
- **Squad volleys ignored LOS** and fired out to 60 m outside the engage band. Now LOS and ≤ 80 m only.
- **Match end.** Conquest ended only when a side had 0 tickets *and* nobody alive. Now 0 tickets loses at once; sides that started with no tickets (debug setups) still need to be wiped out. Ticket rules moved to `FPA250TicketLedger` (plain C++) so the tests run the shipped code.
- **FTEST_BotSpacing could not fail** on kills (floored to `MinKills`) or engagement range (shot counter never incremented, defaulted to 0.8). It now uses real deaths and the bot engagement histogram. `pa250.BotSpacingReport` prints the histogram (<20 / 20–40 / 40–80 / 80–120 / >120 m) and the squad count.
- **Reticle size** was `6 + spread×4` px. It now projects the actual next-shot spread (stance, movement, line bonus, suppression) through the live FOV.
- Kill feed: "You / Regular / Provincial [militia]" instead of raw faction ids.
- **Tests were tautological.** The Conquest test checked its own lambdas; the Aim block case and the Melee butt-stroke case re-derived the rule inline; the preset test only read constants. All `PA250.Battle.*` cases now live in `PA250BattleCases.cpp` and drive shipped code: aim traces in a real test world (wall + post), preset timings through the sequencer, jam on a spawned musket, butt-stroke on a spawned soldier mid-reload, ledger bleed/death/losses, slot-ring geometry and claims. The automation tests and the `PA250Battle` commandlet run the same cases; added `PA250.Battle.Spacing`. `tools/verify.sh ue-test PA250.Battle*` routes to the commandlet.

### Files
`RF/ActiveReloadSequencer.*`, `RF/MusketWeapon.*`, `RF/RFCharacter.cpp`, `Battle/PA250BotController.cpp`, `Battle/PA250VolleyCoordinator.*`, `Battle/PA250ControlPoint.*`, `Battle/PA250BotTuning.h`, `Battle/PA250MeleeComponent.*`, `Battle/PA250BattleGameMode.*`, `Battle/PA250TicketLedger.h` (new), `Battle/PA250BotSpacingTest.cpp`, `Battle/PA250BattleHUD.cpp`, `PA250BattleCases.*` (new), `PA250BattleTests.cpp`, `PA250BattleCommandlet.cpp`, `tools/verify.sh`.

### Verification
- `BUILD` → Succeeded
- `-run=PA250Battle` → checks=82 fails=0 (two consecutive runs)
- `Automation RunTests PA250.Battle+PA250.Combat` → 10/10 Success, EXIT CODE 0 (Automation ran on this Mac this time)
- `-run=PA250Combat` → fails=0
- `unreal/PA250Sim` ctest → 100% passed (sim untouched)
- **Not run:** `Project.Functional Tests./Game/Tests/FTEST_BotSpacing`. `Automation List` shows no functional tests in this editor-cmd environment. The map also has no `NavMeshBoundsVolume`, so bots there fall back to direct movement and crowd avoidance is not exercised. Squad spacing and the fight loop still need a PIE check on `L_Lexington` (`pa250.BotSpacingReport`).

## 2026-11-08 — Battle feel pass: aim, planted firing, bayonet clarity

### Why shots went high (fixed)
- The ball flew at 30 m/s (`ProjectileSpeed 3000` cm/s), so every shot was a visible lob.
- Drop compensation used the camera-trace distance. When nothing was under the reticle (thin target, horizon), that distance was the 200 m max range, so the ball was lofted for 200 m (about 65 m of lift) even at an enemy 60 m away.
- Compensation was an approximation with a hard-coded 0.3 gravity scale, duplicated from the projectile.
- Fix: ball at 300 m/s (`UPA250WeaponTuning::ProjectileSpeed = 30000`, lifespan 2 s). `AMusketWeapon::SolveLaunchDirection` gives an exact low arc from world gravity × the projectile's own gravity scale. No compensation when the camera ray hit nothing (`FPA250AimResult::bCameraHit`). Spread is applied once, in `AMusketWeapon::Fire`. Hip 1.5° (was 4°), ADS 0.25° (was 0.6°).
- Measured (PA250.Battle.Weapon): lands 0.0–0.1 cm from the aim point at 20 / 60 / 150 m; at most 9.2 cm above the sight line at 150 m.

### Firing plants your feet
- `ARFCharacter::LockMovement`: every shot (player and bots, including squad volleys) stops the shooter dead and blocks walking for `FireStillSeconds` (0.6 s); you can still turn and aim. No firing in mid-air. Spread still uses the speed at the trigger pull, so running shots are inaccurate. The butt-stroke stagger uses the same lock. HUD shows "feet planted" under the reticle.

### Reload and bayonet made legible
- Reload steps are numbered: "1/3 Prime the pan", "2/3 Ram the ball", "3/3 Shoulder arms". HUD reload bar: fill, gold press zone, bright perfect zone, white marker. Hint line explains tapping R (or just waiting). Press result flashes: PERFECT / Good / Fumbled. Jam reads "hold R to clear".
- Bayonet charge (Shift+E) is now a real 4 m dash over 0.45 s (constant-force root motion). The first enemy met takes 100 (×2 from behind or vs a reloading man) and is knocked back. A charge that hits no one leaves you off balance for 0.6 s. Musket is levelled for the dash. Thrust (E) visibly jabs out over its 0.35 s wind-up; butt-stroke (Q) jabs short. Melee hits now show the hitmarker. HUD line: "CHARGE!", "Charge hit!", "Charge missed - off balance", or the cooldown / "ready (hold Shift + E)". Bots charge only when facing the target.
- Controls legend bottom-left.
- Placeholder animation only (musket slides along facing); real montages still TODO.

### Verification
- `BUILD` → Succeeded
- `-run=PA250Battle` → checks=101 fails=0 (new: ballistic landing / sight-line, soldier stops on fire and walks again, no mid-air fire, charge dash / cooldown / gating, numbered steps)
- Automation `PA250.Battle+PA250.Combat` → 10/10 Success; `-run=PA250Combat` → fails=0
- Not checked headless: how the HUD panel and musket jab look, and the charge dash distance on real terrain. PIE on `L_Lexington`.

## Fab asset import (2026-11-08)

### M0 — preflight inventory
- Read CLAUDE.md and all of PROGRESS.md; no applicable AGENTS.md found in the repository. Existing unrelated working-tree changes preserved.
- UE_ROOT=`/Users/Shared/Epic Games/UE_5.8`; EDITOR_BIN=`$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor`; executable exists. VAULT=`/Users/Shared/UnrealEngine/Launcher/VaultCache`; search of Epic application support and /Users/Shared found no other VaultCache.

| Asset | Vault path (relative to VAULT) | Kind | Original top folder | Size (bytes) | Status | Skeleton filenames |
|---|---|---|---|---:|---|---|
| Weapon Pack 01 | HorizonW052be976533aV1/data/Content/HS_WeaponPack_01 | UE content | HS_WeaponPack_01 | 939869881 | ready; already complete | SK_HS_WP01_MusketRifle_{Strap_X,Strap_Y,X,Y}_0_Skeleton; SK_HS_WP01_PowderHorn_{Strap_X,Strap_Y,X,Y}_0_Skeleton |
| AMERICAN SOLDIER | americana7faf88f10c5V1/data/Content/american_soldier | UE content | american_soldier | 604582827 | ready; already complete | SK_AMERICAN_SOLDIER_Skeleton |
| Open World Demo Collection | KiteDemo421/data/Content/KiteDemo | UE content | KiteDemo | 6804916227 | ready; already complete | none found |
| CANNON - GAME READY | FabLibrary/CANNON_-_GAME_READY-3cfb1707/fbx/high-quality-1765-americ_extracted | raw RAR with Mesh.FBX + TGA; external PNG textures | n/a | 133125659 | ready | not yet inspected |
| Smuggler’s Cove | not found in vault, Epic support, /Users/Shared or Downloads | unknown | unknown | 0 | missing; SKIPPED as permitted | unknown |
| Grand Strategy Style Map | no vault .uplugin; no Engine/Plugins/Marketplace directory | C++ plugin; GitHub fallback required | pending | pending | source pending | n/a |

- SHA-256 plus size comparison: musket 129 present / 0 missing / 0 differing; soldier 103 / 0 / 0; KiteDemo 274 / 0 / 0. Thus the requested Open World delta is empty. No differing files to report.
- Content top folders: Characters, Collections, Developers, HS_WeaponPack_01, Input, KiteDemo, LevelPrototyping, Map, PA250, Tests, ThirdPerson, Variant_Combat, __ExternalActors__, __ExternalObjects__, american_soldier.
- Enabled .uproject plugins: ModelingToolsEditorMode (Editor only), StateTree, GameplayStateTree, EnhancedInput, PythonScriptPlugin, EditorScriptingUtilities, WmfMedia, AvfMedia, MediaCompositing. Both Python requirements are already enabled.
- .gitattributes covers *.uasset and *.umap with LFS. Missing: *.fbx, *.glb, *.png, *.exr, *.wav (also raw *.tga). No git configuration changed.
- Cannon archive listing verified with `tar -tf <source>/Cannon.rar`: Cannon/Mesh.FBX and five Cannon_*.tga textures. No vault Config fragments or .uproject/.uplugin files found for the three content packs.
- Initial Content disk usage: 8,330,100 KiB; exact byte baseline will be captured before import. No project Plugins directory existed.
- Verify: `test -x "$EDITOR_BIN" && ls "$VAULT" && grep -n "Fab asset import" PROGRESS.md` (variables above): pending below.

## 2026-11-08 — Look sensitivity
- Look input was applied 1:1 with no scale (too fast, especially on a Mac trackpad). Now ×`pa250.LookSensitivity` (default 0.35) and ×0.6 more while aiming down sights (`ARFCharacter::AdsLookScale`).
- `[` / `]` change it in game by 15% steps (0.05–2), show the value on screen, and save it to GameUserSettings so it persists. Console: `pa250.LookSensitivity 0.25`. Legend updated.
- Files: `RF/RFCharacter.*`, `Battle/PA250BattleHUD.cpp`. Verify: BUILD Succeeded; `-run=PA250Battle` checks=101 fails=0. Feel needs a PIE check.

- M0 verify result: PASS; editor executable, vault listing and heading verified. Deviation: all three UE packs are already byte-identical and complete, rather than KiteDemo being partial.

### M1 — editor Python
- PythonScriptPlugin and EditorScriptingUtilities already enabled; no .uproject edit required. Existing tools/ue/ preserved. Baseline logs snapshotted to Saved/FabImport/BaselineLogs before editor invocation.

- M1 verification: supplied editor command outside sandbox exited 0, printed engine version (see Saved/FabImport/M1.log). Sandboxed launch exited 134 before logging; no commandlet syntax change needed.

### M2 — UE content packs
- Executed requested `rsync -a --ignore-existing` for HS_WeaponPack_01, american_soldier, KiteDemo. No missing files to copy; original top folders retained, no assets renamed or resaved. No differing vault files.
- Added tools/ue/verify_assets.py: AssetRegistry class counts; load every StaticMesh, SkeletalMesh, Skeleton, AnimSequence, Material and MaterialInstanceConstant; FAILED diagnostics and Saved/AssetImportReport.json. Records skeleton references; mannequin identification uses read-only skeleton package bone-name evidence.
- Smuggler’s Cove SKIPPED (not on disk). Grand Strategy Map SKIPPED by explicit follow-up user instruction; no plugin copied, patched or enabled. Git fallback inspected only in /private/tmp; v1.0 SHA 92d2ea86a51ce6358a7c116f011ece73f52d3417 and latest tag/main SHA e4748118030a74edec083122d0f7cb6668bbc816 contain documentation only; plugin archives are release attachments. No build attempted.
- Verify command: editor invocation from M1 with `-script="$PWD/tools/ue/verify_assets.py"`; result pending.

## 2026-11-08 — Easier reload, visible shots, ranks and volleys

### Reload too hard (fixed)
- Arcade press zone is now half of each step (perfect zone a fifth), was 35% / 12%.
- A fumble pauses the step for ~0.7 s and then **continues** from where it was (was: restart the step).
- After one fumble in a step, further early presses in that step are ignored, so mashing R costs nothing more.
- Arcade never jams; Authentic keeps its 10% jam on a miss. Timings are unchanged: Arcade 4 / 2.5 / 6 s.
- Sequencer opt-ins (`bMissResumesProgress`, `bIgnoreRepeatMiss`) are set only by the presets. Legacy `PA250.Combat` tests are unchanged.
- **Latent bug found:** `Scripts/build_levels.py` creates `DA_WeaponTuning` / `DA_FactionVisuals` from the class defaults, but the brief values lived only in `MakeRuntimeDefaults()`, which the game never uses once the asset exists. So in game, Authentic was a copy of Arcade, and faction looks were probably the default white coats (inferred from the generator, not seen in PIE). The values now live in the class constructors. New checks load the shipped assets and verify them.

### Musket balls invisible (fixed)
- At 300 m/s a 10 cm ball is on screen for about a frame. `AMusketProjectile` now carries a glowing tracer streak: a thin cylinder using the engine `EmissiveMeshMaterial` (`Color` = warm HDR), 4 cm thick and up to 4.5 m long behind the ball, aligned with its flight. It grows from the muzzle, so there is never a streak in the shooter's face. Tunable: `TracerLengthCm`, `TracerThicknessCm`, `TracerColor`.

### Bots too close together → ranks, then volleys
- Squads of 2+ now fight in formation (`UPA250VolleyCoordinator::TickFormation`). The squad has an anchor and a facing; its men take numbered spots in two staggered ranks (`FormationOffsets`: 2 m files, 2 m ranks, ≤ 6 per rank, one rank under 4 men), re-sorted when the line turns so men don't cross.
- **March:** the anchor walks to the squad's objective (squads sent to the same post line up side by side 15 m apart) and waits for stragglers. Friendly anchors repel to stay ≥ 15 m apart.
- **Contact** (enemy ≤ 80 m with LOS): the line halts and faces the enemy. Once 70% are loaded and in position (or after 6 s): "Make ready!" → "Present!" (men face the target) → "FIRE!", with the whole squad firing together. Between volleys the line steps forward until the enemy is 50 m away, then stands.
- Formed men never fire singly. They break ranks only for the bayonet rule (< 10 m, or < 20 m vs a reloading enemy).
- Reinforcements join the nearest squad with room, wherever it is (no more loners). Bots far from the Green are pulled in to 55 m (was 28 m), so the sides start about 110 m apart. Callouts show only for squads within 60 m of the camera.
- Autoplay battles log `pa250.BotSpacingReport` every 15 s, including a per-squad line (live / formed / engaged / stage / lag / nearest enemy squad).

### Measured (slice autoplay, real L_Lexington battle, 10× dilation)
| | before | after |
|---|---|---|
| shots in the 40–80 m band | 1–12% | **100%** (562 shots) |
| men with > 4 others within 3 m (after the first sample) | 30–60% | **0–11%** (0% in 5 of 9 samples) |
| median nearest neighbour | ~1.8 m | 1.4–2.0 m |
| lone bots fighting outside a squad | yes | none |
| volleys | timer-driven, any range | 343 squad volleys |

### Verification
- `BUILD` → Succeeded. `-run=PA250Battle` → checks=123 fails=0 (new: shipped DataAssets carry the brief values, forgiving fumble, tracer grows/caps, formation layout). `-run=PA250Combat` → fails=0. Automation `PA250.Battle+PA250.Combat` → 10/10. `scripts/run_slice_autoplay.sh` → PASSED.

### Known gap
- In the sampled autoplay battle **no post changed hands** (`post_flips=0`). The lines trade volleys at 50 m and the match is decided by attrition and bleed. Lines need a rule to push onto the post (e.g. when the enemy squad in front is broken or outnumbered) for Conquest to play like Conquest.

- M2 verify: first script run exposed a UE 5.8 API difference (`AssetData.get_soft_object_path` absent); changed path formatting to package_name + asset_name. Retry exited 0; FAB_VERIFY failures=0. No content load errors. Soldier mesh tangent generation emitted a zero-length-normal Display message, not an error. Full counts and skeleton references are in Saved/AssetImportReport.json; final table below.

### M3 — cannon raw import
- Extracted RAR to /private/tmp/pa250-cannon (vault left unchanged). FBX 7400 contains no LimbNode or Skeleton markers, so import as static mesh. Added tools/ue/import_raw.py with AssetImportTask automated=True, replace_existing=False, save=True; FbxImportUI build_nanite=False; imports materials/textures and any remaining TGA textures. Existing destination assets cause a safe skip on reruns.
- Verify: run import_raw.py using M1 invocation, then verify_assets.py with PA250_ASSET_ROOTS including /Game/ThirdParty/PTM_Cannon; pending.

- M3 first legacy FbxFactory attempt failed with `Assertion failed: CurrentApplication.IsValid() [File:Runtime/Slate/Public/Framework/Application/SlateApplication.h] [Line: 321]` after an FBX smoothing warning. Removed explicit legacy factory; default Interchange converts FbxImportUI (confirmed in engine source, read-only), including bBuildNanite=False. Retry exited 0 and created 1 mesh, 1 material, 5 textures. The FBX lacks smoothing groups; importer reports a warning, no errors.

### M4 — Grand Strategy Map
- SKIPPED per user's explicit instruction. No project plugin source, patches, build, demo load or campaign integration performed; Plugins added size = 0.

### M5 — level regression
- Added tools/ue/verify_levels.py to load /Game/PA250/Maps/L_Campaign, L_Lexington, L_DiplomacyRoom without saving. Compares Error:/Failed to load lines against the pre-task Saved/Logs snapshot, writes Saved/LevelImportReport.json and fails on any new errors or unsuccessful level loads.
- Verify command: M1 editor invocation with `-script="$PWD/tools/ue/verify_levels.py" -abslog="$PWD/unreal/PlayAmerica250/Saved/FabImport/M5.log"`; pending.

- M3 final verify exited 0; all four roots have zero FAILED loads. Cannon StaticMesh Mesh loads with nanite_enabled=false.
- M5 verify exited 0: all three levels loaded, new_error_lines=[], error_lines=[]. Commandlet reports Success - 0 error(s), 13 warning(s).
- `rg "Error|Failed to load" Saved/FabImport/M5.log` found only three LoadErrors page headings, three MapCheck lines reporting 0 Error(s)/0 Warning(s), and the Warning/Error Summary heading; no actual errors or Failed to load messages.
- Warnings: twelve existing KiteDemo meshes recompute legacy physics on load (SM_Buttercup_Patch_01, SM_FieldGrass_01, SM_Yarrow, SM_Boulder05a, SM_GroundRevealRock001, HillTree_Tall_02, HillTree_02, SM_LargePlainsBoulder002, Medium_Boulder_001, SM_River_Rock_01, Medium_Boulder_LowPoly, Tree_Stump_01); one missing RecastNavMesh warning. These warning strings were not present in the captured old logs. No resave was performed, per hard rule. They are warnings while reading existing assets, not new load errors.

### Final report

| Asset | Final path | Asset counts | Status |
|---|---|---|---|
| Weapon Pack 01 | `/Game/HS_WeaponPack_01` | 129 total; AnimSequence: 2; MapBuildDataRegistry: 2; Material: 1; MaterialFunction: 3; MaterialInstanceConstant: 35; PhysicsAsset: 8; SkeletalMesh: 8; Skeleton: 8; StaticMesh: 29; Texture2D: 30; TextureCube: 1; World: 2 | PASS, zero failed loads |
| AMERICAN SOLDIER | `/Game/american_soldier` | 103 total; AnimSequence: 6; MapBuildDataRegistry: 1; Material: 10; PhysicsAsset: 1; SkeletalMesh: 18; Skeleton: 1; StaticMesh: 1; Texture2D: 64; World: 1 | PASS, zero failed loads |
| Open World Demo Collection | `/Game/KiteDemo` | 274 total; Blueprint: 1; CurveFloat: 1; CurveLinearColor: 1; MapBuildDataRegistry: 2; Material: 32; MaterialFunction: 1; MaterialInstanceConstant: 45; MaterialParameterCollection: 2; StaticMesh: 47; Texture2D: 138; TextureCube: 2; World: 2 | PASS, zero failed loads |
| CANNON - GAME READY | `/Game/ThirdParty/PTM_Cannon` | 7 total; MaterialInstanceConstant: 1; StaticMesh: 1; Texture2D: 5 | PASS, zero failed loads |
| Smuggler’s Cove | — | 0 | SKIPPED: source not on disk |
| Grand Strategy Style Map | — | 0 | SKIPPED: user requested |

- Plugin source installed: none. Inspected temporary Git checkout only: v1.0 92d2ea86a51ce6358a7c116f011ece73f52d3417; latest tag Example-Project/main e4748118030a74edec083122d0f7cb6668bbc816. No plugin descriptor was available in checked Git trees or vault; EngineVersion/Modules/PlatformAllowList/Binaries/Mac therefore not applicable. Patches applied: none. No plugin enabled or build attempted after user skip.
- Total added to Content/: **77,277,043 bytes (73.70 MiB)**, seven new cannon .uasset files. Total added to Plugins/: **0 bytes**. Musket/soldier/KiteDemo copies added zero bytes; all were already complete.
- All newly added project Content files are .uasset and covered by current LFS patterns. Missing requested patterns: *.fbx, *.glb, *.png, *.exr, *.wav; raw *.tga also uncovered. Raw source stayed in /private/tmp; no git config or .gitattributes edits.
- Soldier skeleton: `SK_AMERICAN_SOLDIER_Skeleton`. It is a custom pack skeleton with UE4-style bone-name evidence (spine_01/02/03, upperarm_twist_01_l, thigh_twist_01_l, ik_foot_root; no spine_04/05 or metacarpals), **not the UE5 Mannequin skeleton**. This identifies the supplied asset, not a retargeting compatibility guarantee.
- Musket/powder horn skeletons (all custom mechanical rigs, **none is a UE5 Mannequin skeleton**; no pelvis/spine_04 names):
  - `SK_HS_WP01_PowderHorn_Strap_Y_0_Skeleton`
  - `SK_HS_WP01_PowderHorn_X_0_Skeleton`
  - `SK_HS_WP01_PowderHorn_Strap_X_0_Skeleton`
  - `SK_HS_WP01_PowderHorn_Y_0_Skeleton`
  - `SK_HS_WP01_MusketRifle_Strap_Y_0_Skeleton`
  - `SK_HS_WP01_MusketRifle_Strap_X_0_Skeleton`
  - `SK_HS_WP01_MusketRifle_X_0_Skeleton`
  - `SK_HS_WP01_MusketRifle_Y_0_Skeleton`
- No retargeting, gameplay wiring, map edits, third-party asset resaves, ini merges, engine edits, git commits/staging/push/stash/reset, or changes to unreal/PA250Sim/ or services/ were performed.
- Evidence: Saved/AssetImportReport.json, Saved/LevelImportReport.json, Saved/FabImport/{M1,M2_retry,M3_interchange,M3_verify,M5}.log, Saved/FabImport/PostImportIntegrity.json. Post-import SHA-256 checks: all 506 vault pack files present and identical; zero differing files.
- Added scripts: tools/ue/verify_assets.py, tools/ue/import_raw.py, tools/ue/verify_levels.py. Engine verified: 5.8.4-58902052+++UE5+Release-5.8 (Apple M3).

## Fab asset import (2026-11-08) — Lexington runtime visuals

- Follow-up screenshot showed Quinn, cube hats, floating cones and the cylinder musket. Import/load verification alone did not connect the assets to gameplay.
- Root cause: RF/PA250ThirdPartyAssets.h searched placeholder /Game/ThirdParty paths; RFCharacter rejected the soldier's non-Manny skeleton; MusketWeapon always constructed a cylinder.
- Added original-path lookups for american_soldier and HS_WeaponPack_01. RFCharacter uses the soldier's own idle/walk/run animations without retargeting, clears mannequin material overrides, and hides primitive hats/markers. MusketWeapon selects the Horizon static musket with its supplied materials and derives grip/muzzle positions from its bounds.
- Creating a project-owned tintable coat material to distinguish British red, Continental blue and militia brown while preserving native face/hair/kit materials. No original Fab asset is edited or resaved.
- Added PA250Visuals commandlet to exercise spawned faction pawns, compatible animations, hidden placeholders, real attached muskets and distinct faction coat colors. Build/runtime/render verification pending.
- Another external build was active in the same workspace; Build.sh -WaitMutex serialized builds. Unrelated source changes are being preserved.

- Build: Build.sh PlayAmerica250Editor Mac Development -Project=<project> -WaitMutex -MaxParallelActions=2 succeeded. First PA250Visuals check stalled while SetSkeletalMesh initialized the old mannequin AnimBP (confirmed with macOS sample at RFCharacter.cpp -> SetSkeletalMesh -> InitAnim -> UpdateAnimation_WithRoot). Stopped only this task’s commandlet process and moved SetAnimInstanceClass(nullptr) before switching meshes. Rebuild/retry pending.
- SHA-256 recheck after runtime material creation: 129 musket + 103 soldier files still identical to vault; zero differences. Evidence: Saved/FabImport/RuntimeVisualIntegrity.json.

- Rebuild after animation-order fix succeeded. PA250Visuals now confirms both imported soldier meshes, compatible idle animations, hidden placeholders and distinct coat tints. Four weapon assertions exposed a test-harness issue: bare world has no GameMode to dispatch BeginPlay (weapon is spawned there). Added explicit DispatchBeginPlay when needed; final retry pending.

- Final native verification: `UnrealEditor <project> -run=PA250Visuals -unattended -nosplash -nullrhi -stdout` exited 0, 12 PASS / failures=0. Both factions spawn imported soldier meshes, compatible idle animation, hidden primitive hats/cones, Horizon musket, hand_r attachment, and distinct red/blue coats. Build4 succeeded. Persistent logs: Saved/FabImport/RuntimeVisual{Build,Verify}.log.
- Render command: `UnrealEditor <project> -ExecutePythonScript=<repo>/tools/ue/preview_soldier_visuals.py -unattended -nosplash -NoSound -windowed -ResX=1280 -ResY=900`; opens Lexington, spawns two comparison pawns in memory, captures PA250_FabSoldiers.png, exits without saving the map. Result pending.

## 2026-11-08 — Training, discipline, morale, adaptable tactics (3 slices)

### What existed (inspection)
- Engine 5.8.4 (CL 58902052) at `/Users/Shared/Epic Games/UE_5.8`; editor target `BuildSettingsVersion.V7`, `Unreal5_8` include order, Mac Development.
- Weapon: `AMusketWeapon` is a **smoothbore musket** (spread cone, round ball). Not renamed.
- No ammunition count exists. Readiness is `Loaded / Reloading / Jammed`, gated by `CanFire()`. Nothing was added.
- AI reload used `BotCommit`, which rolled hit quality regardless of timing, inside a 0.2–0.35 s think gate. Skill came from unit class.
- No morale, no cover, no reload animations. The bot perception used for the bayonet rule ignored line of sight.

### Design
- New `DA_CombatProfiles` (`UPA250CombatProfiles`, values in the constructor). Training (`Veteran` / `Trained` / `Recruit`), tactical role (`Line` / `Light` / `Irregular`) and an assignment table (faction, class) → (training, role) are separate. Faction appears only in the assignment.
- Defaults (user-chosen):
  - british / line → Veteran / Line;
  - continental / line → Trained / Line;
  - continental / militia → Recruit / Irregular.
- All values are labelled gameplay settings (provisional), not historical rates.

### Slice 1 — AI reload by training (player untouched)
- `FPA250AIReloadPlanner` (`Battle/PA250AIReload.h`) decides **when** a bot presses. It presses through the player's own `TryCommit`, so the windows, fumble pause, jams, damage bonus and all three steps are identical. Nothing is skipped and there is no instant reload.
- The press point is never earlier than the window centre. That centre path is the "ideal drill" baseline: the Arcade perfect time, 2.5 s, kept as a balance placeholder.
- Pace, jitter, stress and moving penalties are all ≥ 1×. A press that would land after the zone becomes "let the step run out", so stress, moving and disorder can only add time.
- Stress uses existing signals: volley suppression, squad losses, and squad morale.
- Bots tick the planner every frame. Each bot has a `FRandomStream` seeded from `BattleSetup.seed` and a spawn counter.
- `ConfigureSoldier` assigns training and role. The player's R-press path never reads them, and `PresetToSequencer` is unchanged (golden test).
- `battle_result.json` `trace.reload_by_training`: n / mean / p90 / min / fumbles per training.
- Found by the tests and fixed: a planned press after the zone counted as a fumble, so slower (moving) Recruits came out faster than calm ones.

### Slice 2 — Morale, discipline, withdrawal (through `UPA250VolleyCoordinator`)
- `FPA250MoraleState` (`Battle/PA250Morale.h`):
  - States: Steady → Shaken → Routing → Rallying → Steady, plus a separate deliberate Withdrawing.
  - Pressure combines recent casualties (12 s half-life), the suppressed share, strength, close contact and a flank.
  - Thresholds: enter 0.45 / leave 0.30, rout 0.85, 3 s minimum dwell. Pressure is divided by training resolve.
  - Rally waits `RallySeconds` calm, then `ReformSeconds`.
- `FPA250MoraleOrders` decides what each state may do:
  - **Withdrawing:** keeps ranks, backs off facing the enemy, holds fire while moving, needs no rally. It can still break into a rout.
  - **Routing:** abandons ranks, runs to the side's spawn rear (`GetRearPoint`), never fires or charges, must rally.
  - **Shaken:** slower march, waits for more loaded muskets before volleying, won't charge.
- Withdrawal triggers: outnumbered 2:1 below 60% strength (any role), or exposed (role setting). There is a cooldown.
- Cohesion by training: arrive tolerance and straggler wait scale per training. Rallying re-forms where the men stand.
- Squads only group the same faction + training + role. The player's soldier never joins a squad (`ShouldJoinSquad`).
- Replaced the old "seek cover below 50% strength" rule for formed squads.

### Slice 3 — Adaptable tactics (Light / Irregular roles)
- `PA250Cover` (`Battle/PA250Cover.h`): trace-only, no EQS. A firing position is one where the enemy's ray to the chest (~0.9 m) is stopped by something within 1.5 m in front, while the shoulder ray (~1.45 m) is clear. The search checks a deterministic ring of 17 points (navmesh-projected when a navmesh exists), every 2 s per squad.
- Light / Irregular:
  - open order (one rank, wider files);
  - each man takes the nearest covered firing position near his place;
  - men fire individually, only once in position, after a reaction delay;
  - they withdraw `RepositionCm` when exposed, with a cooldown.
  - Costs: no shots while moving back, slower reload on the move, still targetable.
- Line regulars can still withdraw and rout through morale.
- Perception: `FindNearestEnemy` now needs line of sight (nearest first, at most 4 traces). Contact already did.

### Files
- New: `Battle/PA250CombatProfiles.*`, `Battle/PA250AIReload.h`, `Battle/PA250Morale.h`, `Battle/PA250Cover.h`, `Content/PA250/Data/DA_CombatProfiles.uasset`.
- Edited: `RF/MusketWeapon.*`, `Battle/PA250BotController.*`, `Battle/PA250VolleyCoordinator.*`, `Battle/PA250BattleGameMode.*`, `PA250BattleCases.*`, `PA250BattleTests.cpp`, `PA250BattleCommandlet.cpp`, `Scripts/build_levels.py` (DA spec + `data` builder).
- Not touched: `FActiveReloadSequencer` rules, `PresetToSequencer`, the player input path, `unreal/PA250Sim/`, `protocol/`.

### Tests that ran (headless, this Mac)
- `-run=PA250Battle` → checks=161 fails=0.
- `Automation RunTests PA250.Battle+PA250.Combat` → 13/13 Success, including the new `PA250.Battle.Training`, `.Morale` and `.Tactics`.
- `-run=PA250Combat` → fails=0.
- Key measurements:
  - Player Arcade settings: golden match.
  - Every AI reload passed all 3 steps; fastest 2.52 s vs the 2.50 s ideal.
  - Calm Veteran 2.65 ± 0.20 s (≤ 1.1 × ideal); Recruit 4.31 ± 0.57 s.
  - Same training, British vs Continental: identical with the same seed; means 2.647 vs 2.649 s across different seeds.
  - Stress 0 → 1 × still/moving: no single run got faster (1000 seeds × 5 levels × 2).
  - Morale wobbling ±0.05 at the threshold for 120 s: 1 transition. The same pressure routs Recruits, not Veterans; Veterans still rout at 1.6.
  - Rally: Veteran 8.1 s, Recruit 20.1 s.
  - Withdrawal vs rout orders differ as above. The player's soldier never joins a squad.
  - Fence test world: the cover search moves a man behind the fence; no cover in the open; an enemy behind a wall is not seen; moving reload 4.80 vs 4.30 s.
- `scripts/run_slice_autoplay.sh` → PASSED (real `L_Lexington` battle, headless, 10× dilation):
  - Reloads: Veteran 432 at mean 3.75 s (min 2.53); Trained 39 at 4.46 s; Recruit 197 at 5.08 s with 204 fumbles. Battle stress lifts all of these above the calm figures.
  - Morale transitions — British: Shaken 6, Routing 3, Withdrawing 2, Rallying 2. Continental: Shaken 10, Routing 7, Withdrawing 3, Rallying 5.
  - 87–88% of shots in the 40–80 m band; crowding 0–7%; one post flip.

### Not verified / deferred
- **No PIE or visual check** of withdrawals, routs, cover-taking or skirmish fire. The FTEST map has no navmesh, and `L_Lexington`'s fences were not inspected for chest-height cover.
- Bot aim error is still set by unit class (militia 3°, line 2°) in `ConfigureSoldier`, not by training.
- The unused `LineInfantryReloadSkill` / `MilitiaReloadSkill` (`DA_MusketTuning`) and `TickBotReload` / `BotCommit` are left for the legacy tests.
- No reload animations exist to sync; the HUD and musket state come from the same sequencer.
- There is no historically sourced reload time; the Arcade baseline stays a balance placeholder.

- Rendered QA caught a Metal shader failure in the first generated coat material: `(Node Desaturation) Missing Desaturation input`. Fixed generation to connect the default source output to the actual reflected first input and assert all graph connections succeed. Created M_PA250_SoldierUniform at a fresh path and removed only this task’s failed generated coat material. Original Fab assets untouched. Final source selects M_PA250_SoldierUniform; build5 succeeded. Render retry pending.
- Preview harness fixes: UE 5.8 exposes EditorPythonScripting (ScriptName, without Library suffix); screenshot preparation pumps Slate recursively, so preview now holds the screenshot task and waits for completion before quitting. Output is an absolute path under Saved/PA250.

- Final rendered QA: preview4 exited 0. Visually inspected Saved/PA250/PA250_FabSoldiers.png: imported period models and textured Horizon muskets, distinct blue/red uniform trim, no Quinn/cube hats/floating cones. M_PA250_SoldierUniform has no Metal shader compilation failure in the rendered log. The image is a static editor comparison in Lexington, not a combat-animation playtest.
- Remaining animation scope: supplied pack locomotion (idle/walk/run) is selected at runtime. Dedicated shoulder-aim, firing and body reload poses are not implemented by this visual hookup; the weapon pack’s mechanical animations were not retargeted to the human skeleton.
- Existing rendered warnings recorded, left outside this fix: native soldier skeleton references obsolete /Engine/EngineMeshes/Humanoid retarget metadata; MI_PA250_Yarrow has an existing Metal sampler-type warning; KiteDemo legacy physics warnings; Mac idevice_id helper has a Bad CPU type warning. No original content was saved.
- Final changed/new files: RF/PA250ThirdPartyAssets.h, RF/RFCharacter.{h,cpp}, RF/MusketWeapon.cpp, PA250VisualsCommandlet.{h,cpp}, tools/ue/{build_soldier_visuals,preview_soldier_visuals}.py, new Content/PA250/Materials/M_PA250_SoldierUniform.uasset. No simulation/services/engine edits or git mutations. Restart any previously open Editor to load the rebuilt C++ library.

- Final cache fix: retry weak soldier-mesh lookups after PIE teardown/garbage collection instead of retaining a one-time bResolved flag. This prevents later battles falling back to Quinn after the cached mesh is collected. Build6 succeeded; final PA250Visuals run uses the rebuilt library.

- Final verification completed: latest Build6 + PA250Visuals commandlet exited 0; 12 PASS, failures=0, no Error: lines. RuntimeVisualVerify.log updated at 2026-11-08 10:51 EST. Rendered comparison verified at Saved/PA250/PA250_FabSoldiers.png.

## 2026-11-08 — Stricter checks + battle feel (push, morale callouts, aim by training)

### Stricter tests (previously "never faster" / "no flicker" only)
- Reload: full stress makes every training ≥ 15% slower; moving ≥ 10% slower. Measured (500 seeds):
  - Veteran: 4.13 / 3.54 vs 2.65 s;
  - Trained: 4.48 / 4.40 vs 3.17 s;
  - Recruit: 5.21 / 4.82 vs 4.30 s.
- Morale responsiveness: sustained heavy fire shakes a steady squad in 0.1 s; it steadies 3.1 s after the pressure lifts; overwhelming pressure routs a Trained squad in 0.1 s.

### Feel / gameplay
- **Callouts were never displayed.** `UPA250VolleyCoordinator::GetCallout` had no reader, so "Make ready! / Present! / FIRE!" never reached the screen. Now shown on the HUD, high centre, for squads within 60 m of the camera.
- **Morale you can read:** shouted lines on morale changes near you. Ours: "Steady, lads!", "Fall back - in order!", "The line is breaking - run!", "Rally! Form on me!", "Line re-formed!". Theirs: "They're falling back!", "They're running!". Every rout also goes to the kill feed ("Enemy militia rout!" / "Our Regulars are routing!").
- **Lines press for the objective:** a Steady squad in contact advances on its post between volleys (bayonets at < 10 m) when the enemy in front is routing, withdrawing or rallying, or when it outnumbers the visible enemy 1.5:1 at > 60% strength. Callout "Forward! Push them off the ground!".
- **Bot aim error now comes from training** (Veteran 2°, Trained 2.5°, Recruit 3°) instead of unit class, and now applies to squad volleys too (previously volleys used only the weapon spread).

### Measured (`RUNS=2 scripts/run_slice_autoplay.sh`, real L_Lexington battles)
| | before | after |
|---|---|---|
| post flips per battle | 0–1 | 3 and 3 |
| winner | Continental (attrition / bleed) | Continental in run 1, British in run 2 |
| shots < 20 m (bayonet range closes) | 0 | 0 in run 1, 66 in run 2 |
| shots in the 40–80 m band | 87–88% | 85% / 72% |
| crowding at the end | 0–7% | 20% / 0% |

### Verification
- `BUILD` Succeeded. `-run=PA250Battle` checks=170 fails=0. Automation `PA250.Battle+PA250.Combat` 13/13. `RUNS=2` slice autoplay PASSED.
- Not verified: how it plays in PIE (callout timing and readability, whether the pushes feel earned).

## 2026-11-08 — Musket points forward + weapon-pack animations

### Problem (measured, `-run=PA250Visuals` with the idle actually ticking)
- The musket snapped to `hand_r` with zero rotation, so the barrel followed the hand bone's X axis. In the pack idle that axis points straight up (Z 0.98): barrel vertical, muzzle ~2.1 m off the ground and behind the soldier. Walk/run arm swing turned the barrel too.
- The earlier rendered comparison showed the reference A-pose (editor worlds don't tick animation), so the bug wasn't visible there.
- The weapon pack has only two animations, `AS_HS_WP01_MusketRifle_Firing` (0.4 s) and `_Reloading` (4.2 s). Both are **mechanical** (hammer, ramrod) on the musket's own **Y_0** skeleton. Neither pack has body aim/fire/reload animations; the soldier pack has only idle/walk/run/jump.

### Fix (`RF/MusketWeapon.*`, `RF/PA250ThirdPartyAssets.h`; RFCharacter unchanged)
- Barrel rotation follows the owner's `GetBaseAimRotation()` (camera for the player, AI focus for bots), with pitch clamped to ±45° and roll 0. The right hand only carries the position (grip at `b_gun_Trigger`).
- The weapon uses skeletal `SK_HS_WP01_MusketRifle_Y_0`, laid out from its own bones (trigger → `b_gun_Fire` along +X, 150 cm long). `MuzzleLocation` = `b_gun_Fire`. The static mesh stays as a hidden fallback.
- `Firing` plays once on each shot. `Reloading` is then **scrubbed by the reload sequencer** (three drill phases mapped evenly over 4.2 s). It holds during a fumble delay or jam and rests at the loaded pose, so it can't disagree with the HUD. Visual only; no gameplay timing changed.

### Verification
- `-run=PA250Visuals`: 25 PASS, failures=0. Measured barrel·facing = 1.000 (also for a soldier turned 90°). Muzzle 104 cm in front at 100 cm height (was behind, ~210 cm). Hammer above the barrel on the right. Firing/Reloading assets play.
- `-run=PA250Battle` checks=170 fails=0; `-run=PA250Combat` fails=0. `scripts/run_slice_autoplay.sh` PASSED (real L_Lexington battle).
- Automation `RunTests PA250.Battle+PA250.Combat` exited without output this time (the known Mac crash in that runner); commandlets cover it.
- Builds use `-NoHotReload` while an editor is open; otherwise UBT links `-0001.dylib` and commandlets load the stale library.

### Still open
- **No PIE / rendered check of the new pose.** It looks like a low "hip/ready" carry: the arms still play unarmed idle/walk/run, so the left hand isn't on the stock and there is no shoulder/present pose. That needs body animations for a musket (aim, fire, reload) or IK, which neither pack contains.
- Restart the Unreal Editor to load the rebuilt library.

## 2026-11-08 — Battle audio: gunfire, ball whiz, impacts, reload, melee, shouted orders

### Assets (all new; none existed before)
- **SFX, synthesized and original** (`tools/audio/synth_sfx.py`, numpy + scipy from the negotiation venv, seeded so it's deterministic). 18 WAVs:
  - `musket_fire_1-4`: flint snap → pan whoosh → boom + crack + rolling echo;
  - `musket_distant_1-2`;
  - `ball_whiz_1-3`: Doppler hiss;
  - `impact_body_1-2`, `impact_ground_1-2`;
  - `ramrod_1-3`;
  - `flint_click`, `melee_swing`.
- **Voices, ElevenLabs** (`tools/audio/gen_voice_lines.py`): the existing key from `services/negotiation/.env`, read and never printed. 28 lines, 14 per side, ~544 characters billed. Existing files are skipped unless `--force`.
  - British: George (officer), Daniel (cries). Continental: Adam (officer), Harry (cries).
  - Lines: make_ready, present, fire, forward, enemy_advancing, steady, fall_back, enemy_falling_back, breaking, enemy_running, rally, reformed, charge ("Huzzah!"), charge_b ("At them, boys! Charge!").
- Source WAVs live in `unreal/PlayAmerica250/SourceAudio/`. `Scripts/import_audio.py` imports them to `/Game/PA250/Audio/{SFX,Voice/<side>}` (46 assets).

### Playback (`Battle/PA250BattleAudio.*`)
- Banks are found by name (`<bank>_N` variants), with random variant and pitch. Runtime attenuation by range:
  - gunfire: falloff 400 m, low-pass muffled with distance;
  - voices: 60 m;
  - near sounds (ramrod, swing, impacts): 25 m;
  - whiz: 12 m.
- **Musket shots:** the distant variant is used when the listener is more than 60 m away. Each 80 ms window starts at most 5 shot sounds, so a volley reads as a volley instead of clipping.
- **Hooks:**
  - `AMusketWeapon::Fire`: shot (unless a `FireSound` asset is set).
  - Each finished reload step, player and AI: ramrod. Jam: flint click.
  - Projectile hit: body or ground impact. A ball passing within 5 m of the camera, not your own: whiz.
  - Thrust / butt-stroke: swing. Melee hit: body thud. Charge: the side's charge cry.
  - Squad callouts are voiced through `Shout`. Orders are spoken by that squad's officer at its line, so you hear the enemy's "Make ready!" from their line. News about the enemy ("They're running!", "They're falling back!", "Their line is advancing!") is shouted by your side near the camera.
  - The same line from several squads within 1.5 s plays once.

### Verification
- `BUILD` Succeeded. `-run=PA250Battle` checks=186 fails=0. New `PA250.Battle.Audio` checks:
  - every bank and its variants resolve;
  - all 28 lines are present (shortest 0.43 s);
  - a shot, three ramrod steps, a jam, a swing and a charge cry each trigger;
  - an 8-shot volley → 5 sounds.
- Slice autoplay PASSED with the audio hooks active.
- **Not verified:** how it actually sounds. Headless runs have no audio device; listen in PIE, or preview the WAVs with `afplay unreal/PlayAmerica250/SourceAudio/sfx/musket_fire_1.wav`.

## 2026-11-08 — Slower reloads (10 s / 15–45 s), squad bayonet charges for both sides, easier player charge

### Reload timing (user-chosen gameplay settings, `UPA250WeaponTuning` constructor)
- **Arcade (default):** 7 s perfect, 10 s untouched, 14 s fumbled every step (was 2.5 / 4 / 6). No jams.
- **Authentic:** the user's 15–45 s range — 15 s drill-perfect, 30 s untouched, 45 s fumbled; 10% jam on a miss; 2.5 s to clear.
- The rules are the same; only the step lengths changed. `DA_WeaponTuning` follows the constructor (verified by the asset checks).
- AI stress now raises fumbles more: `(1 - skill) × (1 + 2 × stress)`, plus `0.6 × moving penalty`. With a 10 s reload, a calm Recruit already sits near the untouched time, so stress had little left to slow down. Calm behaviour is unchanged.
- Measured (500 seeds, calm / full stress / moving):
  - Veteran: 7.35 / 10.37 / 10.43 s;
  - Trained: 8.66 / 11.44 / 11.44 s;
  - Recruit: 10.92 / 13.61 / 12.55 s.

### Squad bayonet charges — both sides, same rules (`PA250ShouldOrderCharge`, no faction input)
- **Line squads** (British Veteran / Line and Continental Trained / Line) charge an enemy within 35 m when they outnumber him 1.25:1, or catch him mostly reloading while their own muskets are mostly empty.
- **Cautious roles** (Light, Irregular = militia) charge a wavering enemy, or a reloading one only when they also outnumber him 1.2:1.
- Any side charges a wavering enemy (routing, withdrawing or rallying). Only Steady squads charge.
- A charge lasts up to 12 s; 10 s rest between charges. The squad's officer shouts "Charge! At them!" (voiced). Men sprint at the nearest enemy in sight, lunge within 6 m and thrust within reach.
- Being charged adds +0.3 morale pressure, so charges break green lines.

### Player charge made easier
- New **G** key (no sprint needed); Shift+E still works.
- Dash 6 m over 0.6 s (was 4 m / 0.45 s), bayonet sweep radius 70 cm (was 50), reach 1.7 m.
- Charge assist: snaps the dash (and the camera yaw) onto the nearest visible enemy within 9 m and 40° of facing.
- Whiff recovery 0.5 s.
- HUD: a red ">> Enemy in reach - press G to CHARGE! <<" when the assist has a target; legend updated.

### Verification
- `BUILD` Succeeded. `-run=PA250Battle` checks=198 fails=0. Automation `PA250.Battle+PA250.Combat` 14/14.
- New checks:
  - preset timings 7.01 / 10.01 / 14.02 and 15.01 / 30.01 / 45.01;
  - assist picks the enemy in front, not one at 90°, and not a friend; the charge turns him onto the target (26.6°);
  - charge-order rules for line vs cautious roles, Shaken and range;
  - being-charged pressure;
  - British and Continental line use the same charge rules.
- Slice autoplay:
  - 1 run PASSED (British win, 3 post flips; trace `british.Charge` 2, `continental.Charge` 1);
  - a separate `RUNS=2`: run 2 PASSED. Run 1 exited before writing a trace while another session's UnrealBuildTool was compiling (module unavailable). An earlier `RUNS=2` also failed while that session was editing the game mode; its failing lines were not captured.
- Not verified in PIE: whether G-charges feel good, charge-assist snapping, and the pacing of 10 s reloads.

## 2026-11-08 — Capture posts: flags that lower/raise + objective markers

- Camera pitch report: it came back after an editor restart; no code cause found (`GetBaseAimRotation` is read-only). The PIE log showed a normal possessed soldier.
- **Flagpole at every post** (`APA250ControlPoint`): 9 m pole, stood on the ground by a trace (the actor floats 1.4 m up; the old ring disc sits a full radius below the actor, inside the terrain). The flag follows the existing two-stage capture:
  - held → holder's flag at the top;
  - enemy neutralising → it lowers with progress;
  - neutral → the capturer's flag rises;
  - the flag always lowers fully before it changes colour;
  - a defender stepping back in sends it up again.
  - Pure `FlagTarget()`. Capture logic still steps at 0.1 s (an accumulator replaces `TickInterval`) so the flag can animate every frame.
- **HUD** (`UPA250BattleHUD::NativePaint`):
  - letter boxes A/B/C (sorted post ids: Buckman, Green, Meetinghouse) under the score bar, in the holder's colour, with a capture fill;
  - a world marker over each flagpole (letter, capture bar, distance or IN ZONE), pinned to the screen edge when off-screen;
  - an order per post via pure `PostOrder()`: ATTACK (enemy or neutral), DEFEND (ours under capture), CAPTURING, CONTESTED (pulses), or nothing when ours is secure.
  - Faction colours (British red / Continental blue) match the uniforms, not friend/foe.
  - In-zone text now names the post and the action: "Lowering the enemy flag at B  Lexington Green 40%".
- Verified: `-run=PA250Battle` checks=208 fails=0 (10 new flag/marker checks); slice autoplay PASSED with post_flips=5.
- **Not verified visually:** no PIE or render of the flags or markers yet. Restart the editor to load it.

## 2026-11-08 — Authentic reload is the permanent default
- `APA250BattleGameMode::ReloadPreset = 1` (Authentic), `UPA250WeaponTuning::Values.DefaultPreset = Authentic`, `AMusketWeapon::ActivePreset = Authentic`. No map or config overrode them. Arcade (~10 s) is still available by setting `ReloadPreset = 0`.
- Player: 15 s drill-perfect, 30 s untouched, 45 s fumbling every step; misses can jam (hold R 2.5 s to clear).
- `BUILD` Succeeded. `-run=PA250Battle` checks=209 fails=0, including the new "Authentic reload is the default (game mode, musket, tuning)". Slice autoplay PASSED.
- Real battle (L_Lexington, headless, 208 s):
  - AI reloads: Veteran mean 19.8 s (15.3 – p90 24.8); Trained 29.3 s; Recruit 33.1 s (22.4 – p90 40.0).
  - Charges: British 6, Continental 1. Continentals routed 9 times. 3 post flips, British win.
- Note: this battle's Continentals are mostly militia (Recruit / Irregular, cautious chargers: 18 militia vs 4 line in `boston1775.json`), so they charge far less than the British regulars.

## 2026-11-08 — Volley harden review fixes (coordinator only)
- Aligned ready and discharge on one target: `ResolveVolleyTarget` returns only the squad `Contact` (in engage range, seen by that man). `SafeReady` counting and `ScheduleVolley` now use the same formed+loaded+clear-lane shooter list; the old per-man nearest-enemy search in `ScheduleVolley` is gone, so a man counted ready cannot discharge at a different enemy.
- `HasClearFiringLane`: a different living enemy in the lane is explicitly a hit, not a block (the intended target is trace-ignored); living friendly or world geometry still blocks.
- Pending shots now also require a living target (`ShouldDischargePending` gained `bTargetAlive`); a new schedule bumps `VolleyEpoch` before replacing pending shots; the lone-soldier (`Live < 2`) cancel no longer bumps the epoch every tick.
- Verified: `BUILD` Succeeded; `-run=PA250Combat` fails=0; `-run=PA250Battle` checks=242 fails=1 — all volley/lane checks pass (incl. new dead-target and different-enemy cases); the one fail is the unrelated level check "battle road: walls flank the British approach", not touched by this change.
- `DA_BotTuning` note: no serialized `VolleyFireJitterSeconds` in the asset strings; the field loads from the struct default (0.1 s) until the asset is next saved in the editor. Tuning for volleys: `VolleyReadyFraction` 0.7, `VolleyMaxWaitSeconds` 6 s, `VolleyFireJitterSeconds` 0.1 s.
- Not verified in PIE: two opposing Line squads volleying, charge/rout mid-jitter with `pa250.BotSpacingReport` capture.

## 2026-11-08 — Real flags that wave

- **Artwork** (`Battle/PA250FlagArt.*`), drawn at runtime into transient textures (2×2 supersampled), so nothing is imported:
  - British: the 1707 Union flag. St Patrick's saltire only arrives in 1801.
  - Continental: the New England Pine Tree flag of 1775.
  - Pure `Pixel()`; HUD colours stay faction red/blue.
- **Cloth:** a `UProceduralMeshComponent` grid (16×8 segments, 180×90 cm) driven by pure `ClothOffset()`. The ripple travels away from the pole, grows toward the fly and droops. It is re-shaped only while recently rendered. The pole is wood-coloured with a gilt ball on top.
- **New:**
  - engine plugin `ProceduralMeshComponent` enabled (uproject + Build.cs);
  - `M_PA250_Flag` (two-sided, texture parameter `Flag`), made by `tools/ue/build_flag_material.py`;
  - plain faction colours as fallback if the material is missing.
- **Preview:** `-PA250Preview=<label>` adds `flag_green` (the Green set to British for the shot) and `flag_meetinghouse` stills, with the HUD. Rendered and inspected (`docs/preview/lexington_flags_flag_*.png`): both flags wave, letter boxes and world markers show.
- **Tests:** `-run=PA250Battle` — all flag, flag-art, cloth and marker checks pass. The only failure is `battle road: walls flank the British approach`, from another session's concurrent level work (it was editing `build_levels.py` / `L_Lexington` and adding those tests during this change), not from the flags.
- **Follow-ups:**
  - The Meetinghouse post stands against the building, so its flag flies at eave height close to the wall. Move the post a few metres off the building in `build_levels.py`.
  - Edge-pinned markers now start lower (MinY 215) to clear the callout line; this last tweak wasn't re-rendered.

## 2026-11-08 — Battle Road: walls as cover + militia muster (user picked options 1 + 2)

Context: Lexington Green was a British win (77 militia vs ~240 light infantry). 19 April 1775 as a whole was an American success: militia companies converged all day and fired from walls along the Battle Road. British losses ~273 vs ~93.

### Map (`Scripts/build_levels.py`, builder `lexington_battle_road`, idempotent; also called by `build_lexington`)
- Six new dry-stone walls (`BattleRoad_*`):
  - two parallel to the Boston road, ~17 m north and south;
  - four cross walls facing the British approach, between x 1800 and 6500.
- All stone-wall collision (1.1 m: chest covered, shoulder clear) is tagged `PA250Cover`: 152 segments, 76 between the Green and the British start.
- Three `continental_muster` PlayerStarts: north, south, west.
- The navmesh is runtime-dynamic, so it routes around the new walls. Applied to `L_Lexington.umap`; everything else in the level is untouched.

### Skirmishers use the walls (`UPA250VolleyCoordinator::PickCoverLine`)
- Roles that prefer cover (Light, Irregular) take a line 1.5 m behind the nearest tagged wall within 50 m of their post. The wall must lie toward the enemy, or no more than 15 m behind the post; each squad takes a different wall, ≥ 15 m apart.
- The per-man cover query then finds firing positions along it.
- Skirmish roles answer an incoming bayonet charge with an ordered withdrawal (and no charge-shock morale pressure), then return to their wall after the cooldown. Line troops still feel the full shock.

### Militia muster (`DA_BotTuning`, faction-neutral: applies to any side's militia; GAME INVENTION)
- 50% of a side's militia places start on the field.
- A company of 4 arrives every 15 s at a random muster point. The kill feed shows "Militia company arrives from the north! (+4)".
- Militia grow up to 2× their share. Men in the field never exceed the side's remaining tickets.
- Tickets are untouched (spent on death), so `BattleResult` losses still never exceed the pools.
- Lexington 1775 setup (4 line + 18 militia units, 16 places): Americans start with 3 line + 7 militia vs 16 British, and militia grow to 26. `trace.muster_arrivals` added.

### Verification
- `BUILD` Succeeded. `-run=PA250Battle` checks=242 fails=0. New `PA250.Battle.BattleRoad`:
  - the map has ≥ 12 tagged wall segments on the approach (76 found) and 3 muster points;
  - cover-line picks (forward wall, own side, distinct per squad, none out of reach);
  - muster plan and wave caps (cap, ticket limit, no tickets → no arrivals).
- `RUNS=2` slice autoplay PASSED.
- **Balance result: the British still win.**
  - First tuning (35% start / 20 s waves / 1.6×): British wins with 56 tickets left vs 0.
  - After skirmishers fall back from charges and the heavier muster: British wins with 42 left vs 0. 24 militia arrived; militia withdrew 23 times and routed 6; British routed 2 and charged 8 times.
  - The Americans run out of tickets first. Militia are Recruits (slower reload, worse aim), which cancels much of their numbers. The British take posts early and bleed them.
- Not verified in PIE: militia actually hugging the walls, and how the walls look on the Battle Road.

## 2026-11-08 — Flag fixes (Meetinghouse pole, marker overlap)

- **Clear-spot pole placement** (`APA250ControlPoint::FindClearPoleSpot`): at BeginPlay the pole and its flag need 2.5 m of open air from WorldStatic scenery, searching the post, then rings of 8 at 2.5/5/7.5 m (deterministic). Only the pole moves; the capture zone, slots and HUD distance stay on the post. Logged once per post. Result in L_Lexington: Green and Buckman stay on the post; the Meetinghouse pole moves 250 cm off the wall.
- **Bug found:** `FCollisionObjectQueryParams(ECC_WorldStatic)` queried nothing, because `ECC_WorldStatic` is 0 and resolved to the int32 bitfield constructor. Every spot looked clear. Fixed with `AddObjectTypesToQuery`.
- **HUD:** edge-pinned markers start at local Y 215, below the score bar, letter boxes and callout line.
- Fixed without touching `build_levels.py` / `L_Lexington` (the Codex session is working there).
- Verified:
  - re-rendered `docs/preview/lexington_flags_flag_meetinghouse.png`: Pine Tree flag clear above the eave, markers clear of the callout;
  - `-run=PA250Battle` checks=249 fails=0.

## 2026-11-08 — Trained minutemen + ducking behind walls (and a 4-battle measurement)
- `DA_CombatProfiles`: continental / militia → **Trained** / Irregular (was Recruit). Minute companies drilled; many men were French & Indian War veterans.
- **Ducking:**
  - Crouched capsule half-height 50 cm, so a crouched man is 1.0 m tall, under the 1.1 m wall collision.
  - Player: hold **Left Ctrl** to duck. Firing always rises first (a crouched musket would only hit the wall).
  - Bots at a covered firing position duck unless loaded with a target in sight (`PA250ShouldDuck`): down to reload, up to fire. Never in the open, on the move, routing or charging.
  - No crouch animation exists; the mannequin's mesh sinks with the capsule (stopgap).
- Checks: `-run=PA250Battle` 249/249. New tests:
  - a crouched man is hidden behind a 1.1 m wall while a standing man's head shows (real traces);
  - duck rules;
  - the minutemen assignment.
- **Four full L_Lexington battles** (slice autoplay, each PASSED; collected one at a time because another session kept rebuilding the module):

| battle | winner | tickets left (B / C) | militia arrived | post flips | charges B / C | routs B / C | militia withdrawals | bleed ticks |
|---|---|---|---|---|---|---|---|---|
| 1 | British | 32 / 0 | 22 | 3 | 12 / 2 | 3 / 4 | 19 | 37 |
| 2 | British | 58 / 0 | 20 | 5 | 9 / 2 | 1 / 5 | 22 | 38 |
| 3 | British | 49 / 0 | 24 | 5 | 9 / 9 | 1 / 2 | 22 | 28 |
| 4 | British | 28 / 0 | 23 | 9 | 11 / 3 | 2 / 5 | 29 | 43 |

- Reading: 28–43 tickets per battle go to post bleed. Under Conquest rules the side holding more posts bleeds the other, and the militia's cover-and-fall-back play keeps them off the posts. The scoring, not the firefight, decides it.
- Muster tuning (50% / 15 s / 2×) was not raised further.

## 2026-11-08 — Decision: Lexington stays a likely British win
- The user chose to accept the result: Lexington Green (dawn, ~77 militia vs ~240 regulars) was historically a British win. The Battle Road walls, militia muster, trained minutemen and ducking stay in, since they make the fight closer and play the way the militia fought. No Lexington-specific objective or bleed change.
- Revisit if later battles (e.g. a Battle Road / Concord scenario) need the "hold then march out" objective; it was designed but not built.

## 2026-11-08 — Stone walls fixed + Patriot cover facing the Green

- **"Half-transparent" walls were floating stones.** `place_stone_wall` placed each KiteDemo rock by its centre at a fixed lift. River rocks are flat, so the courses hovered with daylight under them, and 65 cm spacing plus ±15° tilt left see-through gaps.
  - Fix: every stone is seated on the ground or on the stone below, using its real scaled height (sunk 14/9 cm), tilted at most ±4°, every 48 cm.
  - Courses stack until 1.05–1.1 m, the height of the invisible 1.1 m cover box, so what you see matches what blocks; the cap course keeps ~20% gaps.
- **The incremental builder made invisible walls.** `build_lexington_battle_road()` never called `flush_scatter()`, so it added only the hidden collision boxes. It now flushes. Battle Road / Patriot stones live in their own scatter groups (`brwall_`, `ptwall_`), and the builder removes those plus `BattleRoad_*`, `PatriotCover_*` and `Muster_*` before re-placing, so reruns are idempotent.
- **Patriot cover (GAME INVENTION)** in `build_levels.py`:
  - `PATRIOT_WALLS`: three 16 m stone walls 38–48 m from flag B, each turned to face the Green.
    - W1 due west, W2 west-northwest with a gap between them;
    - SW between Buckman Tavern and the Green.
  - `PATRIOT_TREES`: 10 trees 30–64 m from the Green, kept out of the walls' lines of fire.
  - Spots were searched offline with the level's own `keep_clear` (roads, building pads, posts) plus fences, existing trees and the Continental spawn cluster.
  - Nothing fits on the Meetinghouse side (road + pad). All new walls are tagged `PA250Cover` automatically.
- L_Lexington rebuilt with `PA250_LEVELS=lexington` (full build).
- **Preview:** `-PA250Preview=<label>` adds `wall_east` (close-up) and `wall_patriot` (behind W1, looking at flag B). Inspected: solid seated walls; a chest-high wall with the Green and flag in view.
- **Verified:**
  - `-run=PA250Battle` checks=249 fails=0 (cover segments 152 → 170; all Battle Road checks pass);
  - `RUNS=2` slice autoplay PASSED (one Continental win, one British win; post_flips=5).
- An editor that already had L_Lexington open holds the old map. Reload it without saving, or the rebuilt map gets overwritten.

## 2026-11-08 — Post control matters more; hit feedback for the player

### Tickets
- Not a bug: this battle's pools come from the campaign armies at that moment. British 24 line units → 96 tickets; Americans 2 line + 8 militia → 40 tickets (×4 soldiers per unit).
- **Bleed now scales with post control (Battlefront-style):** the side holding fewer posts loses, per 5 s, 1 ticket per post of difference, +1 when the other side holds every post. Holding all 3 = 4 per 5 s = 48 a minute; it was a flat 12 a minute.
  - `FPA250TicketLedger::BleedAmount`; the legacy flat 1 is kept when the caller passes no post count.
  - The other session's scenario switch (`UsesPostBleed`, e.g. Battle Road: no bleed for ground) is untouched.
- The HUD score bar shows the bleed next to the losing side, e.g. "BRITISH 71 (-4/5s)".

### Hit feedback (local player only)
- Hitmarker: a large X centred on the crosshair, white for a hit, red for a kill, fading over 0.45 s (kills 0.7 s). Text under it: "HIT -78" or "KILL 68 m". It was a small "+" 40 px below the crosshair, easy to confuse with the crosshair itself.
- Sounds (2D, synthesized): `hit_confirm` (two bright blips), `kill_confirm` (low thump + chime). Melee hits use the same path.
- Impact marker: wherever the player's own ball lands, a glowing dot shows for 1.5 s (white on a miss, bigger and red on an enemy), so misses show where the shot went.

### Verification
- `BUILD` Succeeded. `-run=PA250Battle` checks=269 fails=0. New checks:
  - bleed amounts 1 / 2 / 4;
  - 48 tickets a minute when holding all posts;
  - bleed floors at 0;
  - confirm sounds exist;
  - impact markers spawn with a 1.5 s life (larger on a hit).
- Slice autoplay PASSED: British win; 23 bleed ticks; 83 s of game time; British finished with 61 tickets.
- The other session fixed the same `HitText` shadowing warning in `PA250BattleHUD.cpp` (renamed the lambda `HitLine`) while I was building.
- Not verified in PIE: hitmarker readability, sound levels, marker visibility at 80 m.

## 2026-11-08 — Black-powder smoke + Patriot homespun

- **There was no muzzle smoke before this.** `SpawnMuzzleSmoke` looked for Niagara assets under `/Game/ThirdParty/FX`, which doesn't exist.
- **Smoke** (`Battle/PA250Smoke.*`, `UPA250SmokeSubsystem`, a world subsystem; game/PIE worlds only, skipped without a renderer so commandlets and autoplay are unaffected):
  - Each shot blows out 3 puffs along the barrel. The jet decays in about a second, then puffs drift on a light breeze (70, 30 cm/s), rise slowly, swell from 0.6–1.5 m to 4–6 m and fade over 22–32 s. Cap 240 puffs, oldest first.
  - All puffs are one `UProceduralMeshComponent` of camera-facing quads, sorted back to front each frame (one draw call).
  - `M_PA250_Smoke` (`tools/ue/build_smoke_material.py`): translucent; vertex colour = colour/opacity, a soft disc broken up by world-space noise, depth fade into the ground.
  - Pure `FPA250SmokePuff::Step/Size/Opacity`.
- **Homespun:** 1775 Patriots (militia, minutemen and the new army) wore their own clothes. Every Continental-side soldier gets a seeded coat from 8 undyed/vegetable-dye wool colours (brown, drab, grey, butternut, dull green, olive drab, dark brown, undyed) with ±15% shade, plus a waistcoat/breeches colour (buff, tan, linen, brown, grey).
  - The palette never includes red (checked over 200 seeds), so red still means a regular at a glance. HUD and flag colours stay blue for the faction.
  - New `M_PA250_SoldierUnder` (mesh slot 0, from `M_american_s_armor2skin`) via `tools/ue/build_soldier_visuals.py`, which now builds both clothing materials. Fab originals are never saved.
  - Coat tint boost: ×3.5 homespun, ×3 regulars (was ×2, which left coat bodies almost black).
- **Preview:** `-PA250Preview=smoke` fires three scripted 15-man volleys west of the Green and captures `smoke_fresh` and `smoke_bank` (+15 s). Preview labels `flags` / `walls` / `smoke` now capture only their own shots.
  - Inspected: a thick bank after the volleys; 15 s later it has drifted downwind and thinned, veiling trees.
  - Soldier comparison (`Saved/PA250/PA250_FabSoldiers.png`): one regular and four varied Patriots, no blue.
- **Verified:** `-run=PA250Visuals` failures=0 (new homespun and smoke checks; the old "Continental blue" check was replaced by "regulars red, Patriots never red"); `-run=PA250Battle` checks=272 fails=0; slice autoplay PASSED.
- **Limits / not done:**
  - The soldier model is cut as a military coat (lapels, cross-belts); recolouring can't make it a civilian frock coat or hunting shirt. That needs another character model.
  - A thin dark straight line on the ground about 10 m from the smoke-preview camera (seen in both smoke stills, unrelated to the smoke) is not investigated.
  - Another full UBT build (not this session's) ran at 14:38; any game launched during it fails with "ConflictingInstance".

## 2026-11-08 — Lexington visual review

- Smoke draw cost: puffs past 70 m are not drawn (they keep moving), aged puffs within 1.6 m merge once the bank is thick, and the mesh rebuilds on alternate frames unless a new shot just fired. Muzzle flash and the shared breeze were already in from the other session.
- Left hand is solved onto the musket's `b_gun_LeftHandSpot` each frame. The body clips are still unarmed idle/walk/run. Not seen in a rendered shot.
- Homespun seed is stored on the soldier and reused when he respawns, including the player. Slot 0 is the pack's pants/waistcoat/boots material; skin, face and hair are separate materials and are not tinted.
- Patriot wall and tree spots are checked in `build_lexington` and `build_lexington_battle_road` against roads, pads, posts, the four Common fences and the meadow trees. Stones seat on the lower ground sample under the rock. The incremental builder was not run (another game had L_Campaign open).
- HUD markers stay 220 px in from the right so they clear the kill feed. The 215 px top inset is the same slate gap at 720p, 1080p and 4K because the callout is a fixed offset, not a fraction of the screen. Not re-rendered.
- The thin dark line in the smoke stills sits where the road edge crosses the view, about 2.5 m beside that camera, on a map with virtual shadows on. Not changed.
- Smoke does not block line of sight. The Pine Tree flag is still the Continental colours.
- Build: first link failed because the march block declared a second `Now` (`-Werror,-Wshadow`). That declaration now uses the `Now` already in the function. Rebuild succeeded (compile `PA250VolleyCoordinator.cpp`, link).
- `-run=PA250Visuals` failures=0. `-run=PA250Battle` checks=272 fails=0. `scripts/run_slice_autoplay.sh` PASSED (Continental win).
- `-PA250Preview=smoke` did not capture. The bare `UnrealEditor` binary exits and relaunches the `.app`, and that launch died with SIGBUS in the Mac cursor image load while another UnrealBuildTool mutex was already held. No new stills.

## 2026-11-08 — Concord kicked back to map (tickets_exhausted)
- **Symptom:** North Bridge / Concord fight returned to campaign after ~30s, then rematches ended instantly. Log: `PA250 battle over: BRITISH victory (tickets_exhausted)` with `flips=2 bleed=0`; rematches same frame as `scenario_preset=concord_bridge`.
- **Cause:** stores gating could leave Continental at **0 tickets**. `CheckTicketExhaustion` treated `Initial==0 && Alive==0` as out on the first Tick (before spawn). Not a map travel bug; not the skinning assert (separate, earlier crash at 18:02).
- **Fix:** floor gated tickets to at least 1 unit while the army still exists; exhaustion for zero-ticket sides only after `EverFielded`; clear scenario `DebugSetHolder` flip flags so setup is not counted as captures. Log starting ticket pools.
- Sim tests updated; UE Mac rebuild left to the player (active build).

## 2026-11-08 — Skinning crash + mouse/trackpad look

- **Crash** (`Assertion failed: IsRotationNormalized()` in `UpdateRefToLocalMatricesInner`, three reports, one a frame after a respawn): `AMusketWeapon::PoseSupportHand` const-cast the body's `GetComponentSpaceTransforms()` from an actor tick and swapped the array (`CS = MoveTemp(Posed)`). On a freshly spawned or re-meshed soldier that ran before its tick prerequisite took effect, racing the parallel animation task.
  - New `UPA250SoldierAnimInstance` (native, `RF/PA250SoldierAnimInstance.*`) plays the pack idle/walk/run and solves the left arm inside pose evaluation (`FPA250SoldierAnimProxy::SolveSupportHand`, `AnimationCore::SolveTwoBoneIK`). The musket now only publishes a component-space target; nothing writes bone buffers from game code.
  - Measured: the stock spot is 76 cm from the shoulder and the arm reaches 52 cm in the unarmed idle carry, so the straightened arm stops 24 cm short (same as before). Hand on the stock needs a musket carry/aim body pose.
- **Look dead on trackpad**: look deltas only reach `IA_Look` while the game viewport has mouse capture (`FSceneViewport::OnMouseMove`). PIE starts without it (`GameGetsMouseControl=False`), and only a click inside the viewport captured. Moving (WASD) now re-takes capture through `FInputModeGameOnly` when the viewport has focus and no UI cursor is shown (`ARFCharacter::EnsureGameMouseCapture`; logs `PA250 input: ... took it`).
- Horizontal look ×0.6 relative to vertical (`ARFCharacter::YawLookScale`); left/right was too fast.
- Trackpad still too fast after that: default `pa250.LookSensitivity` 0.35 → 0.2 (horizontal stays ×0.6 on top). `[` / `]` still adjust and save it.
- Verified: build Succeeded; `-run=PA250Visuals` failures=0 (new: left arm solved, every body bone finite with unit rotation); `-run=PA250Battle` checks=272 fails=0.
- Not verified in PIE: that the crash is gone during real play, or the look feel on a trackpad. The commandlets run with `-nullrhi`, so GPU skinning (where the assert fires) never runs there.

## 2026-11-08 — Envoy voice end to end (Stage 1 done); talking portrait blocked on FAL_KEY

### What was broken
- **Every ElevenLabs request failed with HTTP 402.** `.env` `ELEVENLABS_VOICE_ID` is a *library* voice; this key's tier returns `paid_plan_required` for library voices via the API (Nigel `YHKw1MKBPnTTjuO4M0S4` also 402; premade Daniel/George 200). The `.env` value was left unchanged.
- **The failure was invisible.** `ElevenLabsProvider` swallowed errors and yielded a synthesized sine "fallback" as if it were speech. Its SDK call was synchronous (blocked the event loop), and its MP3 → PCM decode ran on top. The Nov 7 `pa250_*.wav` files are 22,050 Hz: the `say` path, not ElevenLabs.
- **Speech jobs died with the socket.** TTS ran inside the reply task in `provider_tasks`, which the control socket cancels on close (the room closes it on Accept/Leave). `_run` then killed `say`. Nothing was logged.
- **Unreal played WAVs through MediaPlayer** (`OpenFile` on a .wav). It had no open-failure handler, and `MediaSound` was attached to the GameMode, which has no scene root. Every automated run used `-nosound`, so playback was never exercised.
- **Muted when unfocused:** engine default `[Audio] UnfocusedVolumeMultiplier=0.0`. A `-game` launched from Terminal is silent until its window is clicked; the mixer still consumes samples. Project config unchanged; the voice test overrides it on its command line.

### What fixed it
- `diplomacy/avatar.py`: ElevenLabs over async `httpx` with `output_format=pcm_24000`, written straight to a 24 kHz mono 16-bit WAV (no decode). Envoy → voice table `diplomacy/envoy_voices.yaml`: `british` = Daniel (premade, British, formal) + `say -v Daniel` backup. `ELEVENLABS_VOICE_ID` is only the default. Logs `TTS done` (tts_backend, audio_seconds, path, render_s) at INFO. Logs `ElevenLabs TTS failed` (http_status, body), `say TTS failed` / `afconvert failed` (exit_code, stderr) and `TTS produced no audio` at WARNING. The key is never logged.
- `main.py`: the speech job is a detached task (`SPEECH_JOBS`) outside `provider_tasks`. The next utterance, the reply finishing or the socket closing cannot cancel it. It always logs `Speech job started` and then `Speech job finished` (delivered true/false) or a WARNING. `send_yaml` returns False for a closed socket.
- `tts/elevenlabs.py` (legacy WebRTC paths): `pcm_24000`, `sample_rate=24000`, `_convert_mp3_to_pcm` removed.
- `PA250DiplomacyGameMode`: `sadtalker_video_ready` without `video_file` → load WAV, parse with `FWaveModInfo`, queue into a `USoundWaveProcedural`, play via `SpawnSoundAtLocation` at the portrait. Logs `EnvoyVoice started <file> <seconds>` and `EnvoyVoice finished … played= wall= audible=`. "Finished" needs `GetAvailableAudioByteCount()` to reach 0, which only happens as the audio render thread pulls samples, so a `-nosound` run cannot fake it; a stall logs `EnvoyVoice stalled`. MediaPlayer is MP4 only (`OnMediaOpened` → `EnvoyVideo started`, `OnMediaOpenFailed` logs the path, `OnEndReached` → `EnvoyVideo finished`, swap back to `M_PortraitStill`). Each new line stops the previous WAV/MP4, so the two never play together.
- `-PA250VoiceTest`: opens the room, sends "I propose a ceasefire.", and exits 0 only after `EnvoyVoice finished` in real time (wall ≥ 0.9 × length) and audible ≥ 90 %. Result in `Saved/PA250/voicetest_result.json`. `-PA250VoiceTestVideo=<mp4>` substitutes a clip to test the MP4 path.

### Evidence (all run on this Mac, 2026-11-08)
- `scripts/speak_test.py`: ElevenLabs 24000 Hz mono 16-bit, 6.36 s; afplay rc=0, 7.36 s wall.
- `scripts/run_voice_test.sh` (real window, Metal, CoreAudio mixer, sound on): `EnvoyVoice started …wav 6.04 (24000 Hz, 1 ch, focus 0, unfocused volume 1.00)` → `EnvoyVoice finished … played=6.04s wall=6.29s audible=100%` → `PA250 voicetest PASSED`. Log shows **focus 0**: without the override this run would have been muted.
- MP4 path (`EXTRA_ARGS=-PA250VoiceTestVideo=<still+ElevenLabs WAV, H.264/AAC>`): `EnvoyVideo started … 6.01`, `EnvoyVideo finished … wall=6.90s (portrait on video material 1, MP4 voice peak envelope 0.161, WAV voice component idle)`, still restored, PASSED. The clip was a still image, so **lip motion is not verified**. `screencapture` was refused (no Screen Recording permission), so there is no screenshot.
- pytest `tests/diplomacy`: 108 passed (new `test_voice.py`: per-envoy voice, 24 kHz WAV, 402 → logged + `say`, `say` exit code logged, speech job survives socket close). `tests/test_tts.py`: same 3 pre-existing failures as before (they assert 16 kHz). ruff + mypy clean on `avatar.py`.
- `scripts/run_slice_autoplay.sh`: PASSED (headless `-nosound`; built together with another session's uncommitted Campaign edits).
- Not verified by ear: the run proves the mixer played the samples to CoreAudio in real time, not that a person heard them.

### Render time / cost per line (test runs)
- ElevenLabs TTS (Daniel, `eleven_multilingual_v2`): 0.73 s, 0.67 s, 0.81 s for 6.0–6.4 s lines (87 characters each). Average **0.74 s per line**. Cost: 87 characters of the plan's quota per line. The key lacks `user_read`, so remaining quota cannot be read.
- fal video: not run (blocked). No fal cost incurred.

### Stage 2 (talking portrait via fal): BLOCKED
- `FAL_KEY` is not in `services/negotiation/.env` or the environment, and `fal-client` is not installed in the service venv. Nothing in fal_talking, the chain reorder, the D-ID `/audios` upload, or `/v1/avatar/health` was started. The Unreal MP4 side is already in place and verified above.
- To unblock: add `FAL_KEY=…` to `services/negotiation/.env`, then resume Stage 2.

### Commands
- Service: `cd services/negotiation && .venv/bin/python -m uvicorn main:app --host 127.0.0.1 --port 8000` (`PA250_LLM=mock` for the scripted envoy; `PA250_AVATAR=none` for voice only).
- Voice, service chain + afplay: `services/negotiation/.venv/bin/python scripts/speak_test.py` (or `--url http://127.0.0.1:8000` against a running service).
- Voice in Unreal with sound (editor closed, no other game instance): `scripts/run_voice_test.sh`.
- MP4 path: `EXTRA_ARGS="-PA250VoiceTestVideo=/abs/path/clip.mp4" scripts/run_voice_test.sh`.

## 2026-11-09 — Talking portrait via fal (Stage 2)

### What was built
- **`fal_talking`** (`providers/video_sources/fal_talking_source.py`, behind `BaseVideoSource`, file-per-line). Uses `FAL_KEY` from `.env` and `fal-client` 1.0.3.
  - Model from `FAL_TALKING_MODEL`: default `veed/fabric-1.0` at `FAL_RESOLUTION=480p`; alternative `fal-ai/bytedance/omnihuman`, whose audio must be under 30 s, so its parts are capped at 29.5 s. Schemas and prices checked on the fal model pages: Fabric $0.08/s at 480p, $0.15/s at 720p; OmniHuman $0.14/s. `fal-ai/musetalk` is not used.
  - Per line: the portrait is uploaded once (1-day expiry; URL cached by file hash in `generated_video/source_cache.json`, reused for 23 h, re-uploaded once if fal rejects it). The WAV is uploaded with a 1-hour expiry, the model is called with image + audio, and the MP4 is downloaded.
  - Lines over `FAL_MAX_CLIP_SECONDS` (30) are split in the middle of the latest pause (20 ms frames under 4 % of peak RMS, at least 180 ms long), else cut at the limit. Each part is rendered, then ffmpeg concatenates them and muxes the *original* WAV as AAC → one H.264 + AAC file (checked with ffprobe).
  - Render cache: `generated_video/cache/{sha256(model, resolution, portrait, WAV)}.mp4`; a hit copies it with no fal call.
  - Logs `fal render`: model, seconds, billed seconds, est. cost, render time, parts, cache hit.
- **Chain** (`diplomacy/avatar.py`): musetalk_local → musetalk_remote (not built; skipped, noted) → fal_talking → did → audio_only, all under `PA250_AVATAR_TIMEOUT` (90 s). On timeout the in-flight fal requests are cancelled and the line goes out audio-only. `PA250_AVATAR` = auto | musetalk | fal | did | none.
- **D-ID**: `DIDVideoProvider.upload_audio` (POST `/audios`, field `audio`) → `/talks` with `script.type="audio"`. Text is never sent, so D-ID's voice is never used.
- **`GET /v1/avatar/health`**: chain order with `available`, `reason`, `enabled` per backend (fal model/resolution). No secrets.
- **Unreal**: the MP4 plays on `M_PortraitVideo` (MT_Envoy), voice from the MP4 via `MediaSound` (envelope logged), and the portrait swaps back to the still on end. The WAV is never started for a video line. During the voice test, `-PA250VoiceTest` takes two `Shot showui` stills 2.0 s and 3.5 s into the clip.
- **L_DiplomacyRoom was unwatchable.** The saved level (from an older builder run) had the RoomCamera pitched −90, looking at the floor, and the portrait plane lying flat at 195 cm like a ceiling tile. The player could never see the portrait. Patched only those actors in the saved level: camera pitch −4 / yaw −90; portrait upright on the north wall at 160 × 90 cm (landscape, matching the 1.77:1 still and 1.8:1 video); frame 175 × 105. `build_levels.py` now uses the same sizes.
- `fal-client` was added to `pyproject.toml` with `uv add --frozen` and installed into the venv with `uv pip`. **`uv.lock` is not updated**: a full lock/sync wants to build `numpy==2.0.2` from source, which fails here (the venv has numpy 2.3.3).

### Evidence (run on this Mac)
- `scripts/test_talking_portrait.py` run 1: Fabric 480p, 5.34 s video, render **34.4 s**, est **$0.43**, fal calls 1, ffprobe `video=['h264'] audio=['aac']`. Run 2: cache_hit, **fal calls 0**, $0.00, 0.1 s. Contact sheet of the MP4: mouth and head move.
- In the room with sound (`PA250_AVATAR=fal scripts/run_voice_test.sh`): fal render **50.4 s**, est **$0.46**. Then `EnvoyVideo started …mp4 5.80` and `EnvoyVideo finished … wall=6.40s (portrait on video material 1, MP4 voice peak envelope 0.156, WAV voice component idle)`, still restored, PASSED. The screenshots from this run showed the floor, which is how the room bug was found.
- After the room patch, with the cached fal clip (`EXTRA_ARGS=-PA250VoiceTestVideo=…talking_portrait_test.mp4`): PASSED. `ScreenShot00006/7.png` show the portrait upright, in view and right way up, with head and mouth different between the two stills.
- pytest `tests/diplomacy`: 117 passed (new `test_fal_talking.py`: pause splitting, hard cut, availability reasons, cache hit = no fal call, portrait upload reused, timeout → audio-only + fal cancel, D-ID gets audio not text). Tests force `PA250_FAL=0` / `PA250_DID=0` unless a test opts in with mocks, because importing `main` loads `.env`. ruff + mypy clean.
- `scripts/run_slice_autoplay.sh`: PASSED after the room patch.
- **Not verified**: D-ID live (not run, so no D-ID credits spent); OmniHuman live; a line long enough to split (unit-tested only); lip sync accuracy judged by eye/ear in real play.

### Averages (fal runs above)
- Fabric 1.0 480p: render 34.4 s and 50.4 s for 5.3–5.8 s lines → **42 s average**, **$0.08 per second of speech ≈ $0.45 per line**. Cache hits cost $0.
- ElevenLabs TTS: 0.9–1.2 s per line.

### Known issue: the envoy is silent while the video renders
With `fal` in the chain, the reply text appears at once, but voice and video come only after the render (35–50 s). Options: keep it; play the WAV at once and skip the video; or play the WAV at once and use video only on cache hits. Not decided.

### Commands
- Service with video: `cd services/negotiation && .venv/bin/python -m uvicorn main:app --host 127.0.0.1 --port 8000` (chain is `auto` by default; `PA250_AVATAR=none` for voice only).
- Backend status: `curl -s http://127.0.0.1:8000/v1/avatar/health`.
- fal render + ffprobe + cost (run twice for the cache): `services/negotiation/.venv/bin/python scripts/test_talking_portrait.py`.
- Talking portrait in the room with sound: `PA250_AVATAR=fal scripts/run_voice_test.sh` (stills in `unreal/PlayAmerica250/Saved/Screenshots/MacEditor/`).
- Same, without paying fal again: `EXTRA_ARGS="-PA250VoiceTestVideo=$PWD/services/negotiation/generated_video/talking_portrait_test.mp4" scripts/run_voice_test.sh`.

## 2026-11-09 — Parlour, oil portrait, push-to-talk, period speech, voice first

- **Why play was silent / dumb** (user log 23:21): the service was started with `PA250_LLM=mock PA250_TTS=say PA250_DID=0` (autoplay settings), so every reply was a canned MockLocal line and every voice was macOS `say`. Earlier, the voice waited on fal video (35–90 s). New `scripts/start_service.sh` clears those overrides and prints brain/voice/mic before starting `main:app` on :8000 (verified: 0 PA250_LLM/TTS left in the child env).
- **Voice first** (default): the WAV is sent as soon as TTS finishes; a talking-portrait MP4 is used only when this exact line is already in the fal render cache. `PA250_VIDEO_WAIT=1` restores waiting for a render. fal polling 1 s (was 0.1 s); httpx request logs at WARNING.
- **Period speech**: `build_system_prompt` now sets a 1770s British officer's register (sir, His Majesty, guineas, "I am obliged", no modern idiom, no stage directions, 1–3 sentences), answering the player's actual offer and never repeating a line. Live Claude replay of the user's five logged lines: all in period, distinct, haggled 50 guineas + mayoralty down, accepted `defect` for 300 + governorship + estate (sim still validates).
- **Push-to-talk**: Speak/Stop button (timer + level meter). `FPA250MicRecorder` (AudioCaptureCore, RtAudio on Mac; the float `OpenCaptureStream` because `OpenAudioCaptureStream` is not exported in 5.8) → mono 16-bit WAV → `POST /v1/stt` (local faster-whisper `base.en`, vocabulary prompt for Gage/Boston/…, prewarmed at startup, 0.3–1.2 s per clip warm) → sent as an ordinary `player_utterance`. Opening the mic silences the envoy. A silent clip names the launching app: macOS asks the *launching* app (Cursor, Terminal) for the mic; Cursor was denied, so the clip was zeros. Start Unreal with `open -a …/UnrealEditor.app --args <uproject>` (or Dock/Finder) so Unreal asks for itself. Verified in play by the user (6 lines transcribed correctly in the 23:21 log). `-PA250MicTest` / `-PA250MicTestWav=` + `scripts/run_mic_test.sh`.
- **Parlour** (`build_diplomacy`): Georgian room from procedural textures (`tools/art/gen_parlour_textures.py`, seeded, power-of-two, mips on): damask walls, cream panelled wainscot, plank floor, Turkey carpet, marble fireplace with fire and andirons, mahogany mantel with candlesticks, curtained 6-over-6 sash windows, mahogany table and chair. Masters `M_Parlour_World` (world-aligned), `M_Parlour_UV`, `M_Parlour_Glow`. Manual exposure (compensation 0: with physical-camera exposure off the bias is a brightness compensation, not EV100), fire/candle flicker at runtime.
- **Oil portrait instead of "a TV"**: lit `M_PortraitStill`/`M_PortraitVideo` crop the 16:9 still/video to 3:4, multiply linen + craquelure (`canvas`), warm varnish and edge vignette, small emissive share; 96 × 128 cm canvas recessed in a stepped gilt frame (outer moulding, raised ogee, dark sight edge, corner bosses, crest).
- `[Audio] UnfocusedVolumeMultiplier=1.0` (engine default 0 mutes the game whenever Cursor has focus).
- Tests: `tests/diplomacy` 122 passed (+ STT, voice-first). Unreal build Succeeded.
- **Not verified**: the voice/mic automated runs on this Mac stalled while macOS audio output was wedged (`say` hung; `AudioMixer … waiting for h/w`); output recovered later and the user heard the envoy in play. The talking portrait does not move in live play (voice first, no cached renders) — open decision.

## 2026-11-09 — Instant lip-sync on the painting

- fal renders (35–90 s) cannot be live, so live lines lip-sync from a flipbook: `tools/art/build_envoy_lipsync_atlas.py` takes the 276 frames of the two fal renders we already have, aligns each face to a steady rest frame (phase correlation), orders frames by mouth aperture (pixels that are neither lip nor skin: dark interior or teeth), and composites a feathered mouth+jaw patch of 16 of them onto the rest frame → `SourceArt/parlour/envoy_lipsync_atlas.png` (4x4 cells of 362x480, the 3:4 painting crop). The head never moves between cells; only the mouth does.
- `M_PortraitStill` is now the atlas painting material (`Frame` 0 = rest, 1..15 wider open; canvas, varnish, vignette as before). `APA250DiplomacyGameMode` makes a dynamic instance, builds a 30 Hz RMS envelope per WAV (normalised to the line's 90th percentile), and every 1/30 s picks the cell for the playback position (mixer-consumed bytes − 60 ms), fast attack / slower release, ±1 neighbour jitter so held vowels move. Logs `EnvoyLipSync: N frame changes, M distinct cells`.
- Evidence: `scripts/run_voice_test.sh` PASSED with sound: 6.32 s line, `lip-sync 130 changes`; second run `94 frame changes, 16 distinct cells over 6.08s`; four HighResShots mid-line (cells 0, 11, 10, 8) show the same head with different mouths. The test now fails if the mouth moved fewer than 3 times per second of speech.
- No added latency or cost. Not phoneme-accurate (loudness-driven), and middle cells mix shapes (aperture metric).
- The user's running service (PID 88592) still has `PA250_LLM=mock PA250_TTS=say`; restart it with `scripts/start_service.sh`.

## 2026-11-09 — Lip warp / streaming / life: started, paused at usage limit

- Done: envoy voice switched to George (interim; `diplomacy/envoy_voices.yaml`). `diplomacy/visemes.py`: ElevenLabs character timings -> 30 Hz JawOpen/LipsWide/LipsRound/LipsPressed curves with coarticulation (Gaussian blend; closures by max); `approximate_alignment` for the `say` backup. Standalone check ran; not yet wired in.
- Blocked: ElevenLabs Voice Design via API returns 403 `feature_not_available` ("only available on a paid plan"), so the three Gage voice candidates cannot be generated on this key.
- Note: root `.gitignore` ignores `*.mp3` and `*.mp4`, so files in `docs/preview/voices/` and `docs/preview/*.mp4` would not be committed.
- Not started: TTS via `/with-timestamps` + curves in the speech event; latency logging (service + room) and baseline; streaming Claude sentences -> TTS with a chunk queue in the room; considering idle; warp material (Custom HLSL) + per-envoy face data asset + breathing/drift/eye shifts/blink (closed-eyes image via a fal edit model); 16-shape atlas behind a flag; 4 pre-rendered 720p fal lines (greeting, refusal, acceptance, consider); side-by-side capture; before/after latency numbers.

## 2026-11-09/10 — Warp lip-sync, life, streaming, voice design, stock clips (in progress)

Measured / verified:
- **Latency** (`scripts/latency_test.py`, service-side: player line received → first audio sent; Claude + ElevenLabs live, 3-line conversation):
  - before (whole reply, then TTS; Sonnet 5.5, max_tokens 600): median 4.88 s, max 5.90 s, and line 3 came back **empty** (`{}`: thinking used the 600-token budget).
  - Sonnet 5.5, effort low, streaming sentences: median 4.88 → **3.34 s** with prompt caching (max 5.02 s), no empty replies; first sentence 1.9–4.3 s of that is Claude, TTS 0.4–0.7 s.
  - Haiku 5.5, effort low: median 2.72–4.13 s (noisy), weaker on the rules (refused a defection that meets the sim's price; misstated who holds Charlestown); one plain-text reply. Sonnet kept.
  - Sonnet with thinking off (`between_tools`): not faster (3.5–5.4 s) and one reply without the tool call. Rejected.
  - In the room (Unreal `EnvoyLatency`, mock brain): 1.31 s player line → first sound (TTS 1.05 s, service→room 0.01 s, room start 0.03 s). With Claude: not yet measured in-room.
- **Causes fixed**: `max_tokens` 600 → 4000 (thinking counts against it); `anthropic` SDK 0.68 → 0.125.0 (venv + pyproject; `uv.lock` not updated: re-lock builds numpy 2.0.2 from source and fails); `eager_input_streaming` on the envoy tool; plain-text answers are spoken instead of falling back to mock; server-side refusal fallback (`fallbacks: "default"`) on Sonnet 5.5.
- **No content filter** on the envoy's lines (user decision); CLAUDE.md updated.
- **Voice**: George interim (`envoy_voices.yaml`). ElevenLabs Voice Design (paid plan): 3 Gage candidates in `docs/preview/voices/gage_candidate_{1,2,3}.mp3` (git-ignored: root `.gitignore` has `*.mp3`), ids in `candidates.txt`. Not yet chosen/saved.
- **Stock clips** (`scripts/render_stock_lines.py`, Fabric 1.0 720p, George): greeting 4.74 s, consider 2.51 s, acceptance 4.74 s, refusal 4.64 s; est $2.48 total; renders 34–41 s each. Offered to the room on connect (`envoy_stock`).
- **Visemes**: `diplomacy/visemes.py` (6 shapes → jaw/wide/round/press, Gaussian coarticulation, closures by max) from ElevenLabs `/with-timestamps`; tests in `test_visemes_streaming.py`. 128 service tests pass.
- **Warp portrait**: `M_PortraitWarp` (Custom HLSL in `build_levels.py`), face data `DA_EnvoyFace_british` (`UPA250EnvoyFaceData`, landmarks measured on the still: mouth (970,628) half-width 89, chin (970,765), eyes (877,424)/(1065,417)), closed-eyes texture from fal `flux-pro/kontext` registered per eye (`SourceArt/parlour/envoy_blink_aligned.png`). Offline NumPy port of the HLSL previews ah/ee/oo/m/eh correctly. Compiles on Metal after feeding float4 (RGB+A) vectors to the Custom node.
- Fixed the other session's `PA250MapWidget.cpp:1336` (one missing `)`); module builds.

Open:
- Eye-warp blowup seen in-game (eyes filled with flat colour) under heavy swap: the spring used explicit Euler and diverged at Omega·Dt > 1. Replaced with the exact critically damped step (verified numerically, stable at 0.5 s frames). **Not yet rebuilt** (editor was open).
- Side-by-side capture (`scripts/lipsync_compare.sh` → `docs/preview/lipsync_compare.mp4`, git-ignored `*.mp4`) and in-room latency with Claude: not yet run.

## 2026-11-10 — Envoy said his line twice; one sentence in the macOS voice; long gaps
- Play log 19:32 (reply `pa250_053e3282_1794357136736`): Claude wrote two sentences as plain text, then the full line in the tool call; both were spoken (6 chunks). Now text before a tool call is held back and spoken only if no tool call follows (`propose_streaming`); the prompt also forbids text outside the tool.
- Chunk 5 got ElevenLabs 429 `concurrent_limit_exceeded` (account limit 3) and fell back to `say` (Daniel). Now at most `ELEVENLABS_CONCURRENCY` (default 2) requests run at once and a 429 is retried up to 3 times before the fallback.
- ElevenLabs pads each clip with ~0.34 s of silence; with the room's 0.25 s drain wait that was ~0.6 s between sentences. `trim_tail_silence` keeps 0.08 s (start untouched, lip-sync timings unchanged).
- Defect prompt: the envoy is told land only counts as a named player-owned province and to make a concrete counter-offer. Sim rule unchanged.
- Evidence: `tests/diplomacy` 131 passed, 1 failed (`test_malformed_json_message_does_not_kill_socket` gets `envoy_stock` first; not from this change). Live Claude check on the test context (price 74): mayor + 200 gold + "1000 acres" refused with "name a province you own"; mayor + 300 gold accepted with a valid `defect` clause.
- **Not verified**: heard in the room (service restart needed). The framed-portrait look is unchanged.

## 2026-11-10/11 — The envoy is a living man at the table, not a framed painting
- User correction: the framed portrait was a misreading. The idea is a real portrait of the person, turned into real life. CLAUDE.md locked decision (Avatar) updated.
- Source: Copley's portrait of Gage (Yale Center for British Art, CC0, from Wikimedia Commons) → `SourceArt/envoy/gage_copley_original.jpg`.
- `tools/art/build_envoy_live.py` (run with the service venv): `live` and `blink` are image edits (OpenAI `gpt-image-2`, 1024x1536; 2 calls made), `cut` is a BiRefNet-portrait matte. Outputs `gage_live.png`, `gage_blink.png` (RGBA; blink differs only in feathered eye patches), `gage_matte.png`. fal is locked ("Exhausted balance", HTTP 403) and the Gemini key has no image quota (429), so those providers are coded but were not run. The matte was made with `rembg` in a scratch venv (not in the service venv; without it the tool falls back to fal).
- `build_levels.py` `build_diplomacy`: frame, backing and crest removed (a plain looking-glass hangs over the mantel); `Chair_Envoy` added; `EnvoyFigure` plane (59 x 89 cm, tagged `Portrait`) at the far side of the table; `EnvoyKey` light; camera (0,175,128) pitch −9.5, FOV 42. `M_PortraitWarp` is now masked (alpha from the warped figure, dithered edge), no canvas/varnish/vignette; `M_PortraitStill` is a copy (no `Frame`, so the flipbook path is off). `FACE_BRITISH` re-measured on the new image: mouth (587,453) half-width 42, chin (585,535), eyes (509,333)/(611,326). Textures `T_EnvoyLive`, `T_EnvoyLiveBlink`. No C++ change.
- Service: stock MP4s (old painted face) are offered only with `PA250_STOCK_VIDEO=1`.
- Evidence: room rebuilt with `PA250_LEVELS=diplomacy UnrealEditor-Cmd … -run=pythonscript -script=Scripts/build_levels.py` ("Python script executed successfully"); `-game -windowed` stills `Saved/Screenshots/MacEditor/HighresScreenshot00040..43.png` show him seated behind the table, face in the top third. Log: `EnvoyFace: warp (… face data loaded, warp material loaded)`.
- **Not verified**: mouth movement in the room. Run 1 of `scripts/run_voice_test.sh` rendered ~3 fps while another session compiled (jaw open on 7 ticks, test FAILED on its ≥5/s rule; stills show a closed mouth). Run 2 failed with `EnvoyVoice stalled` after `AudioMixer … waiting for h/w` (macOS audio output wedged). Landmarks were measured by eye, so jaw/lip tuning in play is still open. `Shot showui` with the dialogue panel over the new framing not taken.
- Leftovers: `british_general_frame.jpg` is still the default portrait for the fal/D-ID video chain; `T_EnvoyPortrait*`, the lip-sync atlas and `build_envoy_lipsync_atlas.py` are unused. A stale `UnrealEditor-Cmd … import_warhorse.py` (pid 3972, started Nov 9) is still running; not touched.

## 2026-11-11 — Soldier classes: selectable in battle, raised in towns
- **Classes** (`Army.units` keys). Continental: `militia`, `line_infantry` (Provincial), `rifleman`, `officer`. British: `line_infantry`, `grenadier`, `light_infantry`, `officer`. Opening armies in `boston1775.json` keep their totals: British 24 = 12 line + 5 grenadier + 6 light infantry + 1 officer (the 19 April column was flank companies); Massachusetts 22 = 17 militia + 4 line + 1 officer.
- **Sim**: catalog `recruits` gained `faction`, `requires_chapter`, `role`. New companies: `rifle_company` (3 riflemen, 45 gold, own rifles, Militia Muster, only after `chapter_continental` = 14 June 1775 → error `not_yet`), `field_officer` (1 man, 30 gold), and British `regular_company` / `grenadier_company` / `light_company` (hidden from a Continental player: `unknown_unit`). `city_view_json` recruits carry `unit_class` and `role`. `limit_bridge_company` now caps the whole British column at North Bridge in proportion across classes (it only capped `line_infantry`).
- **Battle** (`Battle/PA250UnitClasses`, GAMEPLAY SETTINGS): per-class health, speed, aim cone, range, reload time, hand-to-hand damage, bayonet. Grenadier 120 health / melee x1.3; light infantry speed x1.15, aim x0.88; rifleman aim x0.45, range x2, reload x1.6, no bayonet; officer: men of his side within 15 m get aim x0.8 and reload x0.88 (`TickOfficers`, 2 Hz). Applied to bots and the player in `ConfigureSoldier`; the rifle's slow reload does apply to the player. Bot drill rows for the new classes are built into `UPA250CombatProfiles::ClassAssignments()` so an older `DA_CombatProfiles` still resolves them.
- **Class picker** (`PA250SpawnScreen`): cards for the classes the player's army actually holds (label, weapon, one-line role, trait line, men left); a class with no men left is disabled. Shown before the first shot when the army has two or more classes (Deploy re-kits the player where he stands, no ticket) and after each death. Scripted runs skip the opening screen (`-PA250Preview=`, probes, FPS sample, unattended, `-PA250NoClassPick`); `-PA250ClassPick` forces it. `ChooseClassFor` no longer hands out a class whose men are all dead. HUD top-left shows the player's class and "Officer near".
- **City View**: each company card names its class and battle role; garrison composition reads "17 militia · 4 regulars · 1 officer".
- **Looks per class** (user request, same day): `ARFCharacter::ApplyClassLook` scales the body (grenadier 1.07, light infantry 0.96, officer 1.02, rifleman 0.98) and adds a feather on the model's cocked hat (grenadier tall white, light infantry green, officer gold, rifleman buck-tail; one engine cylinder placed from the head bone's reference pose). Coats: British officer scarlet; Patriot officer blue faced buff; rifleman linen hunting-shirt colour; Provincial line brown with buff; militia keep mixed homespun. The model already wears a tricorn, so no separate caps.
- **Evidence**: `pa250sim_tests` 9485 checks, 0 failed (new checks in `recruitment`, `concord_bridge_company`); golden `clock_golden_1775.json` regenerated (`65765c6c31dd21d7`, opening armies changed). `PlayAmerica250Editor` build Succeeded; `UnrealEditor-Cmd … -run=PA250Battle` 376 checks, 0 fails (new class checks in `RunTraining`). Real window (`-game -windowed`, `-PA250BattleShots=`): log `PA250 class: opening class pick shown (2 classes)`; still of the picker with Provincial / Militia cards, men left and spawn points; autoplay stills (`-PA250Autoplay -PA250SpectateSide=british`, a new dev flag) show the grenadier feather above the hat and kill-feed lines "Grenadier x Militia", "Light Infantry x Militia".
- **Not verified**: clicking Deploy in the opening picker by hand (the re-kit path), the HUD class line, rifleman and officer looks and the officer bonus in play (the Lexington debug battle fields 6 units a side, which gates the single officer out; riflemen need a campaign past 14 June 1775), and the City View company cards beyond one campaign screenshot from another session's run. Balance values are untested in play. Coats read very dark in the stills (existing tint), so the coat-colour differences are subtle; the feather and height carry most of the difference. While the opening picker is up the player cannot be damaged (30 s at most).
- **Class images and stronger battlefield looks** (user request, same day):
  - The pack ships the soldier in separate part meshes (`/Game/american_soldier/base_mesh/separatedmesh`, hat included). Classes with their own headgear are drawn from those parts minus the hat, as leader-pose followers of the main mesh, which keeps animating unseen (`ARFCharacter::ApplyClassLook`, `BodyParts`). Headgear is engine basic shapes on the head bone with a tinted `BasicShapeMaterial` MID: grenadier black bearskin, light infantry low black cap with green feather, rifleman brown round hat, officers a gold feather on the cocked hat. This replaces the feather-only version above. If a part mesh is missing the soldier keeps the cocked hat (log warning).
  - `-PA250ClassPortraits` (`Battle/PA250ClassPortraits.cpp`, run with `-PA250Bots=0` in a real window) photographs one soldier of each of the 8 classes, front-lit, to `Saved/PA250/portraits/`. `tools/art/build_class_portraits.py` cuts head-to-waist card images into `Content/PA250/Source/ClassPortraits/`; `Scripts/import_class_portraits.py` imports `/Game/PA250/UI/Classes/T_Class_<faction>_<class>` (8 textures, 440x330, UI, no mips). The class picker cards and the City View company cards show them, so a card is the soldier as the battle draws him.
  - Evidence: contact sheet of the 8 stills inspected (all 8 differ by headgear and/or coat trim); importer log `CLASS_PORTRAITS: done: 8 textures`; real-window still of the opening picker with the Provincial and Militia images, title "CHOOSE YOUR SOLDIER", HUD line "Provincial - Musket, a few bayonets"; `-run=PA250Battle` 376 checks, 0 fails after the change; autoplay battle still (`-PA250Autoplay -PA250SpectateSide=british -PA250BattleShots=12,30`) shows a grenadier in his bearskin moving and fighting beside regulars in cocked hats.
  - Not verified: the City View thumbnails in a window; ragdoll/death and mounted states of a part-mesh soldier; frame cost of 15 extra skinned components per bearskin/cap/round-hat soldier. Coat bodies stay very dark for every class (the uniform material tints mostly the facings), so headgear and trim colour carry the difference. Other sessions kept terminating `-game` runs on this Mac, so some captures took several tries.
- **Not done from the design note**: rain / wet powder, the named battles (Bunker Hill, Dorchester Heights) and historical figures as special units.

## 2026-11-13 — Photoreal pass with GPT images: treeline, ground drifts, parley room (user request; part built, part waiting for a rebuild)
- **Why the 12 Nov images were hard to see:** they exist (`SourceArt/battlefield/raw/`, 14 files) but are flat surface swatches used as textures, and `SourceArt/battlefield/` is untracked in git. The scene round them was the problem: bare skyline, one-tone ground, black far trees, black coats. Every GPT image so far is on one sheet: `docs/preview/gpt_images_gallery.jpg`.
- **Model:** `gpt-image-2.6-sunburst`, quality high (user: use the latest, Images 2.6). 2.6 takes custom sizes (multiples of 16, up to 3:1) and `background: "transparent"`. 8 calls today (6 treeline, 1 aerial, 1 room edit) plus one throwaway test on `-flare`.
- **In the levels now (built 06:39, `PA250_LEVELS=lexington,concord`):**
  - **Treeline** (`tools/art/build_battlefield_backdrop.py`): six 2560x864 wood-edge photographs with the sky cut out (April first leaf, white pine; a reading, not a record of any one wood) joined into `HorizonTreeline.png` (8192x1024, wraps; alpha step at the wrap 0.9 vs 1.2 inside). `build_levels.py` `place_horizon`: two rings (`HORIZON_RINGS`) 18 m inside the terrain edge and 75 m beyond it, following the ground; `M_PA250_Horizon` is masked, two-sided, lit with a world-up normal so it takes each level's own sun and sky; no collision, shadow or Lumen. Actor tag `PA250Horizon`.
  - **Ground drifts:** `PastureMacro.png` (a pasture from the air, 55 m across, stored as a linear ratio to its mean) multiplies the 3 m grass tile in `make_meadow_material`.
  - `Scripts/import_battlefield_kit.py`: `import_backdrop()` and `PA250_BATTLEFIELD_ONLY=backdrop` (imports only these two textures, leaves the kit alone).
  - Evidence: importer log `backdrop: T_HorizonTreeline 8192x1024, T_PastureMacro 1024x1024`; build log `horizon placed (2 rings)` for both levels; real-window stills `scripts/capture_preview.sh photo <level>` → `docs/preview/{lexington,concord}_photo_{spawn,green,wide}.png`, inspected (compare `*_relay_*` from 12 Nov). No material compile failures in those two preview logs.
- **Fixed on the way:** `MI_PA250_Yarrow` never compiled (sampler type against an sRGB mask texture), so yarrow drew with the grey grid; the sampler now follows the texture.
- **Seen in those stills and NOT yet fixed in the built levels:** far tree billboards draw white (my world-up normal on them was wrong); Concord's grass is too pale against the sky; coats are black.
- **Written but NOT built or seen in the engine** (the user needed the project; no Unreal runs after 06:42). The next level rebuild by anyone applies the first two:
  - Billboards and crowns use two-sided foliage shading instead (`make_foliage_material(leaf=True)`).
  - Ground: drift swing 60 %, x0.86 overall, thatch multiplier (1.1, 1.02, 0.76).
  - **Coats:** the pack's coat cloth is near-black navy (exported and measured: about sRGB 27,32,58), so desaturate x CoatColor is black whatever the colour. `tools/ue/patch_soldier_coat_curve.py` inserts grey^0.35 before the tint in `M_PA250_SoldierUniform` (run once; no C++ change). Not run.
  - **Parley room as a photograph** (`tools/art/build_parlour_plate.py`): an image edit of the box-room still, padded to 2.35:1, into a photograph from the same camera (`SourceArt/parlour/room_plate.png`, 2560x1088). `build_diplomacy` shows it on two unlit planes square to the camera (`place_room_plate`): the whole picture 260 cm away behind the envoy, and its table (from `ROOM_PLATE_TABLE_V` = 0.779 down) 140 cm away in front of him; the box table, candles, papers and chairs are then not built, their lights are. Falls back to the box room if the plate is missing or fails. Offline composite with the game camera's geometry: `docs/preview/parley_room_mockup.png` (he sits at the right scale, the table edge hides his lap). `ROOM_PLATE_GAIN` (brightness under the room's manual exposure) is a guess until seen.
- **To finish (with Unreal free):** `UnrealEditor-Cmd … -run=pythonscript -script=tools/ue/patch_soldier_coat_curve.py`; `PA250_LEVELS=lexington,concord,diplomacy … Scripts/build_levels.py`; `scripts/capture_preview.sh photo lexington|concord`; a `-game` still of `L_DiplomacyRoom`; `-PA250FpsSample` on both battle levels (gate 30; not re-measured with the rings).
- **Locked decisions touched:** none in the sim. Avatar: the envoy plane, tag `Portrait`, warp material and lights are unchanged; only what is drawn round him changes.
