// UTMT script: adapt a modded UFO 50 data.win (PFO 50 + MultiMadnessMod) for the Windows build
// with our netcode (the same rooms, LAN discovery and relay as the Android build).
//  1. Every Steamworks function the game's code uses moves from Steamworks_x64.dll to PFO.dll:
//     our netcode answers the lobby, user id and name ones, and the few others (achievements,
//     screenshot, shutdown) do nothing, as on Android. The build has no Steam library and never
//     talks to Steam.
//  2. PFO.dll gets the functions of ours the game calls (pfo_android_ask_code).
//  3. The game keeps its saves in its own folder, %LOCALAPPDATA%\ufo50_online, never in the
//     Steam game's %LOCALAPPDATA%\ufo50.
//  4. The game fixes and the lobby menu of the Android build that apply here too:
//     Ninpek's game-over crash, HOST PUBLIC / HOST PRIVATE / JOIN BY CODE, SHARE CODE, and the
//     longer join timeout.
// An edit that can't be applied fails the script: the last line it prints is "windows patch: OK"
// only when everything went in.
// Run: UndertaleModCli load data.win -s scripts/windows_patch.csx -o out/data.win

using System.Linq;
using UndertaleModLib.Models;

var failures = new List<string>();

string[] moved = {
    "steam_update", "steam_initialised", "steam_is_screenshot_requested", "steam_stats_ready", "steam_is_user_logged_on", "steam_is_overlay_activated",
    "steam_get_user_steam_id", "steam_get_persona_name", "steam_get_user_persona_name_sync",
    "steam_lobby_create", "steam_lobby_join_id", "steam_lobby_leave", "steam_lobby_get_lobby_id",
    "steam_lobby_is_owner", "steam_lobby_get_owner_id", "steam_lobby_get_member_count", "steam_lobby_get_member_id",
    "steam_lobby_get_data", "steam_lobby_set_data", "steam_lobby_set_joinable", "steam_lobby_set_type",
    "steam_lobby_activate_invite_overlay", "steam_lobby_list_add_string_filter", "steam_lobby_list_add_numerical_filter",
    "steam_lobby_list_add_near_filter", "steam_lobby_list_add_distance_filter", "steam_lobby_list_request",
    "steam_lobby_list_is_loading", "steam_lobby_list_get_count", "steam_lobby_list_get_lobby_id", "steam_lobby_list_get_data",
    // answered with "no" (win/runner_bridge_win.inc)
    "steam_get_achievement", "steam_set_achievement", "steam_reset_all_stats_achievements", "steam_send_screenshot", "steam_shutdown",
};
// ours, called by the game code this script adds: the invite-code box, where the game draws its
// picture, and the three that scripts/pfo_snapshot.gml calls
string[] added = { "pfo_android_ask_code", "pfo_android_screen", "pfo_runner_mark", "pfo_runner_rehide", "pfo_runner_settle" };

var pfo = Data.Extensions.FirstOrDefault(e => e.Name.Content == "PFO");
if (pfo == null) throw new ScriptException("PFO extension not found - is PFO 50 installed in this data.win?");
var pfoFile = pfo.Files.FirstOrDefault(f => f.Filename.Content.Equals("PFO.dll", StringComparison.OrdinalIgnoreCase));
if (pfoFile == null) throw new ScriptException("the PFO extension has no PFO.dll - is this the Windows data.win?");

