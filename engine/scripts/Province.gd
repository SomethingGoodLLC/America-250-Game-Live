extends Area2D
class_name Province

## Province script for handling input and selection in the grand strategy game
## Based on debug output showing provinces load correctly but input doesn't work

@export var province_name: String = ""
@export var province_id: String = ""
@export var is_selected: bool = false

var collision_polygon: CollisionPolygon2D
var visual_polygon: Polygon2D

signal province_clicked(province: Province)
signal province_selected(province: Province)

func _ready():
	print("🏰 Province %s _ready called" % name)
	print("🏰 Province position: %s" % position)
	
	# CRITICAL: Enable input handling
	input_pickable = true
	
	# Connect input signals
	input_event.connect(_on_input_event)
	mouse_entered.connect(_on_mouse_entered)
	mouse_exited.connect(_on_mouse_exited)
	
	# Set up collision detection
	collision_layer = 1  # Province layer
	collision_mask = 0   # Don't detect other objects
	
	# Find collision polygon child
	collision_polygon = get_node("CollisionPolygon2D") if has_node("CollisionPolygon2D") else null
	visual_polygon = get_node("Polygon2D") if has_node("Polygon2D") else null
	
	if collision_polygon:
		var point_count = collision_polygon.polygon.size()
		print("🔧 Province %s: collision polygon has %d points, input should work" % [name, point_count])
	else:
		print("⚠️ Province %s: No CollisionPolygon2D found!" % name)
	
	# Register with ProvinceManager if it exists
	if ProvinceManager:
		ProvinceManager.register_province(self)
		print("📋 Registered province %s with ProvinceManager" % name)

func _on_input_event(viewport: Viewport, event: InputEvent, shape_idx: int):
	"""Handle input events on this province"""
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_LEFT:
			print("🎯 Province %s clicked!" % name)
			_handle_province_click()
			
			# Emit signals
			province_clicked.emit(self)
			
			# Mark input as handled to prevent camera from processing it
			get_viewport().set_input_as_handled()

func _handle_province_click():
	"""Handle province selection logic"""
	print("📍 Selecting province: %s" % name)
	
	# Deselect other provinces first
	if ProvinceManager:
		ProvinceManager.deselect_all_provinces()
	
	# Select this province
	set_selected(true)
	
	# Emit selection signal
	province_selected.emit(self)
	
	# Update UI or game state
	_update_selection_visual()

func set_selected(selected: bool):
	"""Set the selection state of this province"""
	is_selected = selected
	_update_selection_visual()
	
	if selected:
		print("✅ Province %s selected" % name)
	else:
		print("❌ Province %s deselected" % name)

func _update_selection_visual():
	"""Update visual appearance based on selection state"""
	if visual_polygon:
		if is_selected:
			visual_polygon.modulate = Color.YELLOW  # Highlight selected province
		else:
			visual_polygon.modulate = Color.WHITE   # Normal appearance

func _on_mouse_entered():
	"""Handle mouse hover enter"""
	if not is_selected and visual_polygon:
		visual_polygon.modulate = Color.LIGHT_GRAY  # Hover effect

func _on_mouse_exited():
	"""Handle mouse hover exit"""
	if not is_selected and visual_polygon:
		visual_polygon.modulate = Color.WHITE  # Remove hover effect

# Debug function to test input manually
func _input(event):
	"""Fallback input handler for debugging"""
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		# Check if mouse is over this province
		var space_state = get_world_2d().direct_space_state
		var query = PhysicsPointQueryParameters2D.new()
		query.position = get_global_mouse_position()
		query.collision_mask = collision_layer
		
		var result = space_state.intersect_point(query)
		for body_dict in result:
			if body_dict.collider == self:
				print("🎯 Direct hit detected on province: %s" % name)
				_handle_province_click()
				break

# Utility functions
func get_province_data() -> Dictionary:
	"""Return province data for game logic"""
	return {
		"name": province_name,
		"id": province_id,
		"position": position,
		"selected": is_selected
	}

func set_province_data(data: Dictionary):
	"""Set province data from game state"""
	if data.has("name"):
		province_name = data.name
	if data.has("id"):
		province_id = data.id
	if data.has("selected"):
		set_selected(data.selected)
