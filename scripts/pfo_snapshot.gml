// Save-state snapshots (UFO 50 Android and Windows). Added to the game by android_patch.csx and
// windows_patch.csx; called from libPFO / PFO.dll (android/savestate.inc) through
// pfo_snapshot(command, id): native code finds scripts by their asset name, which is this
// function's name.
//
// The "// uses:" lines name the GameMaker functions this script calls. The patches declare them
// before compiling it: the compiler only knows the functions the game itself calls, and would take
// any other for a variable. A function used here for the first time goes on one of those lines.
// (pfo_runner_mark, pfo_runner_rehide and pfo_runner_settle are libPFO's: the patches add those.)
// uses: layer_tile_get_x layer_tile_get_y layer_tile_get_sprite layer_tile_get_region layer_tile_get_visible
// uses: layer_tile_get_xscale layer_tile_get_yscale layer_tile_get_blend layer_tile_get_alpha layer_tile_exists
// uses: layer_tile_create layer_tile_destroy layer_tile_change layer_tile_x layer_tile_y layer_tile_region
// uses: layer_tile_visible layer_tile_xscale layer_tile_yscale layer_tile_blend layer_tile_alpha layer_create
// uses: draw_get_colour draw_get_alpha draw_get_font draw_get_halign draw_get_valign draw_set_colour
// uses: draw_set_alpha draw_set_font draw_set_halign draw_set_valign gpu_get_blendmode_ext gpu_set_blendmode_ext
// uses: array_pop variable_struct_names_count is_numeric texturegroup_get_status texturegroup_load surface_save buffer_get_surface surface_exists surface_get_width surface_get_height
// uses: array_length array_push string_char_at object_get_name buffer_seek string_length
// uses: instance_change instance_activate_object ds_map_exists ds_map_delete asset_get_index
// uses: instance_number array_sort audio_stop_all buffer_create buffer_delete buffer_md5
// uses: buffer_tell buffer_write camera_get_view_height camera_get_view_width camera_get_view_x
// uses: camera_get_view_y camera_set_view_pos camera_set_view_size camera_get_view_target
// uses: camera_get_view_border_x camera_get_view_border_y camera_get_view_speed_x
// uses: camera_get_view_speed_y camera_get_view_angle camera_set_view_target camera_set_view_border
// uses: camera_set_view_speed camera_set_view_angle ds_exists ds_grid_create ds_grid_get
// uses: ds_grid_height ds_grid_resize ds_grid_set ds_grid_width ds_list_add ds_list_clear
// uses: ds_list_create ds_list_destroy ds_list_find_value ds_list_size ds_map_clear ds_map_create
// uses: ds_map_find_value ds_map_keys_to_array ds_map_set ds_priority_copy ds_priority_create
// uses: ds_priority_size ds_queue_copy ds_queue_create ds_queue_size ds_stack_copy ds_stack_create
// uses: ds_stack_size instance_activate_all instance_create_depth instance_create_layer
// uses: instance_deactivate_object instance_destroy instance_exists is_array is_bool is_int32
// uses: is_int64 is_method is_real is_string is_struct layer_depth layer_exists layer_get_all
// uses: layer_get_all_elements layer_get_depth layer_get_element_type layer_get_hspeed layer_get_id
// uses: layer_get_name layer_get_visible layer_get_vspeed layer_get_x layer_get_y layer_hspeed
// uses: layer_set_visible layer_vspeed layer_x layer_y method method_get_index method_get_self real
// uses: room_get_name room_goto show_debug_message static_get static_set string string_copy
// uses: ds_map_destroy ds_grid_destroy ds_stack_destroy ds_queue_destroy ds_priority_destroy
// uses: surface_resize surface_get_width surface_get_height array_copy script_execute asset_get_index
// uses: path_add path_exists path_get_number path_get_point_x path_get_point_y path_get_point_speed path_get_closed
// uses: path_get_kind path_get_precision path_clear_points path_add_point path_set_closed path_set_kind path_set_precision
// uses: path_start path_end mp_grid_create mp_grid_destroy mp_grid_get_cell mp_grid_clear_all mp_grid_add_cell
// uses: string_delete string_pos struct_remove tilemap_get tilemap_get_height tilemap_get_width
// uses: tilemap_set typeof variable_global_exists variable_global_get variable_global_set
// uses: variable_instance_get variable_instance_get_names variable_instance_set
// uses: variable_struct_exists variable_struct_get variable_struct_get_names variable_struct_set
// uses: sprite_exists sprite_duplicate sprite_collision_mask sprite_delete array_concat
// uses: string_replace_all buffer_read buffer_resize handle_parse instanceof ds_stack_write ds_stack_read
// uses: ds_queue_write ds_queue_read ds_priority_write ds_priority_read
//
// A snapshot is the game's state at one moment, kept in memory: every global (except the game's
// functions), every instance (active or deactivated) with its built-in and own variables, the data
// structures they point to, the room's tile layers and the cameras. PFO's own session state and
// GameMaker's random generator are saved by libPFO alongside.
//
//   "capture", id   take a snapshot now and keep it as id
//   "restore", id   put it back; returns 1 when it first has to change room (call again next step)
//   "hash", 0       a fingerprint of the current state (instances by their order, not their ids,
//                   so a restored state compares equal to the original)
//   "drop", id      forget a snapshot
//   "textures", id  the snapshot's game's textures: starts loading them if they aren't; "1" once they
//                   are on the GPU (libPFO waits for that before a load puts the state back)
//   "picture", 0    a fingerprint of the picture the game drew this frame (from its drawing: libPFO
//                   compares the frames after a save with the same frames of a load's check)
//   "probe", 0      log what values look like at runtime
//   "export", id    the snapshot as bytes, in a buffer (its number; libPFO reads and deletes it), or
//                   why it can't be
//   "import", buf   a buffer "export" made, back into a snapshot (kept under the id it was exported
//                   with): 0, or why not
function pfo_snapshot(_cmd, _arg)
{
    if (!variable_global_exists("pfo_snaps")) global.pfo_snaps = {};
    switch (_cmd)
    {
        case "capture":
            variable_struct_set(global.pfo_snaps, string(_arg), pfo_snap_capture());
            return 0;
        case "restore":
            var _s = variable_struct_get(global.pfo_snaps, string(_arg));
            if (is_undefined(_s)) return -1;
            // (a game-code error here would stop the game in an error box: libPFO is told instead and
            // replays; the same for export and import)
            var _rr = -1;
            try { _rr = pfo_snap_restore(_s); } catch (_e) { _rr = "error: " + string(_e.message); }
            return _rr;
        case "hash":
            return pfo_snap_hash(_arg);
        case "keep":   // fingerprint item by item and remember it (for "diff"), under its frame when given
            global.pfo_snap_kept = pfo_snap_items();
            if (!variable_global_exists("pfo_snap_keptby") || !is_struct(global.pfo_snap_keptby)) global.pfo_snap_keptby = {};
            if (_arg != 0) variable_struct_set(global.pfo_snap_keptby, string(_arg), global.pfo_snap_kept);
            show_debug_message("PFOSNAP keep: " + string(array_length(global.pfo_snap_kept)) + " items");
            return array_length(global.pfo_snap_kept);
        case "items":   // testing (libPFO's trace): the whole state, a line per item: name, fingerprint, short value
            var _it = pfo_snap_items();
            var _tb = buffer_create(65536, buffer_grow, 1);
            for (var i = 0; i < array_length(_it); i++)
                buffer_write(_tb, buffer_text, _it[i][0] + "\t" + _it[i][1] + "\t" + string_replace_all(_it[i][2], "\n", " ") + "\n");
            buffer_write(_tb, buffer_u8, 0);
            buffer_seek(_tb, buffer_seek_start, 0);
            var _ts = buffer_read(_tb, buffer_string);
            buffer_delete(_tb);
            return _ts;
        case "diff":   // log what changed since "keep" (the one kept under this frame, when given)
            var _base = global.pfo_snap_kept;
            if (_arg != 0 && variable_global_exists("pfo_snap_keptby") && is_struct(global.pfo_snap_keptby) && variable_struct_exists(global.pfo_snap_keptby, string(_arg)))
                _base = variable_struct_get(global.pfo_snap_keptby, string(_arg));
            return pfo_snap_diff(_base, pfo_snap_items());
        case "settle":
            return 0;
        case "room":   // testing: which room this is, and where the library's menus are (the test commands' "where")
            if (instance_exists(oLibrary)) return room_get_name(room) + ":" + string(oLibrary.state) + "." + string(oLibrary.substate);
            return room_get_name(room);
        case "count":   // development: instance_count vs instances visible
            var _v = 0; with (all) _v++;
            var _sh = instance_number(oPfoSnapShell);
            return string(instance_count) + "/" + string(_v) + " shells " + string(_sh);
        case "rngtrace":   // development: the random-generator calls since the last ask (PFO_RNG_TRACE builds)
            if (!variable_global_exists("pfo_rng_trace")) return "";
            var _t = global.pfo_rng_trace;
            global.pfo_rng_trace = "";
            return _t;
        case "seats":   // the online players' names for the overlay's chips, "name|name|..."
            var _names = "";
            if (variable_global_exists("onlineClientNames") && is_array(global.onlineClientNames))
                for (var i = 0; i < array_length(global.onlineClientNames); i++)
                    _names += string_replace_all(string(global.onlineClientNames[i]), "|", " ") + "|";
            return _names;
        case "touch":   // a replay passing a frame where a capture switched everything on: the same here
            pfo_snap_reoff(pfo_snap_allon());
            return 0;
        case "touched": // did the last capture switch everything on?
            var _t = variable_global_exists("pfo_snap_touched") && global.pfo_snap_touched;
            global.pfo_snap_touched = false;
            return _t ? 1 : 0;
        case "park":   // a game being left from a running state: its own objects stop (libPFO LeaveGame)
            with (all) if (!persistent) instance_deactivate_object(id);
            return 0;
        case "textures":
            // Each game's textures are a dynamic texture group (load_datagroup), loaded in the
            // background. A load from the library starts loading them, and its check would then draw
            // the first frames without them: libPFO waits for "1" before putting the state back.
            var _ts = variable_struct_get(global.pfo_snaps, string(_arg));
            if (is_undefined(_ts) || !variable_struct_exists(_ts.globals, "currGameID") || !variable_global_exists("TEXTUREGROUP")) return "1";
            var _tg = variable_struct_get(_ts.globals, "currGameID");
            if (!is_numeric(_tg) || _tg <= 0 || _tg >= array_length(global.TEXTUREGROUP)) return "1";
            var _grp = global.TEXTUREGROUP[_tg];
            var _st = texturegroup_get_status(_grp);
            if (_st == 0) texturegroup_load(_grp, true);
            return _st == 3 ? "1" : "0";   // (0 unloaded, 1 loading, 2 loaded, 3 on the GPU)
        case "picture":   // the game's 384x216 application surface, before it is scaled to the window
            if (!surface_exists(application_surface)) return "none";
            var _pw = surface_get_width(application_surface), _ph = surface_get_height(application_surface);
            var _pb = buffer_create(_pw * _ph * 4, buffer_fixed, 1);
            buffer_get_surface(_pb, application_surface, 0);
            var _pm = buffer_md5(_pb, 0, _pw * _ph * 4);
            buffer_delete(_pb);
            if (_arg > 0) surface_save(application_surface, "picdbg_" + string(_arg) + ".png");   // (development: libPFO's picdebug)
            return string(_pw) + "x" + string(_ph) + " " + _pm;
        case "drop":
            if (variable_struct_exists(global.pfo_snaps, string(_arg))) struct_remove(global.pfo_snaps, string(_arg));
            return 0;
        case "standin":   // an instance with no events for libPFO to put after oController (SnapArrangePass); a restore drops it
            with (oPfoSnapShell) return real(id);
            return real(instance_create_depth(0, 0, depth, oPfoSnapShell));
        case "standout":  // ...and when no restore came
            with (oPfoSnapShell) instance_destroy(id, false);
            return 0;
        case "probe":
            pfo_snap_probe();
            return 0;
        case "export":
            var _re = -1;
            try { _re = pfo_snap_export(_arg); } catch (_e) { _re = "export error: " + string(_e.message); }
            return _re;
        case "wirecmp":   // development (libPFO's snapwire): what changed going through bytes and back
            return pfo_snap_wirecmp(_arg);
        case "import":
            if (variable_struct_exists(global.pfo_snaps, string(_arg))) global.pfo_snap_wire_orig = undefined;
            var _ri = -1;
            try { _ri = pfo_snap_import(_arg); } catch (_e) { _ri = "import error: " + string(_e.message); }
            return _ri;
    }
    return -1;
}

