// UTMT script: adapt a modded UFO 50 data.win (PFO 50 + MultiMadnessMod) for Android.
//  1. Point the PFO extension at the Android library (libPFO.so).
//  2. Move the Steamworks functions PFO's GML uses (lobbies, user id, names)
//     onto the PFO extension, where libPFO.so implements them as a LAN lobby.
// Run: UndertaleModCli load data.win -s scripts/android_patch.csx -o out/data.win

using System.Linq;
using UndertaleModLib.Models;

string pfoLib = Environment.GetEnvironmentVariable("PFO_ANDROID_LIB") ?? "PFO.so";

string[] moved = {
    "steam_update", "steam_initialised", "steam_is_screenshot_requested", "steam_stats_ready", "steam_is_user_logged_on", "steam_is_overlay_activated",
    "steam_get_user_steam_id", "steam_get_persona_name", "steam_get_user_persona_name_sync",
    "steam_lobby_create", "steam_lobby_join_id", "steam_lobby_leave", "steam_lobby_get_lobby_id",
    "steam_lobby_is_owner", "steam_lobby_get_owner_id", "steam_lobby_get_member_count", "steam_lobby_get_member_id",
    "steam_lobby_get_data", "steam_lobby_set_data", "steam_lobby_set_joinable", "steam_lobby_set_type",
    "steam_lobby_activate_invite_overlay", "steam_lobby_list_add_string_filter", "steam_lobby_list_add_numerical_filter",
    "steam_lobby_list_add_near_filter", "steam_lobby_list_add_distance_filter", "steam_lobby_list_request",
    "steam_lobby_list_is_loading", "steam_lobby_list_get_count", "steam_lobby_list_get_lobby_id", "steam_lobby_list_get_data",
};
string[] added = { "pfo_lan_get_port", "pfo_lan_add_host" };

var pfo = Data.Extensions.FirstOrDefault(e => e.Name.Content == "PFO");
if (pfo == null) throw new ScriptException("PFO extension not found - is PFO 50 installed in this data.win?");
// On Android, libPFO.so registers PFO's functions as runner built-ins itself (see
// android/runner_bridge.inc), so the extension keeps only its options here.
Console.WriteLine($"PFO extension: dropping {pfo.Files.Count} file(s), keeping {pfo.Options.Count} options");
pfo.Files.Clear();

uint maxId = 0;
foreach (var e in Data.Extensions) foreach (var f in e.Files) foreach (var fn in f.Functions) maxId = Math.Max(maxId, fn.ID);

int removed = 0;
foreach (var e in Data.Extensions)
{
    if (e == pfo) continue;
    foreach (var f in e.Files)
    {
        var drop = f.Functions.Where(fn => moved.Contains(fn.Name.Content)).ToList();
        foreach (var fn in drop) { f.Functions.Remove(fn); removed++; }
    }
}
Console.WriteLine($"Removed {removed} functions from other extensions");

Console.WriteLine("Steam lobby/user functions now resolve to libPFO.so built-ins");

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
    string text = new Underanalyzer.Decompiler.DecompileContext(ctx, code, Data.ToolInfo.DecompilerSettings).DecompileToString();
    if (text.Contains(bad))
    {
        var group = new UndertaleModLib.Compiler.CodeImportGroup(Data);
        group.QueueReplace(name, text.Replace(bad, good));
        group.Import();
        Console.WriteLine("Fixed Ninpek game-over high score call");
    }
    else
    {
        Console.WriteLine("Ninpek high score call already OK");
    }
}

