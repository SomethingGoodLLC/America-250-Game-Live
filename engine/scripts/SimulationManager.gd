extends Node
class_name SimulationManager

## Simulation Manager - Authoritative, deterministic game state management
## Handles all game logic and state changes in a deterministic manner

signal game_state_changed(new_state: Dictionary)
signal province_ownership_changed(province_name: String, new_owner: String)
signal diplomatic_outcome_applied(report: Dictionary)

var game_state: Dictionary = {}
var current_turn: int = 1
var current_day: int = 1
var rng_seed: int = 0

func _ready():
	print("✅ SimulationManager initialized - Authoritative game state ready")
	_initialize_game_state()

func _initialize_game_state():
	"""Initialize the deterministic game state"""
	# Generate timestamp-based seed for determinism
	rng_seed = Time.get_unix_time_from_system() as int
	print("Generated timestamp-based seed: %d" % rng_seed)
	
	# Initialize core game state
	game_state = {
		"turn": current_turn,
		"day": current_day,
		"seed": rng_seed,
		"provinces": {},
		"countries": {},
		"diplomatic_relations": {},
		"active_negotiations": {},
		"game_events": []
	}
	
	print("✅ Achilles simulation initialized on day %d with seed %d" % [current_day, rng_seed])

func apply_diplomatic_outcome(game_state_input: Dictionary, negotiation_report: Dictionary) -> Dictionary:
	"""Apply diplomatic negotiation outcomes to game state (pure function)"""
	print("🤝 Applying diplomatic outcome from negotiation report")
	
	var new_state = game_state_input.duplicate(true)
	
	# Extract intents and justifications from report
	var intents = negotiation_report.get("intents", [])
	var justifications = negotiation_report.get("justifications", [])
	
	for i in range(intents.size()):
		var intent = intents[i]
		var justification = justifications[i] if i < justifications.size() else {}
		
		# Apply intent based on type
		match intent.get("type", ""):
			"Proposal":
				new_state = _apply_proposal(new_state, intent, justification)
			"CounterOffer":
				new_state = _apply_counter_offer(new_state, intent, justification)
			"Ultimatum":
				new_state = _apply_ultimatum(new_state, intent, justification)
			"Concession":
				new_state = _apply_concession(new_state, intent, justification)
			"SmallTalk":
				# Small talk doesn't affect game state
				pass
	
	# Update turn/day
	new_state["turn"] = new_state.get("turn", 1) + 1
	
	# Emit signals for UI updates
	diplomatic_outcome_applied.emit(negotiation_report)
	game_state_changed.emit(new_state)
	
	return new_state

func _apply_proposal(state: Dictionary, intent: Dictionary, justification: Dictionary) -> Dictionary:
	"""Apply a diplomatic proposal to game state"""
	print("📜 Applying proposal: %s" % intent.get("summary", "Unknown"))
	
	# Example: Trade agreement proposal
	if "trade" in intent.get("summary", "").to_lower():
		var relations = state.get("diplomatic_relations", {})
		var initiator = intent.get("initiator_faction", "")
		var target = intent.get("target_faction", "")
		
		if initiator and target:
			if not relations.has(initiator):
				relations[initiator] = {}
			relations[initiator][target] = relations[initiator].get(target, 0) + 10
			
			print("📈 Improved relations between %s and %s" % [initiator, target])
	
	return state

func _apply_counter_offer(state: Dictionary, intent: Dictionary, justification: Dictionary) -> Dictionary:
	"""Apply a counter-offer to game state"""
	print("🔄 Applying counter-offer: %s" % intent.get("summary", "Unknown"))
	
	# Counter-offers typically modify existing proposals
	var confidence = justification.get("confidence", 0.5)
	
	# Higher confidence counter-offers have more impact
	if confidence > 0.7:
		# Significant diplomatic shift
		pass
	
	return state