// ---- what a value is -------------------------------------------------------------------------
// refs print as "ref <type> <id or name>"
function pfo_snap_ref_type(_v)
{
    var _s = string(_v);
    var _rest = string_delete(_s, 1, 4);
    var _sp = string_pos(" ", _rest);
    return _sp > 0 ? string_copy(_rest, 1, _sp - 1) : _rest;
}

function pfo_snap_ds_kind(_t)
{
    switch (_t)
    {
        case "ds_list": return ds_type_list;
        case "ds_map": return ds_type_map;
        case "ds_grid": return ds_type_grid;
        case "ds_stack": return ds_type_stack;
        case "ds_queue": return ds_type_queue;
        case "ds_priority": return ds_type_priority;
        case "path": return 100;      // paths a game adds while it runs (pfo_snap_res_*)
        case "mp_grid": return 101;   // motion-planning grids
    }
    return -1;
}

// ---- paths and motion-planning grids ----
// Two games' enemies and units (ids 45 and 50: library 50 and 12) walk paths the game makes at run time (path_add, filled
// by mp_grid_path from a grid of where they can go). They're kept like data structures: their
// contents copied, made again where they're missing, refs to them mapped. A grid's size can't be
// read back, so the build routes mp_grid_create and mp_grid_destroy through the wrappers below,
// which note it; path_start goes through pfo_path_start, which notes what a relative path is
// measured from (pfo_snap_pstart), so following one can be started again where it was.
function pfo_snap_grids_reg()
{
    if (!variable_global_exists("pfo_snap_grids") || !is_struct(global.pfo_snap_grids)) global.pfo_snap_grids = {};
    return global.pfo_snap_grids;
}
function pfo_mp_grid_create(_l, _t, _h, _v, _cw, _ch)
{
    var _g = mp_grid_create(_l, _t, _h, _v, _cw, _ch);
    variable_struct_set(pfo_snap_grids_reg(), string(real(_g)), [_l, _t, _h, _v, _cw, _ch]);
    return _g;
}
function pfo_mp_grid_destroy(_g)
{
    var _reg = pfo_snap_grids_reg();
    if (variable_struct_exists(_reg, string(real(_g)))) struct_remove(_reg, string(real(_g)));
    mp_grid_destroy(_g);
}
function pfo_path_start(_p, _sp, _end, _abs)
{
    pfo_snap_pstart = [_abs, x, y];
    path_start(_p, _sp, _end, _abs);
}
// data structures, paths and grids alike: is it there
function pfo_snap_res_exists(_n, _k)
{
    if (_k == 100) return path_exists(_n);
    if (_k == 101) return variable_struct_exists(pfo_snap_grids_reg(), string(real(_n)));
    return ds_exists(_n, _k);
}
// a new one for a record of the snapshot (a grid needs its size)
function pfo_snap_res_new(_r)
{
    if (_r.kind == 100) return path_add();
    if (_r.kind == 101) { var _gm = _r.data[0]; return pfo_mp_grid_create(_gm[0], _gm[1], _gm[2], _gm[3], _gm[4], _gm[5]); }
    var _want = pfo_snap_ref_number(_r.ref);
    return _want >= 0 ? pfo_snap_new_ds_at(_r.kind, _want) : pfo_snap_new_ds(_r.kind);
}

// global variables that are the game's own functions never change: leave them out
function pfo_snap_is_function(_v)
{
    return is_method(_v) && is_undefined(method_get_self(_v));
}

// ---- deep copy -------------------------------------------------------------------------------
// Arrays and structs are copied (the game keeps changing the originals); refs are kept as they
// are and the data structures they point to are copied separately (pfo_snap_copy_ds).
function pfo_snap_copy(_v, _ctx, _depth)
{
    if (_depth > 24) return _v;
    if (is_array(_v))
    {
        var _n = array_length(_v);
        var _a = array_create(_n);
        for (var i = 0; i < _n; i++) _a[i] = pfo_snap_copy(_v[i], _ctx, _depth + 1);
        return _a;
    }
    if (is_method(_v)) return _v;
    if (is_struct(_v))
    {
        var _names = variable_struct_get_names(_v);
        var _c = {};
        for (var i = 0; i < array_length(_names); i++)
            if (string_char_at(_names[i], 1) != "@") variable_struct_set(_c, _names[i], pfo_snap_copy(variable_struct_get(_v, _names[i]), _ctx, _depth + 1));
        var _st = static_get(_v);
        if (!is_undefined(_st)) static_set(_c, _st);
        return _c;
    }
    if (typeof(_v) == "ref" && !is_undefined(_ctx))
    {
        var _k = pfo_snap_ds_kind(pfo_snap_ref_type(_v));
        if (_k >= 0) pfo_snap_copy_ds(_v, _k, _ctx);
    }
    return _v;
}

// a copy of a data structure, once per structure, with its contents copied in turn
function pfo_snap_copy_ds(_ref, _kind, _ctx)
{
    var _key = string(_ref);
    if (variable_struct_exists(_ctx.ds, _key) || !pfo_snap_res_exists(_ref, _kind)) return;
    var _rec = { ref: _ref, kind: _kind, data: undefined };
    variable_struct_set(_ctx.ds, _key, _rec);
    switch (_kind)
    {
        case ds_type_list:
            var _n = ds_list_size(_ref);
            var _a = array_create(_n);
            for (var i = 0; i < _n; i++) _a[i] = pfo_snap_copy(ds_list_find_value(_ref, i), _ctx, 1);
            _rec.data = _a;
            break;
        case ds_type_map:
            var _keys = ds_map_keys_to_array(_ref);
            var _vals = array_create(array_length(_keys));
            for (var i = 0; i < array_length(_keys); i++) _vals[i] = pfo_snap_copy(ds_map_find_value(_ref, _keys[i]), _ctx, 1);
            _rec.data = [_keys, _vals];
            break;
        case ds_type_grid:
            var _w = ds_grid_width(_ref), _h = ds_grid_height(_ref);
            var _g = array_create(_w * _h);
            for (var gx = 0; gx < _w; gx++)
                for (var gy = 0; gy < _h; gy++) _g[gx * _h + gy] = pfo_snap_copy(ds_grid_get(_ref, gx, gy), _ctx, 1);
            _rec.data = [_w, _h, _g];
            break;
        case 100:   // a path: [closed, kind, precision, [x, y, speed, ...]]
            var _pn = path_get_number(_ref);
            var _pts = array_create(_pn * 3);
            for (var i = 0; i < _pn; i++) { _pts[i * 3] = path_get_point_x(_ref, i); _pts[i * 3 + 1] = path_get_point_y(_ref, i); _pts[i * 3 + 2] = path_get_point_speed(_ref, i); }
            _rec.data = [path_get_closed(_ref), path_get_kind(_ref), path_get_precision(_ref), _pts];
            break;
        case 101:   // a grid: [its size, the blocked cells (column * rows + row)]
            var _gm = variable_struct_get(pfo_snap_grids_reg(), string(real(_ref)));
            var _cells = [];
            for (var gx = 0; gx < _gm[2]; gx++)
                for (var gy = 0; gy < _gm[3]; gy++) if (mp_grid_get_cell(_ref, gx, gy) == -1) array_push(_cells, gx * _gm[3] + gy);
            _rec.data = [_gm, _cells];
            break;
        default:
            // stacks, queues, priority queues: a hidden copy (contents aren't inspected)
            var _c = pfo_snap_new_ds(_kind);
            if (_kind == ds_type_stack) ds_stack_copy(_c, _ref);
            else if (_kind == ds_type_queue) ds_queue_copy(_c, _ref);
            else ds_priority_copy(_c, _ref);
            _rec.data = _c;
            break;
    }
}

// ---- capture ---------------------------------------------------------------------------------
function pfo_snap_builtins()
{
    return [ "x", "y", "xstart", "ystart", "xprevious", "yprevious", "direction", "speed", "friction",
        "gravity", "gravity_direction", "sprite_index", "image_index", "image_speed", "image_xscale",
        "image_yscale", "image_angle", "image_alpha", "image_blend", "mask_index", "depth", "visible",
        "solid", "persistent", "timeline_index", "timeline_position", "timeline_running",
        "timeline_speed", "timeline_loop" ];   // (paths: path_index is read-only, and UFO 50 doesn't use them)
}

function pfo_snap_by_id(_a, _b) { return real(_a) - real(_b); }

// Switch every instance on to read it, returning the ones that were off. instance_activate_all()
// only clears each instance's off bit (GameMaker moves it between its lists at the next list
// update), and instance_deactivate_object() can't reach an instance still in the switched-off list,
// so libPFO notes the off bits first (pfo_runner_mark) and pfo_snap_reoff has it put exactly those
// back (pfo_runner_rehide): reading the game leaves no trace. libPFO also notes the frames where
// something was off (global.pfo_snap_touched) and its replays do the same there ("touch").
function pfo_snap_allon()
{
    var _was = {};
    with (all) variable_struct_set(_was, string(real(id)), true);
    pfo_runner_mark();
    instance_activate_all();
    pfo_runner_settle();   // and into the lists with (all) walks
    var _off = [];
    with (all) if (!variable_struct_exists(_was, string(real(id)))) array_push(_off, id);
    if (array_length(_off) > 0) global.pfo_snap_touched = true;
    return _off;
}

function pfo_snap_reoff(_off)
{
    pfo_runner_rehide();
}

// which instances are on, given what pfo_snap_allon found off
function pfo_snap_onset(_off)
{
    var _on = {};
    with (all) variable_struct_set(_on, string(real(id)), true);
    for (var i = 0; i < array_length(_off); i++) struct_remove(_on, string(real(_off[i])));
    return _on;
}

// instances in an order that doesn't depend on their ids (a restore hands out new ones):
// object, where it started, where it is, depth
function pfo_snap_canonical()
{
    var _keys = [];
    with (all) array_push(_keys, [object_get_name(object_index) + "|" + string(xstart) + "|" + string(ystart) + "|" + string(x) + "|" + string(y) + "|" + string(depth), id]);
    array_sort(_keys, pfo_snap_by_key);
    var _list = array_create(array_length(_keys));
    for (var i = 0; i < array_length(_keys); i++) { var _e = _keys[i]; _list[i] = _e[1]; }
    return _list;
}

function pfo_snap_by_key(_a, _b)
{
    var _ka = _a[0], _kb = _b[0];
    if (_ka < _kb) return -1;
    if (_ka > _kb) return 1;
    return 0;
}