// ---- hot seat ----
// Turn-based games read one player's controls at a time. Report that player (and the game's
// player count) to the touch overlay (pfo_android_turn, a libPFO.so built-in) right where the
// game reads them, so the pad can follow the turn in pass-and-play. Online, libPFO ignores it.
{
    if (Data.Functions.ByName("pfo_android_turn") == null)
        Data.Functions.Add(new UndertaleFunction { Name = Data.Strings.MakeString("pfo_android_turn") });
    var hooks = new (string code, string read)[] {
        ("gml_Object_o36_Game_Step_0", "scrGetInput(cp);"),                       // Party House
        ("gml_Object_o20_Game_Step_0", "scrGetInput(cp);"),                       // Bug Hunter
        ("gml_Object_o01_Game_Step_0", "scrGetInput(currPlayer);"),               // Mortol II (turns pass on each death)
        ("gml_Object_o33_Game_Step_0", "scrGetInput(player - 1);"),               // Lords of Diskonia
        ("gml_Object_o50_Game_Step_0", "scrGetInput(controller - 1);"),           // Avianos
        ("gml_Object_o43_Game_Step_0", "scrGetInput(inputPlayer - 1);"),          // Hot Foot (team draft)
        ("gml_Object_o12__Game_Other_18", "scrGetInput(party[currPlayer].perkPx);"), // Grimstone (perk picks)
    };
    var ctx = new UndertaleModLib.Decompiler.GlobalDecompileContext(Data);
    var group = new UndertaleModLib.Compiler.CodeImportGroup(Data);
    int n = 0;
    foreach (var (name, read) in hooks)
    {
        var code = Data.Code.ByName(name);
        if (code == null) { Console.WriteLine($"hot seat: {name} missing"); continue; }
        string text = new Underanalyzer.Decompiler.DecompileContext(ctx, code, Data.ToolInfo.DecompilerSettings).DecompileToString();
        int at = text.IndexOf(read);
        if (at < 0 || text.Contains("pfo_android_turn(")) { Console.WriteLine($"hot seat: {name} not patched"); continue; }
        string arg = read.Substring("scrGetInput(".Length, read.Length - "scrGetInput(".Length - 2);
        group.QueueReplace(name, text.Substring(0, at) + "pfo_android_turn(" + arg + ", global.numPlayers); " + text.Substring(at));
        n++;
    }
    group.Import();
    Console.WriteLine($"hot seat: reported turns in {n} games");
}

// ---- gamepad bindings ----
// The touch overlay decides which gamepad slot each player reads (players panel: touch, pad 1,
// pad 2 ... -> P1-P4). Every place the game reads a player's slot asks libPFO first
// (pfo_android_joyslot) and writes the answer back into global.joySlot, so the game's own
// menus show the same pairing. The game's auto re-pairing on pad count changes is switched off
// (it counts the touch pads as real ones), and its "pad pairing" menu item opens our panel.
// Each game's controls page and the button remapping stay as they are.
{
    foreach (var fn in new[] { "pfo_android_joyslot", "pfo_android_open_players" })
        if (Data.Functions.ByName(fn) == null)
            Data.Functions.Add(new UndertaleFunction { Name = Data.Strings.MakeString(fn) });
    string sync = "global.joySlot[arg0] = pfo_android_joyslot(arg0, global.joySlot[arg0]);\n";
    string syncAll = "for (var _q = 0; _q < array_length(global.joySlot); _q++) global.joySlot[_q] = pfo_android_joyslot(_q, global.joySlot[_q]);\n";
    var edits = new (string code, string find, string replace)[] {
        ("gml_GlobalScript_scrGetInput", "var _slot = global.joySlot[arg0];", sync + "var _slot = global.joySlot[arg0];"),
        ("gml_GlobalScript_scrJoyCheck", "var _slot = global.joySlot[arg0];", sync + "var _slot = global.joySlot[arg0];"),
        ("gml_GlobalScript_scrGetInputBothStart", "var joy_slot = global.joySlot;", syncAll + "var joy_slot = global.joySlot;"),
        ("gml_GlobalScript_scrGetInputMergedFire1", "var joy_slot = global.joySlot;", syncAll + "var joy_slot = global.joySlot;"),
        ("gml_GlobalScript_scrFixJoyDisconnect", "var _statusChange = false;", "padCount = scrGetJoyCount();\nexit;\nvar _statusChange = false;"),
    };
    var ctx = new UndertaleModLib.Decompiler.GlobalDecompileContext(Data);
    var group = new UndertaleModLib.Compiler.CodeImportGroup(Data);
    int n = 0;
    foreach (var (name, find, replace) in edits)
    {
        var code = Data.Code.ByName(name);
        string text = code == null ? null : new Underanalyzer.Decompiler.DecompileContext(ctx, code, Data.ToolInfo.DecompilerSettings).DecompileToString();
        if (text == null || !text.Contains(find) || text.Contains("pfo_android_joyslot") || text.Contains("padCount = scrGetJoyCount();\n    exit;"))
        { Console.WriteLine($"pad bindings: {name} not patched"); continue; }
        group.QueueReplace(name, text.Replace(find, replace));
        n++;
    }
    // pause menu > joypad config > pad pairing: open the overlay's players panel instead
    {
        string name = "gml_Object_oPauseMenu_Other_13";
        var code = Data.Code.ByName(name);
        string text = new Underanalyzer.Decompiler.DecompileContext(ctx, code, Data.ToolInfo.DecompilerSettings).DecompileToString();
        var re = new System.Text.RegularExpressions.Regex(@"strInput = scrStringValManual\(""menu_misc_joy_assign"", 0, currPlayer \+ 1\);\s*scrSwitchSub\(SUB_GET_SLOT\);");
        if (re.IsMatch(text) && !text.Contains("pfo_android_open_players"))
        {
            group.QueueReplace(name, re.Replace(text, "pfo_android_open_players(currPlayer);\nscrSwitchSub(SUB_INIT);", 1));
            n++;
        }
        else Console.WriteLine("pad bindings: pause menu pairing not patched");
    }
    group.Import();
    Console.WriteLine($"pad bindings: patched {n} scripts");
}