// The runner finds every function of a file in that file's DLL, so moving an entry (with its
// id) is all it takes. Both DLLs export their functions the same way (under the C++ name of
// "void name(RValue&, CInstance*, CInstance*, int, RValue*)", which the runner tries first
// whatever kind and argument types the entry declares).
{
    int already = pfoFile.Functions.Count(fn => moved.Contains(fn.Name.Content));
    int n = 0;
    foreach (var e in Data.Extensions)
    {
        if (e == pfo) continue;
        foreach (var f in e.Files)
        {
            var take = f.Functions.Where(fn => moved.Contains(fn.Name.Content)).ToList();
            foreach (var fn in take)
            {
                f.Functions.Remove(fn);
                pfoFile.Functions.Add(fn);
                n++;
            }
        }
    }
    Console.WriteLine($"extension: moved {n} Steam functions to PFO.dll ({already} were there already)");
    if (n + already != moved.Length) failures.Add($"extension: {n + already} of {moved.Length} Steam functions found");

    uint maxId = 0;
    foreach (var e in Data.Extensions) foreach (var f in e.Files) foreach (var fn in f.Functions) maxId = Math.Max(maxId, fn.ID);
    var model = pfoFile.Functions.First(fn => fn.Name.Content == "pfo_update");
    foreach (var name in added)
    {
        if (pfoFile.Functions.Any(fn => fn.Name.Content == name)) continue;
        pfoFile.Functions.Add(new UndertaleExtensionFunction {
            Name = Data.Strings.MakeString(name), ExtName = Data.Strings.MakeString(name),
            Kind = model.Kind, RetType = model.RetType, ID = ++maxId,
        });
        if (Data.Functions.ByName(name) == null)
            Data.Functions.Add(new UndertaleFunction { Name = Data.Strings.MakeString(name) });
        Console.WriteLine($"extension: added {name} (id {maxId})");
    }
}

// Nothing in the game's code may still need Steamworks_x64.dll, which this build doesn't have.
{
    var left = new HashSet<string>();
    foreach (var code in Data.Code)
    {
        if (code.Instructions == null) continue;
        foreach (var ins in code.Instructions)
        {
            string fn = ins.ValueFunction?.Name?.Content;
            if (fn != null && fn.StartsWith("steam_") && !pfoFile.Functions.Any(f => f.Name.Content == fn)) left.Add(fn);
        }
    }
    if (left.Count > 0) failures.Add("extension: the game uses Steam functions PFO.dll doesn't answer: " + string.Join(" ", left.OrderBy(x => x)));
    else Console.WriteLine("extension: every Steam function the game uses is answered by PFO.dll");
}

// ---- its own save folder ----
// The runner keeps saves in %LOCALAPPDATA%\<name of the game>. The Steam game's name is ufo50;
// this build must never write there.
{
    string was = Data.GeneralInfo.Name.Content;
    Data.GeneralInfo.Name = Data.Strings.MakeString("ufo50_online");
    Console.WriteLine($"save folder: game name {was} -> ufo50_online");
}

// ---- game fixes ----
// MultiMadnessMod 1.30 makes Ninpek's game-over pass points[0..7] to scrHighscore, but
// points[] only has as many slots as there are players, so any 1-7 player game crashes
// at game over ("Variable Index [n] out of range"). Pass only the slots that exist.
{
    string name = "gml_Object_o34_Mas_Step_0";
    string bad = "scrHighscore(34, points[0], points[1], points[2], points[3], points[4], points[5], points[6], points[7]);";
    string good = "scrHighscore(34, points[0], (array_length(points) > 1) ? points[1] : 0, (array_length(points) > 2) ? points[2] : 0, (array_length(points) > 3) ? points[3] : 0, (array_length(points) > 4) ? points[4] : 0, (array_length(points) > 5) ? points[5] : 0, (array_length(points) > 6) ? points[6] : 0, (array_length(points) > 7) ? points[7] : 0);";
    var code = Data.Code.ByName(name);
    var ctx = new UndertaleModLib.Decompiler.GlobalDecompileContext(Data);
    string text = code == null ? "" : new Underanalyzer.Decompiler.DecompileContext(ctx, code, Data.ToolInfo.DecompilerSettings).DecompileToString();
    if (text.Contains(bad))
    {
        var group = new UndertaleModLib.Compiler.CodeImportGroup(Data);
        group.QueueReplace(name, text.Replace(bad, good));
        group.Import();
        Console.WriteLine("Fixed Ninpek game-over high score call");
    }
    else
    {
        // not a failure: a later MultiMadnessMod may have fixed it
        Console.WriteLine("Ninpek high score call already OK");
    }
}