function pfo_snap_capture()
{
    var _ctx = { ds: {} };
    var _snap = { rm: room, ctx: _ctx, globals: {}, insts: [], layers: [], views: [], frame: pfo_get_frame(), icount: instance_count, sprites: pfo_snap_sprites_copy(), maxid: pfo_runner_maxid(-1) };
    // GameMaker's drawing state as the last frame left it: some games draw text without setting a
    // colour first (game 34's score came out in the colour another game had left set)
    _snap.draw = [draw_get_colour(), draw_get_alpha(), draw_get_font(), draw_get_halign(), draw_get_valign(), gpu_get_blendmode_ext()];
    // globals
    var _gn = variable_instance_get_names(global);
    for (var i = 0; i < array_length(_gn); i++)
    {
        var _name = _gn[i];
        if (string_copy(_name, 1, 8) == "pfo_snap" || string_char_at(_name, 1) == "@") continue;   // @@...: the runtime's own
        if (pfo_snap_device_global(_name)) continue;
        var _v = variable_global_get(_name);
        if (pfo_snap_is_function(_v)) continue;
        variable_struct_set(_snap.globals, _name, pfo_snap_copy(_v, _ctx, 0));
    }
    // instances: deactivated ones too (activated just long enough to read them)
    var _offList = pfo_snap_allon();
    var _active = pfo_snap_onset(_offList);
    _snap.offOrder = _offList;
    var _bi = pfo_snap_builtins();
    var _list = [];
    with (all) array_push(_list, id);
    array_sort(_list, pfo_snap_by_id);
    for (var n = 0; n < array_length(_list); n++)
    {
        var _inst = _list[n];
        var _rec = { oid: _inst, obj: _inst.object_index, lay: layer_get_name(_inst.layer), off: !variable_struct_exists(_active, string(real(_inst))),
                     b: array_create(array_length(_bi)), al: array_create(12), vars: {} };
        for (var j = 0; j < array_length(_bi); j++) _rec.b[j] = variable_instance_get(_inst, _bi[j]);
        for (var j = 0; j < 12; j++) _rec.al[j] = _inst.alarm[j];
        if (_inst.path_index != -1) _rec.pth = pfo_snap_copy(pfo_snap_path_of(_inst), _ctx, 0);
        var _vn = variable_instance_get_names(_inst);
        for (var j = 0; j < array_length(_vn); j++)
            if (string_char_at(_vn[j], 1) != "@") variable_struct_set(_rec.vars, _vn[j], pfo_snap_copy(variable_instance_get(_inst, _vn[j]), _ctx, 0));
        array_push(_snap.insts, _rec);
    }
    pfo_snap_reoff(_offList);
    // tile layers and layer positions
    var _layers = layer_get_all();
    for (var i = 0; i < array_length(_layers); i++)
    {
        var _l = _layers[i];
        var _lr = { name: layer_get_name(_l), lx: layer_get_x(_l), ly: layer_get_y(_l), hs: layer_get_hspeed(_l), vs: layer_get_vspeed(_l),
                    vis: layer_get_visible(_l), ld: layer_get_depth(_l), tiles: [], legacy: [] };
        var _els = layer_get_all_elements(_l);
        for (var e = 0; e < array_length(_els); e++)
        {
            // a single tile (type 7: GameMaker 1's tiles, which tile_add and the like make one by one;
            // games 07, 13, 21, 29, 33, 40, 41, 45 and 48 add them at run time, some delete them)
            if (layer_get_element_type(_els[e]) == 7)
            {
                var _t = _els[e];
                array_push(_lr.legacy, [_t, layer_tile_get_x(_t), layer_tile_get_y(_t), layer_tile_get_sprite(_t), layer_tile_get_region(_t),
                    layer_tile_get_visible(_t), layer_tile_get_xscale(_t), layer_tile_get_yscale(_t), layer_tile_get_blend(_t), layer_tile_get_alpha(_t)]);
                continue;
            }
            if (layer_get_element_type(_els[e]) != layerelementtype_tilemap) continue;
            var _tm = _els[e];
            var _w = tilemap_get_width(_tm), _h = tilemap_get_height(_tm);
            var _cells = array_create(_w * _h);
            for (var tx = 0; tx < _w; tx++)
                for (var ty = 0; ty < _h; ty++) _cells[tx * _h + ty] = tilemap_get(_tm, tx, ty);
            array_push(_lr.tiles, [e, _w, _h, _cells]);
        }
        array_push(_snap.layers, _lr);
    }
    // cameras
    for (var i = 0; i < 8; i++)
    {
        var _cam = view_camera[i];
        if (_cam < 0) { array_push(_snap.views, undefined); continue; }
        // with what it follows (games set that at run time, e.g. Waldorf's Journey follows o21_Camera,
        // and a room entered again starts without it)
        array_push(_snap.views, [view_visible[i], camera_get_view_x(_cam), camera_get_view_y(_cam), camera_get_view_width(_cam), camera_get_view_height(_cam),
            camera_get_view_target(_cam), camera_get_view_border_x(_cam), camera_get_view_border_y(_cam), camera_get_view_speed_x(_cam), camera_get_view_speed_y(_cam), camera_get_view_angle(_cam)]);
    }
    return _snap;
}

// ---- restore ---------------------------------------------------------------------------------
// old instance / data structure -> the one standing in for it now
function pfo_snap_remap(_v, _map, _depth)
{
    if (_depth > 24) return _v;
    if (is_array(_v))
    {
        var _n = array_length(_v);
        var _a = array_create(_n);
        for (var i = 0; i < _n; i++) _a[i] = pfo_snap_remap(_v[i], _map, _depth + 1);
        return _a;
    }
    if (is_method(_v))
    {
        var _self = method_get_self(_v);
        if (!is_undefined(_self) && typeof(_self) == "ref")
        {
            var _to = variable_struct_get(_map, string(_self));
            if (!is_undefined(_to)) return method(_to, method_get_index(_v));
        }
        return _v;
    }
    if (is_struct(_v))
    {
        if (variable_struct_exists(_v, "pfo_wref"))   // a ref from bytes (pfo_snap_r)
        {
            var _wt = variable_struct_get(_map, _v.pfo_wref);
            return is_undefined(_wt) ? pfo_snap_here(_v) : _wt;
        }
        if (variable_struct_exists(_v, "pfo_snap_m"))   // a method from bytes, bound to an instance (pfo_snap_r)
        {
            var _m = _v.pfo_snap_m;
            var _mt = variable_struct_get(_map, pfo_snap_key(_m[0]));
            if (!is_undefined(_mt)) return method(_mt, _m[1]);
            var _mh = pfo_snap_here(_m[0]);
            if (typeof(_mh) == "ref" && instance_exists(_mh)) return method(_mh, _m[1]);
            return method(undefined, _m[1]);
        }
        var _names = variable_struct_get_names(_v);
        var _c = {};
        for (var i = 0; i < array_length(_names); i++)
            if (string_char_at(_names[i], 1) != "@") variable_struct_set(_c, _names[i], pfo_snap_remap(variable_struct_get(_v, _names[i]), _map, _depth + 1));
        var _st = static_get(_v);
        if (!is_undefined(_st)) static_set(_c, _st);
        return _c;
    }
    if (typeof(_v) == "ref")
    {
        var _to = variable_struct_get(_map, string(_v));
        if (!is_undefined(_to)) return _to;
    }
    return _v;
}

// here and not on its way out: an instance destroyed this step (a Create event of the room just
// entered can destroy another: Barbuta's ladders take out the cliff under them) still answers
// instance_exists until the step ends, but with (all) has already left it out, and the runner
// removes it then; one reused would be lost a step after the load
function pfo_snap_alive(_inst)
{
    if (!instance_exists(_inst)) return false;
    var _seen = false;
    with (_inst) _seen = true;
    return _seen;
}

function pfo_snap_same_vars(_inst, _vars)
{
    var _vn = variable_instance_get_names(_inst);
    for (var j = 0; j < array_length(_vn); j++)
        if (string_char_at(_vn[j], 1) != "@" && !variable_struct_exists(_vars, _vn[j])) return false;
    return true;
}

function pfo_snap_new_ds(_kind)
{
    switch (_kind)
    {
        case ds_type_list: return ds_list_create();
        case ds_type_map: return ds_map_create();
        case ds_type_grid: return ds_grid_create(1, 1);
        case ds_type_stack: return ds_stack_create();
        case ds_type_queue: return ds_queue_create();
    }
    return ds_priority_create();
}

function pfo_snap_ref_number(_r) { return pfo_snap_is_wref(_r) ? _r.n : real(_r); }
function pfo_snap_by_number(_a, _b) { return pfo_snap_ref_number(_a.ref) - pfo_snap_ref_number(_b.ref); }

function pfo_snap_free_ds(_d, _kind)
{
    switch (_kind)
    {
        case ds_type_list: ds_list_destroy(_d); return;
        case ds_type_map: ds_map_destroy(_d); return;
        case ds_type_grid: ds_grid_destroy(_d); return;
        case ds_type_stack: ds_stack_destroy(_d); return;
        case ds_type_queue: ds_queue_destroy(_d); return;
    }
    ds_priority_destroy(_d);
}

// a data structure made again under the number it had: GameMaker hands out the lowest free number
// of each kind, so the free ones below it are taken (and given back) until it comes up. Games keep
// numbers of lists they've freed (library 12's teams, after its game is left): with other numbers
// here, such a stale one named a live list on one device and nothing on the other.
function pfo_snap_new_ds_at(_kind, _want)
{
    var _spare = [], _got = undefined;
    repeat (512)
    {
        var _d = pfo_snap_new_ds(_kind);
        if (real(_d) == _want) { _got = _d; break; }
        array_push(_spare, _d);
        if (real(_d) > _want) break;
    }
    for (var i = 0; i < array_length(_spare); i++) pfo_snap_free_ds(_spare[i], _kind);
    return is_undefined(_got) ? pfo_snap_new_ds(_kind) : _got;
}

function pfo_snap_count(_where)
{
    var _f = asset_get_index("obj_floor");
    var _n = 0;
    with (all) _n++;
    global.pfo_snap_stages += " " + _where + "=" + string(_n) + "/" + string(instance_count) + "/" + string(_f >= 0 ? instance_number(_f) : -1);
}

// Single tiles (layer element type 7: GameMaker 1's tiles, which tile_add makes one by one) as the
// snapshot had them. Games add and delete them as they play: Barbuta's loose block lands as three
// of them (a load left the block drawn after taking its solid part away), game 33 keeps its
// terrain in them (looked up by place, replaced as it changes), game 48 builds each level's floor
// out of them (a load through the library left the floor black), game 07 builds its levels from
// hundreds of them as a room starts. In three passes, touching only what differs:
//   1. a tile of the snapshot still here (same id, a layer at the same depth): what differs is put
//      back. (Setting every property of every tile again made game 45 play on differently.)
//   2. any other tile here that shows just what one of the rest did (depth, place, picture) stays:
//      a room built afresh by the game's own start has its tiles with other ids. (Replacing those
//      changed the order game 07 finds them in, and the load didn't check out.)
//   3. the tiles here that are still unmatched go, and the snapshot's that are still missing are
//      made again (with new ids, last in their layer).
function pfo_snap_tile_key(_depth, _x, _y, _spr, _reg)
{
    var _k = string(_depth) + "|" + string(_x) + "|" + string(_y) + "|" + string(_spr);
    if (is_array(_reg)) for (var i = 0; i < array_length(_reg); i++) _k += "|" + string(_reg[i]);
    return _k;
}

function pfo_snap_tile_props(_el, _tr)
{
    if (layer_tile_get_sprite(_el) != _tr[3]) layer_tile_change(_el, _tr[3]);
    if (layer_tile_get_x(_el) != _tr[1]) layer_tile_x(_el, _tr[1]);
    if (layer_tile_get_y(_el) != _tr[2]) layer_tile_y(_el, _tr[2]);
    var _reg = _tr[4], _now = layer_tile_get_region(_el);
    if (is_array(_reg) && array_length(_reg) >= 4 && is_array(_now) && array_length(_now) >= 4
        && (_now[0] != _reg[0] || _now[1] != _reg[1] || _now[2] != _reg[2] || _now[3] != _reg[3]))
        layer_tile_region(_el, _reg[0], _reg[1], _reg[2], _reg[3]);
    if (layer_tile_get_visible(_el) != _tr[5]) layer_tile_visible(_el, _tr[5]);
    if (layer_tile_get_xscale(_el) != _tr[6]) layer_tile_xscale(_el, _tr[6]);
    if (layer_tile_get_yscale(_el) != _tr[7]) layer_tile_yscale(_el, _tr[7]);
    if (layer_tile_get_blend(_el) != _tr[8]) layer_tile_blend(_el, _tr[8]);
    if (layer_tile_get_alpha(_el) != _tr[9]) layer_tile_alpha(_el, _tr[9]);
}

function pfo_snap_legacy_tiles(_snap)
{
    // the tiles here now: id -> the depth of their layer
    var _here = {};
    var _ids = [];
    var _all = layer_get_all();
    for (var i = 0; i < array_length(_all); i++)
    {
        var _els = layer_get_all_elements(_all[i]);
        for (var e = 0; e < array_length(_els); e++)
            if (layer_get_element_type(_els[e]) == 7) { variable_struct_set(_here, string(_els[e]), layer_get_depth(_all[i])); array_push(_ids, _els[e]); }
    }
    // 1. by id
    var _used = {};
    var _rest = [];
    for (var i = 0; i < array_length(_snap.layers); i++)
    {
        var _lr = _snap.layers[i];
        if (!variable_struct_exists(_lr, "legacy")) continue;
        for (var t = 0; t < array_length(_lr.legacy); t++)
        {
            var _tr = _lr.legacy[t];
            var _d = variable_struct_get(_here, string(_tr[0]));
            if (!is_undefined(_d) && _d == _lr.ld && !variable_struct_exists(_used, string(_tr[0])))
            {
                variable_struct_set(_used, string(_tr[0]), true);
                pfo_snap_tile_props(_tr[0], _tr);
            }
            else array_push(_rest, [_tr, _lr]);
        }
    }
    if (array_length(_rest) == 0 && array_length(_ids) == variable_struct_names_count(_used)) return;
    // 2. by what they show
    var _free = {};
    for (var i = 0; i < array_length(_ids); i++)
    {
        var _el = _ids[i];
        if (variable_struct_exists(_used, string(_el))) continue;
        var _k = pfo_snap_tile_key(variable_struct_get(_here, string(_el)), layer_tile_get_x(_el), layer_tile_get_y(_el), layer_tile_get_sprite(_el), layer_tile_get_region(_el));
        var _list = variable_struct_get(_free, _k);
        if (is_undefined(_list)) { _list = []; variable_struct_set(_free, _k, _list); }
        array_push(_list, _el);
    }
    var _make = [];
    for (var i = 0; i < array_length(_rest); i++)
    {
        var _tr = _rest[i][0], _lr = _rest[i][1];
        var _list = variable_struct_get(_free, pfo_snap_tile_key(_lr.ld, _tr[1], _tr[2], _tr[3], _tr[4]));
        if (!is_undefined(_list) && array_length(_list) > 0)
        {
            var _el = array_pop(_list);
            variable_struct_set(_used, string(_el), true);
            pfo_snap_tile_props(_el, _tr);
        }
        else array_push(_make, _rest[i]);
    }
    // 3. the unmatched go, the missing are made
    for (var i = 0; i < array_length(_ids); i++)
        if (!variable_struct_exists(_used, string(_ids[i]))) layer_tile_destroy(_ids[i]);
    for (var i = 0; i < array_length(_make); i++)
    {
        var _tr = _make[i][0], _lr = _make[i][1];
        var _r = _tr[4];
        if (!is_array(_r) || array_length(_r) < 4) continue;
        // its layer: by name, else one at its depth (tile_add's way), else a new one
        var _l = -1;
        if (layer_exists(_lr.name)) _l = layer_get_id(_lr.name);
        else
        {
            var _ls = layer_get_all();
            for (var k = 0; k < array_length(_ls); k++) if (layer_get_depth(_ls[k]) == _lr.ld) { _l = _ls[k]; break; }
            if (_l == -1) _l = layer_create(_lr.ld);
        }
        var _el = layer_tile_create(_l, _tr[1], _tr[2], _tr[3], _r[0], _r[1], _r[2], _r[3]);
        pfo_snap_tile_props(_el, _tr);
    }
}

