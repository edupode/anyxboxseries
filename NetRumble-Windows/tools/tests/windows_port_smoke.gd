extends Node

## Runs the actual Windows port with its autoloads and local save directory.
const TEST_VOLUME := 0.37


func _ready() -> void:
	_run.call_deferred()


func _run() -> void:
	var menu: NRScreen = await _wait_for_screen(ScreenManager.MAIN_MENU)
	if not _check(menu != null and Services.is_account_ready(), "Local profile and main menu"):
		return
	if "--reload" in OS.get_cmdline_user_args():
		if _check(is_equal_approx(PlayerProfile.music_volume, TEST_VOLUME), "Settings survived a restart"):
			await _quit_from(menu)
		return

	if not _check(_press_text(menu, "Options"), "Open Options"):
		return
	PlayerProfile.music_volume = TEST_VOLUME
	PlayerProfile.notify_settings_changed()
	if not _check(await _press_after_layout(menu, "Back"), "Save Options"):
		return
	if not _check(await _press_after_layout(menu, "Practice"), "Start Practice from the menu"):
		return
	var lobby: NRScreen = await _wait_for_screen(ScreenManager.LOBBY)
	if not _check(lobby != null and NetManager.is_offline(), "Offline practice lobby"):
		return
	await _tap_controller_button(JOY_BUTTON_X)
	var gameplay: NRScreen = await _wait_for_screen(ScreenManager.GAMEPLAY)
	if not _check(gameplay != null, "Controller Ready starts gameplay"):
		return
	var started := await _wait_for_match()
	var world := gameplay.get("_world") as World
	if not _check(started and world != null and world.local_ship != null, "Practice match and local ship"):
		return
	if DisplayServer.get_name() != "headless":
		await RenderingServer.frame_post_draw
		var frame := get_viewport().get_texture().get_image()
		if not _check(frame != null and not frame.is_empty() and not frame.is_invisible(), "Draw the Practice frame"):
			return
	if not _check(world.get_ships().size() > 1, "Practice has opponents"):
		return

	var ship := world.local_ship
	var origin := ship.position
	_set_controller_axis(JOY_AXIS_LEFT_X, 1.0)
	_set_controller_axis(JOY_AXIS_RIGHT_X, 1.0)
	await get_tree().create_timer(0.35).timeout
	_set_controller_axis(JOY_AXIS_LEFT_X, 0.0)
	_set_controller_axis(JOY_AXIS_RIGHT_X, 0.0)
	if not _check(is_instance_valid(ship) and ship.position.distance_to(origin) > 1.0, "Controller moves the ship"):
		return
	var local_projectile := false
	for projectile: Projectile in world.active_projectiles.values():
		if projectile.owner_id == ship.unique_id:
			local_projectile = true
			break
	if not _check(local_projectile, "Controller fires a projectile"):
		return

	await _tap_controller_button(JOY_BUTTON_START)
	var pause_menu: NRScreen = await _wait_for_screen(ScreenManager.GAME_MENU)
	if not _check(pause_menu != null and _press_text(pause_menu, "Leave Match"), "Pause and leave match"):
		return
	if not await _confirm_dialog():
		return
	menu = await _wait_for_screen(ScreenManager.MAIN_MENU)
	if not _check(menu != null and not NetManager.has_session(), "Return to main menu and close session"):
		return
	await _quit_from(menu)


func _quit_from(menu: NRScreen) -> void:
	if _check(_press_text(menu, "Quit"), "Quit from the main menu") and await _confirm_dialog():
		print("PASS Windows port smoke test")


func _confirm_dialog() -> bool:
	var dialog: NRScreen = await _wait_for_screen(ScreenManager.DIALOG_BOX)
	if not _check(dialog != null, "Confirmation dialog"):
		return false
	var confirm := dialog.get_node_or_null("%OkButton") as Button
	if not _check(confirm != null, "Confirmation button"):
		return false
	confirm.pressed.emit()
	return true


func _wait_for_screen(path: String) -> NRScreen:
	var deadline := Time.get_ticks_msec() + 8000
	while Time.get_ticks_msec() < deadline:
		var screen := ScreenManager.current_screen()
		if screen != null and screen.scene_file_path == path:
			return screen
		await get_tree().process_frame
	return null


func _wait_for_match() -> bool:
	var deadline := Time.get_ticks_msec() + 12000
	while Time.get_ticks_msec() < deadline:
		if NetManager.match_state == NRTypes.MatchState.RUNNING:
			return true
		await get_tree().physics_frame
	return false


func _press_after_layout(screen: NRScreen, label: String) -> bool:
	await get_tree().process_frame
	return _press_text(screen, label)


func _press_text(screen: NRScreen, label: String) -> bool:
	for button: Button in screen.find_children("*", "Button", true, false):
		if button.text == label and button.is_visible_in_tree() and not button.disabled:
			button.pressed.emit()
			return true
	return false


func _tap_controller_button(button: JoyButton) -> void:
	var event := InputEventJoypadButton.new()
	event.button_index = button
	event.pressed = true
	Input.parse_input_event(event)
	await get_tree().process_frame
	var release := InputEventJoypadButton.new()
	release.button_index = button
	Input.parse_input_event(release)


func _set_controller_axis(axis: JoyAxis, value: float) -> void:
	var event := InputEventJoypadMotion.new()
	event.axis = axis
	event.axis_value = value
	Input.parse_input_event(event)


func _check(condition: bool, step: String) -> bool:
	if condition:
		print("PASS ", step)
	else:
		push_error("FAIL " + step)
		get_tree().quit(1)
	return condition
