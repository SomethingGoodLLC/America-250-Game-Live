// PA250Sim public API. All state transitions are pure functions.
#pragma once
#include <string>
#include <vector>

#include "pa250/types.hpp"

namespace pa250 {

// ---- Calendar -----------------------------------------------------------
PA250_API YMDH to_ymdh(GameTime t);
PA250_API GameTime from_ymdh(YMDH v);

// ---- Scenario / persistence --------------------------------------------
PA250_API std::string read_text_file(const std::string& path);                // throws std::runtime_error
PA250_API bool write_text_file(const std::string& path, const std::string& text);
PA250_API CampaignState load_scenario(const std::string& json_text);          // throws std::runtime_error
PA250_API std::string serialize(const CampaignState& state);                  // canonical single-file JSON save
PA250_API CampaignState deserialize(const std::string& json_text);            // throws std::runtime_error
PA250_API uint64_t state_hash(const CampaignState& state);                    // FNV-1a 64 of serialize()
PA250_API std::string state_hash_hex(const CampaignState& state);
PA250_API void rebuild_adjacency(CampaignState& s);                           // from edges
PA250_API std::vector<std::string> validate_scenario_graph(const CampaignState& s);

// ---- Hourly clock / commands / paths ------------------------------------
PA250_API TickOutput advance_hour(const CampaignState& s);  // pure; consumes pending_commands due this hour
PA250_API CampaignState enqueue_command(const CampaignState& s, CommandPayload p);  // apply_on_hour=now+1
PA250_API PathPlan plan_path(const CampaignState& s, const std::string& army_id, const std::string& dest);
// Pure: the march an army is on now (its remaining edges, the towns along them, the march hours
// still to go and the hour it arrives), or the march a queued MoveArmy command will start. ok=false
// with error "not_marching" when it has no march and none is queued.
PA250_API PathPlan march_plan(const CampaignState& s, const std::string& army_id);
// Pure: the halt order (CommandKind::HaltArmy applies this). Standing in a town the order and the
// progress are cleared; part-way along a leg the army turns about and needs the march hours it had
// spent on the leg to be back where the leg began. error: unknown_army.
PA250_API CampaignState halt_army(const CampaignState& s, const std::string& army_id, std::string& error);

// Order validation (adjacency / ceasefire) for UI hints
PA250_API std::vector<std::string> validate_order(const CampaignState& state, const Order& order);

// ---- Battle ------------------------------------------------------------
// Battle rules (GAMEPLAY TUNING, data/battle_rules.json). Optional; defaults apply if unloaded.
PA250_API void load_battle_rules(const std::string& json_text);
PA250_API bool battle_rules_loaded();
PA250_API const BattleRules& battle_rules();
PA250_API bool known_end_reason(const std::string& reason);
// Pure: split the bot budget between two sides in proportion to their fit soldiers, each share
// clamped to the per-side limits. 96 v 88 gives 21 v 19; 2:1 gives 27 v 13.
PA250_API FieldSplit split_field_strength(int men_a, int men_b);
// Pure: rounds in each man's box when `cartridges` are shared among `men` (clamped to the rules).
PA250_API int rounds_per_man_for(int cartridges, int men);
// Pure: of this many men shot or bayoneted down, how many are killed and how many wounded
// (battle_rules.json casualties.wounded_pct_of_fallen, rounded half up). Prisoners are not among
// them: they surrendered. Men a surgeon saved are counted wounded before this is asked.
PA250_API MenDown split_fallen(int fallen);
// Pure: rounds in each man's box in this army (a full box when the army has never drawn on it).
PA250_API int carried_rounds(const Army& army);
// Hourly: an army standing in a town its side holds fills its boxes up to a full box from that
// town's magazine (cartridges first, then powder made up). Nowhere else do boxes fill.
PA250_API void refill_cartridge_boxes(CampaignState& s);
// Hourly: a siege lifts when no besieging army stands in the town; at expiry the town changes hands.
PA250_API void tick_sieges(CampaignState& s);
PA250_API std::string choose_retreat(const CampaignState& state, const std::string& province,
                           const std::string& loser_faction);
PA250_API std::string scenario_preset_for_province(const std::string& province_id);
PA250_API BattleSetup build_battle_setup(const CampaignState& state, const std::string& province);
PA250_API CampaignState apply_battle_result(const CampaignState& state, const BattleResult& result);

// ---- Campaign arc: win and loss, missions, enlistments, the scored AI ----
// Rules (data/campaign_rules_boston_1775.json) and missions (data/missions_boston_1775.json).
// The arc runs only while the rules are loaded; missions only while a mission file is loaded.
PA250_API void load_campaign_rules(const std::string& json_text);
PA250_API void unload_campaign_rules();
PA250_API bool campaign_rules_loaded();
PA250_API const CampaignRules& campaign_rules();
// Returns "code: detail" errors (unknown predicate or effect, dangling prerequisite); empty = loaded.
PA250_API std::vector<std::string> load_missions(const std::string& json_text);
PA250_API void unload_missions();
PA250_API bool missions_loaded();
PA250_API const std::vector<MissionDef>& mission_defs();
// Pure: is the predicate true for a mission of `mission_faction`?
PA250_API bool mission_predicate(const CampaignState& state, const std::string& mission_faction,
                       const MissionPredicate& predicate);
// Plain words for the tracker: where the predicate stands now.
PA250_API std::string mission_progress_text(const CampaignState& state, const std::string& mission_faction,
                                  const MissionPredicate& predicate);
// Hourly, from advance_hour: guns in transit, occupation, enlistments, missions (effects are
// enqueued as MissionEffect commands for the next hour), the evacuation clock, win and loss.
PA250_API void tick_campaign(CampaignState& s, std::vector<Interrupt>& interrupts);
// Pure: the command a completed or failed mission enqueues. outcome = "complete" | "failed".
PA250_API CampaignState apply_mission_effect(const CampaignState& state, const std::string& mission_id,
                                   const std::string& outcome);
// Pure: has the campaign been won or lost at this hour? decided=false while it has not.
PA250_API CampaignOutcome evaluate_outcome(const CampaignState& state);
// Pure: the window score as the end-of-campaign formula would count it now, plus hours until
// window_end. The HUD shows this while the campaign is still running; evaluate_outcome applies it
// only when the window has closed.
struct WindowScore {
    int patriot_towns = 0;
    int british_towns = 0;
    int patriot_points = 0;
    int british_points = 0;
    int64_t hours_remaining = 0;
};
PA250_API WindowScore window_score_snapshot(const CampaignState& state);
// Read-only snapshots for the HUD: the mission tracker and the end screen.
PA250_API std::string missions_json(const CampaignState& state);
PA250_API std::string outcome_json(const CampaignState& state);
// Enlistments. From the warning date until they expire a company may be re-enlisted for gold.
// error: unknown_army / not_your_army / unknown_class / exempt_class / not_yet / too_late /
// nothing_to_reenlist / insufficient_gold / battle_pending.
PA250_API int reenlist_cost(const CampaignState& state, const std::string& army_id, const std::string& unit_class);
PA250_API CampaignState reenlist_companies(const CampaignState& state, const std::string& army_id,
                                 const std::string& unit_class, std::string& error);
PA250_API int company_size(const std::string& faction, const std::string& unit_class);  // catalog recruit size
// The noble train. error: not_available / already_underway / already_delivered / insufficient_gold.
PA250_API CampaignState send_for_siege_guns(const CampaignState& state, std::string& error);
// Hitch the delivered guns to an army standing where they lie (empty army_id: leave them here).
// error: no_guns / unknown_army / not_your_army / not_here.
PA250_API CampaignState attach_siege_guns(const CampaignState& state, const std::string& army_id, std::string& error);
// The AI's daily orders when the rules are loaded (called from the midnight hook in advance_hour).
PA250_API void campaign_daily_ai(CampaignState& s);
// Pure: the town an AI field army would march on now, or "" (scored list, ties by town id).
PA250_API std::string choose_ai_target(const CampaignState& state, const std::string& army_id);
// SIM ONLY (tests and tools): settle the pending battle without Unreal, seeded from the setup.
PA250_API BattleResult auto_resolve_battle(const CampaignState& state, const BattleSetup& setup);
// SIM ONLY: a player who never acts. Advances up to max_hours, settling every battle with
// auto_resolve_battle, and stops when the campaign is decided. Deterministic for a given state.
PA250_API CampaignState run_idle_campaign(const CampaignState& state, int max_hours);

// ---- Campaign pass: people, lobbying, decisions --------------------------
// Data: persons_boston_1775.json, bodies_boston_1775.json + measures_boston_1775.json,
// decisions_boston_1775.json. Each part runs only while its file is loaded (the game loads all of
// them). Loaders return "code: detail" errors and load nothing when there is one.
PA250_API std::vector<std::string> load_persons(const std::string& json_text);
PA250_API void unload_persons();
PA250_API bool persons_loaded();
PA250_API std::vector<std::string> load_politics(const std::string& bodies_json, const std::string& measures_json);
PA250_API void unload_politics();
PA250_API bool politics_loaded();
PA250_API std::vector<std::string> load_decisions(const std::string& json_text);
PA250_API void unload_decisions();
PA250_API bool decisions_loaded();
// A campaign order (protocol/schemas/campaign_orders.yaml), as a JSON object. validate returns ""
// when the order can be carried out now, else its refusal code. apply is the pure function the
// CampaignOrder command runs; on a refusal it returns the state unchanged and sets error.
PA250_API std::string validate_campaign_order(const CampaignState& state, const std::string& order_json);
PA250_API CampaignState apply_campaign_order(const CampaignState& state, const std::string& order_json, std::string& error);
// The general commanding an army ("" for a detachment), and his display name.
PA250_API std::string general_of(const CampaignState& state, const std::string& army_id);
// Why this army cannot take the town by standing in it or by winning a battle there, in plain words
// for the player ("" when it can, or when the town is not an enemy town with works). Today the one
// reason is the people rule: an army with no general is a detachment and cannot besiege a fort.
// Pure; display only (the rules themselves are in apply_battle_result and the occupation tick).
PA250_API std::string why_cannot_besiege(const CampaignState& state, const std::string& army_id, const std::string& province);
// How the side gets a general for an army, in plain words: who is at hand (or when the first
// comes), and the towns it holds where an army can be formed. "" when persons are not loaded.
PA250_API std::string how_to_form_army(const CampaignState& state, const std::string& faction);
PA250_API std::string person_name(const std::string& person_id);
// Most units the army may take in: a detachment's cap, or an army's under a general. 0 = no cap
// (persons not loaded, or no such army).
PA250_API int army_unit_cap(const CampaignState& state, const std::string& army_id);
// The battle's rules for a general, and what becomes of one who falls: wounded | killed |
// captured, drawn from the battle's seed (the same battle always gives the same fate).
PA250_API CommanderRules commander_rules();
PA250_API std::string commander_fate(uint64_t battle_seed, const std::string& faction);
// The hero rules of the loaded persons file (GAME INVENTION). loaded=false when it has none.
PA250_API HeroRules hero_rules();
// Unity: 100 less the spread between the warmest and coolest bloc of the player's body.
PA250_API int unity(const CampaignState& state);
// Read-only snapshots for the HUD (JSON text).
PA250_API std::string persons_json(const CampaignState& state);   // every person of the player's side: role, where, uses, traits
PA250_API std::string politics_json(const CampaignState& state);  // the floor: blocs, measures, dispatches, favors, unity, confidence
PA250_API std::string decision_json(const CampaignState& state);  // the pending decision, or {"pending":false}
// What `viewer` knows of an army: {"known":bool,"men_low":n,"men_high":n,"source":text}.
PA250_API std::string army_intel_json(const CampaignState& state, const std::string& viewer_faction, const std::string& army_id);
// Unscouted band around a true count of men. extra_half is the seed's added width; width_pct 0
// ignores it. The band always contains truth_men. mid is the centre the AI reads.
struct MenBand {
    int low = 0;
    int high = 0;
    int mid = 0;
};
PA250_API MenBand unscouted_men_band(int truth_men, int step_men, int width_pct, int extra_half);
// Units `viewer` counts `army_id` as. Exact when known; otherwise the band midpoint in units,
// and never more than the army's true strength.
PA250_API int units_as_seen(const CampaignState& state, const std::string& viewer_faction, const std::string& army_id);
// Gold a purchase costs now (purchase_cost and the rise in prices paper money brought).
PA250_API int purchase_cost_now(const CampaignState& state, int powder, int cartridges, int muskets, int bayonets,
                      int provisions, int lead, int paper);

// ---- Logistics / correspondence / staff (pure) -------------------------
// Ensure default stores/staff exist (called from load_scenario path).
PA250_API void ensure_logistics_defaults(CampaignState& s);
// Soft-migrate older schema-2 saves (faction stores → province_stores, etc.).
PA250_API void migrate_campaign_state(CampaignState& s);
// Gameplay-tuning file (not historical fact). Optional; defaults apply if unloaded.
PA250_API void load_logistics_tuning(const std::string& json_text);
PA250_API bool logistics_tuning_loaded();
PA250_API int workshop_cartridges_per_level();
PA250_API int farm_readiness_per_level();
PA250_API int depot_readiness_per_level();

// Province ledger helpers.
PA250_API ProvinceStores& province_ledger(CampaignState& s, const std::string& province_id);
PA250_API const ProvinceStores& province_ledger(const CampaignState& s, const std::string& province_id);
// Computed rollup of province ledgers owned by faction (display only; not a write path).
PA250_API FactionStores rollup_faction_stores(const CampaignState& s, const std::string& faction);
PA250_API std::string default_depot_province(const CampaignState& s, const std::string& faction);

// Purchase: debit gold, create in-transit shipment; destination unchanged until delivery.
// Transfer: debit source immediately; credit dest only on delivery.
PA250_API CampaignState approve_purchase(const CampaignState& state, const std::string& dest_province,
                               int powder, int cartridges, int muskets, int bayonets, int provisions,
                               int lead, int paper, std::string& error);
PA250_API CampaignState approve_transfer(const CampaignState& state, const std::string& source_province,
                               const std::string& dest_province, int powder, int cartridges, int muskets,
                               int bayonets, int provisions, int lead, int paper, std::string& error);
// The transfer itself, field guns included (approve_transfer is this with no guns).
PA250_API CampaignState transfer_stores(const CampaignState& state, const std::string& source_province,
                              const std::string& dest_province, int powder, int cartridges, int muskets, int bayonets,
                              int provisions, int lead, int paper, int guns, std::string& error);
// Guns an army fields: those it has, as far as it has crews to serve them (battle_rules guns).
PA250_API int served_guns(const Army& army);
// The kind of fight (pure; battle_rules.json "modes"): "landing" when the attacker arrived by a
// landing edge type into a town with the landing tag, else "assault" when the defender's
// fortification level reaches the assault level, else "conquest". A preset fight is always conquest.
PA250_API std::string choose_battle_mode(const std::string& scenario_preset, const std::string& arrival_edge_type,
                               const std::vector<std::string>& terrain_tags, int defender_fortification_level);
// Clock rule, each hour after the cartridge boxes: an army with gun crews standing in a town its
// side holds draws field guns from that town's magazine, as many as its spare crews can serve.
PA250_API void draw_guns(CampaignState& s);
// Pure: ship a town's whole war stores (not its provisions) to the side's headquarters depot.
// Errors: unknown_province, already_at_depot, not_your_province, empty_order.
PA250_API CampaignState send_stores_to_depot(const CampaignState& state, const std::string& source_province,
                                   std::string& error);
// Gold a purchase order would cost (the price approve_purchase charges). Never mutates state.
PA250_API int purchase_cost(int powder, int cartridges, int muskets, int bayonets, int provisions, int lead,
                  int paper);
// Province development (EU4-style tracks + City View discrete buildings).
PA250_API int province_development(const Province& p);  // 1 + farm+workshop+depot+fort+powder mill
// Level costs and upkeep scale by level (catalog cost_multiplier_pct: 100, 250, 600 by default).
PA250_API int level_cost_pct(int level_index);
// Cartridges one unit of magazine powder makes up into when an army forms up (the craft ratio).
PA250_API int cartridges_per_powder();
// What a town's magazine is worth to an army forming up there, in cartridges: the cartridges it
// holds plus its powder made up. Pure; no workshop bonus.
PA250_API int magazine_cartridges(const CampaignState& state, const std::string& province_id);
// Terrain gates (catalog requires_tags). True when the town has one of the tags, or the building
// asks for none. A building already standing on the wrong ground is "grandfathered": it keeps
// working and may be upgraded; only new construction is refused.
PA250_API bool building_ground_ok(const Province& p, const std::string& building_id);
PA250_API bool building_grandfathered(const Province& p, const std::string& building_id);
// A scenario, or a save written before the ground rules, may hold a building on ground that no longer
// suits it (or a Depot with no harbour or crossroads to trade through). Each such building is marked
// in Province.grandfathered and logged once; it keeps working, earning and upgrading. load_scenario
// and deserialize call this; a save written under the rules ("ground_rules" key) is left alone.
// Returns how many buildings were newly marked.
PA250_API int mark_grandfathered_buildings(CampaignState& s);
// Powder is bought through a Depot in a harbour town (catalog powder_purchase).
PA250_API bool can_buy_powder(const CampaignState& state, const std::string& province_id);
PA250_API int effective_supply_value(const Province& p);  // supply_value + farm_level
PA250_API int development_cost(const std::string& track, int current_level);  // gold, -1 = unknown track
PA250_API int development_max_level();
// Legacy name: now the same timed start as start_construction (gold on start, level on completion).
PA250_API CampaignState develop_province(const CampaignState& state, const std::string& province_id,
                               const std::string& track, std::string& error);
// Player-facing preview strings (costs/effects) for HUD tooltips — never mutate state.
PA250_API std::string development_preview(const CampaignState& state, const std::string& province_id,
                                const std::string& track);
PA250_API std::string aftermath_choice_preview(const CampaignState& state, const std::string& choice);

// City View building catalog / sizes (optional JSON; defaults apply if unloaded).
PA250_API void load_buildings_catalog(const std::string& json_text);
PA250_API void load_province_sizes(const std::string& json_text);
PA250_API bool buildings_catalog_loaded();
PA250_API bool province_sizes_loaded();
PA250_API int province_slot_count(const std::string& province_id);
PA250_API int province_slots_occupied(const CampaignState& state, const std::string& province_id);
PA250_API int building_level(const Province& p, const std::string& building_id);
PA250_API bool building_present(const Province& p, const std::string& building_id);
PA250_API int building_cost(const std::string& building_id, int current_level);  // -1 unknown
PA250_API int building_build_hours(const std::string& building_id);
PA250_API std::string building_preview(const CampaignState& state, const std::string& province_id,
                             const std::string& building_id);
// Start a timed build or upgrade. Gold charged on start. error codes match develop_province plus
// no_free_slot / already_present / already_building / unknown_building.
PA250_API CampaignState start_construction(const CampaignState& state, const std::string& province_id,
                                 const std::string& building_id, std::string& error);
PA250_API CampaignState cancel_construction(const CampaignState& state, const std::string& province_id,
                                  const std::string& building_id, std::string& error);
PA250_API CampaignState demolish_building(const CampaignState& state, const std::string& province_id,
                                const std::string& building_id, std::string& error);
PA250_API void tick_construction_jobs(CampaignState& s);
PA250_API void buildings_monthly_tick(CampaignState& s);  // upkeep + church Support drip
PA250_API int building_battle_morale(const Province& p);  // Meeting House morale for battle setup
// Raising troops (GAME INVENTION). A town the player holds musters one company at a time: gold and
// stores are charged on start, the men arrive after the catalog's days, drawn from the town's
// monthly manpower. Militia need a Militia Muster in the town; regulars need a Fort and muskets
// from the town magazine. A company belongs to a faction (the other side's companies are
// unknown_unit); rifle companies wait for the Continental chapter (not_yet). Unit classes in play:
// militia, line_infantry, rifleman, officer, dragoon (Continental); line_infantry, grenadier, light_infantry,
// officer, dragoon (British). Dragoons are a GAME INVENTION for 1775 (they ride, then fight on foot). error codes: unknown_unit / unknown_province / not_your_province / not_yet /
// battle_pending / already_mustering / needs_building / no_manpower / insufficient_gold /
// insufficient_muskets / insufficient_provisions / no_muster.
PA250_API int province_manpower_cap(const CampaignState& state, const std::string& province_id);
PA250_API std::string recruitment_preview(const CampaignState& state, const std::string& province_id,
                                const std::string& unit_id);
PA250_API CampaignState start_recruitment(const CampaignState& state, const std::string& province_id,
                                const std::string& unit_id, std::string& error);
PA250_API CampaignState cancel_recruitment(const CampaignState& state, const std::string& province_id,
                                 std::string& error);
PA250_API void tick_recruitment_jobs(CampaignState& s);
// A company the player raised that stands idle in a town with another of the player's armies is
// absorbed by it (a battle is set up between one army per side). Scripted armies are never absorbed.
PA250_API void merge_idle_player_armies(CampaignState& s);
// Treasury (GAME INVENTION, after the bills of credit Massachusetts issued in 1775). On the 1st of
// each month the province settles: town levies, depot trade and workshop output come in; army
// pay, building upkeep and interest on debt go out. A shortfall is borrowed automatically up to
// the credit limit; past it the army goes unpaid (morale and Support fall).
PA250_API int town_levy_gold(const Province& p);                    // monthly gold from one town
PA250_API int treasury_credit_limit(const CampaignState& state);    // most the province may owe
PA250_API int monthly_net_gold(const CampaignState& state);         // projected income - expenses
PA250_API std::string treasury_json(const CampaignState& state);    // balance, debt, every line of the budget
// Borrow / repay one loan step. error: battle_pending / credit_exhausted / no_debt / insufficient_gold.
PA250_API CampaignState take_loan(const CampaignState& state, std::string& error);
PA250_API CampaignState repay_loan(const CampaignState& state, std::string& error);
PA250_API void settle_monthly_budget(CampaignState& s);  // called from buildings_monthly_tick
// The other side keeps the same accounts (levies, depot trade, workshop muskets, army pay, building
// upkeep) from its own war chest, plus a subsidy and provisions by sea while it controls the
// harbour and holds its headquarters town. It cannot borrow: an empty chest means an unpaid army
// (morale falls). With gold to spare it raises one company a month from the catalog, armed and fed
// from its own magazine. Called from buildings_monthly_tick after the player's settlement.
PA250_API std::string enemy_faction(const CampaignState& state);
PA250_API int enemy_monthly_net_gold(const CampaignState& state);
PA250_API void settle_enemy_budget(CampaignState& s);
// Read-only City View snapshot (slots, options, totals) for UE HUD.
PA250_API std::string city_view_json(const CampaignState& state, const std::string& province_id);

// Start a cartridge job: reserves powder/lead/paper and commits workers until complete_on_hour.
PA250_API CampaignState start_cartridge_job(const CampaignState& state, const std::string& province_id,
                                  const std::string& army_id, int workers, int powder, int lead, int paper,
                                  std::string& error);
PA250_API void tick_cartridge_jobs(CampaignState& s);

// Player chooses among purchase | request_assistance | requisition | capacity_investment | knox_support | dismiss
// | source_decision choice ids | acknowledge for historical_notice.
// letter_id empty → use the pending aftermath choice letter.
PA250_API CampaignState choose_logistics(const CampaignState& state, const std::string& letter_id,
                               const std::string& choice);
// Clarify / counterorder / escalate a detached commander's request.
PA250_API CampaignState resolve_commander_request(const CampaignState& state, const std::string& action);
// Tick correspondence deliveries and dated chapter/project gates. Only a letter that needs an
// answer right now (letter_is_timely) adds EventPopup; everything else waits on the desk.
PA250_API void tick_logistics(CampaignState& s, std::vector<Interrupt>& interrupts);
// True for the one kind of letter that stops the clock: the detached commander asking for orders.
PA250_API bool letter_is_timely(const CorrespondenceItem& letter);
// Hour at which an unanswered desk letter is filed as set aside; 0 = it never lapses.
PA250_API int64_t letter_lapse_hour(const CorrespondenceItem& letter);

// Opening-war correspondence catalog (immutable). Load once from data JSON text.
PA250_API void load_correspondence_catalog(const std::string& json_text);
PA250_API bool correspondence_catalog_loaded();
PA250_API const CorrespondenceCatalogEntry* find_correspondence_entry(const std::string& source_id);
PA250_API std::vector<CorrespondenceCatalogEntry> correspondence_catalog_entries();
// The detached commander's letter: its answers, and the town its factors stand at (empty when the
// catalog has no commander_letter block).
PA250_API std::vector<CommanderLetterChoice> commander_letter_choices();
PA250_API std::string commander_letter_province();
PA250_API void ensure_donor_reserve_defaults(CampaignState& s);
PA250_API void seed_opening_war_correspondence(CampaignState& s, std::vector<Interrupt>* interrupts);
PA250_API void release_due_personnel_commitments(CampaignState& s);
PA250_API void deliver_due_shipments(CampaignState& s, std::vector<Interrupt>* interrupts);
PA250_API CampaignState apply_source_decision(const CampaignState& state, const std::string& letter_id,
                                    const std::string& choice);

// ---- Diplomacy ---------------------------------------------------------
PA250_API DiplomacyContext build_diplomacy_context(const CampaignState& state, const std::string& faction);
// Returns error strings of the form "code: detail"; empty means valid. spoken_line is never read.
PA250_API std::vector<std::string> validate_proposal(const DiplomacyContext& context,
                                           const DiplomacyProposal& proposal);
PA250_API CampaignState apply_diplomacy(const CampaignState& state, const DiplomacyResult& result);
// Talks ended without agreement: the envoy likes the player less and may snub them.
PA250_API CampaignState register_diplomacy_failure(const CampaignState& state, const std::string& faction);

// ---- JSON bridges (strings in, strings out; nlohmann stays out of this header) ----
PA250_API std::string context_to_json(const DiplomacyContext& context);
PA250_API DiplomacyContext context_from_json(const std::string& json_text);   // throws
// Parses a proposal; structural problems come back as "code: detail" errors.
PA250_API std::vector<std::string> proposal_from_json(const std::string& json_text, DiplomacyProposal& out);
PA250_API std::string proposal_to_json(const DiplomacyProposal& proposal);
PA250_API std::string setup_to_json(const BattleSetup& setup);
PA250_API BattleSetup setup_from_json(const std::string& json_text);          // throws
PA250_API std::string result_to_json(const BattleResult& result);
PA250_API BattleResult result_from_json(const std::string& json_text);        // throws
PA250_API std::string report_to_json(const BattleReport& report);

}  // namespace pa250