// data structures: back into the same structure if it still exists, otherwise a new one (or the one
// already made for it, in _map)
function pfo_snap_ensure_ds(_ctx, _map)
{
    // which are still here is decided before any is made: a new one can get the number of another
    // one's old ref (more so on another device, where none of them were ever made), which would
    // then look as if it was still there, and two of the snapshot's would share it
    var _dk = variable_struct_get_names(_ctx.ds);
    var _need = [];
    for (var i = 0; i < array_length(_dk); i++)
    {
        var _r = variable_struct_get(_ctx.ds, _dk[i]);
        if (pfo_snap_res_exists(pfo_snap_here(_r.ref), _r.kind)) continue;
        var _have = variable_struct_get(_map, pfo_snap_key(_r.ref));
        if (!is_undefined(_have) && pfo_snap_res_exists(_have, _r.kind)) continue;
        array_push(_need, _r);
    }
    array_sort(_need, pfo_snap_by_number);
    for (var i = 0; i < array_length(_need); i++)
        variable_struct_set(_map, pfo_snap_key(_need[i].ref), pfo_snap_res_new(_need[i]));
}

function pfo_snap_fill_ds(_ctx, _map)
{
    var _dk = variable_struct_get_names(_ctx.ds);
    for (var i = 0; i < array_length(_dk); i++)
    {
        var _r = variable_struct_get(_ctx.ds, _dk[i]);
        var _to = variable_struct_get(_map, pfo_snap_key(_r.ref));
        var _ds = is_undefined(_to) ? pfo_snap_here(_r.ref) : _to;
        switch (_r.kind)
        {
            case ds_type_list:
                ds_list_clear(_ds);
                for (var j = 0; j < array_length(_r.data); j++) ds_list_add(_ds, pfo_snap_remap(_r.data[j], _map, 0));
                break;
            case ds_type_map:
                ds_map_clear(_ds);
                var _keys = _r.data[0], _vals = _r.data[1];
                for (var j = 0; j < array_length(_keys); j++) ds_map_set(_ds, _keys[j], pfo_snap_remap(_vals[j], _map, 0));
                break;
            case ds_type_grid:
                var _w = _r.data[0], _h = _r.data[1], _g = _r.data[2];
                ds_grid_resize(_ds, _w, _h);
                for (var gx = 0; gx < _w; gx++)
                    for (var gy = 0; gy < _h; gy++) ds_grid_set(_ds, gx, gy, pfo_snap_remap(_g[gx * _h + gy], _map, 0));
                break;
            case 100:
                path_clear_points(_ds);
                var _pts = _r.data[3];
                for (var j = 0; j < array_length(_pts); j += 3) path_add_point(_ds, _pts[j], _pts[j + 1], _pts[j + 2]);
                path_set_closed(_ds, _r.data[0]); path_set_kind(_ds, _r.data[1]); path_set_precision(_ds, _r.data[2]);
                break;
            case 101:
                mp_grid_clear_all(_ds);
                var _gh = _r.data[0][3], _cells = _r.data[1];
                for (var j = 0; j < array_length(_cells); j++) mp_grid_add_cell(_ds, _cells[j] div _gh, _cells[j] mod _gh);
                break;
            case ds_type_stack: ds_stack_copy(_ds, _r.data); break;
            case ds_type_queue: ds_queue_copy(_ds, _r.data); break;
            case ds_type_priority: ds_priority_copy(_ds, _r.data); break;
        }
    }
}

// ---- sprites the games make while running ----
// Two games (Camouflage's croc, Mini & Max's characters) copy a sprite with sprite_duplicate() to
// give it their own rectangle collision mask. On Android their calls go through these (the build
// patches them in), which note what each copy was made from and its mask, so a snapshot can make
// it again: global.pfo_snap_sprites = { "<sprite ref>": [sprite, source, mask arguments or undefined] }.
function pfo_snap_sprites_reg()
{
    if (!variable_global_exists("pfo_snap_sprites") || !is_struct(global.pfo_snap_sprites)) global.pfo_snap_sprites = {};
    return global.pfo_snap_sprites;
}

function pfo_sprite_duplicate(_src)
{
    var _s = sprite_duplicate(_src);
    variable_struct_set(pfo_snap_sprites_reg(), string(real(_s)), [_s, _src, undefined]);   // (a sprite ref prints its name only while it exists: keyed by number)
    return _s;
}

function pfo_sprite_collision_mask(_s, _sep, _mode, _l, _t, _r, _b, _kind, _tol)
{
    sprite_collision_mask(_s, _sep, _mode, _l, _t, _r, _b, _kind, _tol);
    var _e = variable_struct_get(pfo_snap_sprites_reg(), string(real(_s)));
    if (is_array(_e)) _e[@ 2] = [_sep, _mode, _l, _t, _r, _b, _kind, _tol];
}

function pfo_sprite_delete(_s)
{
    struct_remove(pfo_snap_sprites_reg(), string(real(_s)));
    return sprite_delete(_s);
}

// a snapshot's copy of the registry
function pfo_snap_sprites_copy()
{
    var _reg = pfo_snap_sprites_reg(), _c = {};
    var _k = variable_struct_get_names(_reg);
    for (var i = 0; i < array_length(_k); i++)
    {
        var _e = variable_struct_get(_reg, _k[i]);
        variable_struct_set(_c, _k[i], [_e[0], _e[1], is_array(_e[2]) ? array_concat([], _e[2]) : undefined]);
    }
    return _c;
}

// the snapshot's runtime sprites that are gone: made again (into _map, old -> new)
function pfo_snap_ensure_sprites(_snap, _map)
{
    if (!variable_struct_exists(_snap, "sprites")) return;
    var _k = variable_struct_get_names(_snap.sprites);
    for (var i = 0; i < array_length(_k); i++)
    {
        var _e = variable_struct_get(_snap.sprites, _k[i]);
        if (sprite_exists(pfo_snap_here(_e[0])) || !sprite_exists(pfo_snap_here(_e[1]))) continue;
        var _key = pfo_snap_key(_e[0]);   // how references to the gone sprite print now (pfo_snap_remap's key)
        if (!is_undefined(variable_struct_get(_map, _key))) continue;
        var _s = pfo_sprite_duplicate(pfo_snap_here(_e[1]));
        var _m = _e[2];
        if (is_array(_m)) pfo_sprite_collision_mask(_s, _m[0], _m[1], _m[2], _m[3], _m[4], _m[5], _m[6], _m[7]);
        variable_struct_set(_map, _key, _s);
    }
}

// made as an event-less shell and then turned into its object: its Create event would otherwise
// run (and in some games make or remove other instances)
//
// It gets the id it had: the runner's id counter is set so the next id handed out is that one
// (unless an instance here has it). Games compare ids (games 10 and 35 by order in their logic:
// id < other.id), and new ids would put the ones made again after the ones still here.
function pfo_snap_make(_rec)
{
    var _want = typeof(_rec.oid) == "ref" || is_numeric(_rec.oid) ? real(_rec.oid) : (pfo_snap_is_wref(_rec.oid) ? _rec.oid.n : -1);
    var _exact = _want >= 100000 && !instance_exists(_want) && pfo_snap_id_step() >= 0;
    var _was = _exact ? pfo_runner_maxid(_want - pfo_snap_id_step()) : -1;
    var _inst = layer_exists(_rec.lay) ? instance_create_layer(_rec.b[0], _rec.b[1], _rec.lay, oPfoSnapShell)
        : instance_create_depth(_rec.b[0], _rec.b[1], _rec.b[20], oPfoSnapShell);
    if (_exact)
    {
        pfo_runner_maxid(max(_was, real(_inst)));   // (the counter never goes back below an id in use)
        if (real(_inst) != _want) global.pfo_snap_idhi = max(global.pfo_snap_idhi, real(_inst));
    }
    else global.pfo_snap_idhi = max(global.pfo_snap_idhi, real(_inst));
    var _obj = _rec.obj;
    with (_inst) instance_change(_obj, false);
    return _inst;
}

// what the runner adds to its counter for a new instance's id (found once, with a throwaway shell)
function pfo_snap_id_step()
{
    if (variable_global_exists("pfo_snap_idstep")) return global.pfo_snap_idstep;
    var _m = pfo_runner_maxid(-1);
    if (_m < 0) { global.pfo_snap_idstep = -1; return -1; }
    var _t = instance_create_depth(-10000, -10000, 0, oPfoSnapShell);
    var _d = real(_t) - _m;
    instance_destroy(_t, false);
    global.pfo_snap_idstep = _d == 0 || _d == 1 ? _d : -1;
    return global.pfo_snap_idstep;
}

// ---- split screen (MultiMadnessMod) ----
// With four players apart (Onion Delivery, Rock On! Island) the screen handler splits the screen:
// three more cameras, and the drawing surface twice the size; each step it turns that on or off
// from what the game asked for (scrActivateSplitScreen / scrDeactivateSplitScreen, which act only
// on a change of its splitScreenOn). A restore brings that flag back as the snapshot had it, while
// this device's cameras and surface stay as they were: the next step would then switch the wrong
// way, or not at all (one view where there should be four). So after a restore this device is put
// the way the flag says, by the mod's own scripts.
function pfo_snap_split_here()
{
    if (!instance_exists(oScreenHandler) || !variable_instance_exists(oScreenHandler.id, "splitScreenOn")) return undefined;
    return oScreenHandler.splitScreenOn;
}
function pfo_snap_split_match(_dev)
{
    if (is_undefined(_dev)) return;
    var _on = asset_get_index("scrActivateSplitScreen"), _off = asset_get_index("scrDeactivateSplitScreen");
    if (_on < 0 || _off < 0) return;
    var _want = pfo_snap_split_here();
    if (is_undefined(_want)) return;
    with (oScreenHandler)
    {
        // (switching off writes the cameras' places into these: the snapshot's are kept)
        var _sx = variable_instance_exists(id, "splitScreenX") && is_array(splitScreenX) ? array_create(0) : undefined;
        if (!is_undefined(_sx)) { array_copy(_sx, 0, splitScreenX, 0, array_length(splitScreenX)); var _sy = array_create(0); array_copy(_sy, 0, splitScreenY, 0, array_length(splitScreenY)); }
        if (_want && _dev && (view_camera[1] < 0 || !view_visible[1]))
        {
            // split here too, but its cameras went with a room change: made again (the surface is
            // already doubled: halved first, the mod's script doubles it)
            surface_resize(application_surface, surface_get_width(application_surface) / 2, surface_get_height(application_surface) / 2);
            splitScreenOn = false;
            script_execute(_on);
        }
        else if (_want && !_dev) { splitScreenOn = false; script_execute(_on); }
        else if (!_want && _dev) { splitScreenOn = true; script_execute(_off); }
        if (!is_undefined(_sx)) { splitScreenX = _sx; splitScreenY = _sy; }
    }
}