// ---- private lobbies in the game's own online menu ----
// The lobby list gets a third button, JOIN BY CODE, which opens our invite-code box
// (pfo_android_ask_code); the buttons become two-line "compact" boxes so three fit above BACK:
// HOST PUBLIC / HOST PRIVATE / JOIN BY CODE. In a lobby, INVITE FRIENDS becomes SHARE CODE (our
// window shows the private lobby's code; Steam's invite overlay knows nothing of these lobbies).
{
    var ctx = new UndertaleModLib.Decompiler.GlobalDecompileContext(Data);
    var group = new UndertaleModLib.Compiler.CodeImportGroup(Data);
    int n = 0;
    var edits = new (string code, string find, string replace)[] {
        // compact two-line buttons (optional 4th argument), any number of lines
        ("gml_GlobalScript_oLibrary_Online", "var buttonHeight = 57;", "var compact = argument_count > 3 && argument[3];\nvar buttonHeight = compact ? 41 : 57;"),
        ("gml_GlobalScript_oLibrary_Online",
            "draw_text_centered(buttonLeft + (buttonWidth / 2), buttonTextY + textOffsetY, arg0[0], 8);",
            "for (var _ln = 0; _ln < array_length(arg0); _ln++)\n{\ndraw_text_centered(buttonLeft + (buttonWidth / 2), buttonTextY + textOffsetY + (_ln * 16), arg0[_ln], 8);\n}"),
        ("gml_GlobalScript_oLibrary_Online", "draw_text_centered(buttonLeft + (buttonWidth / 2), buttonTextY + textOffsetY + 16, arg0[1], 8);", ""),
        ("gml_GlobalScript_oLibrary_Online", "draw_text_centered(buttonLeft + (buttonWidth / 2), buttonTextY + textOffsetY + 32, arg0[2], 8);", ""),
        ("gml_GlobalScript_oLibrary_Online", "draw_sprite(sMenuHand, 0, buttonLeft - 18, buttonTextY + 17);", "draw_sprite(sMenuHand, 0, buttonLeft - 18, buttonTextY + (compact ? 9 : 17));"),
        ("gml_GlobalScript_oLibrary_Online", "selY += 64;", "selY += compact ? 46 : 64;"),
        // BACK sits after however many buttons were drawn (was always the 3rd)
        ("gml_GlobalScript_oLibrary_Online", "var selected = sel2 == 2 && subsel == 1;", "var selected = sel2 == sel && subsel == 1;"),
        // the lobby list: three compact buttons
        ("gml_Object_oLibrary_Other_19",
            "scrDrawOnlineMenuSelection([\"CREATE\", \"PUBLIC\", \"LOBBY\"], true);",
            "scrDrawOnlineMenuSelection([\"HOST\", \"PUBLIC\"], true, 0, true);\nscrDrawOnlineMenuSelection([\"HOST\", \"PRIVATE\"], true, 0, true);\nscrDrawOnlineMenuSelection([\"JOIN BY\", \"CODE\"], true, 0, true);"),
        ("gml_Object_oLibrary_Other_19", "scrDrawOnlineMenuSelection([\"CREATE\", \"PRIVATE\", \"LOBBY\"], true);", ""),
        ("gml_Object_oLibrary_Other_19", "sel2 = scrVertNav(sel2, 2);\n    }\n    scrUpdateLobbySel();\n    if (selectionAcceptLockoutTime > current_time)\n    {\n        fire2pressed = false;\n    }\n    if ((fire2pressed && subsel == 1 && sel2 == 2) || fire1pressed)",
            "sel2 = scrVertNav(sel2, 3);\n    }\n    scrUpdateLobbySel();\n    if (selectionAcceptLockoutTime > current_time)\n    {\n        fire2pressed = false;\n    }\n    if (fire2pressed && subsel == 1 && sel2 == 2)\n    {\n        pfo_android_ask_code();\n    }\n    else if ((fire2pressed && subsel == 1 && sel2 == 3) || fire1pressed)"),
        // in a lobby
        ("gml_Object_oLibrary_Other_20", "scrDrawOnlineMenuSelection([\"INVITE\", \"FRIENDS\", \"\"], true, 8);", "scrDrawOnlineMenuSelection([\"SHARE\", \"CODE\", \"\"], true, 8);"),
        // a join that can't reach the host directly falls back to the okplay relay after a few
        // seconds; the game's 6 s join timeout is too short for that, so allow 20 s
        ("gml_GlobalScript_oLibrary_Online", "requestTimeoutTime = current_time + 6000;\n        steam_lobby_join_id(arg0.lobbyId);",
            "requestTimeoutTime = current_time + 20000;\n        steam_lobby_join_id(arg0.lobbyId);"),
        // PFO's "Delay: Nf" text means nothing in solo play (a session of one, there for saves and rewinds)
        ("gml_Object_oController_Draw_64", "if (showDelay && (pfo_get_frame() > 4 || playingReplay))",
            "if (showDelay && pfo_get_client_count() > 1 && (pfo_get_frame() > 4 || playingReplay))"),
    };
    var texts = new Dictionary<string, string>();   // several edits to one script build on each other
    foreach (var (name, find, replace) in edits)
    {
        if (!texts.ContainsKey(name))
        {
            var code = Data.Code.ByName(name);
            texts[name] = code == null ? null : new Underanalyzer.Decompiler.DecompileContext(ctx, code, Data.ToolInfo.DecompilerSettings).DecompileToString();
        }
        string t = texts[name];
        int count = t == null ? 0 : (t.Length - t.Replace(find, "").Length) / Math.Max(1, find.Length);
        if (count != 1) { failures.Add($"lobby menu: {name} edit not applied ({count} matches): {find.Substring(0, Math.Min(40, find.Length))}"); continue; }
        texts[name] = t.Replace(find, replace);
        n++;
    }
    foreach (var kv in texts) if (kv.Value != null) group.QueueReplace(kv.Key, kv.Value);
    group.Import();
    Console.WriteLine($"lobby menu: applied {n} of {edits.Length} edits (HOST PUBLIC / HOST PRIVATE / JOIN BY CODE, SHARE CODE, 20 s join timeout, no delay text in solo play)");
}

