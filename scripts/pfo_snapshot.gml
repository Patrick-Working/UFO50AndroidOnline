// Save-state snapshots (UFO 50 Android). Added to the game by android_patch.csx; called from
// libPFO (android/savestate.inc) through pfo_snapshot(command, id): native code finds scripts by
// their asset name, which is this function's name.
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
//   "probe", 0      log what values look like at runtime
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
            return pfo_snap_restore(_s);
        case "hash":
            return pfo_snap_hash(_arg);
        case "keep":   // fingerprint item by item and remember it (for "diff"), under its frame when given
            global.pfo_snap_kept = pfo_snap_items();
            if (!variable_global_exists("pfo_snap_keptby") || !is_struct(global.pfo_snap_keptby)) global.pfo_snap_keptby = {};
            if (_arg != 0) variable_struct_set(global.pfo_snap_keptby, string(_arg), global.pfo_snap_kept);
            show_debug_message("PFOSNAP keep: " + string(array_length(global.pfo_snap_kept)) + " items");
            return array_length(global.pfo_snap_kept);
        case "diff":   // log what changed since "keep" (the one kept under this frame, when given)
            var _base = global.pfo_snap_kept;
            if (_arg != 0 && variable_global_exists("pfo_snap_keptby") && is_struct(global.pfo_snap_keptby) && variable_struct_exists(global.pfo_snap_keptby, string(_arg)))
                _base = variable_struct_get(global.pfo_snap_keptby, string(_arg));
            return pfo_snap_diff(_base, pfo_snap_items());
        case "settle":
            return 0;
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
        case "drop":
            if (variable_struct_exists(global.pfo_snaps, string(_arg))) struct_remove(global.pfo_snaps, string(_arg));
            return 0;
        case "probe":
            pfo_snap_probe();
            return 0;
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
    }
    return -1;
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
    if (variable_struct_exists(_ctx.ds, _key) || !ds_exists(_ref, _kind)) return;
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
    var _snap = { rm: room, ctx: _ctx, globals: {}, insts: [], layers: [], views: [], frame: pfo_get_frame(), icount: instance_count, sprites: pfo_snap_sprites_copy() };
    // globals
    var _gn = variable_instance_get_names(global);
    for (var i = 0; i < array_length(_gn); i++)
    {
        var _name = _gn[i];
        if (string_copy(_name, 1, 8) == "pfo_snap" || string_char_at(_name, 1) == "@") continue;   // @@...: the runtime's own
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
                    vis: layer_get_visible(_l), ld: layer_get_depth(_l), tiles: [] };
        var _els = layer_get_all_elements(_l);
        for (var e = 0; e < array_length(_els); e++)
        {
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

function pfo_snap_count(_where)
{
    var _f = asset_get_index("obj_floor");
    var _n = 0;
    with (all) _n++;
    global.pfo_snap_stages += " " + _where + "=" + string(_n) + "/" + string(instance_count) + "/" + string(_f >= 0 ? instance_number(_f) : -1);
}

// data structures: back into the same structure if it still exists, otherwise a new one (or the one
// already made for it, in _map)
function pfo_snap_ensure_ds(_ctx, _map)
{
    var _dk = variable_struct_get_names(_ctx.ds);
    for (var i = 0; i < array_length(_dk); i++)
    {
        var _r = variable_struct_get(_ctx.ds, _dk[i]);
        if (ds_exists(_r.ref, _r.kind)) continue;
        var _have = variable_struct_get(_map, string(_r.ref));
        if (!is_undefined(_have) && ds_exists(_have, _r.kind)) continue;
        variable_struct_set(_map, string(_r.ref), pfo_snap_new_ds(_r.kind));
    }
}

function pfo_snap_fill_ds(_ctx, _map)
{
    var _dk = variable_struct_get_names(_ctx.ds);
    for (var i = 0; i < array_length(_dk); i++)
    {
        var _r = variable_struct_get(_ctx.ds, _dk[i]);
        var _to = variable_struct_get(_map, string(_r.ref));
        var _ds = is_undefined(_to) ? _r.ref : _to;
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
        if (sprite_exists(_e[0]) || !sprite_exists(_e[1])) continue;
        var _key = string(_e[0]);   // how references to the gone sprite print now (pfo_snap_remap's key)
        if (!is_undefined(variable_struct_get(_map, _key))) continue;
        var _s = pfo_sprite_duplicate(_e[1]);
        var _m = _e[2];
        if (is_array(_m)) pfo_sprite_collision_mask(_s, _m[0], _m[1], _m[2], _m[3], _m[4], _m[5], _m[6], _m[7]);
        variable_struct_set(_map, _key, _s);
    }
}

// made as an event-less shell and then turned into its object: its Create event would otherwise
// run (and in some games make or remove other instances)
function pfo_snap_make(_rec)
{
    var _inst = layer_exists(_rec.lay) ? instance_create_layer(_rec.b[0], _rec.b[1], _rec.lay, oPfoSnapShell)
        : instance_create_depth(_rec.b[0], _rec.b[1], _rec.b[20], oPfoSnapShell);
    var _obj = _rec.obj;
    with (_inst) instance_change(_obj, false);
    return _inst;
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
            var _sv = _snap.insts[n].b[j];
            if (!is_undefined(_sv) && real(_sv) >= 0 && !sprite_exists(_sv) && is_undefined(variable_struct_get(_map0, string(_sv)))) return "missing sprite " + string(_sv);
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
        pfo_snap_ensure_ds(_snap.ctx, _map0);
        var _bi0 = pfo_snap_builtins();
        var _pi = -1;
        for (var j = 0; j < array_length(_bi0); j++) if (_bi0[j] == "persistent") _pi = j;
        var _carried = [];
        for (var n = 0; n < array_length(_snap.insts); n++)
        {
            var _rec = _snap.insts[n];
            if (_pi < 0 || !_rec.b[_pi]) continue;
            if (instance_exists(_rec.oid) && _rec.oid.object_index == _rec.obj) continue;   // still here (oController and the like)
            var _have = variable_struct_get(_map0, string(_rec.oid));
            if (!is_undefined(_have) && instance_exists(_have)) continue;
            var _inst = pfo_snap_make(_rec);
            variable_struct_set(_map0, string(_rec.oid), _inst);
            array_push(_carried, [_rec, _inst]);
        }
        pfo_snap_fill_ds(_snap.ctx, _map0);
        for (var n = 0; n < array_length(_carried); n++) pfo_snap_set_vars(_carried[n][0], _carried[n][1], _map0, _bi0);
        var _gn0 = variable_struct_get_names(_g);
        for (var i = 0; i < array_length(_gn0); i++) variable_global_set(_gn0[i], pfo_snap_remap(variable_struct_get(_g, _gn0[i]), _map0, 0));
        global.pfo_snap_room_map = _map0;
        global.pfo_snap_room_for = _snap;   // only this load reuses them
        room_goto(_snap.rm);
        return 1;
    }
    audio_stop_all();
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
    var _keep = {};
    var _made = {};
    var _madeIds = "";   // "old:new," for libPFO
    for (var n = 0; n < array_length(_snap.insts); n++)
    {
        var _rec = _snap.insts[n];
        var _old = _rec.oid;
        // reuse it if it's still here, as the same object, without variables it didn't have then
        // (an instance's variables can't be removed: one that gained some is made afresh)
        if (instance_exists(_old) && _old.object_index == _rec.obj && pfo_snap_same_vars(_old, _rec.vars))
        {
            variable_struct_set(_keep, string(real(_old)), true);
            continue;
        }
        // made afresh (event-less), unless the room-change pass already made it
        var _have = variable_struct_get(_map, string(_old));
        var _inst = !is_undefined(_have) && instance_exists(_have) && _have.object_index == _rec.obj ? _have : pfo_snap_make(_rec);
        variable_struct_set(_map, string(_old), _inst);
        variable_struct_set(_made, string(_old), _inst);
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
        variable_global_set(_gn[i], pfo_snap_remap(variable_struct_get(_snap.globals, _gn[i]), _map, 0));
    pfo_snap_count("globals");
    // instance variables
    var _bi = pfo_snap_builtins();
    for (var n = 0; n < array_length(_snap.insts); n++)
    {
        var _rec = _snap.insts[n];
        var _to = variable_struct_get(_map, string(_rec.oid));
        var _inst = is_undefined(_to) ? _rec.oid : _to;
        if (!instance_exists(_inst)) continue;
        pfo_snap_set_vars(_rec, _inst, _map, _bi);
    }
    pfo_snap_count("variables");
    // which instances were switched off, the order of GameMaker's instance lists and the like are
    // put back by libPFO afterwards (savestate.inc): it needs to know the ones made afresh
    global.pfo_snap_made_ids = _madeIds;
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
    if (!ds_exists(_ref, _kind)) { buffer_write(_buf, buffer_u8, 0); return; }
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
