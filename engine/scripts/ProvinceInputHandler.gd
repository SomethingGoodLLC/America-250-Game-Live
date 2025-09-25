extends Node
class_name ProvinceInputHandler

## Province Input Handler for managing province selection and input routing
## Fixes the issue where CameraController consumes input before provinces can handle it

signal province_selected(province: Province)
signal province_deselected(province: Province)

var selected_province: Province = null
var hovered_province: Province = null

func _ready():
	print("=== Province Input Handler (NEW) ===")
	print("🔧 ProvinceInputHandler: Area2D input priority enabled")
	print("Province input handler initialized, waiting for provinces to load...")
	
	# Set process priority to handle input before camera
	process_priority = 100  # Higher priority than camera controller

func _unhandled_input(event):
	"""Handle input that wasn't consumed by UI or other systems"""
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_LEFT:
			_handle_province_click(event.position)

func _handle_province_click(screen_pos: Vector2):
	"""Handle province clicking with proper input routing"""
	# Convert screen position to world position
	var camera = get_viewport().get_camera_2d()
	var world_pos = screen_pos
	if camera:
		world_pos = camera.get_global_mouse_position()
	
	print("🎮 ProvinceInputHandler: Checking click at world pos: %s" % world_pos)
	
	# Query for provinces at this position
	var space_state = get_world_2d().direct_space_state
	var query = PhysicsPointQueryParameters2D.new()
	query.position = world_pos
	query.collision_mask = 1  # Province layer
	
	var results = space_state.intersect_point(query)
	
	# Find the first province in results
	var clicked_province: Province = null
	for result in results:
		if result.collider is Province:
			clicked_province = result.collider as Province
			break
	
	if clicked_province:
		print("🎯 ProvinceInputHandler: Found province %s at click position" % clicked_province.name)
		_select_province(clicked_province)
		
		# Mark input as handled to prevent camera from processing it
		get_viewport().set_input_as_handled()
	else:
		print("🎮 ProvinceInputHandler: No province found at click position")
		# Deselect current province if clicking empty space
		if selected_province:
			_deselect_current_province()

func _select_province(province: Province):
	"""Select a province and handle the selection logic"""
	if selected_province == province:
		print("🔄 Province %s already selected" % province.name)
		return
	
	# Deselect current province
	if selected_province:
		_deselect_current_province()
	
	# Select new province
	selected_province = province
	province.set_selected(true)
	
	print("✅ ProvinceInputHandler: Selected province %s" % province.name)
	province_selected.emit(province)
	
	# Notify game systems
	if ProvinceManager:
		ProvinceManager.on_province_selected(province)

func _deselect_current_province():
	"""Deselect the currently selected province"""
	if selected_province:
		var old_province = selected_province
		selected_province.set_selected(false)
		selected_province = null
		
		print("❌ ProvinceInputHandler: Deselected province %s" % old_province.name)
		province_deselected.emit(old_province)
		
		# Notify game systems
		if ProvinceManager:
			ProvinceManager.on_province_deselected(old_province)

func get_selected_province() -> Province:
	"""Get the currently selected province"""
	return selected_province

func deselect_all():
	"""Deselect all provinces"""
	if selected_province:
		_deselect_current_province()

# Debug functions
func _input(event):
	"""Debug input handler to catch all input"""
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_LEFT:
			print("🔍 ProvinceInputHandler: Raw input detected at %s" % event.position)

func debug_province_at_position(world_pos: Vector2) -> Province:
	"""Debug function to check what province is at a position"""
	var space_state = get_world_2d().direct_space_state
	var query = PhysicsPointQueryParameters2D.new()
	query.position = world_pos
	query.collision_mask = 1
	
	var results = space_state.intersect_point(query)
	for result in results:
		if result.collider is Province:
			return result.collider as Province
	
	return null