// an instance following a path: which, where on it, how
function pfo_snap_path_of(_inst)
{
    return [_inst.path_index, _inst.path_position, _inst.path_positionprevious, _inst.path_speed, _inst.path_scale, _inst.path_orientation, _inst.path_endaction];
}

function pfo_snap_set_vars(_rec, _inst, _map, _bi)
{
    var _vn = variable_struct_get_names(_rec.vars);
    for (var j = 0; j < array_length(_vn); j++)
        variable_instance_set(_inst, _vn[j], pfo_snap_remap(variable_struct_get(_rec.vars, _vn[j]), _map, 0));
    for (var j = 0; j < array_length(_bi); j++)
    {
        // setting depth moves an instance off its layer: only when it really changed
        if (_bi[j] == "depth" && _inst.depth == _rec.b[j]) continue;
        variable_instance_set(_inst, _bi[j], pfo_snap_remap(_rec.b[j], _map, 0));
    }
    for (var j = 0; j < 12; j++) _inst.alarm[j] = _rec.al[j];
    // following a path: started again from where it was measured from, then put where it was on it
    if (variable_struct_exists(_rec, "pth"))
    {
        var _pt = pfo_snap_remap(_rec.pth, _map, 0);
        with (_inst)
        {
            var _x = x, _y = y;
            var _ps = variable_instance_exists(id, "pfo_snap_pstart") && is_array(pfo_snap_pstart) ? pfo_snap_pstart : [true, x, y];
            x = _ps[1]; y = _ps[2];
            path_start(_pt[0], _pt[3], _pt[6], _ps[0]);
            path_position = _pt[1]; path_positionprevious = _pt[2]; path_scale = _pt[4]; path_orientation = _pt[5];
            x = _x; y = _y;
        }
    }
    else if (_inst.path_index != -1) with (_inst) path_end();
}

function pfo_snap_restore(_snap)
{
    // Sprites a game made while it ran are gone once it was left: the snapshot's copies are made
    // again (pfo_snap_ensure_sprites). Any other missing sprite can't be restored (libPFO then loads
    // by replay, which makes it again).
    var _map0 = variable_global_exists("pfo_snap_room_map") && is_struct(global.pfo_snap_room_map) && global.pfo_snap_room_for == _snap ? global.pfo_snap_room_map : {};
    pfo_snap_ensure_sprites(_snap, _map0);
    var _bis = pfo_snap_builtins();
    for (var j = 0; j < array_length(_bis); j++)
    {
        if (_bis[j] != "sprite_index" && _bis[j] != "mask_index") continue;
        for (var n = 0; n < array_length(_snap.insts); n++)
        {
            var _sw = _snap.insts[n].b[j];
            var _sv = pfo_snap_here(_sw);
            if (!is_undefined(_sv) && real(_sv) >= 0 && !sprite_exists(_sv) && is_undefined(variable_struct_get(_map0, pfo_snap_key(_sw)))) return "missing sprite " + pfo_snap_key(_sw);
        }
    }
    if (room != _snap.rm)
    {
        // Changing room runs the room's own setup on the way in (its creation code, the Create
        // events of what it places), and that reads globals and the data structures they name:
        // which game this is, which file, the game's grids. So leave the current game the way the
        // game does (when the snapshot is of another one), switch off what's left of this room so
        // it can't run on with the wrong globals for the rest of the step, then give the setup the
        // snapshot's data structures and globals. The full restore follows once the room has
        // changed, and reuses the data structures made here (global.pfo_snap_room_map).
        var _g = _snap.globals;
        var _game = variable_struct_exists(_g, "currGame") ? variable_struct_get(_g, "currGame") : global.currGame;
        if (global.currGame > 0 && global.currGame != _game)
        {
            muteAll();
            if (global.currGameID > 0) unload_datagroup(global.currGameID, true, true);
            if (global.paused) scrUnpause();
            scrCleanup();
        }
        with (all) if (!persistent) instance_deactivate_object(id);
        global.pfo_snap_idhi = -1;
        pfo_snap_ensure_ds(_snap.ctx, _map0);
        var _bi0 = pfo_snap_builtins();
        var _pi = -1;
        for (var j = 0; j < array_length(_bi0); j++) if (_bi0[j] == "persistent") _pi = j;
        var _carried = [];
        for (var n = 0; n < array_length(_snap.insts); n++)
        {
            var _rec = _snap.insts[n];
            if (_pi < 0 || !_rec.b[_pi]) continue;
            var _o0 = pfo_snap_here(_rec.oid), _k0 = pfo_snap_key(_rec.oid);
            if (typeof(_o0) == "ref" && instance_exists(_o0) && _o0.object_index == _rec.obj) continue;   // still here (oController and the like)
            var _have = variable_struct_get(_map0, _k0);
            if (!is_undefined(_have) && pfo_snap_alive(_have)) continue;
            var _inst = pfo_snap_make(_rec);
            variable_struct_set(_map0, _k0, _inst);
            array_push(_carried, [_rec, _inst]);
        }
        pfo_snap_fill_ds(_snap.ctx, _map0);
        for (var n = 0; n < array_length(_carried); n++) pfo_snap_set_vars(_carried[n][0], _carried[n][1], _map0, _bi0);
        var _gn0 = variable_struct_get_names(_g);
        for (var i = 0; i < array_length(_gn0); i++) if (!pfo_snap_device_global(_gn0[i])) variable_global_set(_gn0[i], pfo_snap_remap(variable_struct_get(_g, _gn0[i]), _map0, 0));
        global.pfo_snap_room_map = _map0;
        global.pfo_snap_room_for = _snap;   // only this load reuses them
        // what the room makes on its way in (dropped by the full restore) gets ids well clear of the
        // snapshot's, so the instances made again can have theirs
        if (variable_struct_exists(_snap, "maxid") && _snap.maxid >= 0) pfo_runner_maxid(max(pfo_runner_maxid(-1), _snap.maxid, global.pfo_snap_idhi, pfo_runner_maxid(-2)) + 100000);
        room_goto(_snap.rm);
        return 1;
    }
    audio_stop_all();
    var _devSplit = pfo_snap_split_here();
    global.pfo_snap_stages = "";
    var _map = _map0;
    global.pfo_snap_room_map = undefined;
    global.pfo_snap_room_for = undefined;
    var _ctx = _snap.ctx;
    pfo_snap_ensure_ds(_ctx, _map);
    // instances: keep the ones still here, make the missing ones, drop the rest
    pfo_snap_count("start");
    // (everything on, so all of them can be seen; libPFO switches the snapshot's off ones off again
    // afterwards)
    pfo_snap_allon();
    global.pfo_snap_touched = false;
    pfo_snap_count("activated");
    if (!variable_global_exists("pfo_snap_idhi")) global.pfo_snap_idhi = -1;
    var _keep = {};
    var _made = {};
    var _madeIds = "";   // "old:new," for libPFO
    // first the ones to reuse: each still here, as the same object, without variables it didn't
    // have then (an instance's variables can't be removed: one that gained some is made afresh).
    // All of them before any is made: a new instance takes the next free id, which can be the one
    // a later record had in the other game (a fast rejoin: Barbuta's cliffs), and that record
    // would then take the new instance for its own and its own would never be made
    // (nor one the room-change pass made for its record: that one is its record's below)
    var _roomMade = {};
    var _mk = variable_struct_get_names(_map);
    for (var i = 0; i < array_length(_mk); i++)
    {
        var _mv = variable_struct_get(_map, _mk[i]);
        if (string_pos("instance", _mk[i]) > 0 && typeof(_mv) == "ref" && instance_exists(_mv)) variable_struct_set(_roomMade, string(real(_mv)), true);
    }
    var _toMake = [];
    for (var n = 0; n < array_length(_snap.insts); n++)
    {
        var _rec = _snap.insts[n];
        var _old = pfo_snap_here(_rec.oid);
        if (typeof(_old) == "ref" && pfo_snap_alive(_old) && _old.object_index == _rec.obj && pfo_snap_same_vars(_old, _rec.vars)
            && !variable_struct_exists(_keep, string(real(_old))) && !variable_struct_exists(_roomMade, string(real(_old))))
            variable_struct_set(_keep, string(real(_old)), true);
        else array_push(_toMake, n);
    }
    for (var m = 0; m < array_length(_toMake); m++)
    {
        var _rec = _snap.insts[_toMake[m]];
        var _old = pfo_snap_here(_rec.oid), _okey = pfo_snap_key(_rec.oid);
        // made afresh (event-less), unless the room-change pass already made it
        var _have = variable_struct_get(_map, _okey);
        var _inst = !is_undefined(_have) && pfo_snap_alive(_have) && _have.object_index == _rec.obj && !variable_struct_exists(_keep, string(real(_have))) ? _have : pfo_snap_make(_rec);
        variable_struct_set(_map, _okey, _inst);
        variable_struct_set(_made, _okey, _inst);
        _madeIds += string(real(_old)) + ":" + string(real(_inst)) + ",";
        variable_struct_set(_keep, string(real(_inst)), true);
    }
    pfo_snap_count("made");
    var _drop = [];
    with (all) if (!variable_struct_exists(_keep, string(real(id)))) array_push(_drop, id);
    // ...and removed the same way round, so their Destroy and Clean Up events don't run either
    for (var i = 0; i < array_length(_drop); i++) with (_drop[i]) instance_change(oPfoSnapShell, false);
    for (var i = 0; i < array_length(_drop); i++) instance_destroy(_drop[i], false);
    pfo_snap_count("dropped");
    // contents of the data structures
    pfo_snap_fill_ds(_ctx, _map);
    // globals
    var _gn = variable_struct_get_names(_snap.globals);
    for (var i = 0; i < array_length(_gn); i++)
        if (!pfo_snap_device_global(_gn[i])) variable_global_set(_gn[i], pfo_snap_remap(variable_struct_get(_snap.globals, _gn[i]), _map, 0));
    pfo_snap_count("globals");
    // instance variables
    var _bi = pfo_snap_builtins();
    for (var n = 0; n < array_length(_snap.insts); n++)
    {
        var _rec = _snap.insts[n];
        var _to = variable_struct_get(_map, pfo_snap_key(_rec.oid));
        var _inst = is_undefined(_to) ? pfo_snap_here(_rec.oid) : _to;
        if (!instance_exists(_inst)) continue;
        pfo_snap_set_vars(_rec, _inst, _map, _bi);
    }
    pfo_snap_count("variables");
    // which instances were switched off, the order of GameMaker's instance lists and the like are
    // put back by libPFO afterwards (savestate.inc): it needs to know the ones made afresh
    global.pfo_snap_made_ids = _madeIds;
    // the id counter as it was then (or past the highest id here, if one couldn't have its own)
    // (and past every id still in the runner: the dropped ones keep theirs until the step ends, and
    // games only compare ids by order, which new ones keep, after all the others, as there)
    if (variable_struct_exists(_snap, "maxid") && _snap.maxid >= 0) pfo_runner_maxid(max(_snap.maxid, global.pfo_snap_idhi, pfo_runner_maxid(-2)));
    global.pfo_snap_idhi = -1;
    // tile layers and layer positions
    for (var i = 0; i < array_length(_snap.layers); i++)
    {
        var _lr = _snap.layers[i];
        if (!layer_exists(_lr.name)) continue;
        var _l = layer_get_id(_lr.name);
        layer_x(_l, _lr.lx); layer_y(_l, _lr.ly); layer_hspeed(_l, _lr.hs); layer_vspeed(_l, _lr.vs);
        layer_set_visible(_l, _lr.vis); layer_depth(_l, _lr.ld);
        var _els = layer_get_all_elements(_l);
        for (var t = 0; t < array_length(_lr.tiles); t++)
        {
            var _tr = _lr.tiles[t];
            if (_tr[0] >= array_length(_els)) continue;
            var _tm = _els[_tr[0]];
            if (layer_get_element_type(_tm) != layerelementtype_tilemap) continue;
            var _w = _tr[1], _h = _tr[2], _cells = _tr[3];
            for (var tx = 0; tx < _w; tx++)
                for (var ty = 0; ty < _h; ty++) tilemap_set(_tm, _cells[tx * _h + ty], tx, ty);
        }
    }
    pfo_snap_legacy_tiles(_snap);
    // the drawing state
    if (variable_struct_exists(_snap, "draw"))
    {
        var _dr = _snap.draw;
        draw_set_colour(_dr[0]); draw_set_alpha(_dr[1]);
        if (_dr[2] >= 0) draw_set_font(_dr[2]);
        draw_set_halign(_dr[3]); draw_set_valign(_dr[4]);
        if (is_array(_dr[5]) && array_length(_dr[5]) >= 2) gpu_set_blendmode_ext(_dr[5][0], _dr[5][1]);
    }
    pfo_snap_split_match(_devSplit);
    // cameras
    for (var i = 0; i < 8; i++)
    {
        var _vr = _snap.views[i];
        var _cam = view_camera[i];
        if (is_undefined(_vr) || _cam < 0) continue;
        view_visible[i] = _vr[0];
        camera_set_view_pos(_cam, _vr[1], _vr[2]);
        camera_set_view_size(_cam, _vr[3], _vr[4]);
        if (array_length(_vr) > 5)
        {
            camera_set_view_target(_cam, pfo_snap_remap(_vr[5], _map, 0));
            camera_set_view_border(_cam, _vr[6], _vr[7]);
            camera_set_view_speed(_cam, _vr[8], _vr[9]);
            camera_set_view_angle(_cam, _vr[10]);
        }
    }
    // the game's textures and music for the game we're back in
    if (global.currGameID > 0) load_datagroup(global.currGameID, true, true);
    pfo_snap_report(_snap, _made, array_length(_drop));
    return 0;
}