// ---- save states: where the picture is ----
// The game tells PFO.dll where it draws its picture, from the last Draw event of every frame
// (pfo_android_screen, as on Android). A save's picture is cut to that, and a replay stops
// showing frames there: the Draw events still run (the game does some of its work in them), only
// the wait for the screen is skipped.
{
    const string name = "gml_Object_oScreenHandler_Draw_75", find = "    if (global_scanlines && global_scale > 1)";
    var ctx = new UndertaleModLib.Decompiler.GlobalDecompileContext(Data);
    var code = Data.Code.ByName(name);
    string t = code == null ? "" : new Underanalyzer.Decompiler.DecompileContext(ctx, code, Data.ToolInfo.DecompilerSettings).DecompileToString();
    int count = (t.Length - t.Replace(find, "").Length) / find.Length;
    if (t.Contains("pfo_android_screen(")) Console.WriteLine("screen: already reported");
    else if (count != 1) failures.Add($"screen: {name} edit not applied ({count} matches)");
    else
    {
        var group = new UndertaleModLib.Compiler.CodeImportGroup(Data);
        group.QueueReplace(name, t.Replace(find, "    pfo_android_screen(screenX, screenY, screenWidth, screenHeight, window_get_width(), window_get_height());\n" + find));
        group.Import();
        Console.WriteLine("screen: the game reports where it draws its picture");
    }
}

