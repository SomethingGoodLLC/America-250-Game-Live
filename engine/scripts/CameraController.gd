extends Camera2D
class_name CameraController

## Camera Controller for colonial map navigation
## FIXED: No longer consumes input that should go to provinces

@export var zoom_speed: float = 0.1
@export var zoom_min: float = 0.5
@export var zoom_max: float = 3.0
@export var pan_speed: float = 500.0

var is_dragging: bool = false
var drag_start_pos: Vector2
var camera_start_pos: Vector2

func _ready():
	print("CameraController initialized - Ready for colonial map navigation")
	
	# Set lower process priority so provinces get input first
	process_priority = -100  # Lower priority than ProvinceInputHandler

func _unhandled_input(event):
	"""Handle input that wasn't consumed by provinces or UI"""
	# Only handle input if it wasn't already processed by provinces
	
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT:
			if event.pressed:
				# IMPORTANT: Only start dragging if no province was clicked
				# The ProvinceInputHandler will mark input as handled if a province was clicked
				print("🎮 CameraController: LEFT CLICK at screen pos: %s" % event.position)
				var world_pos = get_global_mouse_position()
				print("🎮 CameraController: World position: %s" % world_pos)
				
				# Start drag operation (this will be cancelled if input was handled by province)
				_start_drag(event.position)
			else:
				_end_drag()
		
		elif event.button_index == MOUSE_BUTTON_RIGHT:
			if event.pressed:
				print("🎮 CameraController: RIGHT CLICK at screen pos: %s" % event.position)
				# Right click for context menu or other actions
		
		elif event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_zoom_in(event.position)
		
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_zoom_out(event.position)
	
	elif event is InputEventMouseMotion:
		if is_dragging:
			_handle_drag(event.position)

func _start_drag(screen_pos: Vector2):
	"""Start camera dragging"""
	# Don't start drag if input was already handled by a province
	if get_viewport().gui_get_focus_owner() != null:
		return
	
	is_dragging = true
	drag_start_pos = screen_pos
	camera_start_pos = global_position

func _handle_drag(screen_pos: Vector2):
	"""Handle camera dragging"""
	if not is_dragging:
		return
	
	var delta = (drag_start_pos - screen_pos) * zoom.x
	global_position = camera_start_pos + delta

func _end_drag():
	"""End camera dragging"""
	is_dragging = false

func _zoom_in(screen_pos: Vector2):
	"""Zoom camera in"""
	var old_zoom = zoom.x
	var new_zoom = clamp(zoom.x + zoom_speed, zoom_min, zoom_max)
	
	if new_zoom != old_zoom:
		_zoom_at_point(screen_pos, new_zoom / old_zoom)

func _zoom_out(screen_pos: Vector2):
	"""Zoom camera out"""
	var old_zoom = zoom.x
	var new_zoom = clamp(zoom.x - zoom_speed, zoom_min, zoom_max)
	
	if new_zoom != old_zoom:
		_zoom_at_point(screen_pos, new_zoom / old_zoom)

func _zoom_at_point(screen_pos: Vector2, zoom_factor: float):
	"""Zoom camera at a specific screen point"""
	var viewport_size = get_viewport().get_visible_rect().size
	var mouse_world_pos = global_position + (screen_pos - viewport_size * 0.5) * zoom.x
	
	zoom *= zoom_factor
	
	var new_mouse_world_pos = global_position + (screen_pos - viewport_size * 0.5) * zoom.x
	global_position += mouse_world_pos - new_mouse_world_pos

# Keyboard controls for camera movement
func _process(delta):
	"""Handle keyboard camera movement"""
	var movement = Vector2.ZERO
	
	if Input.is_action_pressed("ui_left") or Input.is_key_pressed(KEY_A):
		movement.x -= 1
	if Input.is_action_pressed("ui_right") or Input.is_key_pressed(KEY_D):
		movement.x += 1
	if Input.is_action_pressed("ui_up") or Input.is_key_pressed(KEY_W):
		movement.y -= 1
	if Input.is_action_pressed("ui_down") or Input.is_key_pressed(KEY_S):
		movement.y += 1
	
	if movement != Vector2.ZERO:
		global_position += movement.normalized() * pan_speed * zoom.x * delta

# Utility functions
func center_on_province(province: Province):
	"""Center camera on a specific province"""
	if province:
		global_position = province.global_position
		print("📍 CameraController: Centered on province %s" % province.name)

func center_on_position(world_pos: Vector2):
	"""Center camera on a world position"""
	global_position = world_pos
	print("📍 CameraController: Centered on position %s" % world_pos)

func set_zoom_level(new_zoom: float):
	"""Set camera zoom level"""
	zoom = Vector2(clamp(new_zoom, zoom_min, zoom_max), clamp(new_zoom, zoom_min, zoom_max))
	print("🔍 CameraController: Zoom set to %s" % zoom.x)

# Debug functions
func debug_camera_info():
	"""Print camera debug information"""
	print("🔍 CameraController Debug Info:")
	print("  Position: %s" % global_position)
	print("  Zoom: %s" % zoom)
	print("  Is Dragging: %s" % is_dragging)
	print("  Mouse World Pos: %s" % get_global_mouse_position())
