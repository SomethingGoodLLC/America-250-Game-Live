extends Node
class_name ProvinceManager

## Province Manager singleton for handling province registration and management
## Fixes the province input issues by centralizing province management

signal province_registered(province: Province)
signal province_selected(province: Province)
signal province_deselected(province: Province)

var provinces: Dictionary = {}  # province_name -> Province
var selected_province: Province = null
var input_handler: ProvinceInputHandler = null

func _ready():
	print("ProvinceManager singleton initialized")
	
	# Create input handler
	input_handler = ProvinceInputHandler.new()
	add_child(input_handler)
	
	# Connect input handler signals
	input_handler.province_selected.connect(_on_province_selected)
	input_handler.province_deselected.connect(_on_province_deselected)

func register_province(province: Province):
	"""Register a province with the manager"""
	if not province:
		print("⚠️ ProvinceManager: Attempted to register null province")
		return
	
	provinces[province.name] = province
	
	# Connect province signals
	province.province_clicked.connect(_on_province_clicked)
	province.province_selected.connect(_on_province_selected)
	
	print("📋 ProvinceManager: Registered province %s (total: %d)" % [province.name, provinces.size()])
	province_registered.emit(province)

func get_province(province_name: String) -> Province:
	"""Get a province by name"""
	return provinces.get(province_name, null)

func get_all_provinces() -> Array[Province]:
	"""Get all registered provinces"""
	var result: Array[Province] = []
	for province in provinces.values():
		result.append(province)
	return result

func deselect_all_provinces():
	"""Deselect all provinces"""
	for province in provinces.values():
		if province.is_selected:
			province.set_selected(false)
	
	selected_province = null
	print("❌ ProvinceManager: Deselected all provinces")

func select_province(province_name: String) -> bool:
	"""Select a province by name"""
	var province = get_province(province_name)
	if province:
		_select_province(province)
		return true
	else:
		print("⚠️ ProvinceManager: Province '%s' not found" % province_name)
		return false

func _select_province(province: Province):
	"""Internal province selection logic"""
	if selected_province == province:
		return  # Already selected
	
	# Deselect current province
	if selected_province:
		selected_province.set_selected(false)
		province_deselected.emit(selected_province)
	
	# Select new province
	selected_province = province
	province.set_selected(true)
	province_selected.emit(province)
	
	print("✅ ProvinceManager: Selected province %s" % province.name)

func _on_province_clicked(province: Province):
	"""Handle province click events"""
	print("🎯 ProvinceManager: Province %s clicked" % province.name)
	_select_province(province)

func _on_province_selected(province: Province):
	"""Handle province selection events"""
	selected_province = province
	province_selected.emit(province)

func _on_province_deselected(province: Province):
	"""Handle province deselection events"""
	if selected_province == province:
		selected_province = null
	province_deselected.emit(province)

func on_province_selected(province: Province):
	"""Called by ProvinceInputHandler when a province is selected"""
	selected_province = province
	province_selected.emit(province)

func on_province_deselected(province: Province):
	"""Called by ProvinceInputHandler when a province is deselected"""
	if selected_province == province:
		selected_province = null
	province_deselected.emit(province)

# Debug functions
func debug_print_provinces():
	"""Debug function to print all registered provinces"""
	print("🔍 ProvinceManager: Registered provinces (%d):" % provinces.size())
	for name in provinces.keys():
		var province = provinces[name]
		print("  - %s at %s (selected: %s)" % [name, province.position, province.is_selected])

func debug_test_province_input():
	"""Debug function to test province input handling"""
	print("🧪 ProvinceManager: Testing province input...")
	
	for province in provinces.values():
		print("  Testing province: %s" % province.name)
		print("    input_pickable: %s" % province.input_pickable)
		print("    collision_layer: %s" % province.collision_layer)
		print("    position: %s" % province.position)
		
		# Test if collision polygon exists
		var collision_poly = province.get_node("CollisionPolygon2D") if province.has_node("CollisionPolygon2D") else null
		if collision_poly:
			print("    collision_polygon points: %d" % collision_poly.polygon.size())
		else:
			print("    ⚠️ No CollisionPolygon2D found!")

# Utility functions for game integration
func get_province_data() -> Dictionary:
	"""Get data for all provinces"""
	var data = {}
	for name in provinces.keys():
		data[name] = provinces[name].get_province_data()
	return data

func load_province_data(data: Dictionary):
	"""Load province data from save game"""
	for name in data.keys():
		var province = get_province(name)
		if province:
			province.set_province_data(data[name])