// which objects came out with a different count than the snapshot had (logcat PFOSNAP)
function pfo_snap_report(_snap, _made, _dropped)
{
    var _want = {}, _have = {};
    for (var n = 0; n < array_length(_snap.insts); n++)
    {
        var _k = object_get_name(_snap.insts[n].obj);
        variable_struct_set(_want, _k, (variable_struct_exists(_want, _k) ? variable_struct_get(_want, _k) : 0) + 1);
    }
    var _offR = pfo_snap_allon();
    with (all)
    {
        var _k = object_get_name(object_index);
        variable_struct_set(_have, _k, (variable_struct_exists(_have, _k) ? variable_struct_get(_have, _k) : 0) + 1);
    }
    pfo_snap_reoff(_offR);
    var _msg = "PFOSNAP restore [" + global.pfo_snap_stages + " ]: " + string(array_length(_snap.insts)) + " wanted, " + string(array_length(variable_struct_get_names(_made))) + " made, " + string(_dropped) + " dropped;";
    var _names = variable_struct_get_names(_want);
    var _shown = 0;
    for (var i = 0; i < array_length(_names) && _shown < 12; i++)
    {
        var _w = variable_struct_get(_want, _names[i]);
        var _h = variable_struct_exists(_have, _names[i]) ? variable_struct_get(_have, _names[i]) : 0;
        if (_w != _h) { _msg += " " + _names[i] + " " + string(_h) + "/" + string(_w); _shown++; }
    }
    show_debug_message(_msg);
}

// ---- fingerprint -----------------------------------------------------------------------------
// The state as bytes, with instances numbered by their order (so the ids a restore hands out
// don't matter) and data structures by first appearance, hashed.
function pfo_snap_hash_value(_buf, _v, _h, _depth)
{
    if (_depth > 24) return;
    if (is_array(_v))
    {
        buffer_write(_buf, buffer_u8, 6); buffer_write(_buf, buffer_u32, array_length(_v));
        for (var i = 0; i < array_length(_v); i++) pfo_snap_hash_value(_buf, _v[i], _h, _depth + 1);
    }
    else if (is_method(_v)) { buffer_write(_buf, buffer_u8, 9); }
    else if (is_struct(_v))
    {
        var _names = variable_struct_get_names(_v);
        array_sort(_names, true);
        buffer_write(_buf, buffer_u8, 7); buffer_write(_buf, buffer_u32, array_length(_names));
        for (var i = 0; i < array_length(_names); i++)
        {
            if (string_char_at(_names[i], 1) == "@") continue;
            buffer_write(_buf, buffer_string, _names[i]);
            pfo_snap_hash_value(_buf, variable_struct_get(_v, _names[i]), _h, _depth + 1);
        }
    }
    else if (typeof(_v) == "ref")
    {
        var _t = pfo_snap_ref_type(_v);
        if (_t == "instance")
        {
            var _o = variable_struct_get(_h.inst, string(_v));
            buffer_write(_buf, buffer_u8, 8); buffer_write(_buf, buffer_s32, is_undefined(_o) ? -1 : _o);
        }
        else if (pfo_snap_ds_kind(_t) >= 0)
        {
            var _k = string(_v);
            var _o = variable_struct_get(_h.ds, _k);
            if (is_undefined(_o))
            {
                _o = _h.nds;
                _h.nds += 1;
                variable_struct_set(_h.ds, _k, _o);
                buffer_write(_buf, buffer_u8, 10); buffer_write(_buf, buffer_s32, _o);
                pfo_snap_hash_ds(_buf, _v, pfo_snap_ds_kind(_t), _h, _depth + 1);
            }
            else { buffer_write(_buf, buffer_u8, 11); buffer_write(_buf, buffer_s32, _o); }
        }
        else { buffer_write(_buf, buffer_u8, 12); buffer_write(_buf, buffer_string, string(_v)); }
    }
    else if (is_string(_v)) { buffer_write(_buf, buffer_u8, 5); buffer_write(_buf, buffer_string, _v); }
    else if (is_bool(_v)) { buffer_write(_buf, buffer_u8, 2); buffer_write(_buf, buffer_u8, _v ? 1 : 0); }
    else if (is_int64(_v) || is_int32(_v)) { buffer_write(_buf, buffer_u8, 3); buffer_write(_buf, buffer_u64, _v); }
    else if (is_real(_v)) { buffer_write(_buf, buffer_u8, 1); buffer_write(_buf, buffer_f64, _v); }
    else buffer_write(_buf, buffer_u8, 0);
}

function pfo_snap_hash_ds(_buf, _ref, _kind, _h, _depth)
{
    if (!pfo_snap_res_exists(_ref, _kind)) { buffer_write(_buf, buffer_u8, 0); return; }
    if (_kind == 100 || _kind == 101)   // (their contents, as a snapshot keeps them)
    {
        var _hc = { ds: {} };
        pfo_snap_copy_ds(_ref, _kind, _hc);
        pfo_snap_hash_value(_buf, variable_struct_get(_hc.ds, string(_ref)).data, _h, _depth + 1);
        return;
    }
    switch (_kind)
    {
        case ds_type_list:
            buffer_write(_buf, buffer_u32, ds_list_size(_ref));
            for (var i = 0; i < ds_list_size(_ref); i++) pfo_snap_hash_value(_buf, ds_list_find_value(_ref, i), _h, _depth);
            break;
        case ds_type_map:
            var _keys = ds_map_keys_to_array(_ref);
            array_sort(_keys, true);
            buffer_write(_buf, buffer_u32, array_length(_keys));
            for (var i = 0; i < array_length(_keys); i++) { pfo_snap_hash_value(_buf, _keys[i], _h, _depth); pfo_snap_hash_value(_buf, ds_map_find_value(_ref, _keys[i]), _h, _depth); }
            break;
        case ds_type_grid:
            var _w = ds_grid_width(_ref), _gh = ds_grid_height(_ref);
            buffer_write(_buf, buffer_u32, _w); buffer_write(_buf, buffer_u32, _gh);
            for (var gx = 0; gx < _w; gx++) for (var gy = 0; gy < _gh; gy++) pfo_snap_hash_value(_buf, ds_grid_get(_ref, gx, gy), _h, _depth);
            break;
        default:
            var _sz = 0;
            if (_kind == ds_type_stack) _sz = ds_stack_size(_ref);
            else if (_kind == ds_type_queue) _sz = ds_queue_size(_ref);
            else _sz = ds_priority_size(_ref);
            buffer_write(_buf, buffer_u32, _sz);
            break;
    }
}

function pfo_snap_hash(_part)
{
    // _part (development): 0 everything, 1 only the instance order, 2 only globals, 3 only instances
    //                       4 only switching everything on and off again, 5 only the content-order sort
    if (_part == 4) { var _o4 = pfo_snap_allon(); pfo_snap_reoff(_o4); return "on/off only (" + string(array_length(_o4)) + " were off)"; }
    if (_part == 5) { var _l5 = pfo_snap_canonical(); return "sort only (" + string(array_length(_l5)) + ")"; }
    var _buf = buffer_create(1 << 20, buffer_grow, 1);
    var _h = { inst: {}, ds: {}, nds: 0 };
    var _offList = pfo_snap_allon();
    var _active = pfo_snap_onset(_offList);
    var _list = pfo_snap_canonical();
    for (var n = 0; n < array_length(_list); n++) variable_struct_set(_h.inst, string(_list[n]), n);
    buffer_write(_buf, buffer_s32, room);
    if (_part == 1) { pfo_snap_reoff(_offList); buffer_delete(_buf); return "order only"; }
    // globals, by name
    var _gn = _part == 3 ? [] : variable_instance_get_names(global);
    array_sort(_gn, true);
    for (var i = 0; i < array_length(_gn); i++)
    {
        if (string_copy(_gn[i], 1, 8) == "pfo_snap" || string_char_at(_gn[i], 1) == "@") continue;
        var _v = variable_global_get(_gn[i]);
        if (pfo_snap_is_function(_v)) continue;
        buffer_write(_buf, buffer_string, _gn[i]);
        pfo_snap_hash_value(_buf, _v, _h, 0);
    }
    if (_part == 2) { pfo_snap_reoff(_offList); buffer_delete(_buf); return "globals only"; }
    // instances, in order
    var _bi = pfo_snap_builtins();
    for (var n = 0; n < array_length(_list); n++)
    {
        var _inst = _list[n];
        buffer_write(_buf, buffer_s32, _inst.object_index);
        buffer_write(_buf, buffer_u8, variable_struct_exists(_active, string(real(_inst))) ? 1 : 0);
        for (var j = 0; j < array_length(_bi); j++) pfo_snap_hash_value(_buf, variable_instance_get(_inst, _bi[j]), _h, 1);
        for (var j = 0; j < 12; j++) buffer_write(_buf, buffer_f64, _inst.alarm[j]);
        var _vn = variable_instance_get_names(_inst);
        array_sort(_vn, true);
        for (var j = 0; j < array_length(_vn); j++)
        {
            if (string_char_at(_vn[j], 1) == "@") continue;
            buffer_write(_buf, buffer_string, _vn[j]);
            pfo_snap_hash_value(_buf, variable_instance_get(_inst, _vn[j]), _h, 1);
        }
    }
    pfo_snap_reoff(_offList);
    var _size = buffer_tell(_buf);
    var _md5 = buffer_md5(_buf, 0, _size);
    buffer_delete(_buf);
    return _md5 + ":" + string(array_length(_list)) + ":" + string(_size);
}

