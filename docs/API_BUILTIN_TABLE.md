# 内置函数总表（由源码自动生成，共 370 个）
# 内置函数总表（自动生成，共 370 个）
## runtime 模组（59）
`sqrt` `round` `int` `float` `str` `bool` `len` `size` `list` `sum` `push` `pop` `join` `split` `chars` `ord` `chr` `keys` `has` `remove` `substr` `replace` `startswith` `endswith` `trim` `upper` `lower` `index` `type` `args` `vm_exec` `usage` `match` `range` `load_params` `save_params` `list_params` `spi_meta` `spi_on` `spi_emit` `mod_limit` `mod_usage` `spi_caps` `spi_has` `spi_mods` `gc_auto` `gc_now` `atomic_add` `atomic_get` `atomic_set` `entity_spawn` `entity_kill` `entity_set` `entity_get` `entity_count` `entity_clear` `entity_neighbors` `entity_at` `gc_stats`

## vm 模组（10）
`dbg_set_active` `dbg_active` `dbg_wait_boundary` `dbg_resume` `dbg_pause_now` `dbg_break` `dbg_ip` `dbg_disasm` `dbg_var` `dbg_exec`

## infiverse_mod 模组（24）
`verse_use` `verse_block_set` `verse_block_get` `verse_block_count` `verse_link` `verse_neighbors` `verse_dial` `verse_portal_set` `verse_portal_get` `verse_entity_put` `verse_entity_remove` `verse_nearby` `verse_law_set` `verse_law_get` `verse_snapshot` `verse_biome_add` `verse_biome_id` `verse_biome_name` `verse_biome_info` `verse_biome_children` `verse_biome_ancestors` `verse_biome_set` `verse_biome_get` `verse_biome_palette`

## io_mod 模组（44）
`read_file` `file_exists` `mkdir` `write_file` `input` `exec` `random` `io_list_dir` `http_get` `http_post` `serial_open` `serial_write` `serial_read` `serial_close` `key_press` `mouse_move` `mouse_click` `copy_file` `str2int` `clipboard_set` `clipboard_get` `timer_ms` `exec_async` `proc_list` `proc_kill` `proc_kill_tag` `proc_alive` `proc_prune` `ai_ask` `ai_text` `ai_vision` `ai_models` `ai_status` `ai_busy` `ai_lock` `ai_unlock` `ai_start` `ai_done` `ai_result` `ai_progress` `ai_cancel` `ai_wait_task` `ai_code` `stdin_ready`

## gui_mod 模组（163）
`window` `gui_fullscreen` `gui_window_size` `gui_display_size` `gui_viewport` `show_image` `gui_wait` `gui_quit_on_escape` `gui_fullscreen` `gui_resize` `gui_width` `gui_height` `gui_screen_w` `gui_screen_h` `gui_title` `gui_minimize` `gui_maximize` `gui_restore` `gui_is_maximized` `gui_resizable` `gui_topmost` `gui_window_pos` `gui_window_x` `gui_window_y` `gui_show_window` `gui_caption` `gui_alpha` `gui_icon` `gui_center` `gui_quit` `gui_open_folder` `gui_console` `gui_set_font` `gui_progress` `gui_progress_set` `gui_input` `gui_button` `gui_button_clicked` `gui_button_show` `gui_button_text` `gui_button_pos` `gui_log_show` `gui_log_p` `gui_input_p` `gui_button_p` `gui_input_text` `gui_input_set` `gui_input_focus` `gui_input_show` `gui_clicked` `gui_click_x` `gui_click_y` `gui_input_submitted` `gui_log` `gui_log_clear` `gui_scroll` `gui_wheel` `gui_edit` `gui_edit_text` `gui_edit_set` `gui_edit_load` `gui_edit_save` `gui_edit_find` `gui_edit_replace` `gui_edit_line` `gui_edit_col` `gui_edit_sel` `gui_edit_undo` `gui_edit_redo` `gui_edit_select_all` `gui_edit_pos` `gui_open_file` `gui_save_file` `gui_fixed` `gui_ghost` `gui_clickable` `gui_drag` `gui_secret` `clicked` `gui_stage` `gui_background` `gui_sprite` `gui_bind` `gui_goto` `gui_move` `gui_show` `gui_hide` `gui_box` `gui_costume` `gui_face` `gui_turn` `gui_point_to` `gui_size` `gui_velocity` `gui_gravity` `gui_bounce` `gui_say` `gui_text` `gui_clear_text` `gui_rect` `gui_frame` `gui_hline` `gui_vline` `gui_text_a` `gui_panel` `gui_clear_shapes` `gui_rect_p` `gui_frame_p` `entity_render` `gui_hline_p` `gui_vline_p` `gui_text_a_p` `gui_panel_p` `gui_w` `gui_h` `gui_debug_layout` `gui_theme` `gui_color_rgb` `gui_broadcast` `gui_wait_broadcast` `gui_clone` `gui_delete` `x` `y` `vx` `vy` `touch` `key` `mouse_x` `mouse_y` `mouse_down` `timer` `wheel` `screenshot` `layout_desc` `round_rect` `gradient` `shadow` `edge` `rand` `self` `gui_sound` `gui_music` `im2d_init` `im2d_camera` `im2d_goto_scene` `im2d_dt` `im2d_cam_x` `im2d_cam_y` `im2d_cam_zoom` `im2d_world_to_screen` `im2d_draw_sprite` `im2d_draw_rect` `gui_circle` `gui_canvas` `gui_px` `gui_canvas_clear` `gui_canvas_save` `gui_canvas_sprite` `gui_vram_used` `gui_vram_limit` `noise2d` `gui_frame_interval`

## verse_dist_mod 模组（19）
`verse_open` `verse_pack` `verse_share` `verse_listen` `verse_stop` `verse_hub_list` `verse_update` `verse_hub_add` `verse_hub_remove` `verse_hubs` `verse_hub_ping` `verse_public_ip` `verse_publish` `verse_identity_new` `verse_identity_pubkey` `verse_sign` `verse_verify` `verse_list` `verse_remove`

## ai_mod 模组（6）
`ai_config` `ai_register` `ai_list` `ai_chat` `ai_code_check` `ai_params`

## net_mod 模组（11）
`net_connect` `net_listen` `net_accept` `net_send` `net_recv` `net_status` `net_close` `udp_bind` `udp_send` `udp_recv` `udp_close`

## server_mod 模组（10）
`server_ports` `port_check` `port_pid` `port_kill` `lan_ip` `server_start` `server_join` `server_status` `server_stop` `server_rooms`

## record_mod 模组（12）
`save` `gui_autosave` `tag_register` `tagged` `tag_count` `load` `record_dirty` `record_mark_clean` `record_value` `record_set_value` `record_get` `record_snapshot`

## social_mod 模组（5）
`friend_add` `friend_list` `friend_remove` `chat_send` `chat_history`

## identity_mod 模组（3）
`me` `profile_set` `profile_get`

## json_mod 模组（2）
`json_serialize` `json_parse`

## isolate_mod 模组（1）
`isolate_run`

## lint_mod 模组（1）
`lint_check`