// ---- screen scale + key bindings for the touch overlay ----
// 1. The game only honours Video Settings > Scale in fullscreen; on Android it counts as
//    windowed and always stretches to the screen height. Treat Android as fullscreen, so a
//    smaller scale (e.g. 4x) leaves room beside the picture for the touch controls.
// 2. Report each player's keyboard bindings (pfo_android_keys) so touch sends the keys the
//    game has bound, rebinds included, and can show their key icons.
{
    if (Data.Functions.ByName("pfo_android_keys") == null)
        Data.Functions.Add(new UndertaleFunction { Name = Data.Strings.MakeString("pfo_android_keys") });
    var ctx = new UndertaleModLib.Decompiler.GlobalDecompileContext(Data);
    var group = new UndertaleModLib.Compiler.CodeImportGroup(Data);
    int n = 0;
    var edits = new (string code, string find, string replace)[] {
        ("gml_Object_oScreenHandler_Draw_75", "var window_is_fullscreen = window_get_fullscreen();",
            "var window_is_fullscreen = window_get_fullscreen() || os_type == os_android;"),
        ("gml_GlobalScript_scrGetInput", "var key_map = global.keyMap[arg0];",
            "var key_map = global.keyMap[arg0];\npfo_android_keys(arg0, key_map[0], key_map[1], key_map[2], key_map[3], key_map[4], key_map[5], key_map[6]);"),
    };
    foreach (var (name, find, replace) in edits)
    {
        var code = Data.Code.ByName(name);
        string text = new Underanalyzer.Decompiler.DecompileContext(ctx, code, Data.ToolInfo.DecompilerSettings).DecompileToString();
        if (!text.Contains(find)) { Console.WriteLine($"overlay support: {name} not patched"); continue; }
        group.QueueReplace(name, text.Replace(find, replace));
        n++;
    }
    group.Import();
    Console.WriteLine($"overlay support: patched {n} scripts (screen scale, key bindings)");
}