// ---- a snapshot as bytes: a rejoin sends one to another device (libPFO's RejoinState) ---------
// A value is a type byte and what follows it:
//   0 undefined   1 real (f64)   2 string   3 bool (u8)   4 int32 (s32)   5 int64 (u64)
//   6 array: its length (u32) and the values
//   7 struct: its count (u32), (name, value)..., and its constructor's name ("" for none)
//   8 ref: its type and its number (f64)
//   9 method: its self (a value) and its function's number   10 a method bound to the struct
//     it's in: its function's number   11 anything else (read back as undefined)
//   12 a method bound to the global struct (the game's functions kept in globals): its function's number
// Refs keep their numbers: assets are the same in the same game data, and the instances and data
// structures a restore makes stand in for the old ones by their old refs (pfo_snap_remap's keys).
// A snapshot's structs are copies, and their methods stay bound to the originals (pfo_snap_copy):
// a method in a struct (or in an array in it) bound to a struct like that one (the same constructor
// and as many variables: its original) is bound to the copy it's in; one bound to any other struct
// goes unbound (called as struct.method() it then runs on that struct anyway). The hidden copies
// of stacks, queues and priority queues go as their ds_*_write text. global.pfo_snap_wire_info says
// what didn't go across exactly.
function pfo_snap_w(_b, _v, _parent, _depth, _st)
{
    switch (typeof(_v))
    {
        case "undefined": buffer_write(_b, buffer_u8, 0); return;
        case "number": buffer_write(_b, buffer_u8, 1); buffer_write(_b, buffer_f64, _v); return;
        case "string": buffer_write(_b, buffer_u8, 2); buffer_write(_b, buffer_string, _v); return;
        case "bool": buffer_write(_b, buffer_u8, 3); buffer_write(_b, buffer_u8, _v ? 1 : 0); return;
        case "int32": buffer_write(_b, buffer_u8, 4); buffer_write(_b, buffer_s32, _v); return;
        case "int64": buffer_write(_b, buffer_u8, 5); buffer_write(_b, buffer_u64, _v); return;
        case "array":
            if (_depth > 32) break;
            var _n = array_length(_v);
            buffer_write(_b, buffer_u8, 6);
            buffer_write(_b, buffer_u32, _n);
            for (var i = 0; i < _n; i++) pfo_snap_w(_b, _v[i], _parent, _depth + 1, _st);
            return;
        case "method":
            var _self = method_get_self(_v);
            var _fn = real(method_get_index(_v));
            if (is_struct(_self) && variable_struct_exists(_self, "pfo_snaps")) { buffer_write(_b, buffer_u8, 12); buffer_write(_b, buffer_f64, _fn); _st.glob++; return; }
            if (is_struct(_self) && is_struct(_parent) && (_self == _parent
                || (variable_struct_names_count(_self) == variable_struct_names_count(_parent) && instanceof(_self) == instanceof(_parent))))
            {
                buffer_write(_b, buffer_u8, 10); buffer_write(_b, buffer_f64, _fn); _st.own++;
                return;
            }
            if (is_struct(_self))
            {
                if (_st.unbound < 3) _st.what += " [" + string(instanceof(_self)) + " of " + string(variable_struct_names_count(_self)) + (is_struct(_parent) ? " in " + string(variable_struct_names_count(_parent)) : " outside a struct") + "]";
                _st.unbound++;
                _self = undefined;
            }
            buffer_write(_b, buffer_u8, 9);
            pfo_snap_w(_b, _self, undefined, _depth + 1, _st);
            buffer_write(_b, buffer_f64, _fn);
            return;
        case "struct":
            if (_depth > 32) break;
            var _names = variable_struct_get_names(_v);
            var _keep = [];
            for (var i = 0; i < array_length(_names); i++) if (string_char_at(_names[i], 1) != "@") array_push(_keep, _names[i]);
            buffer_write(_b, buffer_u8, 7);
            buffer_write(_b, buffer_u32, array_length(_keep));
            for (var i = 0; i < array_length(_keep); i++)
            {
                buffer_write(_b, buffer_string, _keep[i]);
                pfo_snap_w(_b, variable_struct_get(_v, _keep[i]), _v, _depth + 1, _st);
            }
            var _ctor = instanceof(_v);
            buffer_write(_b, buffer_string, is_string(_ctor) && _ctor != "Object" && _ctor != "struct" ? _ctor : "");
            return;
        case "ref":
            buffer_write(_b, buffer_u8, 8);
            buffer_write(_b, buffer_string, pfo_snap_ref_type(_v));
            buffer_write(_b, buffer_f64, real(_v));
            return;
    }
    _st.odd++;
    buffer_write(_b, buffer_u8, 11);
}

function pfo_snap_r(_b, _into, _st)
{
    switch (buffer_read(_b, buffer_u8))
    {
        case 0: return undefined;
        case 1: return buffer_read(_b, buffer_f64);
        case 2: return buffer_read(_b, buffer_string);
        case 3: return buffer_read(_b, buffer_u8) != 0;
        case 4: return buffer_read(_b, buffer_s32);
        case 5: return buffer_read(_b, buffer_u64);
        case 6:
            var _n = buffer_read(_b, buffer_u32);
            var _a = array_create(_n);
            for (var i = 0; i < _n; i++) _a[i] = pfo_snap_r(_b, _into, _st);
            return _a;
        case 7:
            var _n = buffer_read(_b, buffer_u32);
            var _c = {};
            for (var i = 0; i < _n; i++)
            {
                var _k = buffer_read(_b, buffer_string);
                variable_struct_set(_c, _k, pfo_snap_r(_b, _c, _st));
            }
            var _ctor = buffer_read(_b, buffer_string);
            if (_ctor != "")
            {
                var _f = asset_get_index(_ctor);
                if (_f != -1) static_set(_c, static_get(method(undefined, _f)));
                else _st.odd++;
            }
            return _c;
        case 8:
            var _ty = buffer_read(_b, buffer_string);
            var _num = buffer_read(_b, buffer_f64);
            var _txt = "ref " + _ty + " " + string(_num);
            // an instance, data structure or sprite of the other device's: a marker (pfo_snap_here)
            if (_ty == "instance" || _ty == "sprite" || pfo_snap_ds_kind(_ty) >= 0) return { pfo_wref: _txt, t: _ty, n: _num };
            var _h = handle_parse(_txt);
            if (is_undefined(_h)) { _st.refs++; return _num; }
            return _h;
        case 9:
            var _s = pfo_snap_r(_b, undefined, _st);
            var _fn = buffer_read(_b, buffer_f64);
            // bound to an instance: the restore binds it, once it knows which instance stands in for
            // that one (pfo_snap_remap). Anything else that isn't a struct (noone, for one: a method
            // whose instance is gone) goes unbound.
            if (pfo_snap_is_wref(_s)) return _s.t == "instance" ? { pfo_snap_m: [_s, _fn] } : method(undefined, _fn);
            if (is_struct(_s)) return method(_s, _fn);
            return method(undefined, _fn);
        case 10:
            return method(_into, buffer_read(_b, buffer_f64));
        case 12:
            var _gf = buffer_read(_b, buffer_f64);
            var _gs = pfo_snap_global_struct();
            return is_undefined(_gs) ? method(undefined, _gf) : method(_gs, _gf);
    }
    return undefined;
}

// the global struct as a value: the self of the methods bound to it (the game keeps some of its
// functions in globals like that; ours, global.pfo_snaps, tells it from any other struct)
function pfo_snap_global_struct()
{
    if (variable_global_exists("pfo_snap_gs") && is_struct(global.pfo_snap_gs)) return global.pfo_snap_gs;
    var _gn = variable_instance_get_names(global);
    for (var i = 0; i < array_length(_gn); i++)
    {
        var _v = variable_global_get(_gn[i]);
        if (!is_method(_v)) continue;
        var _s = method_get_self(_v);
        if (is_struct(_s) && variable_struct_exists(_s, "pfo_snaps")) { global.pfo_snap_gs = _s; return _s; }
    }
    return undefined;
}

// A ref from another device's snapshot to an instance, a data structure or a sprite can't be made
// into a ref here when there's nothing of that number (handle_parse gives noone for an instance
// that isn't there): such refs come as markers, { pfo_wref: "ref instance 100045", t: "instance",
// n: 100045 }. pfo_snap_key gives the text a ref had (a restore's map is keyed by it), and
// pfo_snap_here what it is here: the ref when there is one of that number, else the number.
function pfo_snap_is_wref(_v) { return is_struct(_v) && variable_struct_exists(_v, "pfo_wref"); }
function pfo_snap_key(_v) { return pfo_snap_is_wref(_v) ? _v.pfo_wref : string(_v); }
function pfo_snap_here(_v)
{
    if (!pfo_snap_is_wref(_v)) return _v;
    var _n = _v.n, _there = false;
    if (_v.t == "instance") _there = _n >= 100000 && instance_exists(_n);
    else if (_v.t == "sprite") _there = sprite_exists(_n);
    else { var _k = pfo_snap_ds_kind(_v.t); _there = _k >= 0 && pfo_snap_res_exists(_n, _k); }
    if (!_there)
    {
        // something gone by then was still a ref there (a weapon's last hit, a list a game freed but
        // still names, noone): a ref here too
        // (a freed list can't be named by handle_parse: one is made under its number for the ref,
        // and freed again, so the stale ref is the same as there and names whatever takes it next)
        var _dk = pfo_snap_ds_kind(_v.t);
        if (_dk >= 0 && _dk < 100 && _n >= 0)
        {
            var _tmp = pfo_snap_new_ds_at(_dk, _n);
            pfo_snap_free_ds(_tmp, _dk);
            return real(_tmp) == _n ? _tmp : _n;
        }
        if (_v.t != "instance" || _n >= 100000 || _n < 0) { var _gone = handle_parse(_v.pfo_wref); if (typeof(_gone) == "ref" && real(_gone) == _n) return _gone; }
        return _n;
    }
    var _h = handle_parse(_v.pfo_wref);
    return typeof(_h) == "ref" && real(_h) == _n ? _h : _n;
}

// globals that are this device's settings and screen, not the game's state (scrInitDisplay,
// scrSetDisplayDefaults): a load keeps ours, so it never changes the picture mode, and a
// snapshot from another device (a fast rejoin) doesn't bring that device's (it drew the game at
// the other device's window size)
function pfo_snap_device_global(_name)
{
    switch (_name)
    {
        case "scale": case "scaleMax": case "scaleFill": case "DEFAULT_SCALE": case "fullscreen":
        case "dispBordered": case "scanlines": case "windowHasFocus": case "canDoShaders":
            return true;
    }
    return false;
}

// what a global of this kind of ref holds is this device's own (made at run time, numbered by it):
// a snapshot from another device keeps ours there
function pfo_snap_local_kind(_ty)
{
    switch (_ty)
    {
        case "buffer": case "surface": case "camera": case "audio_emitter": case "audio_listener":
        case "sound_instance": case "time_source": case "particle_system": case "particle_system_instance":
        case "particle_emitter": case "particle_type": case "vertex_buffer": case "vertex_format":
        case "audio_sync_group": case "audio_bus":
            return true;
    }
    return false;
}

function pfo_snap_export(_id)
{
    var _snap = variable_struct_get(global.pfo_snaps, string(_id));
    if (is_undefined(_snap)) return "no snapshot " + string(_id);
    // the hidden copies as text
    var _ds = {};
    var _dk = variable_struct_get_names(_snap.ctx.ds);
    for (var i = 0; i < array_length(_dk); i++)
    {
        var _r = variable_struct_get(_snap.ctx.ds, _dk[i]);
        var _d = _r.data;
        if (_r.kind == ds_type_stack) _d = ["pfo_dsw", ds_stack_write(_d)];
        else if (_r.kind == ds_type_queue) _d = ["pfo_dsw", ds_queue_write(_d)];
        else if (_r.kind == ds_type_priority) _d = ["pfo_dsw", ds_priority_write(_d)];
        variable_struct_set(_ds, _dk[i], { ref: _r.ref, kind: _r.kind, data: _d });
    }
    var _root = {};
    var _names = variable_struct_get_names(_snap);
    for (var i = 0; i < array_length(_names); i++)
        variable_struct_set(_root, _names[i], _names[i] == "ctx" ? { ds: _ds } : variable_struct_get(_snap, _names[i]));
    var _st = { odd: 0, unbound: 0, own: 0, glob: 0, what: "" };
    var _b = buffer_create(1 << 20, buffer_grow, 1);
    buffer_write(_b, buffer_string, "PFOSNAP1");
    buffer_write(_b, buffer_f64, _id);
    pfo_snap_w(_b, _root, undefined, 0, _st);
    var _size = buffer_tell(_b);
    buffer_resize(_b, _size);
    global.pfo_snap_wire_info = string(_size) + " bytes, " + string(array_length(_snap.insts)) + " instances, "
        + string(array_length(_dk)) + " data structures; " + string(_st.odd) + " values not sent; methods: " + string(_st.glob) + " bound to global, " + string(_st.own) + " to their struct, " + string(_st.unbound) + " sent unbound" + _st.what;
    return real(_b);
}

function pfo_snap_import(_b)
{
    buffer_seek(_b, buffer_seek_start, 0);
    if (buffer_read(_b, buffer_string) != "PFOSNAP1") return "not a snapshot";
    var _id = buffer_read(_b, buffer_f64);
    // (libPFO can have it kept under another id: another device's ids can be ours too)
    if (variable_global_exists("pfo_snap_import_as") && is_numeric(global.pfo_snap_import_as) && global.pfo_snap_import_as >= 0)
    {
        _id = global.pfo_snap_import_as;
        global.pfo_snap_import_as = -1;
    }
    var _st = { odd: 0, refs: 0 };
    var _root = pfo_snap_r(_b, undefined, _st);
    if (!is_struct(_root) || !variable_struct_exists(_root, "ctx") || !variable_struct_exists(_root, "globals") || !variable_struct_exists(_root, "insts")) return "not a whole snapshot";
    // the hidden copies made again
    var _dk = variable_struct_get_names(_root.ctx.ds);
    for (var i = 0; i < array_length(_dk); i++)
    {
        var _r = variable_struct_get(_root.ctx.ds, _dk[i]);
        if (!is_array(_r.data) || array_length(_r.data) != 2 || _r.data[0] != "pfo_dsw") continue;
        var _c = pfo_snap_new_ds(_r.kind);
        if (_r.kind == ds_type_stack) ds_stack_read(_c, _r.data[1]);
        else if (_r.kind == ds_type_queue) ds_queue_read(_c, _r.data[1]);
        else ds_priority_read(_c, _r.data[1]);
        _r.data = _c;
    }
    // globals holding this device's own buffers, surfaces, cameras and the like keep ours
    var _kept = 0;
    var _gn = variable_struct_get_names(_root.globals);
    for (var i = 0; i < array_length(_gn); i++)
    {
        var _v = variable_struct_get(_root.globals, _gn[i]);
        if (typeof(_v) != "ref" || !pfo_snap_local_kind(pfo_snap_ref_type(_v)) || !variable_global_exists(_gn[i])) continue;
        var _mine = variable_global_get(_gn[i]);
        if (typeof(_mine) != "ref" || pfo_snap_ref_type(_mine) != pfo_snap_ref_type(_v)) continue;
        variable_struct_set(_root.globals, _gn[i], _mine);
        _kept++;
    }
    // (development, for "wirecmp": the snapshot this replaces, when libPFO asks for it)
    global.pfo_snap_wire_orig = variable_global_exists("pfo_snap_wire_keep") && global.pfo_snap_wire_keep ? variable_struct_get(global.pfo_snaps, string(_id)) : undefined;
    variable_struct_set(global.pfo_snaps, string(_id), _root);
    global.pfo_snap_wire_info = string(buffer_tell(_b)) + " bytes read, " + string(_st.odd) + " odd values, "
        + string(_st.refs) + " refs kept as numbers, " + string(_kept) + " globals kept ours";
    return 0;
}