func _apply_ultimatum(state: Dictionary, intent: Dictionary, justification: Dictionary) -> Dictionary:
	"""Apply an ultimatum to game state"""
	print("⚡ Applying ultimatum: %s" % intent.get("summary", "Unknown"))
	
	# Ultimatums can trigger war or major diplomatic changes
	var severity = justification.get("severity", "medium")
	
	if severity == "high":
		# Potential war declaration
		var relations = state.get("diplomatic_relations", {})
		var initiator = intent.get("initiator_faction", "")
		var target = intent.get("target_faction", "")
		
		if initiator and target:
			if not relations.has(initiator):
				relations[initiator] = {}
			relations[initiator][target] = -50  # Hostile relations
			
			print("⚔️ Hostile relations declared between %s and %s" % [initiator, target])
	
	return state

func _apply_concession(state: Dictionary, intent: Dictionary, justification: Dictionary) -> Dictionary:
	"""Apply a concession to game state"""
	print("🤝 Applying concession: %s" % intent.get("summary", "Unknown"))
	
	# Concessions improve relations and may transfer resources/territory
	var relations = state.get("diplomatic_relations", {})
	var initiator = intent.get("initiator_faction", "")
	var target = intent.get("target_faction", "")
	
	if initiator and target:
		if not relations.has(initiator):
			relations[initiator] = {}
		relations[initiator][target] = relations[initiator].get(target, 0) + 20
		
		print("🕊️ Concession improved relations between %s and %s" % [initiator, target])
	
	return state

func get_current_game_state() -> Dictionary:
	"""Get the current authoritative game state"""
	return game_state.duplicate(true)

func set_game_state(new_state: Dictionary):
	"""Set the game state (for loading saves, etc.)"""
	game_state = new_state.duplicate(true)
	current_turn = game_state.get("turn", 1)
	current_day = game_state.get("day", 1)
	
	game_state_changed.emit(game_state)
	print("📊 Game state updated to turn %d, day %d" % [current_turn, current_day])

func advance_turn():
	"""Advance to the next turn"""
	current_turn += 1
	game_state["turn"] = current_turn
	
	# Process turn-based events
	_process_turn_events()
	
	game_state_changed.emit(game_state)
	print("⏭️ Advanced to turn %d" % current_turn)

func _process_turn_events():
	"""Process events that happen each turn"""
	# Economic updates, unit movements, etc.
	pass

# Province management
func get_province_owner(province_name: String) -> String:
	"""Get the owner of a province"""
	var provinces = game_state.get("provinces", {})
	return provinces.get(province_name, {}).get("owner", "")

func set_province_owner(province_name: String, new_owner: String):
	"""Set the owner of a province"""
	var provinces = game_state.get("provinces", {})
	if not provinces.has(province_name):
		provinces[province_name] = {}
	
	var old_owner = provinces[province_name].get("owner", "")
	provinces[province_name]["owner"] = new_owner
	
	province_ownership_changed.emit(province_name, new_owner)
	print("🏰 Province %s ownership changed from %s to %s" % [province_name, old_owner, new_owner])

# Diplomatic relations
func get_diplomatic_relation(country_a: String, country_b: String) -> float:
	"""Get diplomatic relation value between two countries"""
	var relations = game_state.get("diplomatic_relations", {})
	return relations.get(country_a, {}).get(country_b, 0.0)

func set_diplomatic_relation(country_a: String, country_b: String, value: float):
	"""Set diplomatic relation between two countries"""
	var relations = game_state.get("diplomatic_relations", {})
	if not relations.has(country_a):
		relations[country_a] = {}
	relations[country_a][country_b] = value
	
	print("🤝 Diplomatic relation set: %s -> %s = %.1f" % [country_a, country_b, value])

# Save/Load system
func save_game_state() -> Dictionary:
	"""Save the current game state"""
	return {
		"version": "1.0",
		"timestamp": Time.get_unix_time_from_system(),
		"game_state": game_state.duplicate(true)
	}

func load_game_state(save_data: Dictionary) -> bool:
	"""Load game state from save data"""
	if save_data.has("game_state"):
		set_game_state(save_data["game_state"])
		print("📁 Game state loaded successfully")
		return true
	else:
		print("❌ Invalid save data format")
		return false