// ---- private lobbies in the game's own online menu ----
// The lobby list gets a third button, JOIN BY CODE, which opens the overlay's invite-code box
// (pfo_android_ask_code); the buttons become two-line "compact" boxes so three fit above BACK:
// HOST PUBLIC / HOST PRIVATE / JOIN BY CODE. In a lobby, INVITE FRIENDS becomes SHARE CODE (the
// overlay shows the private lobby's code; Steam's invite overlay doesn't exist here).
{
    if (Data.Functions.ByName("pfo_android_ask_code") == null)
        Data.Functions.Add(new UndertaleFunction { Name = Data.Strings.MakeString("pfo_android_ask_code") });
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
    };
    var texts = new Dictionary<string, string>();
    foreach (var (name, find, replace) in edits)
    {
        if (!texts.ContainsKey(name))
        {
            var code = Data.Code.ByName(name);
            texts[name] = code == null ? null : new Underanalyzer.Decompiler.DecompileContext(ctx, code, Data.ToolInfo.DecompilerSettings).DecompileToString();
        }
        string t = texts[name];
        int count = t == null ? 0 : (t.Length - t.Replace(find, "").Length) / Math.Max(1, find.Length);
        if (count != 1) { Console.WriteLine($"lobby menu: {name} edit not applied ({count} matches): {find.Substring(0, Math.Min(40, find.Length))}"); continue; }
        texts[name] = t.Replace(find, replace);
        n++;
    }
    foreach (var kv in texts) if (kv.Value != null) group.QueueReplace(kv.Key, kv.Value);
    group.Import();
    Console.WriteLine($"lobby menu: applied {n} of {edits.Length} edits (HOST PUBLIC / HOST PRIVATE / JOIN BY CODE, SHARE CODE)");
}


// ---- Quit closes the app ----
// The main menu's Quit calls game_end(), which doesn't close the app on Android; call libPFO's
// pfo_android_quit instead (finishes the activity and ends the process).
{
    if (Data.Functions.ByName("pfo_android_quit") == null)
        Data.Functions.Add(new UndertaleFunction { Name = Data.Strings.MakeString("pfo_android_quit") });
    string name = "gml_Object_oPauseMenu_Other_10";
    var ctx = new UndertaleModLib.Decompiler.GlobalDecompileContext(Data);
    var code = Data.Code.ByName(name);
    string text = new Underanalyzer.Decompiler.DecompileContext(ctx, code, Data.ToolInfo.DecompilerSettings).DecompileToString();
    int count = (text.Length - text.Replace("game_end();", "").Length) / "game_end();".Length;
    if (count == 1)
    {
        var group = new UndertaleModLib.Compiler.CodeImportGroup(Data);
        group.QueueReplace(name, text.Replace("game_end();", "pfo_android_quit();"));
        group.Import();
        Console.WriteLine("quit: main menu Quit now closes the app");
    }
    else Console.WriteLine($"quit: not patched ({count} game_end calls)");
}


// ---- longer join timeout ----
// A join that can't reach the host directly falls back to the okplay relay after a few seconds;
// the game's 6 s join timeout is too short for that, so allow 20 s.
{
    string name = "gml_GlobalScript_oLibrary_Online";
    string find = "requestTimeoutTime = current_time + 6000;\n        steam_lobby_join_id(arg0.lobbyId);";
    var ctx = new UndertaleModLib.Decompiler.GlobalDecompileContext(Data);
    var code = Data.Code.ByName(name);
    string text = new Underanalyzer.Decompiler.DecompileContext(ctx, code, Data.ToolInfo.DecompilerSettings).DecompileToString();
    if (text.Contains(find))
    {
        var group = new UndertaleModLib.Compiler.CodeImportGroup(Data);
        group.QueueReplace(name, text.Replace(find, "requestTimeoutTime = current_time + 20000;\n        steam_lobby_join_id(arg0.lobbyId);"));
        group.Import();
        Console.WriteLine("join timeout: 20 s (room for the relay fallback)");
    }
    else Console.WriteLine("join timeout: not patched");
}