// development: the snapshot before bytes (kept by the import) against the one after, value by
// value: differences ("path: before / after", with their types) and values whose type changed
function pfo_snap_wirecmp(_id)
{
    if (!variable_global_exists("pfo_snap_wire_orig") || is_undefined(global.pfo_snap_wire_orig)) return "no original kept";
    var _st = { n: 0, types: 0, out: "", tout: "" };
    pfo_snap_wcmp(global.pfo_snap_wire_orig, variable_struct_get(global.pfo_snaps, string(_id)), "", _st, 0);
    global.pfo_snap_wire_orig = undefined;
    return string(_st.n) + " differ, " + string(_st.types) + " changed type" + _st.out + (_st.tout != "" ? " | types:" + _st.tout : "");
}

function pfo_snap_wshort(_v)
{
    if (is_struct(_v) || is_array(_v) || is_method(_v)) return typeof(_v);
    var _s = string(_v);
    return string_length(_s) > 40 ? string_copy(_s, 1, 40) + "..." : _s;
}

function pfo_snap_wcmp(_a, _b, _path, _st, _depth)
{
    if (_depth > 32 || _st.n > 200) return;
    if (pfo_snap_is_wref(_b))
    {
        if (typeof(_a) != "ref" || real(_a) != _b.n || pfo_snap_ref_type(_a) != _b.t) { _st.n++; if (_st.n <= 12) _st.out += " " + _path + ": " + pfo_snap_wshort(_a) + " / " + _b.pfo_wref; }
        return;
    }
    if (is_struct(_b) && variable_struct_exists(_b, "pfo_snap_m"))
    {
        if (!is_method(_a) || real(method_get_index(_a)) != _b.pfo_snap_m[1]) { _st.n++; if (_st.n <= 12) _st.out += " " + _path + ": method / bound later"; }
        return;
    }
    var _ta = typeof(_a), _tb = typeof(_b);
    if (_ta == "method" || _tb == "method")
    {
        if (_ta != _tb || real(method_get_index(_a)) != real(method_get_index(_b))) { _st.n++; if (_st.n <= 12) _st.out += " " + _path + ": " + _ta + " / " + _tb; }
        return;
    }
    if (_ta != _tb)
    {
        var _same = (is_numeric(_a) && is_numeric(_b) && real(_a) == real(_b));
        if (_same) { _st.types++; if (_st.types <= 6) _st.tout += " " + _path + " " + _ta + "->" + _tb; }
        else { _st.n++; if (_st.n <= 12) _st.out += " " + _path + ": " + pfo_snap_wshort(_a) + " (" + _ta + ") / " + pfo_snap_wshort(_b) + " (" + _tb + ")"; }
        return;
    }
    if (_ta == "array")
    {
        if (array_length(_a) != array_length(_b)) { _st.n++; if (_st.n <= 12) _st.out += " " + _path + ": length " + string(array_length(_a)) + " / " + string(array_length(_b)); return; }
        for (var i = 0; i < array_length(_a); i++) pfo_snap_wcmp(_a[i], _b[i], _path + "[" + string(i) + "]", _st, _depth + 1);
        return;
    }
    if (_ta == "struct")
    {
        var _na = variable_struct_get_names(_a);
        for (var i = 0; i < array_length(_na); i++)
        {
            if (string_char_at(_na[i], 1) == "@") continue;
            if (!variable_struct_exists(_b, _na[i])) { _st.n++; if (_st.n <= 12) _st.out += " " + _path + "." + _na[i] + ": missing"; continue; }
            pfo_snap_wcmp(variable_struct_get(_a, _na[i]), variable_struct_get(_b, _na[i]), _path + "." + _na[i], _st, _depth + 1);
        }
        if (instanceof(_a) != instanceof(_b)) { _st.n++; if (_st.n <= 12) _st.out += " " + _path + ": " + string(instanceof(_a)) + " / " + string(instanceof(_b)); }
        return;
    }
    if (_ta == "ref")
    {
        var _ka = pfo_snap_ds_kind(pfo_snap_ref_type(_a));
        if (_ka == ds_type_queue || _ka == ds_type_stack || _ka == ds_type_priority)   // a hidden copy: its contents
        {
            var _wa, _wb, _sa, _sb;
            if (_ka == ds_type_queue) { _wa = ds_queue_write(_a); _wb = ds_queue_write(_b); _sa = ds_queue_size(_a); _sb = ds_queue_size(_b); }
            else if (_ka == ds_type_stack) { _wa = ds_stack_write(_a); _wb = ds_stack_write(_b); _sa = ds_stack_size(_a); _sb = ds_stack_size(_b); }
            else { _wa = ds_priority_write(_a); _wb = ds_priority_write(_b); _sa = ds_priority_size(_a); _sb = ds_priority_size(_b); }
            if (_wa != _wb || _sa != _sb) { _st.n++; if (_st.n <= 12) _st.out += " " + _path + ": contents differ (" + string(_sa) + " / " + string(_sb) + " items, " + string_copy(_wa, 1, 60) + " / " + string_copy(_wb, 1, 60) + ")"; }
            return;
        }
        if (string(_a) != string(_b)) { _st.n++; if (_st.n <= 12) _st.out += " " + _path + ": " + string(_a) + " / " + string(_b); }
        return;
    }
    if (_ta == "number" && is_nan(_a) && is_nan(_b)) return;
    if (_a != _b) { _st.n++; if (_st.n <= 12) _st.out += " " + _path + ": " + pfo_snap_wshort(_a) + " / " + pfo_snap_wshort(_b) + " (" + _ta + ")"; }
}

// ---- probe -----------------------------------------------------------------------------------
function pfo_snap_probe()
{
    var _before = 0; with (all) _before++;
    var _off = pfo_snap_allon();
    var _on = 0; with (all) _on++;
    var _ex1 = array_length(_off) > 0 ? instance_exists(_off[0]) : -1;
    pfo_snap_reoff(_off);
    var _after = 0; with (all) _after++;
    var _ex2 = array_length(_off) > 0 ? instance_exists(_off[0]) : -1;
    show_debug_message("PFOSNAP probe: " + string(_before) + " on, " + string(array_length(_off)) + " off; after switching all on " + string(_on)
        + " (first off one exists: " + string(_ex1) + "); after switching them off again " + string(_after) + " (exists: " + string(_ex2) + ")");
}

// ---- what's different (development) ---------------------------------------------------------
// one [name, md5, short text] per global and per instance variable, instances in their
// content order, plus what PFO's checksum reads besides variables
function pfo_snap_items()
{
    var _out = [];
    var _h = { inst: {}, ds: {}, nds: 0 };
    var _offList = pfo_snap_allon();
    var _list = pfo_snap_canonical();
    for (var n = 0; n < array_length(_list); n++) variable_struct_set(_h.inst, string(_list[n]), n);
    var _buf = buffer_create(4096, buffer_grow, 1);
    array_push(_out, ["builtin.instance_count", string(instance_count), string(instance_count)]);
    array_push(_out, ["builtin.random_seed", string(random_get_seed()), string(random_get_seed())]);
    array_push(_out, ["builtin.room", string(room), string(room)]);
    var _gn = variable_instance_get_names(global);
    array_sort(_gn, true);
    for (var i = 0; i < array_length(_gn); i++)
    {
        if (string_copy(_gn[i], 1, 8) == "pfo_snap" || string_char_at(_gn[i], 1) == "@") continue;
        var _v = variable_global_get(_gn[i]);
        if (pfo_snap_is_function(_v)) continue;
        buffer_seek(_buf, buffer_seek_start, 0);
        _h.ds = {}; _h.nds = 0;
        pfo_snap_hash_value(_buf, _v, _h, 0);
        array_push(_out, ["global." + _gn[i], buffer_md5(_buf, 0, buffer_tell(_buf)), pfo_snap_short(_v)]);
    }
    var _bi = pfo_snap_builtins();
    for (var n = 0; n < array_length(_list); n++)
    {
        var _inst = _list[n];
        var _tag = string(n) + ":" + object_get_name(_inst.object_index) + ".";
        for (var j = 0; j < array_length(_bi); j++)
        {
            buffer_seek(_buf, buffer_seek_start, 0); _h.ds = {}; _h.nds = 0;
            var _v = variable_instance_get(_inst, _bi[j]);
            pfo_snap_hash_value(_buf, _v, _h, 1);
            array_push(_out, [_tag + _bi[j], buffer_md5(_buf, 0, buffer_tell(_buf)), pfo_snap_short(_v)]);
        }
        for (var j = 0; j < 12; j++) array_push(_out, [_tag + "alarm" + string(j), string(_inst.alarm[j]), string(_inst.alarm[j])]);
        if (_inst.path_index != -1)
        {
            buffer_seek(_buf, buffer_seek_start, 0); _h.ds = {}; _h.nds = 0;
            var _pv = pfo_snap_path_of(_inst);
            pfo_snap_hash_value(_buf, _pv, _h, 1);
            array_push(_out, [_tag + "path", buffer_md5(_buf, 0, buffer_tell(_buf)), pfo_snap_short(_pv)]);
        }
        var _vn = variable_instance_get_names(_inst);
        array_sort(_vn, true);
        for (var j = 0; j < array_length(_vn); j++)
        {
            if (string_char_at(_vn[j], 1) == "@") continue;
            buffer_seek(_buf, buffer_seek_start, 0); _h.ds = {}; _h.nds = 0;
            var _v = variable_instance_get(_inst, _vn[j]);
            pfo_snap_hash_value(_buf, _v, _h, 1);
            array_push(_out, [_tag + _vn[j], buffer_md5(_buf, 0, buffer_tell(_buf)), pfo_snap_short(_v)]);
        }
    }
    pfo_snap_reoff(_offList);
    buffer_delete(_buf);
    return _out;
}

function pfo_snap_short(_v)
{
    var _s = typeof(_v);
    if (!is_struct(_v)) _s += " " + string(_v);   // (string() of a struct would run its toString)
    return string_length(_s) > 60 ? string_copy(_s, 1, 60) + "..." : _s;
}

function pfo_snap_diff(_a, _b)
{
    var _ma = ds_map_create();
    for (var i = 0; i < array_length(_a); i++) { var _ea = _a[i]; ds_map_set(_ma, _ea[0], _ea); }
    show_debug_message("PFOSNAP diff: kept " + string(array_length(_a)) + " items, now " + string(array_length(_b)));
    var _diffs = 0;
    for (var i = 0; i < array_length(_b); i++)
    {
        var _eb = _b[i];
        var _key = _eb[0];
        if (!ds_map_exists(_ma, _key)) { if (_diffs < 25) show_debug_message("PFOSNAP diff new " + _key + " = " + _eb[2]); _diffs++; continue; }
        var _was = ds_map_find_value(_ma, _key);
        if (_was[1] != _eb[1]) { if (_diffs < 25) show_debug_message("PFOSNAP diff " + _key + ": " + _was[2] + " -> " + _eb[2]); _diffs++; }
        ds_map_delete(_ma, _key);
    }
    var _gone = ds_map_keys_to_array(_ma);
    for (var i = 0; i < array_length(_gone) && i < 10; i++) show_debug_message("PFOSNAP diff gone " + _gone[i]);
    show_debug_message("PFOSNAP diff: " + string(_diffs) + " changed or new, " + string(array_length(_gone)) + " gone, of " + string(array_length(_b)));
    ds_map_destroy(_ma);
    return _diffs + array_length(_gone);
}