// ---- save states: snapshots (the game-side code) ----
// scripts/pfo_snapshot.gml (path in PFO_SNAPSHOT_GML) becomes the global script pfo_snapshot, as in
// android_patch.csx; PFO.dll calls its functions by name (android/savestate.inc). PFO.dll has
// snapshots only with this script in the game and the ufo50.exe it knows (win/runner_bridge_win.inc);
// without either, loads and rewinds replay the session.
{
    string path = Environment.GetEnvironmentVariable("PFO_SNAPSHOT_GML");
    if (string.IsNullOrEmpty(path) || !File.Exists(path)) failures.Add("snapshots: PFO_SNAPSHOT_GML isn't set to scripts/pfo_snapshot.gml");
    else
    {
        // GameMaker functions the script uses that the game itself never calls: the compiler only
        // knows the ones the game uses, and would take the rest for variables. The script names
        // them on its "// uses:" lines.
        string source = File.ReadAllText(path);
        var builtins = source.Split('\n').Where(l => l.StartsWith("// uses:"))
            .SelectMany(l => l.Substring(8).Split(new[] { ' ', ',', '\r', '\t' }, StringSplitOptions.RemoveEmptyEntries)).Distinct().ToArray();
        if (builtins.Length < 100) failures.Add("snapshots: " + Path.GetFileName(path) + " has no \"// uses:\" lines");
        int declared = 0;
        foreach (var fn in builtins)
            if (Data.Functions.ByName(fn) == null) { Data.Functions.Add(new UndertaleFunction { Name = Data.Strings.MakeString(fn) }); declared++; }
        // an object with no events: a restore makes instances as this and turns them into their
        // object with instance_change(obj, false), so no Create (or Destroy) event runs
        if (Data.GameObjects.ByName("oPfoSnapShell") == null)
        {
            var shell = new UndertaleGameObject();
            shell.Name = Data.Strings.MakeString("oPfoSnapShell");
            shell.Visible = false;
            Data.GameObjects.Add(shell);
        }
        var group = new UndertaleModLib.Compiler.CodeImportGroup(Data);
        group.AutoCreateAssets = true;
        group.QueueReplace("gml_GlobalScript_pfo_snapshot", source);
        group.Import();
        if (Data.Code.ByName("gml_GlobalScript_pfo_snapshot") == null) failures.Add("snapshots: the script didn't go in");
        else Console.WriteLine($"snapshots: game-side code added ({declared} functions declared)");

        // The games that copy sprites at run time (sprite_duplicate + sprite_collision_mask, deleted
        // with sprite_delete) call the script's wrappers instead, which note each copy so a
        // snapshot can make it again. (After the import: the wrappers must already be script
        // functions, or the compiler would take them for runner built-ins, which don't exist.)
        var sctx = new UndertaleModLib.Decompiler.GlobalDecompileContext(Data);
        var sgroup = new UndertaleModLib.Compiler.CodeImportGroup(Data);
        int routed = 0;
        foreach (var code in Data.Code)
        {
            if (code.ParentEntry != null || !(code.Name.Content.StartsWith("gml_Object_o04_") || code.Name.Content.StartsWith("gml_Object_o41_"))) continue;
            string text;
            try { text = new Underanalyzer.Decompiler.DecompileContext(sctx, code, Data.ToolInfo.DecompilerSettings).DecompileToString(); }
            catch { continue; }
            if (!text.Contains("sprite_duplicate(") && !text.Contains("sprite_collision_mask(") && !text.Contains("sprite_delete(")) continue;
            if (text.Contains("pfo_sprite_")) continue;   // (patched before)
            text = text.Replace("sprite_duplicate(", "pfo_sprite_duplicate(").Replace("sprite_collision_mask(", "pfo_sprite_collision_mask(").Replace("sprite_delete(", "pfo_sprite_delete(");
            sgroup.QueueReplace(code.Name.Content, text);
            routed++;
        }
        sgroup.Import();
        Console.WriteLine("runtime sprites: " + routed + " scripts route through the snapshot wrappers");
    }
}

foreach (var f in failures) Console.WriteLine("FAILED " + f);
if (failures.Count > 0) throw new ScriptException($"windows patch: {failures.Count} edit(s) could not be applied (see the FAILED lines)");
Console.WriteLine("windows patch: OK");
