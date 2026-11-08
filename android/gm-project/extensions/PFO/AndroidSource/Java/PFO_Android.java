package ${YYAndroidPackageName};

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.LightingColorFilter;
import android.graphics.Rect;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.HapticFeedbackConstants;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;

import com.yoyogames.runner.RunnerJNILib;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

// 1. Loads libPFO.so when the activity starts (before the game data is linked), so its
//    JNI_OnLoad can register PFO's functions as runner built-ins.
// 2. Adds a classic touch controller (d-pad, A, B, Start, Back) on top of the game.
//    Tap the gear (top right) to move, resize, hide controls and change opacity.
public class PFO_Android {
    static {
        try {
            System.loadLibrary("PFO");
            Log.i("yoyo", "PFO: native library loaded");
        } catch (Throwable t) {
            Log.i("yoyo", "PFO: failed to load native library: " + t);
        }
    }

    public PFO_Android() {
        final Handler main = new Handler(Looper.getMainLooper());
        main.post(new Runnable() {
            int tries = 0;
            @Override public void run() {
                Activity a = RunnerActivity.CurrentActivity;
                if (a == null || a.getWindow() == null || a.findViewById(android.R.id.content) == null) {
                    if (++tries < 100) main.postDelayed(this, 100);
                    return;
                }
                a.addContentView(new TouchOverlay(a), new ViewGroup.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
                Log.i("yoyo", "PFO: touch overlay added");
            }
        });
    }

    public void Init() {}

    // The game's Quit (pfo_android_quit): close the app for real - finish the activity, drop it
    // from recent apps, then end the process so nothing (network threads) keeps running.
    public static void quitApp() {
        final Activity a = RunnerActivity.CurrentActivity;
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override public void run() {
                try { if (a != null) a.finishAndRemoveTask(); } catch (Throwable t) { }
                new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
                    @Override public void run() { android.os.Process.killProcess(android.os.Process.myPid()); }
                }, 400);
            }
        });
    }

    // libPFO.so: which virtual touch pad (1-4) is in gamepad slot `slot`, or -1
    static native int nativePadForSlot(int slot);
    static native int nativeOnlineSeat();
    static native String nativeSeatInfo();                 // online: "<our client>;<seat 0 client>,...;<name>|<name>|..."
    static native int nativeHotSeatTurn();
    static native int nativeHotSeatCount();
    static native String nativePadState();                 // real pads: "slot\tbuttons\tname" lines
    static native void nativeSetPlayerSlots(int[] slots);  // per player: real pad slot, -3 touch pad, -1 none
    static native int nativeTakeOpenPanel();
    static native int[] nativeKeys(int player);
    static native void nativeSetPlayerName(String name);    // "" = the device's default name
    static native String nativeGetPlayerName();
    static native String nativeLobbyCode();                  // our private lobby's invite code, "" if none
    static native boolean nativeTakeInviteRequest();         // INVITE FRIENDS was pressed
    static native void nativeAddJoinCode(String code);       // show that private lobby in our list
    static native boolean nativeTakeAskCode();               // JOIN BY CODE was pressed in the lobby list
    static native int nativeNetStatus();                     // how we're connected + lobby/online bits (runner_bridge.inc)
    static native void nativeStateRequest(int what, int slot); // save states: 1 start solo play, 2 save, 3 load (savestate.inc)
    static native int nativeStateStatus();                   // solo/can-save/restoring bits, progress, last result
    static native int nativeLastSavedSlot();                 // the slot the last save went to (online: libPFO picks it)
    static native int[] nativeVote();                        // online load vote (savestate.inc nativeVote)
    static native int[] nativeWaiting();                     // online: seats we're frozen waiting for, seconds left, reconnections
    static native String nativeRejoinOffer();                // a match we were in and didn't see end: "<s ago>|<seat>|<names>" or ""
    static native int nativeRejoinResult();                  // 1 rejoined, -1 couldn't, 0 nothing new
    static native int nativeOfferRefused();                  // a save that can't load in this match: its player count
    static native void nativeVoteAnswer(boolean accept);     // our answer: ACCEPT or DECLINE
    static native int nativeMatchSave(int slot);             // this match's save number for a slot (0: not loadable online)
    static native int[] nativeScreenRect();                  // where the game draws: x, y, w, h, window w, window h
    static native void nativeSetUpnp(boolean on);            // let a host ask its router to forward the game port            // the game's key bindings (vk codes) or null               // pause menu asked for the players panel (player) or -1

    // Called from libPFO.so's network thread (never the UI or GL thread): one form POST to the
    // okplay room server. Returns "<http status>\n<body>", or "0\n<error>" when it couldn't connect.
    public static String okpPost(String url, String body) {
        // A kept-alive connection the server already closed fails at once ("unexpected end of
        // stream") and Android doesn't retry a POST by itself, so try again.
        // (Up to 3 tries: the pool can hold more than one dead connection.)
        String r = okpPostOnce(url, body, false);
        for (int i = 0; i < 2 && (r.startsWith("0\njava.io.IOException") || r.startsWith("0\njava.io.EOFException")); i++)
            r = okpPostOnce(url, body, true);
        return r;
    }

    static String okpPostOnce(String url, String body, boolean fresh) {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL(url).openConnection();
            c.setConnectTimeout(6000);
            c.setReadTimeout(8000);
            c.setRequestMethod("POST");
            c.setDoOutput(true);
            c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded");
            c.setRequestProperty("User-Agent", "UFO50-Android-PFO");
            if (fresh) c.setRequestProperty("Connection", "close");
            byte[] data = body.getBytes("UTF-8");
            c.setFixedLengthStreamingMode(data.length);
            OutputStream o = c.getOutputStream();
            o.write(data);
            o.close();
            int code = c.getResponseCode();
            InputStream in = code < 400 ? c.getInputStream() : c.getErrorStream();
            ByteArrayOutputStream buf = new ByteArrayOutputStream();
            if (in != null) {
                byte[] tmp = new byte[8192];
                int n;
                while ((n = in.read(tmp)) > 0 && buf.size() < (1 << 20)) buf.write(tmp, 0, n);
                in.close();
            }
            return code + "\n" + buf.toString("UTF-8");
        } catch (Throwable t) {
            return "0\n" + t;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    // ------------------------------------------------------------------------------------------

    // UFO 50's own menu font (sFontDefault, exported at build time into assets/pfo_font.png:
    // 95 glyphs, ASCII 32-126, white with a dark shadow), drawn at whole-pixel scales.
    static final class PixelFont {
        final Bitmap strip;
        final int cw, ch;
        final Paint paint = new Paint();
        final Rect src = new Rect();
        final RectF dst = new RectF();
        int lastColor = 0;

        PixelFont(Bitmap b) { strip = b; ch = b.getHeight(); cw = b.getWidth() / 95; paint.setFilterBitmap(false); paint.setAntiAlias(false); }

        static PixelFont load(Context c) {
            try {
                InputStream in = c.getAssets().open("pfo_font.png");
                Bitmap b = BitmapFactory.decodeStream(in);
                in.close();
                return b == null ? null : new PixelFont(b);
            } catch (Throwable t) {
                Log.i("yoyo", "PFO: overlay font not found, using the system font: " + t);
                return null;
            }
        }

        static int glyph(char c) {
            if (c == '\u2212' || c == '\u2022') c = '-';
            int k = c - 32;
            return k >= 0 && k < 95 ? k : '?' - 32;
        }

        float width(String t, float px) { return t.length() * cw * px; }

        // centred on (cx, cy); px = screen pixels per font pixel
        void draw(Canvas cv, String t, float cx, float cy, float px, int color, int alpha) {
            if ((color | 0xFF000000) != lastColor) { lastColor = color | 0xFF000000; paint.setColorFilter(new LightingColorFilter(color & 0xFFFFFF, 0)); }
            paint.setAlpha(alpha);
            float x = Math.round(cx - width(t, px) / 2f), y = Math.round(cy - ch * px / 2f);
            for (int i = 0; i < t.length(); i++) {
                int k = glyph(t.charAt(i));
                src.set(k * cw, 0, k * cw + cw, ch);
                dst.set(x, y, x + cw * px, y + ch * px);
                cv.drawBitmap(strip, src, dst, paint);
                x += cw * px;
            }
        }
    }

    // One of the game's icon sprites, packed into a strip at build time: sKeyIcons (frame = vk code,
    // the boxed key names the game shows in its prompts) and sVirtualInputs (arrows, I, II, SYS).
    static final class IconStrip {
        final Bitmap strip;
        final int cw, ch, count;
        final Paint paint = new Paint();
        final Rect src = new Rect();
        final RectF dst = new RectF();

        IconStrip(Bitmap b, int cellW) { strip = b; cw = cellW; ch = b.getHeight(); count = b.getWidth() / cellW; paint.setFilterBitmap(false); }

        static IconStrip load(Context c, String asset, int cellW) {
            try {
                InputStream in = c.getAssets().open(asset);
                Bitmap b = BitmapFactory.decodeStream(in);
                in.close();
                return b == null ? null : new IconStrip(b, cellW);
            } catch (Throwable t) {
                return null;
            }
        }

        boolean has(int frame) { return frame >= 0 && frame < count; }

        // The lettering inside a keycap icon, without the cap: dark pixels enclosed by the cap's
        // light border in their row and column (the drop shadow isn't). Trimmed to its bounds.
        final java.util.HashMap<Integer, boolean[][]> glyphs = new java.util.HashMap<>();
        boolean[][] glyph(int frame) {
            if (!has(frame)) return null;
            boolean[][] g = glyphs.get(frame);
            if (g != null) return g;
            int x0 = frame * cw;
            boolean[][] light = new boolean[ch][cw], dark = new boolean[ch][cw];
            for (int y = 0; y < ch; y++)
                for (int x = 0; x < cw; x++) {
                    int c = strip.getPixel(x0 + x, y);
                    if ((c >>> 24) < 128) continue;
                    int sum = ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255);
                    if (sum > 384) light[y][x] = true; else dark[y][x] = true;
                }
            int minX = cw, minY = ch, maxX = -1, maxY = -1;
            boolean[][] ink = new boolean[ch][cw];
            for (int y = 0; y < ch; y++)
                for (int x = 0; x < cw; x++) {
                    if (!dark[y][x]) continue;
                    boolean l = false, r = false, u = false, d = false;
                    for (int k = 0; k < x; k++) l |= light[y][k];
                    for (int k = x + 1; k < cw; k++) r |= light[y][k];
                    for (int k = 0; k < y; k++) u |= light[k][x];
                    for (int k = y + 1; k < ch; k++) d |= light[k][x];
                    if (l && r && u && d) { ink[y][x] = true; minX = Math.min(minX, x); maxX = Math.max(maxX, x); minY = Math.min(minY, y); maxY = Math.max(maxY, y); }
                }
            if (maxX < 0) g = new boolean[0][0];
            else {
                g = new boolean[maxY - minY + 1][maxX - minX + 1];
                for (int y = minY; y <= maxY; y++) for (int x = minX; x <= maxX; x++) g[y - minY][x - minX] = ink[y][x];
            }
            glyphs.put(frame, g);
            return g;
        }

        // centred on (x, y), px = screen pixels per icon pixel
        void draw(Canvas cv, int frame, float x, float y, float px, int alpha) {
            if (!has(frame)) return;
            paint.setAlpha(alpha);
            float l = Math.round(x - cw * px / 2f), t = Math.round(y - ch * px / 2f);
            src.set(frame * cw, 0, frame * cw + cw, ch);
            dst.set(l, t, l + cw * px, t + ch * px);
            cv.drawBitmap(strip, src, dst, paint);
        }
    }

    // GameMaker vk code -> Android key code (keys the game lets you bind)
    static int vkToAndroid(int vk) {
        if (vk >= 65 && vk <= 90) return KeyEvent.KEYCODE_A + (vk - 65);
        if (vk >= 48 && vk <= 57) return KeyEvent.KEYCODE_0 + (vk - 48);
        if (vk >= 96 && vk <= 105) return KeyEvent.KEYCODE_NUMPAD_0 + (vk - 96);
        if (vk >= 112 && vk <= 123) return KeyEvent.KEYCODE_F1 + (vk - 112);
        switch (vk) {
            case 8: return KeyEvent.KEYCODE_DEL;
            case 9: return KeyEvent.KEYCODE_TAB;
            case 13: return KeyEvent.KEYCODE_ENTER;
            case 16: case 160: return KeyEvent.KEYCODE_SHIFT_LEFT;
            case 161: return KeyEvent.KEYCODE_SHIFT_RIGHT;
            case 17: case 162: return KeyEvent.KEYCODE_CTRL_LEFT;
            case 163: return KeyEvent.KEYCODE_CTRL_RIGHT;
            case 18: case 164: return KeyEvent.KEYCODE_ALT_LEFT;
            case 165: return KeyEvent.KEYCODE_ALT_RIGHT;
            case 20: return KeyEvent.KEYCODE_CAPS_LOCK;
            case 27: return KeyEvent.KEYCODE_ESCAPE;
            case 32: return KeyEvent.KEYCODE_SPACE;
            case 33: return KeyEvent.KEYCODE_PAGE_UP;
            case 34: return KeyEvent.KEYCODE_PAGE_DOWN;
            case 35: return KeyEvent.KEYCODE_MOVE_END;
            case 36: return KeyEvent.KEYCODE_MOVE_HOME;
            case 37: return KeyEvent.KEYCODE_DPAD_LEFT;
            case 38: return KeyEvent.KEYCODE_DPAD_UP;
            case 39: return KeyEvent.KEYCODE_DPAD_RIGHT;
            case 40: return KeyEvent.KEYCODE_DPAD_DOWN;
            case 45: return KeyEvent.KEYCODE_INSERT;
            case 46: return KeyEvent.KEYCODE_FORWARD_DEL;
            case 106: return KeyEvent.KEYCODE_NUMPAD_MULTIPLY;
            case 107: return KeyEvent.KEYCODE_NUMPAD_ADD;
            case 109: return KeyEvent.KEYCODE_NUMPAD_SUBTRACT;
            case 110: return KeyEvent.KEYCODE_NUMPAD_DOT;
            case 111: return KeyEvent.KEYCODE_NUMPAD_DIVIDE;
            case 186: return KeyEvent.KEYCODE_SEMICOLON;
            case 187: return KeyEvent.KEYCODE_EQUALS;
            case 188: return KeyEvent.KEYCODE_COMMA;
            case 189: return KeyEvent.KEYCODE_MINUS;
            case 190: return KeyEvent.KEYCODE_PERIOD;
            case 191: return KeyEvent.KEYCODE_SLASH;
            case 192: return KeyEvent.KEYCODE_GRAVE;
            case 219: return KeyEvent.KEYCODE_LEFT_BRACKET;
            case 220: return KeyEvent.KEYCODE_BACKSLASH;
            case 221: return KeyEvent.KEYCODE_RIGHT_BRACKET;
            case 222: return KeyEvent.KEYCODE_APOSTROPHE;
        }
        return 0;
    }

    // A real gamepad, as the runner sees it.
    static final class Pad {
        int slot, player = -1, number;
        String name = "";
        long litUntil;
    }

    static final class Control {
        static final int DPAD = 0, ROUND = 1, PILL = 2, GEAR = 3, PLAYER = 4;
        final String id, label;
        final int type;
        final int[] keys; // actions (TouchOverlay.ACT_*); DPAD: up, down, left, right
        final float defX, defY, defSize;
        float x, y, size;   // centre as fraction of width/height; size as fraction of the short side
        boolean hidden;
        final boolean[] down = new boolean[4];
        boolean pressed;

        Control(String id, int type, String label, float x, float y, float size, int... keys) {
            this.id = id; this.type = type; this.label = label; this.keys = keys;
            defX = this.x = x; defY = this.y = y; defSize = this.size = size;
        }
    }

    static final class TouchOverlay extends View {
        // Actions the pad can send; each player maps them to its own keys (UFO 50's defaults, see
        // scrInitInput): P1 and P2 have keyboard layouts, P3 and P4 only gamepad slots 2 and 3.
        static final int ACT_UP = 0, ACT_DOWN = 1, ACT_LEFT = 2, ACT_RIGHT = 3, ACT_A = 4, ACT_B = 5, ACT_START = 6, ACT_BACK = 7;
        static final int[][] KEYBOARD = {
            { KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT,
              KeyEvent.KEYCODE_Z, KeyEvent.KEYCODE_X, KeyEvent.KEYCODE_D, KeyEvent.KEYCODE_ESCAPE },
            { KeyEvent.KEYCODE_NUMPAD_5, KeyEvent.KEYCODE_NUMPAD_2, KeyEvent.KEYCODE_NUMPAD_1, KeyEvent.KEYCODE_NUMPAD_3,
              KeyEvent.KEYCODE_FORWARD_DEL, KeyEvent.KEYCODE_MOVE_END, KeyEvent.KEYCODE_PAGE_UP, KeyEvent.KEYCODE_ESCAPE },
        };
        static final int[] GAMEPAD = { KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_DPAD_LEFT,
            KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_START, 0 };
        // all overlay colours come from UFO 50's palette (read from the game's textures, build/ufo_palette.png)
        static final int[] PLAYER_COLOR = { 0xFFE03C32, 0xFF006AB4, 0xFF63B31D, 0xFFE8EA4A };
        static final int VPAD_BASE = 0x7F5F0000;  // device ids of the virtual gamepads (slots 0..3)
        static TouchOverlay sInstance;

        int player = 0;              // whom the pad controls (0 = P1)
        // Online the game reads only this device's P1 controls, for whichever seat it was given, and
        // every other seat comes from the network. So while online the switch is locked: keys go out
        // as P1 and the chip shows the seat. -1 = offline, -2 = online without a seat.
        int onlineSeat = -1;
        int playerBeforeOnline = 0;
        // Hot seat: turn-based games report whose turn it is and the pad follows. A separate on/off
        // choice in the player drop-down, independent of the P1-P4 pick.
        boolean hotSeat = true, following = false;
        int followedTurn = -1, playerBeforeFollow = 0;
        long hsFlashUntil = 0;
        int seatCount = 0;   // players in the hot seat game (from the game), shown as a row of seats
        final List<RectF> seatRects = new ArrayList<>();
        // seats 5-8 (MultiMadnessMod): more colours from the palette
        static final int[] SEAT_COLOR = { 0xFFE03C32, 0xFF006AB4, 0xFF63B31D, 0xFFE8EA4A, 0xFFFE7000, 0xFF9600DC, 0xFF27BADB, 0xFFFE48DE };   // the HS block blinks for a couple of seconds when the turn changes
        // Players panel ("who's on what"): opened from the player chip, a pad badge, or the game's
        // pause menu > joypad config > pad pairing. Touch and each real pad get bound to a player.
        boolean panelOpen = false;
        final List<RectF> hitRects = new ArrayList<>();
        final List<Integer> hitCodes = new ArrayList<>();
        final RectF panelRect = new RectF();
        static final int HIT_HOTSEAT = 900, HIT_DONE = 901, HIT_NAME = 902, HIT_JOINCODE = 903, HIT_UPNP = 904;
        boolean upnp = true;      // prefs "upnp"
        int netStatus = 0;        // nativeNetStatus(): bits 0-3 connection, 16 lobby, 32 host, 64 session, 128 catching up, 8-15 delay

        // ---- one look for everything the overlay shows ----
        // colour = which player (never status); opacity = active (solid) or not (faded); the icon in a
        // chip = what drives that seat; the session pill after the chips = mode and what just happened;
        // motion = something just changed. Buttons: one primary action per screen, neutral for the
        // rest, destructive as an outline, player colours only where the choice is about a player.
        static final int BTN_PRIMARY = 0xFF008456, BTN_NEUTRAL = 0xFF202040, BTN_DANGER = 0xFFC43A3A, ATTN = 0xFFE06A10, NO_PLAYER = 0xFF6F6F6F;
        static final int ICON_TOUCH = 0, ICON_PAD = 1, ICON_NET = 2;
        String pillText = null;   // the pill says this until pillUntil, then settles
        long pillUntil = 0;
        int lastMode = 0, lastKind = -1, lastDelay = 0;
        final RectF pillRect = new RectF();
        float rowEnd = -1;        // where the chip row ends: the pill sits after it
        boolean chipsColumn = false;   // the player chips stacked top to bottom instead of a row (layout editor: CHIPS)
        // where the chip after r goes: right of it in a row, below it (left edges lined up) in a column
        float nextX(RectF r, float gap) { return chipsColumn ? r.left : r.right + gap; }
        float nextY(RectF r, float gap, float hh) { return chipsColumn ? r.bottom + gap + hh : r.centerY(); }
        final RectF chipRect = new RectF();   // the touch chip as drawn this frame
        int seatMyClient = -1;    // online: our client, each seat's client, the clients' names
        final int[] seatClient = { -1, -1, -1, -1, -1, -1, -1, -1 };
        String[] clientNames = new String[0];
        String playerName = "";   // chosen name for online play ("" = device default), prefs "name"
        final List<Pad> pads = new ArrayList<>();
        final java.util.HashMap<String, Integer> rememberedPlayer = new java.util.HashMap<>();
        final List<RectF> badgeRects = new ArrayList<>();
        PixelFont font;
        IconStrip keyIcons, vInputs;
        static final int VI_BUTTON1 = 4, VI_BUTTON2 = 5, VI_SYS = 6;   // sVirtualInputs frames
        // touch action -> index into the game's key map (up, down, left, right, fire1, fire2, start).
        // A (right) is the game's BUTTON II / fire2 (Z), B (left) is BUTTON I / fire1 (X).
        static final int[] KEY_SLOT = { 0, 1, 2, 3, 5, 4, 6, -1 };
        final int[][] gameKeys = new int[4][];
        final java.util.HashMap<Integer, Integer> downCode = new java.util.HashMap<>();
        int bannerColor = 0;
        // debug stand-in for a real pad (no gamepads in the emulator): files/testpad registers it,
        // files/testpress holds its A button down
        boolean testPadOn = false, testPressed = false;
        static final int TESTPAD_DEVICE = 0x7F5F0010;
        boolean vpadsRegistered = false;
        String banner = null;        // e.g. "P2'S TURN"
        long bannerUntil = 0;
        static final int TOOLBAR_DONE = 0, TOOLBAR_RESET = 1, TOOLBAR_SMALLER = 2, TOOLBAR_BIGGER = 3,
                TOOLBAR_FAINTER = 4, TOOLBAR_STRONGER = 5, TOOLBAR_HIDE = 6, TOOLBAR_AUTOHIDE = 7, TOOLBAR_DPAD = 8, TOOLBAR_CHIPS = 9;
        static final String[] TOOLBAR = { "DONE", "RESET", "SIZE −", "SIZE +", "OPACITY −", "OPACITY +", "HIDE", "AUTO-HIDE", "D-PAD", "CHIPS" };

        final List<Control> controls = new ArrayList<>();
        final Control gear;
        final SharedPreferences prefs;
        final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), line = new Paint(Paint.ANTI_ALIAS_FLAG), text = new Paint(Paint.ANTI_ALIAS_FLAG);
        final Set<Integer> keysDown = new HashSet<>();   // player * 16 + action
        final RectF tmp = new RectF();
        final RectF[] toolbarRects = new RectF[TOOLBAR.length];
        final Handler handler = new Handler(Looper.getMainLooper());

        float opacity = 0.55f;
        boolean autoHide = false, gamepadConnected = false, editing = false;
        boolean dpad4 = false;   // 4-way d-pad: only the strongest direction (else 8-way, diagonals too)
        Control selected;
        // edit gesture state
        int dragPointer = -1; boolean dragResize; float dragDX, dragDY;
        float pinchStartDist, pinchStartSize;

        TouchOverlay(Context c) {
            super(c);
            setFocusable(false);
            prefs = c.getSharedPreferences("pfo_touch", Context.MODE_PRIVATE);
            controls.add(new Control("dpad", Control.DPAD, "", 0.14f, 0.68f, 0.40f, ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT));
            controls.add(new Control("b", Control.ROUND, "BUTTON I", 0.80f, 0.78f, 0.17f, ACT_B));
            controls.add(new Control("a", Control.ROUND, "BUTTON II", 0.91f, 0.62f, 0.17f, ACT_A));
            controls.add(new Control("start", Control.PILL, "START", 0.56f, 0.92f, 0.13f, ACT_START));
            controls.add(new Control("back", Control.PILL, "BACK", 0.44f, 0.92f, 0.13f, ACT_BACK));
            controls.add(new Control("player", Control.PLAYER, "P1", 0.055f, 0.075f, 0.09f));
            sInstance = this;
            gear = new Control("gear", Control.GEAR, "", 0.965f, 0.065f, 0.075f);
            font = PixelFont.load(c);
            keyIcons = IconStrip.load(c, "pfo_keyicons.png", 24);
            vInputs = IconStrip.load(c, "pfo_vinputs.png", 16);
            gameIcons = IconStrip.load(c, "pfo_gameicons.png", 24);   // game N's picture: frame (N-1)*2
            statesBtn = new Control("states", Control.GEAR, "", 0.895f, 0.065f, 0.075f);
            load();
            text.setTextAlign(Paint.Align.CENTER);
            text.setFakeBoldText(true);
            line.setStyle(Paint.Style.STROKE);
            pollGamepads();
        }

        // ---- persistence ----
        void load() {
            opacity = prefs.getFloat("opacity", opacity);
            autoHide = prefs.getBoolean("autohide", autoHide);
            hotSeat = prefs.getBoolean("hotseat", hotSeat);
            dpad4 = prefs.getBoolean("dpad4", dpad4);
            chipsColumn = prefs.getBoolean("chips_column", false);
            playerName = prefs.getString("name", "");
            upnp = prefs.getBoolean("upnp", true);
            autoSavePause = prefs.getBoolean("autosave_pause", false);
            try { nativeSetUpnp(upnp); } catch (Throwable t) { }
            try { nativeSetPlayerName(playerName); } catch (Throwable t) { }
            for (Control c : controls) {
                c.x = prefs.getFloat(c.id + ".x", c.defX);
                c.y = prefs.getFloat(c.id + ".y", c.defY);
                c.size = prefs.getFloat(c.id + ".size", c.defSize);
                c.hidden = prefs.getBoolean(c.id + ".hidden", false);
            }
        }

        void save() {
            SharedPreferences.Editor e = prefs.edit();
            e.putFloat("opacity", opacity).putBoolean("autohide", autoHide).putBoolean("hotseat", hotSeat).putBoolean("dpad4", dpad4).putBoolean("chips_column", chipsColumn).putString("name", playerName).putBoolean("upnp", upnp);
            for (Control c : controls) {
                e.putFloat(c.id + ".x", c.x).putFloat(c.id + ".y", c.y).putFloat(c.id + ".size", c.size).putBoolean(c.id + ".hidden", c.hidden);
            }
            e.apply();
        }

        void reset() {
            for (Control c : controls) { c.x = c.defX; c.y = c.defY; c.size = c.defSize; c.hidden = false; }
            opacity = 0.55f;
            autoHide = false;
            dpad4 = false;
        }

        // Hide the pad while a physical gamepad is connected (if enabled).
        void pollGamepads() {
            boolean found = false;
            for (int id : InputDevice.getDeviceIds()) {
                InputDevice d = InputDevice.getDevice(id);
                if (d == null || d.isVirtual()) continue;
                int s = d.getSources();
                if ((s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD || (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) found = true;
            }
            if (found != gamepadConnected) { gamepadConnected = found; invalidate(); }
            handler.postDelayed(new Runnable() { @Override public void run() { pollGamepads(); } }, 2000);
        }

        boolean padVisible() { return editing || !(autoHide && gamepadConnected); }

        // ---- geometry ----
        float unit() { return Math.min(getWidth(), getHeight()); }
        float cx(Control c) { return c.x * getWidth(); }
        float cy(Control c) { return c.y * getHeight(); }
        float radius(Control c) { return c.size * unit() * 0.5f; }
        float halfW(Control c) { return c.type == Control.PLAYER ? radius(c) * 1.5f : radius(c); }
        float halfH(Control c) { return c.type == Control.PILL ? radius(c) * 0.42f : c.type == Control.PLAYER ? radius(c) * 0.75f : radius(c); }

        boolean hit(Control c, float px, float py, float slop) {
            float dx = Math.abs(px - cx(c)), dy = Math.abs(py - cy(c));
            if (c.type == Control.ROUND || c.type == Control.GEAR) return Math.hypot(dx, dy) <= radius(c) * slop;
            return dx <= halfW(c) * slop && dy <= halfH(c) * slop;
        }

        float[] handlePos(Control c) { return new float[] { cx(c) + halfW(c), cy(c) + halfH(c) }; }

        // ---- input while playing ----
        @Override public boolean onTouchEvent(MotionEvent e) {
            if (editing) { onEditTouch(e); return true; }
            int action = e.getActionMasked();
            if (statesOpen && stateGesture && action == MotionEvent.ACTION_MOVE) { stateMove(e.getY(0)); return true; }
            if (statesOpen && stateGesture && action == MotionEvent.ACTION_UP) { stateGesture = false; stateTap(e.getX(0), e.getY(0)); return true; }
            if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
                int i = e.getActionIndex();
                if (restoring()) return true;
                if (voteShown()) { voteTap(e.getX(i), e.getY(i)); return true; }
                if (rejoinShown()) { rejoinTap(e.getX(i), e.getY(i)); return true; }
                if (statesOpen) { stateDown(e.getX(i), e.getY(i)); return true; }
                if (naming) return nameTap(e.getX(i), e.getY(i));
                if (inviting) return inviteTap(e.getX(i), e.getY(i));
                if (panelOpen) { panelTap(e.getX(i), e.getY(i)); return true; }
                Control chip = control("player");
                if (!pillRect.isEmpty() && pillRect.contains(e.getX(i), e.getY(i))) {   // the pill: say it all again
                    String d = pillDetail();
                    if (d != null) notePill(d, 4000);
                    performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
                    return true;
                }
                boolean onBadge = false;
                for (RectF r : badgeRects) if (r.contains(e.getX(i), e.getY(i))) onBadge = true;
                if (onBadge || (chip != null && !chip.hidden && hit(chip, e.getX(i), e.getY(i), 1.3f))) {
                    if (onlineSeat != -1 && !soloSession()) { if (showOnlineSeats()) showNames(); else setPlayer(player, null); }   // online: who's who
                    else openPanel(-1);
                    performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
                    return true;
                }
                if (!panelOpen && !naming && !inviting && hit(statesBtn, e.getX(i), e.getY(i), 1.2f)) {
                    openStates();
                    performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
                    return true;
                }
                if (hit(gear, e.getX(i), e.getY(i), 1.3f)) {
                    releaseAll();
                    editing = true;
                    selected = null;
                    performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
                    invalidate();
                    return true;
                }
            }
            if (panelOpen || naming || inviting || statesOpen || restoring() || voteShown() || rejoinShown()) return true;
            updatePressed(e, action);
            return true;
        }

        void openPanel(int forPlayer) {
            releaseAll();
            panelOpen = true;
            if (forPlayer >= 0) showBanner(pads.isEmpty() ? "NO GAMEPADS CONNECTED" : "PICK A PAD FOR P" + (forPlayer + 1), 2200);
            invalidate();
        }

        void panelTap(float x, float y) {
            if (!panelRect.contains(x, y)) { panelOpen = false; invalidate(); return; }
            for (int k = 0; k < hitRects.size(); k++) {
                if (!hitRects.get(k).contains(x, y)) continue;
                int code = hitCodes.get(k);
                if (code == HIT_DONE) panelOpen = false;
                else if (code == HIT_NAME) {
                    if (onlineSeat != -1 && !soloSession()) showBanner("LEAVE ONLINE PLAY TO CHANGE YOUR NAME", 1800);
                    else askEntry(ENTRY_NAME);
                }
                else if (code == HIT_UPNP) {
                    upnp = !upnp;
                    save();
                    try { nativeSetUpnp(upnp); } catch (Throwable t) { }
                    showBanner(upnp ? "UPNP ON: HOSTS OPEN THEIR ROUTER PORT" : "UPNP OFF", 1800);
                }
                else if (code == HIT_JOINCODE) {
                    if (onlineSeat != -1) showBanner(soloSession() ? "NOT DURING SOLO PLAY" : "YOU'RE ALREADY IN AN ONLINE GAME", 1800);
                    else askEntry(ENTRY_CODE);
                }
                else if (code == HIT_HOTSEAT) {
                    hotSeat = !hotSeat;
                    save();
                    if (!hotSeat) { following = false; followedTurn = -1; }
                } else {
                    int row = code / 8, col = code % 8;   // row 0 = touch, row k = pads[k-1]; col 0 = off, 1-4 = P1-P4
                    if (row == 0) { if (col > 0) setPlayer(col - 1, null); }
                    else if (row - 1 < pads.size()) {
                        if (soloSession() && col >= 2) showBanner("SOLO PLAY IS ONE PLAYER", 1500);   // a session of one sends P1 only
                        else movePad(pads.get(row - 1), col - 1);
                    }
                }
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
                break;
            }
            invalidate();
        }

        String currentName() {
            String n = playerName;
            if (n.isEmpty()) { try { n = nativeGetPlayerName(); } catch (Throwable t) { } }
            return n == null || n.isEmpty() ? "DEFAULT" : n;
        }

        // Name for online play, in a box drawn like the game's own menus. The phone's keyboard types
        // into a hidden text field (so keys never reach the game); the box shows what's typed.
        // Letters, digits, spaces and a little punctuation (what the game's font has), up to 16.
        boolean naming = false;
        static final int ENTRY_NAME = 0, ENTRY_CODE = 1;
        int entryMode = ENTRY_NAME;
        String nameDraft = "";
        android.widget.EditText nameField;
        final RectF nameBox = new RectF(), nameInput = new RectF(), nameSave = new RectF(), nameDefault = new RectF(), nameCancel = new RectF();

        void askName() { askEntry(ENTRY_NAME); }

        void askEntry(int mode) {
            entryMode = mode;
            if (nameField == null) {
                nameField = new android.widget.EditText(getContext());
                nameField.setSingleLine(true);
                nameField.setAlpha(0f);
                nameField.setImeOptions(android.view.inputmethod.EditorInfo.IME_ACTION_DONE | android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI);
                nameField.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);

                nameField.addTextChangedListener(new android.text.TextWatcher() {
                    @Override public void beforeTextChanged(CharSequence c, int a, int b, int d) {}
                    @Override public void onTextChanged(CharSequence c, int a, int b, int d) {}
                    @Override public void afterTextChanged(android.text.Editable e) { nameDraft = e.toString(); invalidate(); }
                });
                nameField.setOnEditorActionListener(new android.widget.TextView.OnEditorActionListener() {
                    @Override public boolean onEditorAction(android.widget.TextView v, int id, KeyEvent ev) { finishName(true, false); return true; }
                });
                ((Activity) getContext()).addContentView(nameField, new ViewGroup.LayoutParams(1, 1));
            }
            // names: letters, digits, spaces, a little punctuation, up to 16; codes: 6 letters/digits
            final boolean code = mode == ENTRY_CODE;
            nameField.setFilters(new android.text.InputFilter[] {
                new android.text.InputFilter.LengthFilter(code ? 6 : 16),
                new android.text.InputFilter() {
                    @Override public CharSequence filter(CharSequence src, int start, int end, android.text.Spanned dst, int ds, int de) {
                        StringBuilder b = new StringBuilder();
                        for (int k = start; k < end; k++) {
                            char ch = Character.toUpperCase(src.charAt(k));
                            if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || (!code && " -_.!?'&".indexOf(ch) >= 0)) b.append(ch);
                        }
                        return b.toString().equals(src.subSequence(start, end).toString()) ? null : b.toString();
                    }
                },
            });
            releaseAll();
            naming = true;
            String start = code ? "" : playerName;
            nameDraft = start;
            nameField.setText(start);
            nameField.setSelection(nameField.getText().length());
            showNameKeyboard();
            invalidate();
            blinkName();
        }

        void showNameKeyboard() {
            nameField.setFocusableInTouchMode(true);
            nameField.requestFocus();
            android.view.inputmethod.InputMethodManager imm = (android.view.inputmethod.InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) imm.showSoftInput(nameField, android.view.inputmethod.InputMethodManager.SHOW_IMPLICIT);
        }

        void finishName(boolean save, boolean useDefault) {
            if (!naming || nameField == null) return;
            if (entryMode == ENTRY_CODE && useDefault) {   // PASTE a copied code (or a shared message)
                String c = clipboardCode();
                if (c.isEmpty()) showBanner("NO CODE ON THE CLIPBOARD", 1500);
                else { nameField.setText(c); nameField.setSelection(c.length()); }
                showNameKeyboard();
                return;
            }
            naming = false;
            android.view.inputmethod.InputMethodManager imm = (android.view.inputmethod.InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) imm.hideSoftInputFromWindow(nameField.getWindowToken(), 0);
            // Take the hidden field out entirely (a focusable view left behind makes Android use the
            // arrow keys to move focus instead of passing them to the game), then refocus the game.
            ViewGroup parent = (ViewGroup) nameField.getParent();
            if (parent != null) parent.removeView(nameField);
            nameField = null;
            View content = ((Activity) getContext()).findViewById(android.R.id.content);
            if (content instanceof ViewGroup && ((ViewGroup) content).getChildCount() > 0) ((ViewGroup) content).getChildAt(0).requestFocus();
            if (entryMode == ENTRY_CODE) {
                String c = nameDraft.trim();
                if (save && c.length() > 0) {
                    try { nativeAddJoinCode(c); } catch (Throwable t) { }
                    showBanner("CODE " + c + " ADDED - THE LOBBY WILL SHOW IN THE LIST", 3000);
                }
            }
            else if (useDefault) setName("");
            else if (save) setName(nameDraft.trim());
            invalidate();
        }

        void blinkName() {
            if (!naming) return;
            invalidate();
            postDelayed(new Runnable() { @Override public void run() { blinkName(); } }, 400);
        }

        boolean nameTap(float x, float y) {
            if (nameSave.contains(x, y)) finishName(true, false);
            else if (nameDefault.contains(x, y)) finishName(false, true);
            else if (nameCancel.contains(x, y) || !nameBox.contains(x, y)) finishName(false, false);
            else if (nameInput.contains(x, y)) showNameKeyboard();
            performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
            return true;
        }

        // INVITE FRIENDS (in the game's lobby): show our private lobby's code, and a SHARE button
        // that hands it to any app (messages, chat...). Public lobbies need no code.
        boolean inviting = false;
        String inviteCode = "";
        final RectF inviteBox = new RectF(), inviteShare = new RectF(), inviteCopy = new RectF(), inviteClose = new RectF();

        void openInvite() {
            String c = "";
            try { c = nativeLobbyCode(); } catch (Throwable t) { }
            inviteCode = c == null ? "" : c;
            releaseAll();
            panelOpen = false;
            inviting = true;
            invalidate();
        }

        boolean inviteTap(float x, float y) {
            if (inviteShare.contains(x, y) && !inviteCode.isEmpty()) shareCode();
            else if (inviteCopy.contains(x, y) && !inviteCode.isEmpty()) copyCode();
            else if (inviteClose.contains(x, y) || !inviteBox.contains(x, y)) inviting = false;
            performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
            invalidate();
            return true;
        }

        void copyCode() {
            try {
                android.content.ClipboardManager cb = (android.content.ClipboardManager) getContext().getSystemService(Context.CLIPBOARD_SERVICE);
                cb.setPrimaryClip(android.content.ClipData.newPlainText("UFO 50 invite code", inviteCode));
                showBanner("CODE " + inviteCode + " COPIED", 1500);
            } catch (Throwable t) {
                showBanner("COULDN'T COPY", 1500);
            }
        }

        // the clipboard's text cut down to what a code can hold ("" if nothing usable)
        String clipboardCode() {
            try {
                android.content.ClipboardManager cb = (android.content.ClipboardManager) getContext().getSystemService(Context.CLIPBOARD_SERVICE);
                if (cb == null || !cb.hasPrimaryClip() || cb.getPrimaryClip().getItemCount() == 0) return "";
                CharSequence t = cb.getPrimaryClip().getItemAt(0).coerceToText(getContext());
                String txt = t == null ? "" : t.toString().toUpperCase();
                // a shared message ("...Invite code: SVJT3U...") or just the code
                int at = txt.indexOf("CODE:");
                if (at >= 0) txt = txt.substring(at + 5);
                StringBuilder b = new StringBuilder();
                for (int k = 0; k < txt.length() && b.length() < 6; k++) {
                    char ch = txt.charAt(k);
                    if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) b.append(ch);
                    else if (b.length() > 0) break;
                }
                return b.toString();
            } catch (Throwable t) {
                return "";
            }
        }

        void shareCode() {
            try {
                android.content.Intent send = new android.content.Intent(android.content.Intent.ACTION_SEND);
                send.setType("text/plain");
                send.putExtra(android.content.Intent.EXTRA_TEXT, "Join my UFO 50 lobby! Invite code: " + inviteCode
                        + "\n(Play Online > JOIN BY CODE, then pick my lobby from the list.)");
                getContext().startActivity(android.content.Intent.createChooser(send, "Share invite code"));
            } catch (Throwable t) {
                showBanner("COULDN'T OPEN SHARING", 1500);
            }
        }

        void drawInviteBox(Canvas cv, float u) {
            float px = Math.max(2, Math.round(u / 216f));
            solid(fill, 0xFF000000, 0.6f);
            cv.drawRect(0, 0, getWidth(), getHeight(), fill);
            float w = Math.min(getWidth() * 0.9f, px * 260), h = px * 100;
            float l = Math.round((getWidth() - w) / 2f), t = Math.round((getHeight() - h) / 2f);
            inviteBox.set(l, t, l + w, t + h);
            ufoBox(cv, inviteBox, px, 0xFFFFFFFF);
            float th = px * 8;
            boolean has = !inviteCode.isEmpty();
            txt(cv, has ? "PRIVATE LOBBY INVITE CODE" : "THIS LOBBY IS PUBLIC", inviteBox.centerX(), t + px * 14, th, 0xFFFFFFFF, 255);
            if (has) {
                txt(cv, inviteCode, inviteBox.centerX(), t + px * 38, th * 3, 0xFFE8EA4A, 255);
                fitTxt(cv, "FRIENDS PICK JOIN BY CODE IN THE LOBBY LIST", inviteBox.centerX(), t + px * 60, th, w - px * 12, 0xFF6F6F6F);
            } else {
                txt(cv, "ANYONE CAN JOIN IT FROM", inviteBox.centerX(), t + px * 34, th, 0xFFC8C8C8, 255);
                txt(cv, "THE LOBBY LIST - NO CODE NEEDED", inviteBox.centerX(), t + px * 46, th, 0xFFC8C8C8, 255);
            }
            float bh = px * 20, by = t + h - px * 12 - bh, bw = (w - px * 16 * 2 - px * 8 * 2) / 3f;
            inviteShare.set(l + px * 16, by, l + px * 16 + bw, by + bh);
            inviteCopy.set(inviteShare.right + px * 8, by, inviteShare.right + px * 8 + bw, by + bh);
            inviteClose.set(inviteCopy.right + px * 8, by, inviteCopy.right + px * 8 + bw, by + bh);
            if (has) {
                ufoBox(cv, inviteShare, px, 0xFFFFFFFF); txt(cv, "SHARE", inviteShare.centerX(), inviteShare.centerY(), th, 0xFFE8EA4A, 255);
                ufoBox(cv, inviteCopy, px, 0xFFFFFFFF); txt(cv, "COPY", inviteCopy.centerX(), inviteCopy.centerY(), th, 0xFFFFFFFF, 255);
            } else { inviteShare.setEmpty(); inviteCopy.setEmpty(); }
            if (!has) inviteClose.set(inviteBox.centerX() - bw / 2f, by, inviteBox.centerX() + bw / 2f, by + bh);
            ufoBox(cv, inviteClose, px, 0xFFFFFFFF);
            txt(cv, "CLOSE", inviteClose.centerX(), inviteClose.centerY(), th, 0xFFFFFFFF, 255);
        }

        // text shrunk (in whole pixels) until it fits maxW
        void fitTxt(Canvas cv, String t, float x, float y, float h, float maxW, int color) {
            while (font_w(t, h) > maxW && h > 4) h -= Math.max(1f, h / 8f);
            txt(cv, t, x, y, h, color, 255);
        }

        // white-bordered black box, like the game's menus (border = 2 of its pixels)
        void ufoBox(Canvas cv, RectF r, float px, int border) {
            solid(fill, 0xFF000000, 1f);
            cv.drawRect(r, fill);
            solid(fill, border, 1f);
            cv.drawRect(r.left, r.top, r.right, r.top + px * 2, fill);
            cv.drawRect(r.left, r.bottom - px * 2, r.right, r.bottom, fill);
            cv.drawRect(r.left, r.top, r.left + px * 2, r.bottom, fill);
            cv.drawRect(r.right - px * 2, r.top, r.right, r.bottom, fill);
        }

        void drawNameBox(Canvas cv, float u) {
            float px = Math.max(2, Math.round(u / 216f));   // one game pixel at the current screen size
            solid(fill, 0xFF000000, 0.6f);
            cv.drawRect(0, 0, getWidth(), getHeight(), fill);
            float w = Math.min(getWidth() * 0.9f, px * 260), h = px * 92;
            float l = Math.round((getWidth() - w) / 2f), t = Math.round(getHeight() * 0.05f);
            nameBox.set(l, t, l + w, t + h);
            ufoBox(cv, nameBox, px, 0xFFFFFFFF);
            float th = px * 8;
            boolean code = entryMode == ENTRY_CODE;
            txt(cv, code ? "ENTER AN INVITE CODE" : "YOUR NAME FOR ONLINE PLAY", nameBox.centerX(), t + px * 14, th, 0xFFFFFFFF, 255);
            fitTxt(cv, code ? "THE HOST'S SHARE CODE BUTTON SHOWS IT" : "SHOWN IN LOBBIES", nameBox.centerX(), t + px * 25, th, w - px * 12, 0xFF6F6F6F);
            // the name being typed, yellow like the game's selected text, with a blinking cursor
            nameInput.set(l + px * 16, t + px * 33, l + w - px * 16, t + px * 53);
            ufoBox(cv, nameInput, px, 0xFFFFFFFF);
            boolean on = (android.os.SystemClock.uptimeMillis() / 400) % 2 == 0;
            String shown = nameDraft.isEmpty() ? "" : nameDraft;
            float tw = font_w(shown + "_", th * 1.5f);
            float tx = nameInput.centerX() - tw / 2f + font_w(shown, th * 1.5f) / 2f;
            if (!shown.isEmpty()) txt(cv, shown, tx, nameInput.centerY(), th * 1.5f, 0xFFE8EA4A, 255);
            else txt(cv, code ? "------" : "DEFAULT", nameInput.centerX(), nameInput.centerY(), th * 1.5f, 0xFF6F6F6F, 255);
            if (on && !shown.isEmpty()) txt(cv, "_", tx + font_w(shown, th * 1.5f) / 2f + font_w("_", th * 1.5f) / 2f, nameInput.centerY(), th * 1.5f, 0xFFE8EA4A, 255);
            // buttons, like the game's lobby boxes
            float bw = (w - px * 16 * 2 - px * 8 * 2) / 3f, by = t + px * 62, bh = px * 20;
            nameSave.set(l + px * 16, by, l + px * 16 + bw, by + bh);
            nameDefault.set(nameSave.right + px * 8, by, nameSave.right + px * 8 + bw, by + bh);
            nameCancel.set(nameDefault.right + px * 8, by, nameDefault.right + px * 8 + bw, by + bh);
            ufoBox(cv, nameSave, px, 0xFFFFFFFF);
            ufoBox(cv, nameDefault, px, 0xFFFFFFFF);
            ufoBox(cv, nameCancel, px, 0xFFFFFFFF);
            txt(cv, code ? "JOIN" : "SAVE", nameSave.centerX(), nameSave.centerY(), th, 0xFFE8EA4A, 255);
            txt(cv, code ? "PASTE" : "DEFAULT", nameDefault.centerX(), nameDefault.centerY(), th, 0xFFFFFFFF, 255);
            txt(cv, "CANCEL", nameCancel.centerX(), nameCancel.centerY(), th, 0xFFFFFFFF, 255);
        }

        void setName(String n) {
            playerName = n.toUpperCase();
            save();
            try { nativeSetPlayerName(playerName); } catch (Throwable t) { }
            showBanner("NAME: " + currentName().toUpperCase(), 1500);
            invalidate();
        }

        // Bind a pad to a player (-1 = off). A pad already on that player swaps to this pad's old one.
        void movePad(Pad pad, int p) {
            if (p >= 0) for (Pad o : pads) if (o != pad && o.player == p) { o.player = pad.player; rememberedPlayer.put(o.name, o.player); }
            pad.player = p;
            rememberedPlayer.put(pad.name, p);
            pushBindings();
            showBanner("PAD " + pad.number + (p >= 0 ? " -> P" + (p + 1) : " OFF"), 1200, p >= 0 ? PLAYER_COLOR[p] : 0xFF6F6F6F);
        }

        Pad padOn(int p) {
            for (Pad o : pads) if (o.player == p) return o;
            return null;
        }

        // Which slot each player reads: its real pad, else the touch pad when touch is on P3/P4
        // (P1/P2 touch uses keyboard keys), else none.
        final int[] lastSlots = new int[8];
        void pushBindings() {
            int[] slots = new int[8];
            for (int q = 0; q < 8; q++) {
                Pad o = padOn(q);
                slots[q] = o != null ? o.slot : (q == player && q >= 2 ? -3 : -1);
            }
            try { nativeSetPlayerSlots(slots); System.arraycopy(slots, 0, lastSlots, 0, 8); } catch (Throwable t) { }
        }

        // Read the runner's real pads; new ones get the lowest player without a pad.
        void pollPads() {
            String st = null;
            try { st = nativePadState(); } catch (Throwable t) { }
            if (st == null) return;
            List<Pad> now = new ArrayList<>();
            long t = android.os.SystemClock.uptimeMillis();
            for (String ln : st.split("\n")) {
                String[] f = ln.split("\t", 3);
                if (f.length < 3) continue;
                int slot = Integer.parseInt(f[0]);
                long buttons = Long.parseLong(f[1]);
                Pad pad = null;
                for (Pad o : pads) if (o.slot == slot && o.name.equals(f[2])) pad = o;
                if (pad == null) {
                    pad = new Pad();
                    pad.slot = slot; pad.name = f[2];
                    Integer r = rememberedPlayer.get(pad.name);
                    if (r != null && padOnIn(now, r) == null) pad.player = r;
                    else {
                        pad.player = -1;
                        for (int q = 0; q < 4 && pad.player < 0; q++) if (padOnIn(now, q) == null && padOnIn(pads, q) == null) pad.player = q;
                    }
                    now.add(pad);
                    pad.number = now.size();
                    pad.litUntil = t + 3000;   // its chip shows "PAD n" for a while
                } else now.add(pad);
                if (buttons != 0) pad.litUntil = t + 250;
            }
            for (Pad o : pads) if (!now.contains(o)) { rememberedPlayer.put(o.name, o.player); showBanner("PAD " + o.number + " DISCONNECTED", 1800); }
            boolean changed = now.size() != pads.size();
            for (int k = 0; k < now.size(); k++) { if (now.get(k).number != k + 1) changed = true; now.get(k).number = k + 1; }
            pads.clear();
            pads.addAll(now);
            pushBindings();
            if (changed || panelOpen) invalidate();
        }

        static Pad padOnIn(List<Pad> list, int p) {
            for (Pad o : list) if (o.player == p) return o;
            return null;
        }

        void pollTestPad() {
            java.io.File dir = getContext().getExternalFilesDir(null);
            if (dir == null) return;
            boolean want = new java.io.File(dir, "testpad").exists();
            if (want && !testPadOn) {
                testPadOn = true;
                RunnerJNILib.onGPDeviceAdded(TESTPAD_DEVICE, "Test Pad (debug)", "pfo-test-pad", 0, 0, 0, 1, (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 6));
                try { RunnerJNILib.onGamepadChange(); } catch (Throwable t) { }
            }
            if (!testPadOn) return;
            boolean press = new java.io.File(dir, "testpress").exists();
            if (press != testPressed) {
                testPressed = press;
                if (press) RunnerJNILib.onGPKeyDown(TESTPAD_DEVICE, KeyEvent.KEYCODE_BUTTON_A);
                else RunnerJNILib.onGPKeyUp(TESTPAD_DEVICE, KeyEvent.KEYCODE_BUTTON_A);
            }
        }

        // pixel-art gamepad, `cell` screen pixels per art pixel, centred on (x, y)
        static final String[] PAD_ART = {
            "   kkkkkkkkkkkk   ",
            "  kbbbbbbbbbbbbk  ",
            " kbbwbbbbbbbbwbbk ",
            "kbbwwwbbbbbbwbwbbk",
            "kbbbwbbbbbbbbwbbbk",
            "kbbbbbbbbbbbbbbbbk",
            "kbbbbbkkkkkkbbbbbk",
            " kbbbk      kbbbk ",
            "  kkk        kkk  ",
        };
        void drawPadIcon(Canvas cv, float x, float y, float cell, int body, int number, int alpha) {
            cell = Math.max(1, Math.round(cell));
            float left = Math.round(x - cell * 9), top = Math.round(y - cell * 4.5f);
            for (int r = 0; r < PAD_ART.length; r++) {
                for (int c = 0; c < PAD_ART[r].length(); c++) {
                    char k = PAD_ART[r].charAt(c);
                    if (k == ' ') continue;
                    fill.setStyle(Paint.Style.FILL);
                    fill.setColor(k == 'k' ? 0xFF000000 : k == 'w' ? 0xFFFFFFFF : body);
                    fill.setAlpha(alpha);
                    cv.drawRect(left + c * cell, top + r * cell, left + (c + 1) * cell, top + (r + 1) * cell, fill);
                }
            }
            if (number > 0) txt(cv, String.valueOf(number), left + cell * 9, top + cell * 3.6f, cell * 4.6f, 0xFFFFFFFF, alpha);
        }

        void drawPanel(Canvas cv, float u) {
            hitRects.clear(); hitCodes.clear();
            int rows = 1 + pads.size();
            float pw = Math.min(getWidth() * 0.92f, u * 1.6f);
            float rowH = u * 0.1f, gap = u * 0.012f;
            float ph = rowH * (rows + 3.6f) + gap * (rows + 5);
            if (ph > getHeight() * 0.94f) { float k = getHeight() * 0.94f / ph; rowH *= k; gap *= k; ph = getHeight() * 0.94f; }
            float left = (getWidth() - pw) / 2f, top = (getHeight() - ph) / 2f;
            panelRect.set(left, top, left + pw, top + ph);
            solid(fill, 0xFF000000, 0.94f);
            cv.drawRoundRect(panelRect, rowH * 0.2f, rowH * 0.2f, fill);
            solid(line, 0xFFFFFFFF, 0.9f);
            cv.drawRoundRect(panelRect, rowH * 0.2f, rowH * 0.2f, line);
            float y = top + gap + rowH * 0.45f;
            txt(cv, "PLAYERS", left + pw / 2f, y, rowH * 0.5f, 0xFFFFFFFF, 255);
            y += rowH * 0.55f;
            txt(cv, pads.isEmpty() ? "CONNECT A GAMEPAD TO ADD IT HERE" : "PRESS A BUTTON ON A GAMEPAD TO FIND IT",
                    left + pw / 2f, y, rowH * 0.28f, 0xFFC8C8C8, 255);
            y += rowH * 0.45f;
            float labelW = pw * 0.34f, cellsLeft = left + labelW + gap;
            float cellW = (left + pw - gap - cellsLeft - gap * 4) / 5f;
            long t = android.os.SystemClock.uptimeMillis();
            for (int row = 0; row < rows; row++) {
                Pad pad = row == 0 ? null : pads.get(row - 1);
                int bound = row == 0 ? player : pad.player;
                float ry = y + row * (rowH + gap);
                boolean lit = pad != null && pad.litUntil > t;
                if (lit) {
                    solid(fill, 0xFFE8EA4A, 0.35f);
                    tmp.set(left + gap * 0.5f, ry - gap * 0.4f, left + pw - gap * 0.5f, ry + rowH + gap * 0.4f);
                    cv.drawRoundRect(tmp, rowH * 0.15f, rowH * 0.15f, fill);
                }
                float lx = left + gap + rowH * 0.55f, cyRow = ry + rowH / 2f;
                if (pad == null) {
                    drawTouchIcon(cv, lx, cyRow, rowH * 0.045f, bound >= 0 ? PLAYER_COLOR[bound] : 0xFF6F6F6F, 255);
                    txt(cv, "TOUCH", left + gap + rowH * 1.15f + font_w("TOUCH", rowH * 0.36f) / 2f, cyRow, rowH * 0.36f, 0xFFFFFFFF, 255);
                } else {
                    drawPadIcon(cv, lx, cyRow, rowH * 0.06f, bound >= 0 ? PLAYER_COLOR[bound] : 0xFF6F6F6F, pad.number, 255);
                    String head = "PAD " + pad.number + (lit ? " <" : "");
                    float tx = left + gap + rowH * 1.15f;
                    txt(cv, head, tx + font_w(head, rowH * 0.36f) / 2f, cyRow - rowH * 0.15f, rowH * 0.36f, 0xFFFFFFFF, 255);
                    String nm = pad.name.toUpperCase();
                    float small = rowH * 0.24f;
                    int maxChars = Math.max(4, (int) ((labelW - rowH * 1.2f) / Math.max(1f, font_w("M", small))));
                    if (nm.length() > maxChars) nm = nm.substring(0, maxChars - 1) + ".";
                    txt(cv, nm, tx + font_w(nm, small) / 2f, cyRow + rowH * 0.24f, small, 0xFFC8C8C8, 255);
                }
                for (int col = 0; col < 5; col++) {
                    if (row == 0 && col == 0) continue;   // touch is always on someone (hide it in the editor)
                    RectF r = new RectF(cellsLeft + col * (cellW + gap), ry + rowH * 0.08f, cellsLeft + col * (cellW + gap) + cellW, ry + rowH * 0.92f);
                    boolean on = bound == col - 1;
                    boolean unused = soloSession() && col >= 2;   // solo play is one player
                    int color = col == 0 ? NO_PLAYER : PLAYER_COLOR[col - 1];
                    solid(fill, on ? color : BTN_NEUTRAL, on ? 1f : unused ? 0.4f : 0.9f);
                    cv.drawRoundRect(r, rowH * 0.15f, rowH * 0.15f, fill);
                    if (on) { solid(line, 0xFFFFFFFF, 1f); cv.drawRoundRect(r, rowH * 0.15f, rowH * 0.15f, line); }
                    txt(cv, col == 0 ? "OFF" : "P" + col, r.centerX(), r.centerY(), rowH * 0.36f, on ? 0xFFFFFFFF : NO_PLAYER, unused ? 90 : 255);
                    hitRects.add(r); hitCodes.add(row * 8 + col);
                }
            }
            float fy = y + rows * (rowH + gap) + gap;
            float half = (pw - gap * 5) / 2f, bh = rowH * 0.9f;
            boolean locked = onlineSeat != -1 && !soloSession();
            float third = (pw - gap * 6) / 3f;
            RectF nm = new RectF(left + gap * 2, fy, left + gap * 2 + third, fy + bh);
            RectF jc = new RectF(nm.right + gap, fy, nm.right + gap + third, fy + bh);
            RectF up = new RectF(jc.right + gap, fy, left + pw - gap * 2, fy + bh);
            RectF hs = new RectF(nm.left, fy + bh + gap, nm.left + half, fy + bh * 2 + gap);
            RectF dn = new RectF(hs.right + gap, hs.top, left + pw - gap * 2, hs.bottom);
            for (RectF r : new RectF[] { nm, jc }) {
                solid(fill, 0xFF202040, 1f); cv.drawRoundRect(r, rowH * 0.15f, rowH * 0.15f, fill);
                solid(line, 0xFFFFFFFF, locked ? 0.3f : 0.9f); cv.drawRoundRect(r, rowH * 0.15f, rowH * 0.15f, line);
            }
            String shown = "NAME: " + currentName().toUpperCase();
            float nh = rowH * 0.34f;
            while (font_w(shown, nh) > third - gap * 2 && nh > rowH * 0.12f) nh *= 0.9f;
            txt(cv, shown, nm.centerX(), nm.centerY(), nh, 0xFFFFFFFF, locked ? 110 : 255);
            txt(cv, "JOIN CODE", jc.centerX(), jc.centerY(), rowH * 0.34f, 0xFFFFFFFF, locked ? 110 : 255);
            hitRects.add(nm); hitCodes.add(HIT_NAME);
            hitRects.add(jc); hitCodes.add(HIT_JOINCODE);
            toggle(cv, up, upnp ? "UPNP: ON" : "UPNP: OFF", upnp, rowH);
            hitRects.add(up); hitCodes.add(HIT_UPNP);
            toggle(cv, hs, hotSeat ? "HOT SEAT: ON" : "HOT SEAT: OFF", hotSeat, rowH);
            hitRects.add(hs); hitCodes.add(HIT_HOTSEAT);
            solid(fill, BTN_NEUTRAL, 1f); cv.drawRoundRect(dn, rowH * 0.15f, rowH * 0.15f, fill);
            solid(line, 0xFFFFFFFF, 0.9f); cv.drawRoundRect(dn, rowH * 0.15f, rowH * 0.15f, line);
            txt(cv, "DONE", dn.centerX(), dn.centerY(), rowH * 0.34f, 0xFFFFFFFF, 255);
            hitRects.add(dn); hitCodes.add(HIT_DONE);
        }

        // an on/off setting: a neutral button, outlined bright when on
        void toggle(Canvas cv, RectF r, String label, boolean on, float rowH) {
            solid(fill, BTN_NEUTRAL, 1f); cv.drawRoundRect(r, rowH * 0.15f, rowH * 0.15f, fill);
            solid(line, 0xFFFFFFFF, on ? 1f : 0.3f); cv.drawRoundRect(r, rowH * 0.15f, rowH * 0.15f, line);
            txt(cv, label, r.centerX(), r.centerY(), rowH * 0.34f, 0xFFFFFFFF, on ? 255 : 150);
        }

        // pixel-art phone being tapped: the symbol for the touch controls
        static final String[] TOUCH_ART = {
            " kkkkkkkkkk      ",
            "kbbbbbbbbbbk     ",
            "kbbbkkkkbbbk     ",
            "kbsssssssssk     ",
            "kbsssssssssk     ",
            "kbsssrrrsssk     ",
            "kbssrsssrssk     ",
            "kbssrskksrsk     ",
            "kbsssrkwkssk     ",
            "kbssssk wkkk     ",
            "kbssssk wkwkkk   ",
            "kbssssk wkwkwkk  ",
            "kbsskkk wwwwwwk  ",
            "kbbkwwk wwwwwwk  ",
            "kbbkwwwwwwwwwwk  ",
            " kkkwwwwwwwwwk   ",
            "     kwwwwwwk    ",
            "      kbbbbk     ",
        };
        // centred on (x, y), `cell` screen pixels per art pixel; body = the player's colour
        void drawTouchIcon(Canvas cv, float x, float y, float cell, int body, int alpha) {
            cell = Math.max(1, Math.round(cell));
            float left = Math.round(x - cell * 8.5f), top = Math.round(y - cell * 9);
            for (int r = 0; r < TOUCH_ART.length; r++)
                for (int c = 0; c < TOUCH_ART[r].length(); c++) {
                    char k = TOUCH_ART[r].charAt(c);
                    if (k == ' ') {
                        if (r < 9 || r > 14 || c != 7) continue;   // inside of the finger
                        k = 'w';
                    }
                    fill.setStyle(Paint.Style.FILL);
                    fill.setColor(k == 'k' ? 0xFF000000 : k == 'w' ? 0xFFFFFFFF : k == 's' ? 0xFF27BADB : k == 'r' ? 0xFFE8EA4A : body);
                    fill.setAlpha(alpha);
                    cv.drawRect(left + c * cell, top + r * cell, left + (c + 1) * cell, top + (r + 1) * cell, fill);
                }
        }

        // pad badges next to the player chip: who's holding which pad
        boolean showSeats() { return following && !editing && seatCount >= 2 && padVisible(); }

        // A seat: a chip in its player's colour (grey for nobody), solid when active, faded when not.
        // The icon says what drives it (touch, a pad, someone online); a short name can follow the
        // number; a tag under it says something for a while. A pulsing ring = it just became active.
        RectF drawSeatChip(Canvas cv, float x, float y, float hh, int color, boolean on, int icon, int padNumber, String label, String name, String tag, boolean pulse) {
            return drawSeatChip(cv, x, y, hh, color, on, icon, padNumber, null, label, name, tag, pulse);
        }

        // morePads: pads that drive the same seat, drawn after the main icon (one chip per seat)
        RectF drawSeatChip(Canvas cv, float x, float y, float hh, int color, boolean on, int icon, int padNumber, int[] morePads, String label, String name, String tag, boolean pulse) {
            float th = hh * 1.05f, small = hh * 0.62f;
            float tcell = Math.max(1, Math.round(hh * 1.65f / 18f)), pcell = Math.max(1, Math.round(hh * 0.11f));
            float iconW = icon == ICON_PAD ? pcell * 18 : icon == ICON_NET ? hh * 1.3f : tcell * 17;
            int extra = morePads == null ? 0 : morePads.length;
            float padW = pcell * 18, iconGap = hh * 0.2f;
            float w = hh * 0.35f + iconW + extra * (iconGap + padW) + hh * 0.3f + font_w(label, th) + (name != null ? hh * 0.35f + font_w(name, small) : 0) + hh * 0.45f;
            RectF r = new RectF(x, y - hh, x + w, y + hh);
            int alpha = on ? 245 : 90;
            if (pulse) {
                float k = (android.os.SystemClock.uptimeMillis() % 600) / 600f;
                solid(line, 0xFFFFFFFF, 0.85f * (1 - k));
                tmp.set(r.left - hh * 0.6f * k, r.top - hh * 0.6f * k, r.right + hh * 0.6f * k, r.bottom + hh * 0.6f * k);
                cv.drawRoundRect(tmp, hh * 0.5f, hh * 0.5f, line);
                postInvalidateDelayed(40);
            }
            solid(fill, color, alpha / 255f);
            cv.drawRoundRect(r, hh * 0.4f, hh * 0.4f, fill);
            solid(line, 0xFFFFFFFF, on ? 0.95f : 0.35f);
            cv.drawRoundRect(r, hh * 0.4f, hh * 0.4f, line);
            float ix = r.left + hh * 0.35f + iconW / 2f;
            if (icon == ICON_PAD) drawPadIcon(cv, ix, y, pcell, 0xFF000000, padNumber, alpha);
            else if (icon == ICON_NET) drawNetIcon(cv, ix, y, hh * 0.62f, alpha);
            else drawTouchIcon(cv, ix, y, tcell, 0xFF000000, alpha);
            float ie = ix + iconW / 2f;
            for (int k = 0; k < extra; k++) {
                drawPadIcon(cv, ie + iconGap + padW / 2f, y, pcell, 0xFF000000, morePads[k], alpha);
                ie += iconGap + padW;
            }
            float tx = ie + hh * 0.3f;
            txt(cv, label, tx + font_w(label, th) / 2f, y, th, 0xFFFFFFFF, alpha);
            if (name != null) {
                float nx = tx + font_w(label, th) + hh * 0.35f;
                txt(cv, name, nx + font_w(name, small) / 2f, y, small, 0xFFFFFFFF, alpha);
            }
            if (tag != null) {
                // in a column a tag under the chip would cover the next one: beside it instead
                if (chipsColumn) { float th2 = hh * 0.6f, tw2 = font_w(tag, th2) + th2 * 0.9f; drawTag(cv, r.right + hh * 0.3f + tw2 / 2f, r.centerY() - th2 * 0.75f, tag, th2, false); }
                else drawTag(cv, r.centerX(), r.bottom + hh * 0.22f, tag, hh * 0.6f, false);
            }
            return r;
        }

        // someone online: a globe, drawn in black like the other icons
        void drawNetIcon(Canvas cv, float x, float y, float rad, int alpha) {
            Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
            p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(Math.max(2f, rad * 0.22f)); p.setColor(0xFF000000); p.setAlpha(alpha);
            cv.drawCircle(x, y, rad, p);
            tmp.set(x - rad * 0.45f, y - rad, x + rad * 0.45f, y + rad); cv.drawOval(tmp, p);
            cv.drawLine(x - rad, y, x + rad, y, p);
        }

        // a small label under a chip: white, or the attention colour when it needs someone
        void drawTag(Canvas cv, float cxT, float topT, String t, float h, boolean attn) {
            float w = font_w(t, h) + h * 0.9f;
            tmp.set(Math.round(cxT - w / 2f), Math.round(topT), Math.round(cxT + w / 2f), Math.round(topT + h * 1.5f));
            solid(fill, attn ? ATTN : 0xFFFFFFFF, 1f); cv.drawRoundRect(tmp, h * 0.3f, h * 0.3f, fill);
            txt(cv, t, cxT, tmp.centerY(), h, attn ? 0xFFFFFFFF : 0xFF000000, 255);
        }

        boolean showOnlineSeats() { return seatMyClient >= 0 && onlineSeat != -1 && !soloSession() && !editing && padVisible(); }

        // Hot seat: one seat per player in the game, left to right from the chip's spot: whose turn it
        // is is the solid one (it pulses when the turn arrives); a seat with a pad shows the pad.
        float drawSeats(Canvas cv, float u, Control chip) {
            seatRects.clear();
            float hh = halfH(chip), gap = u * 0.012f;
            float x = cx(chip) - halfW(chip), y = cy(chip);
            long now = android.os.SystemClock.uptimeMillis();
            int n = Math.min(seatCount, 8);
            for (int q = 0; q < n; q++) {
                Pad pad = padOn(q);
                boolean on = q == followedTurn;
                RectF r = drawSeatChip(cv, x, y, hh, SEAT_COLOR[q], on, pad != null ? ICON_PAD : ICON_TOUCH, pad != null ? pad.number : 0,
                        "P" + (q + 1), null, pad != null && pad.litUntil > now ? "PAD " + pad.number : null, on && now < hsFlashUntil);
                seatRects.add(r);
                badgeRects.add(r);
                x = nextX(r, gap); y = nextY(r, gap, hh);
            }
            return x;
        }

        // Online: every seat in the game, ours with the touch icon and YOU, the others with a globe
        // and their name.
        float drawOnlineSeats(Canvas cv, float u, Control chip) {
            float hh = halfH(chip), gap = u * 0.012f;
            float x = cx(chip) - halfW(chip), y = cy(chip);
            for (int q = 0; q < 8; q++) {
                int c = seatClient[q];
                if (c < 0) continue;
                boolean mine = c == seatMyClient;
                String nm = mine ? "YOU" : shortName(c < clientNames.length ? clientNames[c] : "");
                RectF r = drawSeatChip(cv, x, y, hh, SEAT_COLOR[q], true, mine ? ICON_TOUCH : ICON_NET, 0, "P" + (q + 1), nm.isEmpty() || !namesShown() ? null : nm, null, false);
                badgeRects.add(r);
                x = nextX(r, gap); y = nextY(r, gap, hh);
            }
            return x;
        }

        static String shortName(String n) {
            StringBuilder b = new StringBuilder();
            for (char ch : (n == null ? "" : n).toUpperCase().toCharArray()) {
                if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) b.append(ch);
                if (b.length() >= 5) break;
            }
            return b.toString();
        }

        // the numbers of the pads that control a seat (null when none)
        int[] padsOnSeat(int seat) {
            int n = 0;
            for (Pad p : pads) if (p.player == seat) n++;
            if (n == 0) return null;
            int[] r = new int[n]; n = 0;
            for (Pad p : pads) if (p.player == seat) r[n++] = p.number;
            return r;
        }

        void drawBadges(Canvas cv, float u) {
            badgeRects.clear();
            seatRects.clear();
            rowEnd = -1;
            if (editing) return;
            Control chip = control("player");
            if (chip == null || chip.hidden || !padVisible()) return;
            if (showOnlineSeats()) { rowEnd = drawOnlineSeats(cv, u, chip); return; }
            float seatsEnd = showSeats() ? drawSeats(cv, u, chip) : -1;
            float hh = halfH(chip), gap = u * 0.012f;
            float x = seatsEnd >= 0 ? seatsEnd : (chipRect.isEmpty() ? cx(chip) + halfW(chip) : chipRect.right) + gap;
            float y = cy(chip);
            if (chipsColumn) {
                // below the last chip drawn so far (a seat row, or the touch chip)
                RectF last = !badgeRects.isEmpty() ? badgeRects.get(badgeRects.size() - 1) : chipRect.isEmpty() ? null : chipRect;
                if (last != null) { x = last.left; y = last.bottom + gap + hh * 0.85f; }
            }
            long t = android.os.SystemClock.uptimeMillis();
            for (Pad pad : pads) {
                if (seatsEnd >= 0 && pad.player >= 0 && pad.player < seatCount) continue;   // already a seat
                if (seatsEnd < 0 && !chipRect.isEmpty() && onlineSeat != -2 && pad.player == shownPlayer()) continue;   // inside the touch chip
                boolean on = pad.player >= 0;
                RectF r = drawSeatChip(cv, x, y, hh * 0.85f, on ? PLAYER_COLOR[pad.player] : NO_PLAYER, on, ICON_PAD, pad.number,
                        on ? "P" + (pad.player + 1) : "OFF", null, pad.litUntil > t ? "PAD " + pad.number : null, false);
                badgeRects.add(r);
                x = nextX(r, gap); y = nextY(r, gap, hh * 0.85f);
            }
            rowEnd = x;
        }

        // ---- the session pill ----
        // After the chips: what kind of session this is and what just happened. It says it in words
        // for a few seconds when something changes (or when tapped), then settles: a dot in solo play,
        // the dot and ONLINE online, nothing offline. Catching up is the one thing it keeps saying.
        void notePill(String t, long ms) {
            pillText = t;
            pillUntil = android.os.SystemClock.uptimeMillis() + ms;
            invalidate();
            postDelayed(new Runnable() { @Override public void run() { invalidate(); } }, ms + 50);
        }

        int sessionMode() { return soloSession() ? 1 : (netStatus & 64) != 0 ? 3 : (netStatus & 16) != 0 ? 2 : 0; }   // offline, solo, lobby, online

        String netWord() { int kind = netStatus & 15; return kind == 1 ? "LAN" : kind == 2 ? "DIRECT" : kind == 3 ? "RELAY" : ""; }

        String pillDetail() {
            int mode = sessionMode(), delay = (netStatus >> 8) & 255;
            if (mode == 1) return "SOLO: SAVES ON";
            if (mode == 2) return (netStatus & 32) != 0 ? "HOSTING" : netWord().isEmpty() ? "CONNECTING" : netWord();
            if (mode == 3) { String w = netWord(); return "ONLINE" + (w.isEmpty() ? "" : ": " + w) + (delay > 0 ? " " + delay + "F" : ""); }
            return null;
        }

        // from the poll: a change in the session announces itself
        void watchPill() {
            int mode = sessionMode(), kind = netStatus & 15, delay = (netStatus >> 8) & 255;
            if (mode != lastMode || (mode >= 2 && kind != lastKind)) {
                lastMode = mode; lastKind = kind; lastDelay = delay;
                String d = pillDetail();
                if (d != null) notePill(d, 3000); else { pillText = null; invalidate(); }
                return;
            }
            if (mode == 3 && delay > lastDelay) notePill("DELAY " + delay + "F", 2000);
            lastDelay = delay;
        }

        void drawPill(Canvas cv, float u) {
            pillRect.setEmpty();
            Control chip = control("player");
            if (chip == null || chip.hidden || !padVisible()) return;
            int mode = sessionMode();
            long now = android.os.SystemClock.uptimeMillis();
            boolean talking = pillText != null && now < pillUntil;
            if (mode == 0 && !talking) return;
            boolean attn = mode == 3 && (netStatus & 128) != 0;
            String t = attn ? "CATCHING UP" : talking ? pillText : mode == 2 ? pillDetail() : mode == 3 ? "ONLINE" : null;
            float hh = halfH(chip) * 0.62f, th = hh * 0.95f, dot = hh * 0.32f;
            float x = (rowEnd >= 0 ? rowEnd : cx(chip) + halfW(chip) + u * 0.012f), y = cy(chip);
            if (chipsColumn) {
                RectF top = !chipRect.isEmpty() ? chipRect : !badgeRects.isEmpty() ? badgeRects.get(0) : null;
                if (top != null) { x = top.right + u * 0.012f; y = top.centerY(); }
            }
            float w = hh * 0.6f + dot * 2 + (t != null ? hh * 0.45f + font_w(t, th) : 0) + hh * 0.6f;
            pillRect.set(x, y - hh, x + w, y + hh);
            boolean dim = attn && (now / 400) % 2 == 0;
            solid(fill, attn ? ATTN : 0xFF2A2838, dim ? 0.65f : 0.92f);
            cv.drawRoundRect(pillRect, hh, hh, fill);
            solid(line, 0xFFFFFFFF, 0.5f);
            cv.drawRoundRect(pillRect, hh, hh, line);
            solid(fill, 0xFFFFFFFF, 1f);
            float dx = x + hh * 0.6f + dot;
            cv.drawCircle(dx, y, dot, fill);
            if (t != null) txt(cv, t, dx + dot + hh * 0.45f + font_w(t, th) / 2f, y, th, 0xFFFFFFFF, 255);
            if (attn) postInvalidateDelayed(200);
        }

        // Online chips show the seat and its input; the players' names show for a few seconds when the
        // match starts, when the seats change, and when the chips are tapped.
        static final long NAMES_MS = 4000;
        long namesUntil = 0;
        String lastSeatInfo = "";

        void showNames() {
            namesUntil = android.os.SystemClock.uptimeMillis() + NAMES_MS;
            invalidate();
            postDelayed(new Runnable() { @Override public void run() { invalidate(); } }, NAMES_MS + 50);
        }

        boolean namesShown() { return android.os.SystemClock.uptimeMillis() < namesUntil; }

        void parseSeats(String si) {
            String key = si == null ? "" : si;
            if (!key.equals(lastSeatInfo)) { lastSeatInfo = key; if (!key.isEmpty()) showNames(); }
            java.util.Arrays.fill(seatClient, -1);
            seatMyClient = -1;
            if (si == null || si.isEmpty()) return;
            String[] f = si.split(";", 3);
            if (f.length < 3) return;
            try {
                seatMyClient = Integer.parseInt(f[0].trim());
                String[] cs = f[1].split(",");
                for (int q = 0; q < cs.length && q < 8; q++) seatClient[q] = Integer.parseInt(cs[q].trim());
            } catch (NumberFormatException ex) { seatMyClient = -1; return; }
            clientNames = f[2].split("\\|", -1);
        }

        // text in the game's pixel font (system font if the asset is missing), centred on (x, y)
        void txt(Canvas cv, String t, float x, float y, float h, int color, int alpha) {
            if (font != null) { font.draw(cv, t, x, y, Math.max(1, Math.round(h / font.ch)), color, alpha); return; }
            text.setTextSize(h); text.setColor(color); text.setAlpha(alpha);
            cv.drawText(t, x, y + h * 0.36f, text);
        }

        float font_w(String t, float h) {
            if (font != null) return font.width(t, Math.max(1, Math.round(h / font.ch)));
            text.setTextSize(h);
            return text.measureText(t);
        }

        void updatePressed(MotionEvent e, int action) {
            for (Control c : controls) { c.pressed = false; java.util.Arrays.fill(c.down, false); }
            Set<Integer> want = new HashSet<>();
            if (padVisible() && action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL) {
                for (int i = 0; i < e.getPointerCount(); i++) {
                    if (action == MotionEvent.ACTION_POINTER_UP && i == e.getActionIndex()) continue;
                    float px = e.getX(i), py = e.getY(i);
                    Control best = null;
                    for (Control c : controls) {
                        if (c.hidden || c.type == Control.PLAYER) continue;
                        // generous slop so thumbs that slide slightly off a button keep it held
                        if (hit(c, px, py, c.type == Control.DPAD ? 1.35f : 1.15f)) { best = c; break; }
                    }
                    if (best == null) continue;
                    best.pressed = true;
                    if (best.type == Control.DPAD) {
                        float dx = px - cx(best), dy = py - cy(best);
                        float len = (float) Math.hypot(dx, dy);
                        if (len < radius(best) * 0.18f) continue; // dead zone
                        float nx = dx / len, ny = dy / len;
                        if (dpad4) {
                            if (Math.abs(nx) > Math.abs(ny)) best.down[nx < 0 ? 2 : 3] = true;
                            else best.down[ny < 0 ? 0 : 1] = true;
                        } else {
                            if (ny < -0.38f) best.down[0] = true;
                            if (ny > 0.38f) best.down[1] = true;
                            if (nx < -0.38f) best.down[2] = true;
                            if (nx > 0.38f) best.down[3] = true;
                        }
                        for (int d = 0; d < 4; d++) if (best.down[d]) want.add(player * 16 + best.keys[d]);
                    } else {
                        want.add(player * 16 + best.keys[0]);
                    }
                }
            }
            boolean changed = false;
            for (Integer k : new ArrayList<>(keysDown)) if (!want.contains(k)) { sendKey(1, k); keysDown.remove(k); changed = true; }
            for (Integer k : want) if (!keysDown.contains(k)) { sendKey(0, k); keysDown.add(k); changed = true; }
            if (changed) {
                if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN)
                    performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
                invalidate();
            }
        }

        // type 0 = down, 1 = up; key = player * 16 + action
        void sendKey(int type, int key) {
            int p = key / 16, act = key % 16;
            try {
                Integer held = type == 0 ? null : downCode.remove(key);
                int code = held != null ? held : keyCodeFor(p, act);
                if (code != 0) {
                    if (type == 0) downCode.put(key, code);
                    RunnerJNILib.KeyEvent(type, code, 0, InputDevice.SOURCE_KEYBOARD, 0);
                } else {
                    registerVirtualPads();
                    // touch pad N always plays player N; libPFO points that player at its slot.
                    // Release on the device the press went to.
                    Integer heldDev = type == 0 ? null : downDevice.remove(key);
                    int dev = heldDev != null ? heldDev : VPAD_BASE + p;
                    if (type == 0) downDevice.put(key, dev);
                    if (act <= ACT_RIGHT) {   // d-pad: reported as a hat, like a real controller
                        int[] h = hat[p];
                        if (type == 0) {
                            if (act == ACT_UP) h[1] = -1; else if (act == ACT_DOWN) h[1] = 1;
                            else if (act == ACT_LEFT) h[0] = -1; else h[0] = 1;
                        } else {
                            if (act == ACT_UP && h[1] < 0) h[1] = 0; else if (act == ACT_DOWN && h[1] > 0) h[1] = 0;
                            else if (act == ACT_LEFT && h[0] < 0) h[0] = 0; else if (act == ACT_RIGHT && h[0] > 0) h[0] = 0;
                        }
                        RunnerJNILib.onGPNativeHat(dev, 0, h[0], h[1]);
                    } else if (type == 0) RunnerJNILib.onGPKeyDown(dev, GAMEPAD[act]);
                    else RunnerJNILib.onGPKeyUp(dev, GAMEPAD[act]);
                }
            } catch (Throwable t) {
                Log.i("yoyo", "PFO: key event failed: " + t);
            }
        }

        // The keyboard key touch sends for a player's action: the key the game has bound (its
        // Keyboard Config), else UFO 50's default. 0 = no key (P3/P4 by default): use the touch pad.
        int keyCodeFor(int p, int act) {
            if (act == ACT_BACK) return KeyEvent.KEYCODE_ESCAPE;
            int vk = boundVk(p, act);
            if (vk > 2) { int c = vkToAndroid(vk); if (c != 0) return c; }
            if (vk < 0 && p < KEYBOARD.length) return KEYBOARD[p][act];
            return 0;
        }

        // vk the game has bound for this action, -1 when the game hasn't reported yet
        int boundVk(int p, int act) {
            if (act == ACT_BACK) return 27;
            int[] k = p < gameKeys.length ? gameKeys[p] : null;
            if (k == null) return -1;
            return k[KEY_SLOT[act]];
        }

        void pollKeys() {
            boolean changed = false;
            for (int q = 0; q < gameKeys.length; q++) {
                int[] k = null;
                try { k = nativeKeys(q); } catch (Throwable t) { }
                if (k != null && !java.util.Arrays.equals(k, gameKeys[q])) { gameKeys[q] = k; changed = true; }
            }
            if (changed) invalidate();
        }

        final int[][] hat = new int[4][2];
        final java.util.HashMap<Integer, Integer> downDevice = new java.util.HashMap<>();

        // P3 and P4 have no keyboard keys in UFO 50, only gamepad slots 2 and 3. Register four
        // virtual pads (so ours land in slots 0-3) the first time P3/P4 is used, not at start-up:
        // a connected pad makes the game show gamepad button prompts.
        void registerVirtualPads() {
            if (vpadsRegistered) return;
            vpadsRegistered = true;
            int mask = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 6) | (1 << 9) | (1 << 10);  // A B X Y BACK START L1 R1
            for (int i = 0; i < 4; i++) {
                RunnerJNILib.onGPDeviceAdded(VPAD_BASE + i, "PFO Touch Pad " + (i + 1), "pfo-touch-pad-" + (i + 1), 0, 0, 0, 1, mask);  // 1 hat = d-pad
            }
            try { RunnerJNILib.onGamepadChange(); } catch (Throwable t) { }
            Log.i("yoyo", "PFO: virtual gamepads registered");
        }

        Control control(String id) {
            for (Control c : controls) if (c.id.equals(id)) return c;
            return null;
        }

        int shownPlayer() { return onlineSeat >= 0 ? onlineSeat : player; }

        final Runnable seatPoll = new Runnable() { @Override public void run() {
            int seat = -1;
            try { seat = nativeOnlineSeat(); } catch (Throwable t) { }
            if (seat != onlineSeat) {
                boolean wasOnline = onlineSeat != -1;
                onlineSeat = seat;
                if (seat != -1 && !wasOnline) {         // joined an online game
                    playerBeforeOnline = player;
                    releaseAll();
                    player = 0;
                } else if (seat == -1 && wasOnline) {   // back to local play
                    releaseAll();
                    player = playerBeforeOnline;
                }
                boolean solo = false;
                try { solo = (nativeStateStatus() & 1) != 0; } catch (Throwable t) { }
                if (seat != -1 && !solo) showBanner(seat >= 0 ? "YOU ARE P" + (seat + 1) : "YOU ARE WATCHING", 2200, seat >= 0 ? SEAT_COLOR[Math.min(7, seat)] : 0);
                invalidate();
            }
            followTurn();
            int ns = 0;
            try { ns = nativeNetStatus(); } catch (Throwable t) { }
            if (ns != netStatus) { netStatus = ns; invalidate(); }
            String si = null;
            try { si = (ns & 64) != 0 ? nativeSeatInfo() : null; } catch (Throwable t) { }
            parseSeats(si);
            if ((ns & (16 | 64)) != 0) PFOOnlineService.start(getContext());   // keep the app alive in the background
            else PFOOnlineService.stop(getContext());
            pollTestPad();
            pollPads();
            pollKeys();
            pollStates();
            pollVote();
            pollWaiting();
            pollRejoin();
            keepCleanFrame();
            watchPill();
            int ask = -1;
            try { ask = nativeTakeOpenPanel(); } catch (Throwable t) { }
            if (ask >= 0 && (onlineSeat == -1 || soloSession())) openPanel(ask);
            boolean invite = false;
            try { invite = nativeTakeInviteRequest(); } catch (Throwable t) { }
            if (invite) openInvite();
            boolean askCode = false;
            try { askCode = nativeTakeAskCode(); } catch (Throwable t) { }
            if (askCode && !naming) askEntry(ENTRY_CODE);
            postDelayed(this, 150);
        } };

        void followTurn() {
            int turn = -1, count = 0;
            try { turn = nativeHotSeatTurn(); count = nativeHotSeatCount(); } catch (Throwable t) { }
            if (turn >= 0 && count != seatCount) { seatCount = count; invalidate(); }
            if (onlineSeat != -1 || !hotSeat) { following = false; followedTurn = -1; return; }
            if (turn >= 0) {
                if (!following) { following = true; playerBeforeFollow = player; followedTurn = -1; invalidate(); }
                if (turn != followedTurn) {
                    followedTurn = turn;
                    hsFlashUntil = android.os.SystemClock.uptimeMillis() + 2000;
                    invalidate();
                    Pad own = padOn(turn);
                    if (own == null && turn > 3) showBanner("P" + (turn + 1) + "'S TURN: USE A GAMEPAD", 2200);
                    else if (own == null && turn != player) setPlayer(turn, "");   // the seat's chip pulses
                }
            } else if (following) {   // the game stopped reporting turns: back to the earlier pick
                following = false;
                followedTurn = -1;
                if (player != playerBeforeFollow) setPlayer(playerBeforeFollow, null);
                invalidate();
            }
        }

        @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); removeCallbacks(seatPoll); post(seatPoll); }

        void showBanner(String t, long ms) { showBanner(t, ms, 0); }

        void showBanner(String t, long ms, int color) {
            banner = t;
            bannerColor = color;
            bannerUntil = android.os.SystemClock.uptimeMillis() + ms;
            invalidate();
            postDelayed(new Runnable() { @Override public void run() { invalidate(); } }, ms + 100);
        }

        void setPlayer(int p, String bannerText) {
            if (onlineSeat != -1) {   // locked: the other seats belong to the network
                if (soloSession()) { if (p != 0) showBanner("SOLO PLAY IS ONE PLAYER", 1500); return; }
                showBanner(onlineSeat >= 0 ? "YOU ARE P" + (onlineSeat + 1) : "YOU ARE WATCHING", 1200, onlineSeat >= 0 ? SEAT_COLOR[Math.min(7, onlineSeat)] : 0);
                return;
            }
            if (p == player && bannerText == null) return;
            releaseAll();
            player = Math.max(0, Math.min(3, p));
            if (player >= 2) registerVirtualPads();   // early, so the slot map is ready before the first press
            pushBindings();
            Control chip = control("player");
            if (chip != null) chip.pressed = false;
            if (bannerText != null && bannerText.isEmpty()) { invalidate(); return; }
            banner = bannerText != null ? bannerText : "P" + (player + 1);
            bannerColor = PLAYER_COLOR[player];
            bannerUntil = android.os.SystemClock.uptimeMillis() + (bannerText != null ? 2200 : 900);
            invalidate();
            postDelayed(new Runnable() { @Override public void run() { invalidate(); } }, 2300);
        }

        // Called from libPFO.so (any thread) when a game reports whose turn it is.
        static void setPlayerFromGame(final int p, final String bannerText) {
            final TouchOverlay o = sInstance;
            if (o == null) return;
            o.post(new Runnable() { @Override public void run() { o.setPlayer(p, bannerText); } });
        }

        void releaseAll() {
            for (Integer k : keysDown) sendKey(1, k);
            keysDown.clear();
            for (Control c : controls) { c.pressed = false; java.util.Arrays.fill(c.down, false); }
        }

        @Override protected void onDetachedFromWindow() { removeCallbacks(seatPoll); releaseAll(); super.onDetachedFromWindow(); }

        @Override public void onWindowFocusChanged(boolean focus) { if (!focus) { releaseAll(); invalidate(); } super.onWindowFocusChanged(focus); }

        // ---- layout editor ----
        void onEditTouch(MotionEvent e) {
            int action = e.getActionMasked();
            int i = e.getActionIndex();
            float px = e.getX(i), py = e.getY(i);
            switch (action) {
                case MotionEvent.ACTION_DOWN: {
                    int t = toolbarHit(px, py);
                    if (t >= 0) { toolbar(t); dragPointer = -1; break; }
                    dragPointer = -1;
                    if (selected != null) {
                        float[] h = handlePos(selected);
                        if (Math.hypot(px - h[0], py - h[1]) < unit() * 0.06f) {
                            dragPointer = e.getPointerId(i); dragResize = true; break;
                        }
                    }
                    Control c = controlAt(px, py);
                    selected = c;
                    if (c != null) {
                        dragPointer = e.getPointerId(i); dragResize = false;
                        dragDX = cx(c) - px; dragDY = cy(c) - py;
                    }
                    break;
                }
                case MotionEvent.ACTION_POINTER_DOWN:
                    if (selected != null && e.getPointerCount() == 2) {
                        pinchStartDist = pinchDistance(e);
                        pinchStartSize = selected.size;
                        dragPointer = -1;
                    }
                    break;
                case MotionEvent.ACTION_MOVE:
                    if (selected == null) break;
                    if (e.getPointerCount() >= 2 && pinchStartDist > 0) {
                        selected.size = clamp(pinchStartSize * pinchDistance(e) / pinchStartDist, 0.05f, 0.8f);
                    } else if (dragPointer >= 0) {
                        int pi = e.findPointerIndex(dragPointer);
                        if (pi < 0) break;
                        float mx = e.getX(pi), my = e.getY(pi);
                        if (dragResize) {
                            float dx = Math.abs(mx - cx(selected)), dy = Math.abs(my - cy(selected));
                            float r = selected.type == Control.PILL ? Math.max(dx, dy / 0.42f) : (float) Math.hypot(dx, dy) / 1.414f;
                            selected.size = clamp(r * 2f / unit(), 0.05f, 0.8f);
                        } else {
                            selected.x = clamp((mx + dragDX) / getWidth(), 0f, 1f);
                            selected.y = clamp((my + dragDY) / getHeight(), 0f, 1f);
                        }
                    }
                    break;
                case MotionEvent.ACTION_POINTER_UP:
                    pinchStartDist = 0;
                    break;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    dragPointer = -1; pinchStartDist = 0;
                    break;
            }
            invalidate();
        }

        Control controlAt(float px, float py) {
            for (int k = controls.size() - 1; k >= 0; k--) if (hit(controls.get(k), px, py, 1.1f)) return controls.get(k);
            return null;
        }

        static float clamp(float v, float lo, float hi) { return Math.max(lo, Math.min(hi, v)); }

        static float pinchDistance(MotionEvent e) { return (float) Math.hypot(e.getX(0) - e.getX(1), e.getY(0) - e.getY(1)); }

        int toolbarHit(float px, float py) {
            for (int t = 0; t < toolbarRects.length; t++) if (toolbarRects[t] != null && toolbarRects[t].contains(px, py)) return t;
            return -1;
        }

        void toolbar(int t) {
            switch (t) {
                case TOOLBAR_DONE: editing = false; selected = null; save(); break;
                case TOOLBAR_RESET: reset(); selected = null; break;
                case TOOLBAR_SMALLER: if (selected != null) selected.size = clamp(selected.size / 1.1f, 0.05f, 0.8f); break;
                case TOOLBAR_BIGGER: if (selected != null) selected.size = clamp(selected.size * 1.1f, 0.05f, 0.8f); break;
                case TOOLBAR_FAINTER: opacity = clamp(opacity - 0.1f, 0.1f, 1f); break;
                case TOOLBAR_STRONGER: opacity = clamp(opacity + 0.1f, 0.1f, 1f); break;
                case TOOLBAR_HIDE: if (selected != null) selected.hidden = !selected.hidden; break;
                case TOOLBAR_AUTOHIDE: autoHide = !autoHide; break;
                case TOOLBAR_DPAD: dpad4 = !dpad4; break;
                case TOOLBAR_CHIPS: chipsColumn = !chipsColumn; break;
            }
            performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
        }

        // ---- drawing ----
        int a(float base) { return (int) (255 * clamp(base * opacity, 0f, 1f)); }

        @Override protected void onDraw(Canvas canvas) {
            float u = unit();
            if (u <= 0) return;
            text.setTextSize(u * 0.045f);
            line.setStrokeWidth(Math.max(2f, u * 0.006f));

            chipRect.setEmpty();
            if (padVisible()) {
                for (Control c : controls) {
                    if (c.hidden && !editing) continue;
                    if (statesOpen && !editing && c.type == Control.PLAYER) continue;   // would peek out past the save list
                    drawControl(canvas, c, c.hidden);
                }
            }
            // the save list covers nearly the whole screen: the top bar behind it stays hidden, so
            // nothing pokes out at its corners
            boolean behindStates = statesOpen && !editing;
            if (!behindStates) drawGear(canvas);
            if (!editing && !behindStates) drawStatesButton(canvas);
            if (!editing && !behindStates) drawBadges(canvas, u);
            if (!editing && !behindStates) drawPill(canvas, u);
            if (!editing && panelOpen) drawPanel(canvas, u);
            if (naming) drawNameBox(canvas, u);
            if (inviting) drawInviteBox(canvas, u);
            if (!editing && banner != null && android.os.SystemClock.uptimeMillis() < bannerUntil) drawBanner(canvas, u);
            if (editing) drawEditor(canvas);
            if (!editing && statesOpen) drawStates(canvas, u);
            if (!editing && vote[0] != 0) drawVote(canvas, u);
            if (!editing && rejoinShown()) drawRejoin(canvas, u);
            if (restoring()) drawRestore(canvas, u);
        }

        void setFill(int color, float alpha) { fill.setStyle(Paint.Style.FILL); fill.setColor(color); fill.setAlpha(a(alpha)); }
        void solid(Paint p, int color, float alpha) { if (p == fill) fill.setStyle(Paint.Style.FILL); p.setColor(color); p.setAlpha((int) (255 * alpha)); }
        void setLine(int color, float alpha) { line.setColor(color); line.setAlpha(a(alpha)); }

        void drawControl(Canvas cv, Control c, boolean ghost) {
            float x = cx(c), y = cy(c), r = radius(c);
            float g = ghost ? 0.35f : 1f;
            switch (c.type) {
                case Control.DPAD: {
                    float arm = r * 0.36f, corner = r * 0.12f;
                    // the cross as one shape (two overlapping see-through bars would darken the middle)
                    android.graphics.Path v = new android.graphics.Path(), h = new android.graphics.Path();
                    v.addRoundRect(x - arm, y - r, x + arm, y + r, corner, corner, android.graphics.Path.Direction.CW);
                    h.addRoundRect(x - r, y - arm, x + r, y + arm, corner, corner, android.graphics.Path.Direction.CW);
                    v.op(h, android.graphics.Path.Op.UNION);
                    setFill(0xFF000000, 0.75f * g);
                    cv.drawPath(v, fill);
                    setFill(PLAYER_COLOR[shownPlayer()], 0.85f * g);
                    if (c.down[0]) { tmp.set(x - arm, y - r, x + arm, y - arm); cv.drawRoundRect(tmp, corner, corner, fill); }
                    if (c.down[1]) { tmp.set(x - arm, y + arm, x + arm, y + r); cv.drawRoundRect(tmp, corner, corner, fill); }
                    if (c.down[2]) { tmp.set(x - r, y - arm, x - arm, y + arm); cv.drawRoundRect(tmp, corner, corner, fill); }
                    if (c.down[3]) { tmp.set(x + arm, y - arm, x + r, y + arm); cv.drawRoundRect(tmp, corner, corner, fill); }
                    setLine(0xFFFFFFFF, 0.7f * g);
                    cv.drawPath(v, line);
                    // arrow hints
                    setFill(0xFFFFFFFF, 0.6f * g);
                    float t = arm * 0.45f, o = r * 0.72f;
                    tri(cv, x, y - o - t * 0.5f, x - t, y - o + t * 0.5f, x + t, y - o + t * 0.5f);
                    tri(cv, x, y + o + t * 0.5f, x - t, y + o - t * 0.5f, x + t, y + o - t * 0.5f);
                    tri(cv, x - o - t * 0.5f, y, x - o + t * 0.5f, y - t, x - o + t * 0.5f, y + t);
                    tri(cv, x + o + t * 0.5f, y, x + o - t * 0.5f, y - t, x + o - t * 0.5f, y + t);
                    break;
                }
                case Control.ROUND: {
                    setFill(c.pressed ? PLAYER_COLOR[shownPlayer()] : 0xFF000000, 0.8f * g);
                    cv.drawCircle(x, y, r, fill);
                    setLine(0xFFFFFFFF, 0.7f * g);
                    cv.drawCircle(x, y, r, line);
                    drawButtonFace(cv, c, x, y, r, a(0.9f * g));
                    break;
                }
                case Control.PLAYER: {
                    if (showSeats() || showOnlineSeats()) break;   // a seat row (drawBadges) stands in for it
                    float h = r;
                    float hw = halfW(c);
                    // pads on the touch seat are shown inside this chip, not as a second chip for the same seat
                    int[] same = onlineSeat == -2 ? null : padsOnSeat(shownPlayer());
                    String lit = null;
                    long now = android.os.SystemClock.uptimeMillis();
                    if (same != null) for (Pad p : pads) if (p.player == shownPlayer() && p.litUntil > now) lit = "PAD " + p.number;
                    chipRect.set(drawSeatChip(cv, x - hw, y, halfH(c), PLAYER_COLOR[shownPlayer()], !ghost, ICON_TOUCH, 0, same, onlineSeat == -2 ? "--" : "P" + (shownPlayer() + 1), null, lit, false));
                    if (onlineSeat != -1 && !soloSession()) {   // small padlock: locked to the online seat
                        float lx = x + hw * 0.95f, ly = y - h * 0.62f, lw = h * 0.42f;
                        setFill(0xFF000000, 0.95f * g);
                        tmp.set(lx - lw * 0.6f, ly - lw * 0.15f, lx + lw * 0.6f, ly + lw * 0.75f); cv.drawRoundRect(tmp, lw * 0.15f, lw * 0.15f, fill);
                        setLine(0xFFFFFFFF, 0.95f * g);
                        cv.drawRoundRect(tmp, lw * 0.15f, lw * 0.15f, line);
                        tmp.set(lx - lw * 0.38f, ly - lw * 0.7f, lx + lw * 0.38f, ly + lw * 0.1f); cv.drawArc(tmp, 180, 180, false, line);
                    }
                    break;
                }
                case Control.PILL: {
                    float hw = halfW(c), hh = halfH(c);
                    setFill(c.pressed ? PLAYER_COLOR[shownPlayer()] : 0xFF000000, 0.8f * g);
                    tmp.set(x - hw, y - hh, x + hw, y + hh); cv.drawRoundRect(tmp, hh, hh, fill);
                    setLine(0xFFFFFFFF, 0.7f * g);
                    cv.drawRoundRect(tmp, hh, hh, line);
                    drawPillFace(cv, c, x, y, hw, hh, a(0.9f * g));
                    break;
                }
            }
        }

        // Buttons show what the game's own prompts show: the key they send, in the game's key
        // lettering (P1 Z / X, P2 DEL / END, or whatever is bound in Keyboard Config), big and
        // centred. Players without keys (P3/P4 on the touch pad) get the game's I / II instead.
        static final int[][] DEFAULT_VK = { { 38, 40, 37, 39, 90, 88, 68 }, { 101, 98, 97, 99, 46, 35, 33 } };   // up..right, A(II), B(I), start

        int shownVk(int act) {
            if (act == ACT_BACK) return 27;
            if (keyCodeFor(player, act) == 0) return -1;   // no key: goes out on the touch pad
            int vk = boundVk(player, act);
            if (vk < 0 && player < DEFAULT_VK.length) vk = DEFAULT_VK[player][act == ACT_A ? 4 : act == ACT_B ? 5 : act == ACT_START ? 6 : 0];
            return vk;
        }

        boolean[][] keyGlyph(int vk) {
            boolean[][] g = keyIcons != null && vk > 2 ? keyIcons.glyph(vk) : null;
            return g != null && g.length > 0 ? g : null;
        }

        void drawButtonFace(Canvas cv, Control c, float x, float y, float r, int alpha) {
            int act = c.keys[0];
            boolean[][] g = keyGlyph(shownVk(act));
            if (g == null && vInputs != null) g = vInputs.glyph(act == ACT_A ? VI_BUTTON2 : VI_BUTTON1);
            if (g == null || g.length == 0) { txt(cv, act == ACT_A ? "II" : "I", x, y, r * 0.8f, 0xFFFFFFFF, alpha); return; }
            drawGlyph(cv, g, x, y, r * 1.25f, r * 0.9f, alpha);
        }

        void drawPillFace(Canvas cv, Control c, float x, float y, float hw, float hh, int alpha) {
            int act = c.keys[0];
            boolean[][] g = keyGlyph(shownVk(act));
            if (g == null && act == ACT_START && vInputs != null) g = vInputs.glyph(VI_SYS);
            if (g == null || g.length == 0) { txt(cv, c.label, x, y, hh * 0.85f, 0xFFFFFFFF, alpha); return; }
            drawGlyph(cv, g, x, y, hw * 1.45f, hh * 1.15f, alpha);
        }

        // pixel glyph, whole-pixel cells as big as fit in maxW x maxH, centred on (x, y)
        void drawGlyph(Canvas cv, boolean[][] g, float x, float y, float maxW, float maxH, int alpha) {
            int gh = g.length, gw = g[0].length;
            float cell = Math.max(1, (float) Math.floor(Math.min(maxW / gw, maxH / gh)));
            float left = Math.round(x - gw * cell / 2f), top = Math.round(y - gh * cell / 2f);
            fill.setStyle(Paint.Style.FILL); fill.setColor(0xFFFFFFFF); fill.setAlpha(alpha);
            for (int r = 0; r < gh; r++)
                for (int k = 0; k < gw; k++)
                    if (g[r][k]) cv.drawRect(left + k * cell, top + r * cell, left + (k + 1) * cell, top + (r + 1) * cell, fill);
        }

        void drawBanner(Canvas cv, float u) {
            float th = u * 0.06f;   // shrink long messages to fit the screen
            while (font_w(banner, th) + u * 0.08f > getWidth() * 0.96f && th > u * 0.015f) th *= 0.9f;
            float w = font_w(banner, th) + u * 0.08f, h = Math.max(u * 0.07f, th * 2f);
            float cx = getWidth() / 2f, cy = getHeight() * 0.18f;
            fill.setStyle(Paint.Style.FILL); fill.setColor(bannerColor != 0 ? bannerColor : 0xFF2A2838); fill.setAlpha(235);
            tmp.set(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2); cv.drawRoundRect(tmp, h * 0.25f, h * 0.25f, fill);
            line.setColor(0xFFFFFFFF); line.setAlpha(255); cv.drawRoundRect(tmp, h * 0.25f, h * 0.25f, line);
            txt(cv, banner, cx, cy, th, 0xFFFFFFFF, 255);
        }

        void tri(Canvas cv, float x1, float y1, float x2, float y2, float x3, float y3) {
            android.graphics.Path p = new android.graphics.Path();
            p.moveTo(x1, y1); p.lineTo(x2, y2); p.lineTo(x3, y3); p.close();
            cv.drawPath(p, fill);
        }

        // ---- save states ----
        // A list of saves, newest first: SAVE NOW adds one. Each is PFO's replay of the session so
        // far (libPFO savestate.inc) plus a picture of the screen and what was going on (the game,
        // online or not, who was playing, how long into the game). Tap one to see it; LOAD goes
        // back to it. Saving needs a solo session (START SOLO PLAY) or, later, an online game.
        static final String STATE_DIR = "/sdcard/Android/data/com.unofficial.ufo50/files/savestates";
        boolean statesOpen = false;
        int stateStatus = 0, lastResultSeq = -1, pendingSlot = 0, pendingWhat = 0;
        IconStrip gameIcons;
        Control statesBtn;
        final RectF statesRect = new RectF(), listRect = new RectF();
        final List<RectF> stateHits = new ArrayList<>();
        final List<Integer> stateCodes = new ArrayList<>();
        static final int ST_CLOSE = -10, ST_SOLO = -11, ST_SAVE = -12, ST_LOAD = -13, ST_DELETE = -14;   // >= 0: a list row (save id)
        final List<Integer> saves = new ArrayList<>();                 // save ids, newest first
        final java.util.HashMap<Integer, org.json.JSONObject> saveInfo = new java.util.HashMap<>();
        final java.util.HashMap<Integer, Bitmap> saveThumb = new java.util.HashMap<>();
        int selectedSave = -1;
        float listScroll = 0, rowH = 1, dragStartY, dragStartScroll;
        boolean dragging = false, stateGesture = false;   // stateGesture: this touch started on the open menu
        long deleteArmedUntil = 0;

        // "auto save when I pause": opening the game's pause menu in solo play saves into one auto
        // slot (id 0, replaced each time). Not right after a load, which can land on a paused game.
        static final int AUTO_SLOT = 0;
        static final int ST_AUTO = -15;
        boolean autoSavePause = false, pendingAuto = false;
        long noAutoUntil = 0;
        boolean gamePaused() { return (stateStatus & 8) != 0; }

        void maybeAutoSave(int oldStatus, int st) {
            long now = android.os.SystemClock.uptimeMillis();
            if ((oldStatus & 4) != 0 && (st & 4) == 0) noAutoUntil = now + 2500;   // a load just finished
            boolean pausedNow = (st & 8) != 0, pausedBefore = (oldStatus & 8) != 0;
            if (!autoSavePause || !pausedNow || pausedBefore || now < noAutoUntil) return;
            if ((st & 2) == 0 || (st & 4) != 0 || pendingWhat == 2 && pendingAuto) return;
            pendingAuto = true;
            if ((st & 1) == 0) {
                // online: everyone saves (libPFO sends one SAVE; the first per pause counts)
                pendingSlot = -1; pendingWhat = 2;
                nativeStateRequest(2, -2);
                return;
            }
            saveState(AUTO_SLOT);
        }

        // ---- online loads: the vote ----
        // Online, LOAD proposes the save to everyone with a seat; it loads (on every device, on the
        // same frame) once they all accept. Ours: a line saying who we're waiting for. Someone
        // else's: a panel with the save and ACCEPT / DECLINE, declined by itself after 15 s.
        static final int VOTE_SECONDS = 15, PROPOSAL_SECONDS = 20;
        int[] vote = new int[11];
        int lastVoteSeq = -1;
        final RectF voteYes = new RectF(), voteNo = new RectF();

        // ---- rejoining: the app was closed (or crashed) in a match that's still waiting for us ----
        String rejoinOffer = "", rejoinDeclined = "";
        long lastOfferCheck = 0;
        final RectF rejoinYes = new RectF(), rejoinNo = new RectF();

        boolean rejoinShown() { return !rejoinOffer.isEmpty() && !restoring() && (netStatus & 64) == 0; }

        void pollRejoin() {
            long now = android.os.SystemClock.uptimeMillis();
            if (now - lastOfferCheck > 1000) {
                lastOfferCheck = now;
                String o = "";
                try { o = (netStatus & 64) == 0 ? nativeRejoinOffer() : ""; } catch (Throwable t) { }
                // the same match we said no to stays dismissed (the seat and names identify it)
                String key = o.contains("|") ? o.substring(o.indexOf('|')) : "";
                if (!key.isEmpty() && key.equals(rejoinDeclined)) o = "";
                if (!o.equals(rejoinOffer)) { rejoinOffer = o; invalidate(); }
            }
            int r = 0;
            try { r = nativeRejoinResult(); } catch (Throwable t) { }
            if (r == 1) showBanner("BACK IN THE MATCH", 2200);
            else if (r == -1) showBanner("COULDN'T REJOIN THE MATCH", 2500, ATTN);
        }

        void rejoinTap(float x, float y) {
            if (rejoinYes.contains(x, y)) {
                try { nativeStateRequest(20, 0); } catch (Throwable t) { }
                rejoinOffer = "";
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
            } else if (rejoinNo.contains(x, y)) {
                rejoinDeclined = rejoinOffer.contains("|") ? rejoinOffer.substring(rejoinOffer.indexOf('|')) : "";
                try { nativeStateRequest(21, 0); } catch (Throwable t) { }
                rejoinOffer = "";
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
            }
            invalidate();
        }

        void drawRejoin(Canvas cv, float u) {
            String[] f = rejoinOffer.split("\\|", -1);
            int seat = 0;
            try { seat = Integer.parseInt(f[1]); } catch (Throwable t) { }
            StringBuilder others = new StringBuilder();
            for (int i = 2; i < f.length; i++) if (i - 2 != seat && !f[i].isEmpty()) others.append(others.length() > 0 ? ", " : "").append(f[i].toUpperCase());
            float pw = Math.min(getWidth() * 0.8f, u * 1.2f), ph = u * 0.42f, gap = u * 0.02f;
            float left = (getWidth() - pw) / 2f, top = (getHeight() - ph) / 2f;
            solid(fill, 0xFF000000, 0.55f); cv.drawRect(0, 0, getWidth(), getHeight(), fill);
            tmp.set(left, top, left + pw, top + ph);
            solid(fill, 0xFF000000, 0.95f); cv.drawRoundRect(tmp, gap, gap, fill);
            solid(line, 0xFFFFFFFF, 0.9f); cv.drawRoundRect(tmp, gap, gap, line);
            float lineH = u * 0.034f, y = top + gap + lineH * 1.2f;
            txt(cv, "REJOIN YOUR MATCH?", left + pw / 2f, y, u * 0.045f, 0xFFFFFFFF, 255);
            y += lineH * 1.6f;
            String who = "YOU WERE P" + (seat + 1) + (others.length() > 0 ? " WITH " + others : "");
            float sz = lineH * 0.85f;
            while (font_w(who, sz) > pw - gap * 2 && sz > lineH * 0.4f) sz *= 0.9f;
            txt(cv, who, left + pw / 2f, y, sz, seat < PLAYER_COLOR.length ? PLAYER_COLOR[seat] : 0xFFFFFFFF, 255);
            y += lineH * 1.2f;
            txt(cv, "THEY'RE WAITING FOR YOU", left + pw / 2f, y, lineH * 0.75f, 0xFFC8C8C8, 255);
            float btnH = u * 0.075f, bw = (pw - gap * 3) / 2f, by = top + ph - gap - btnH;
            rejoinNo.set(left + gap, by, left + gap + bw, by + btnH);
            rejoinYes.set(rejoinNo.right + gap, by, left + pw - gap, by + btnH);
            stateHits.clear(); stateCodes.clear();
            button(cv, rejoinNo, "NO", BTN_NEUTRAL, true, btnH, ST_CLOSE);
            button(cv, rejoinYes, "REJOIN", BTN_PRIMARY, true, btnH, ST_CLOSE);
        }

        // ---- a player's connection dropped: the match is frozen until they're back (or time runs out) ----
        int waitMask = 0, lastRejoinSeq = -1, waitedFor = 0;

        void pollWaiting() {
            int[] w;
            try { w = nativeWaiting(); } catch (Throwable t) { return; }
            if (lastRejoinSeq < 0) lastRejoinSeq = w[2];
            if (w[0] != 0) {
                waitedFor |= w[0];
                StringBuilder b = new StringBuilder();
                for (int p = 0; p < 8; p++) if ((w[0] & (1 << p)) != 0) b.append(b.length() > 0 ? " " : "").append("P").append(p + 1);
                int sec = w[1];
                // the one thing that needs attention: shown until it's over (a countdown once they're known gone)
                showBanner("WAITING FOR " + b + (sec >= 0 ? " - " + sec / 60 + ":" + (sec % 60 < 10 ? "0" : "") + sec % 60 : ""), 600, ATTN);
            } else if (w[2] != lastRejoinSeq && waitedFor != 0) {
                StringBuilder b = new StringBuilder();
                for (int p = 0; p < 8; p++) if ((waitedFor & (1 << p)) != 0) b.append(b.length() > 0 ? " " : "").append("P").append(p + 1);
                showBanner(b + " IS BACK", 2000);
                waitedFor = 0;
            } else if (waitMask != 0 && w[0] == 0) {
                // the wait ended without them: the match is over
                if ((netStatus & 64) == 0 && waitedFor != 0) {
                    StringBuilder b = new StringBuilder();
                    for (int p = 0; p < 8; p++) if ((waitedFor & (1 << p)) != 0) b.append(b.length() > 0 ? " " : "").append("P").append(p + 1);
                    showBanner(b + " DIDN'T COME BACK - MATCH ENDED", 3500, ATTN);
                }
                waitedFor = 0;
            }
            lastRejoinSeq = w[2];
            waitMask = w[0];
        }

        int matchSave(int slot) { try { return nativeMatchSave(slot); } catch (Throwable t) { return 0; } }
        boolean voteShown() { return vote[0] != 0 && vote[6] == 0 && onlineSeat >= 0; }

        void pollVote() {
            if ((netStatus & 64) == 0 || soloSession()) { if (vote[0] != 0) { vote = new int[11]; invalidate(); } return; }
            int[] v;
            try { v = nativeVote(); } catch (Throwable t) { return; }
            boolean changed = !java.util.Arrays.equals(v, vote);
            vote = v;
            if (lastVoteSeq < 0) lastVoteSeq = v[9];
            if (v[9] != lastVoteSeq) {
                lastVoteSeq = v[9];
                String who = v[8] >= 0 ? "P" + (v[8] + 1) : "SOMEONE";
                if (v[7] == 2) showBanner(who + " SAID NO - NOT LOADED", 2500, ATTN);
                else if (v[7] == 3) showBanner(who + " CAN'T LOAD THAT SAVE", 2500, ATTN);
                else if (v[7] == 4) showBanner("LOAD CANCELLED", 2000);
            }
            if (v[0] != 0) {
                boolean mine = v[2] == onlineSeat;
                // no answer in time: ours is withdrawn, theirs is declined
                if (mine && v[10] >= PROPOSAL_SECONDS) { try { nativeVoteAnswer(false); } catch (Throwable t) { } }
                else if (!mine && v[6] == 0 && onlineSeat >= 0 && v[10] >= VOTE_SECONDS) { try { nativeVoteAnswer(false); } catch (Throwable t) { } }
                if (statesOpen && v[6] == 0 && !mine) statesOpen = false;   // the question goes first
                invalidate();
            } else if (changed) invalidate();
        }

        void voteTap(float x, float y) {
            if (voteYes.contains(x, y)) { try { nativeVoteAnswer(true); } catch (Throwable t) { } performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP); }
            else if (voteNo.contains(x, y)) { try { nativeVoteAnswer(false); } catch (Throwable t) { } performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP); }
            invalidate();
        }

        // seats that haven't accepted yet: "P2 P3"
        String waitingFor() {
            StringBuilder b = new StringBuilder();
            for (int p = 0; p < 8; p++) if ((vote[4] & (1 << p)) != 0 && (vote[3] & (1 << p)) == 0) b.append(b.length() > 0 ? " " : "").append("P").append(p + 1);
            return b.toString();
        }

        void drawVote(Canvas cv, float u) {
            if (!voteShown()) {
                // we've answered (or proposed): who are we waiting for
                String w = waitingFor();
                String t = w.isEmpty() ? "LOADING..." : "LOAD ASKED - WAITING FOR " + w;
                float th = u * 0.04f;
                float wd = font_w(t, th) + u * 0.06f, h = th * 2f;
                float cx = getWidth() / 2f, cy = getHeight() * 0.18f;
                solid(fill, 0xFF2A2838, 0.92f); tmp.set(cx - wd / 2, cy - h / 2, cx + wd / 2, cy + h / 2); cv.drawRoundRect(tmp, h * 0.25f, h * 0.25f, fill);
                solid(line, 0xFFFFFFFF, 0.8f); cv.drawRoundRect(tmp, h * 0.25f, h * 0.25f, line);
                txt(cv, t, cx, cy, th, 0xFFFFFFFF, 255);
                postInvalidateDelayed(500);
                return;
            }
            // someone else's proposal: what it is, and our answer
            float pw = Math.min(getWidth() * 0.8f, u * 1.3f), ph = Math.min(getHeight() * 0.86f, u * 0.82f), gap = u * 0.02f;
            float left = (getWidth() - pw) / 2f, top = (getHeight() - ph) / 2f;
            solid(fill, 0xFF000000, 0.55f); cv.drawRect(0, 0, getWidth(), getHeight(), fill);
            tmp.set(left, top, left + pw, top + ph);
            solid(fill, 0xFF000000, 0.95f); cv.drawRoundRect(tmp, gap, gap, fill);
            solid(line, 0xFFFFFFFF, 0.9f); cv.drawRoundRect(tmp, gap, gap, line);
            float lineH = u * 0.034f, y = top + gap + lineH;
            int prop = vote[2];
            String who = prop >= 0 ? "P" + (prop + 1) : "A PLAYER";
            String head = who + " WANTS TO LOAD";
            float hsz = u * 0.045f;
            float hw = font_w(head, hsz);
            txt(cv, head, left + pw / 2f, y, hsz, prop >= 0 ? PLAYER_COLOR[Math.min(prop, PLAYER_COLOR.length - 1)] : 0xFFFFFFFF, 255);
            y += hsz * 1.4f;
            int slot = vote[5];
            org.json.JSONObject o = slot >= 0 ? saveInfo.get(slot) : null;
            if (o == null && slot >= 0) { o = readInfo(slot); if (o != null) saveInfo.put(slot, o); }
            float btnH = u * 0.075f;
            float picBottom = top + ph - gap * 3 - btnH - lineH * 2.6f;
            Bitmap b = slot >= 0 ? thumbOf(slot) : null;
            float th = Math.max(u * 0.1f, picBottom - y), tw = th * 384f / 216f;
            if (tw > pw - gap * 4) { tw = pw - gap * 4; th = tw * 216f / 384f; }
            RectF pic = new RectF(left + (pw - tw) / 2f, y, left + (pw + tw) / 2f, y + th);
            if (b != null) { Paint pp = new Paint(); pp.setFilterBitmap(false); cv.drawBitmap(b, null, pic, pp); }
            else { solid(fill, 0xFF101028, 1f); cv.drawRect(pic, fill); }
            solid(line, 0xFFFFFFFF, 0.6f); cv.drawRect(pic, line);
            y = pic.bottom + lineH * 1.1f;
            if (o != null) {
                String t = titleOf(o);
                txt(cv, t, left + pw / 2f, y, lineH * 1.1f, 0xFFFFFFFF, 255);
                y += lineH * 1.25f;
                long gf = o.optLong("gameFrames", 0);
                txt(cv, (gf > 0 ? clock(gf) + " INTO THE GAME" : "IN THE LIBRARY") + "  -  SAVED " + when(o.optLong("savedAt", 0)), left + pw / 2f, y, lineH * 0.75f, 0xFFC8C8C8, 255);
            }
            int left2 = Math.max(0, VOTE_SECONDS - vote[10]);
            float bw = (pw - gap * 3) / 2f, by = top + ph - gap - btnH;
            voteNo.set(left + gap, by, left + gap + bw, by + btnH);
            voteYes.set(voteNo.right + gap, by, left + pw - gap, by + btnH);
            stateHits.clear(); stateCodes.clear();
            button(cv, voteNo, "DECLINE (" + left2 + ")", BTN_NEUTRAL, true, btnH, ST_CLOSE);
            button(cv, voteYes, "ACCEPT", BTN_PRIMARY, true, btnH, ST_CLOSE);
            postInvalidateDelayed(500);
        }

        boolean soloSession() { return (stateStatus & 1) != 0; }
        boolean canSaveState() { return (stateStatus & 2) != 0; }
        boolean restoring() { return (stateStatus & 4) != 0; }

        java.io.File saveFile(int id, String ext) { return new java.io.File(STATE_DIR, "slot" + id + ext); }

        static int idOf(String name, String ext) {
            if (!name.startsWith("slot") || !name.endsWith(ext)) return -1;
            try { return Integer.parseInt(name.substring(4, name.length() - ext.length())); } catch (Throwable t) { return -1; }
        }

        org.json.JSONObject readInfo(int id) {
            try {
                java.io.File f = saveFile(id, ".json");
                byte[] b = new byte[(int) f.length()];
                java.io.FileInputStream in = new java.io.FileInputStream(f);
                int n = 0; while (n < b.length) { int k = in.read(b, n, b.length - n); if (k < 0) break; n += k; }
                in.close();
                return new org.json.JSONObject(new String(b, 0, n, "UTF-8"));
            } catch (Throwable t) { return null; }
        }

        void refreshSaves() {
            saves.clear();
            String[] names = new java.io.File(STATE_DIR).list();
            if (names != null) for (String n : names) {
                int id = idOf(n, ".pfostate");
                if (id < 0 || !saveFile(id, ".json").exists()) continue;
                if (!saveInfo.containsKey(id)) { org.json.JSONObject o = readInfo(id); if (o == null) continue; saveInfo.put(id, o); }
                saves.add(id);
            }
            java.util.Collections.sort(saves, new java.util.Comparator<Integer>() {
                @Override public int compare(Integer a, Integer b) { return Long.compare(saveInfo.get(b).optLong("savedAt", 0), saveInfo.get(a).optLong("savedAt", 0)); }
            });
            if (!saves.contains(selectedSave)) selectedSave = saves.isEmpty() ? -1 : saves.get(0);
        }

        Bitmap thumbOf(int id) {
            if (!saveThumb.containsKey(id)) {
                java.io.File p = saveFile(id, ".png");
                // a picture older than the save belongs to whatever was in this slot before: show none
                org.json.JSONObject o = saveInfo.get(id);
                long savedAt = o != null ? o.optLong("savedAt", 0) : 0;
                boolean current = p.exists() && p.lastModified() / 1000 + 60 >= savedAt;
                saveThumb.put(id, current ? BitmapFactory.decodeFile(p.getPath()) : null);
            }
            return saveThumb.get(id);
        }

        int nextSaveId() {
            int max = 0;
            String[] names = new java.io.File(STATE_DIR).list();
            if (names != null) for (String n : names) { int id = idOf(n, ".pfostate"); if (id > max) max = id; }
            return max + 1;
        }

        void openStates() {
            releaseAll();
            stateGesture = false;
            refreshSaves();
            listScroll = 0;
            statesOpen = true;
            invalidate();
        }

        // the game's own drawing surface (the overlay isn't part of it, so screenshots are clean)
        android.view.SurfaceView findSurface(View v) {
            if (v instanceof android.view.SurfaceView) return (android.view.SurfaceView) v;
            if (v instanceof ViewGroup) {
                ViewGroup g = (ViewGroup) v;
                for (int i = 0; i < g.getChildCount(); i++) { android.view.SurfaceView s = findSurface(g.getChildAt(i)); if (s != null) return s; }
            }
            return null;
        }

        // Picture of the screen as it is now, cropped to where the game draws (pfo_android_screen)
        // and kept small next to the save; then ask libPFO to save.
        // auto save on pause: the pause menu would be in the picture, so a recent picture from before
        // the pause is kept (refreshed every 2 s while auto save is on and the game isn't paused)
        Bitmap cleanFrame;
        long cleanFrameAt = 0;

        void keepCleanFrame() {
            long now = android.os.SystemClock.uptimeMillis();
            if (!autoSavePause || (!soloSession() && (netStatus & 64) == 0) || gamePaused() || restoring() || statesOpen || now - cleanFrameAt < 2000) return;
            final android.view.SurfaceView sv = findSurface(getRootView());
            if (sv == null || android.os.Build.VERSION.SDK_INT < 24 || sv.getWidth() <= 0) return;
            cleanFrameAt = now;
            if (cleanFrame == null || cleanFrame.getWidth() != sv.getWidth() || cleanFrame.getHeight() != sv.getHeight())
                cleanFrame = Bitmap.createBitmap(sv.getWidth(), sv.getHeight(), Bitmap.Config.ARGB_8888);
            final Bitmap into = cleanFrame;
            try {
                android.view.PixelCopy.request(sv, into, new android.view.PixelCopy.OnPixelCopyFinishedListener() {
                    @Override public void onPixelCopyFinished(int result) { if (result != android.view.PixelCopy.SUCCESS) cleanFrameAt = 0; }
                }, handler);
            } catch (Throwable t) { }
        }

        void saveState(final int id) {
            pendingSlot = id; pendingWhat = 2;
            if (pendingAuto && cleanFrame != null && cleanFrameAt > 0 && android.os.SystemClock.uptimeMillis() - cleanFrameAt < 6000) {
                writeThumb(id, cleanFrame);   // from just before the pause
                nativeStateRequest(2, id);
                return;
            }
            final android.view.SurfaceView sv = findSurface(getRootView());
            if (sv == null || android.os.Build.VERSION.SDK_INT < 24 || sv.getWidth() <= 0) { nativeStateRequest(2, id); return; }
            final Bitmap full = Bitmap.createBitmap(sv.getWidth(), sv.getHeight(), Bitmap.Config.ARGB_8888);
            try {
                android.view.PixelCopy.request(sv, full, new android.view.PixelCopy.OnPixelCopyFinishedListener() {
                    @Override public void onPixelCopyFinished(int result) {
                        if (result == android.view.PixelCopy.SUCCESS) writeThumb(id, full);
                        nativeStateRequest(2, id);
                    }
                }, handler);
            } catch (Throwable t) { nativeStateRequest(2, id); }
        }

        // a picture for a save made without one (online: the save happened on a command)
        void thumbNow(final int id) {
            if (id == AUTO_SLOT && cleanFrame != null && cleanFrameAt > 0 && android.os.SystemClock.uptimeMillis() - cleanFrameAt < 6000) {
                writeThumb(id, cleanFrame);   // from before the pause
                java.io.File fresh = saveFile(id, ".png.new");
                if (fresh.exists()) { saveFile(id, ".png").delete(); fresh.renameTo(saveFile(id, ".png")); }
                saveThumb.remove(id);
                invalidate();
                return;
            }
            final android.view.SurfaceView sv = findSurface(getRootView());
            if (sv == null || android.os.Build.VERSION.SDK_INT < 24 || sv.getWidth() <= 0) return;
            final Bitmap full = Bitmap.createBitmap(sv.getWidth(), sv.getHeight(), Bitmap.Config.ARGB_8888);
            try {
                android.view.PixelCopy.request(sv, full, new android.view.PixelCopy.OnPixelCopyFinishedListener() {
                    @Override public void onPixelCopyFinished(int result) {
                        if (result != android.view.PixelCopy.SUCCESS) return;
                        writeThumb(id, full);
                        java.io.File fresh = saveFile(id, ".png.new");
                        if (fresh.exists()) { saveFile(id, ".png").delete(); fresh.renameTo(saveFile(id, ".png")); }
                        saveThumb.remove(id);
                        invalidate();
                    }
                }, handler);
            } catch (Throwable t) { }
        }

        void writeThumb(int id, Bitmap full) {
            try {
                int[] r = nativeScreenRect();
                int ww = r[4] > 0 ? r[4] : full.getWidth(), wh = r[5] > 0 ? r[5] : full.getHeight();
                float kx = full.getWidth() / (float) ww, ky = full.getHeight() / (float) wh;
                Rect src = new Rect(Math.round(r[0] * kx), Math.round(r[1] * ky), Math.round((r[0] + r[2]) * kx), Math.round((r[1] + r[3]) * ky));
                if (r[2] <= 0 || r[3] <= 0 || !src.intersect(0, 0, full.getWidth(), full.getHeight())) src.set(0, 0, full.getWidth(), full.getHeight());
                Bitmap thumb = Bitmap.createBitmap(384, 216, Bitmap.Config.ARGB_8888);
                Paint p = new Paint(); p.setFilterBitmap(true);
                new Canvas(thumb).drawBitmap(full, src, new Rect(0, 0, 384, 216), p);
                new java.io.File(STATE_DIR).mkdirs();
                java.io.FileOutputStream out = new java.io.FileOutputStream(saveFile(id, ".png.new"));
                thumb.compress(Bitmap.CompressFormat.PNG, 100, out);
                out.close();
            } catch (Throwable t) { }
        }

        void pollStates() {
            int st = 0;
            try { st = nativeStateStatus(); } catch (Throwable t) { return; }
            if (st != stateStatus) { int old = stateStatus; stateStatus = st; maybeAutoSave(old, st); invalidate(); }
            int seq = (st >> 24) & 0x7f, result = (byte) ((st >> 16) & 0xff);
            if (lastResultSeq < 0) { lastResultSeq = seq; return; }
            if (seq == lastResultSeq) return;
            lastResultSeq = seq;
            int id = pendingSlot;
            if (result == 1) {
                // online saves (ours or another player's) go wherever libPFO put them
                int last = -1;
                try { last = nativeLastSavedSlot(); } catch (Throwable t) { }
                if (last >= 0 && (id < 0 || (netStatus & 64) != 0 && !soloSession())) id = last;
            }
            java.io.File fresh = saveFile(id, ".png.new");
            if (result == 1) {
                if (!fresh.exists() && (!saveFile(id, ".png").exists() || id == AUTO_SLOT)) thumbNow(id);
                if (fresh.exists()) { saveFile(id, ".png").delete(); fresh.renameTo(saveFile(id, ".png")); }
                saveInfo.remove(id); saveThumb.remove(id);
                selectedSave = id;
                refreshSaves();
                notePill(pendingAuto ? "AUTO SAVED" : "SAVED", 2000);
                pendingAuto = false;
            } else {
                fresh.delete();
                if (pendingAuto && pendingWhat == 2) { pendingAuto = false; return; }   // an auto save that couldn't happen: say nothing
                if (result == 3) notePill("LOADED", 2000);
                else if (result == -3) showBanner("LOADED - BUT IT DIDN'T CHECK OUT", 3000, ATTN);
                else if (result == -2) showBanner("THAT SAVE CAN'T BE READ", 1800);
                else if (result == -4) { }   // a rejoin that didn't work: pollRejoin says so
                else if (result == -5) {
                    int n = 0;
                    try { n = nativeOfferRefused(); } catch (Throwable t) { }
                    showBanner("THAT SAVE IS FOR " + n + (n == 1 ? " PLAYER" : " PLAYERS"), 2200);
                }
                else if (result == -1) showBanner(pendingWhat == 2 ? "START SOLO PLAY TO SAVE" : "NOT DURING AN ONLINE GAME", 2000);
            }
        }

        static String titleOf(org.json.JSONObject o) {
            String t = o.optString("title", "");
            int game = o.optInt("game", 0);
            return (t.isEmpty() ? (game == 0 ? "THE LIBRARY" : "GAME " + game) : t).toUpperCase();
        }

        // what kind of play it was: "SINGLE PLAYER - OFFLINE", "3 PLAYERS - ONLINE"
        static String kindOf(org.json.JSONObject o) {
            boolean online = o.optBoolean("online", o.optInt("clients", 1) > 1);
            int players = Math.max(o.optInt("clients", 1), o.optInt("numPlayers", 1));
            return (players <= 1 ? "SINGLE PLAYER" : players + " PLAYERS") + (online ? "  -  ONLINE" : "  -  OFFLINE");
        }

        void stateDown(float x, float y) {
            stateGesture = true;
            dragging = false;
            dragStartY = y; dragStartScroll = listScroll;
        }

        void stateMove(float y) {
            if (!dragging && Math.abs(y - dragStartY) > rowH * 0.3f) dragging = true;
            if (dragging) {
                float maxScroll = Math.max(0, saves.size() * rowH - listRect.height());
                listScroll = Math.max(0, Math.min(maxScroll, dragStartScroll - (y - dragStartY)));
                invalidate();
            }
        }

        // a tap (not a drag) on the menu
        void stateTap(float x, float y) {
            if (dragging) return;
            if (!statesRect.contains(x, y)) { statesOpen = false; invalidate(); return; }
            for (int k = 0; k < stateHits.size(); k++) {
                if (!stateHits.get(k).contains(x, y)) continue;
                int code = stateCodes.get(k);
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
                if (code >= 0) { selectedSave = code; deleteArmedUntil = 0; }
                else if (code == ST_CLOSE) statesOpen = false;
                else if (code == ST_AUTO) {
                    autoSavePause = !autoSavePause;
                    prefs.edit().putBoolean("autosave_pause", autoSavePause).apply();
                }
                else if (code == ST_SOLO) {
                    statesOpen = false;
                    nativeStateRequest(1, 0);   // the pill says SOLO: SAVES ON when it starts
                } else if (code == ST_SAVE) {
                    boolean onlineNow = (netStatus & 64) != 0 && !soloSession();
                    if (!canSaveState()) { showBanner(onlineNow ? "ONLY PLAYERS WITH A SEAT CAN SAVE" : soloSession() ? "CAN'T SAVE RIGHT NOW" : "START SOLO PLAY TO SAVE", 1800); break; }
                    statesOpen = false;
                    pendingAuto = false;
                    if (onlineNow) {
                        // online every device saves on the same frame (libPFO sends a command and picks the
                        // slot); the picture is taken when the save is done
                        pendingSlot = -1; pendingWhat = 2;
                        nativeStateRequest(2, -1);
                    } else saveState(nextSaveId());
                } else if (code == ST_LOAD && selectedSave >= 0) {
                    boolean onlineNow = (netStatus & 64) != 0 && !soloSession();
                    if (onlineNow && onlineSeat < 0) { showBanner("ONLY PLAYERS WITH A SEAT CAN LOAD", 1800); break; }
                    statesOpen = false;
                    pendingSlot = selectedSave; pendingWhat = 3; pendingAuto = false;
                    nativeStateRequest(3, selectedSave);
                } else if (code == ST_DELETE && selectedSave >= 0) {
                    long now = android.os.SystemClock.uptimeMillis();
                    if (now > deleteArmedUntil) { deleteArmedUntil = now + 2500; postDelayed(new Runnable() { @Override public void run() { invalidate(); } }, 2600); }
                    else {
                        for (String ext : new String[] { ".pfostate", ".json", ".png" }) saveFile(selectedSave, ext).delete();
                        saveInfo.remove(selectedSave); saveThumb.remove(selectedSave);
                        deleteArmedUntil = 0;
                        refreshSaves();
                    }
                }
                break;
            }
            invalidate();
        }

        static String clock(long frames) {
            long s = frames / 60, h = s / 3600, m = (s / 60) % 60;
            s %= 60;
            return h > 0 ? h + ":" + (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s : m + ":" + (s < 10 ? "0" : "") + s;
        }

        static String when(long unix) {
            java.util.Calendar c = java.util.Calendar.getInstance(), now = java.util.Calendar.getInstance();
            c.setTimeInMillis(unix * 1000);
            String hm = String.format(java.util.Locale.US, "%d:%02d", c.get(java.util.Calendar.HOUR_OF_DAY), c.get(java.util.Calendar.MINUTE));
            if (c.get(java.util.Calendar.YEAR) == now.get(java.util.Calendar.YEAR) && c.get(java.util.Calendar.DAY_OF_YEAR) == now.get(java.util.Calendar.DAY_OF_YEAR)) return "TODAY " + hm;
            String[] mon = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
            return mon[c.get(java.util.Calendar.MONTH)] + " " + c.get(java.util.Calendar.DAY_OF_MONTH) + " " + c.get(java.util.Calendar.YEAR) + " " + hm;
        }

        // color 0 = destructive: an outline in the danger colour until it's armed
        void button(Canvas cv, RectF r, String label, int color, boolean on, float h, int code) {
            if (color == 0) {
                solid(fill, 0xFF000000, 0.4f); cv.drawRoundRect(r, h * 0.2f, h * 0.2f, fill);
                Paint o = new Paint(line); o.setStrokeWidth(Math.max(3f, h * 0.06f)); o.setColor(BTN_DANGER); o.setAlpha(255);
                cv.drawRoundRect(r, h * 0.2f, h * 0.2f, o);
            } else {
                solid(fill, on ? color : 0xFF303050, 1f); cv.drawRoundRect(r, h * 0.2f, h * 0.2f, fill);
                if (color == BTN_NEUTRAL && on) { solid(line, 0xFFFFFFFF, 0.6f); cv.drawRoundRect(r, h * 0.2f, h * 0.2f, line); }
            }
            float sz = h * 0.42f;
            while (font_w(label, sz) > r.width() * 0.92f && sz > h * 0.2f) sz *= 0.9f;
            txt(cv, label, r.centerX(), r.centerY(), sz, color == 0 ? 0xFFFF8A80 : 0xFFFFFFFF, on ? 255 : 110);
            stateHits.add(r); stateCodes.add(code);
        }

        void gameIcon(Canvas cv, org.json.JSONObject o, float x, float y, float size) {
            int icon = o.optInt("icon", 0);
            if (gameIcons == null || icon <= 0 || !gameIcons.has((icon - 1) * 2)) return;
            float px = Math.max(1, Math.round(size / 24f));
            gameIcons.draw(cv, (icon - 1) * 2, x, y, px, 255);
        }

        void drawStates(Canvas cv, float u) {
            stateHits.clear(); stateCodes.clear();
            float pw = Math.min(getWidth() * 0.94f, u * 1.95f), ph = Math.min(getHeight() * 0.94f, u * 1.0f), gap = u * 0.02f;
            float left = (getWidth() - pw) / 2f, top = (getHeight() - ph) / 2f;
            statesRect.set(left, top, left + pw, top + ph);
            solid(fill, 0xFF000000, 0.95f); cv.drawRoundRect(statesRect, gap, gap, fill);
            solid(line, 0xFFFFFFFF, 0.9f); cv.drawRoundRect(statesRect, gap, gap, line);
            float btnH = u * 0.075f, lineH = u * 0.034f;
            txt(cv, "SAVE STATES", left + gap + font_w("SAVE STATES", u * 0.05f) / 2f, top + gap + btnH / 2f, u * 0.05f, 0xFFFFFFFF, 255);
            // top row: SAVE NOW (or START SOLO PLAY) and CLOSE
            RectF close = new RectF(left + pw - gap - u * 0.28f, top + gap, left + pw - gap, top + gap + btnH);
            button(cv, close, "CLOSE", BTN_NEUTRAL, true, btnH, ST_CLOSE);
            RectF save = new RectF(close.left - gap - u * 0.36f, top + gap, close.left - gap, top + gap + btnH);
            boolean online = (netStatus & 64) != 0;
            if (soloSession() || online) button(cv, save, "SAVE NOW", BTN_PRIMARY, canSaveState(), btnH, ST_SAVE);
            else button(cv, new RectF(save.left - u * 0.3f, save.top, save.right, save.bottom), "START SOLO PLAY", BTN_PRIMARY, true, btnH, ST_SOLO);
            if (!soloSession() && !online)
                txt(cv, "SOLO PLAY RECORDS YOUR SESSION SO YOU CAN SAVE (IT STARTS IN THE LIBRARY)", left + pw / 2f, top + gap * 2 + btnH + lineH * 0.5f, lineH * 0.75f, 0xFFC8C8C8, 255);
            // the list (left) and the selected save (right)
            float bodyTop = top + gap * 3 + btnH + lineH, bodyBottom = top + ph - gap;
            float listW = pw * 0.42f;
            float btnTop = bodyBottom - btnH;
            listRect.set(left + gap, bodyTop, left + gap + listW, btnTop - gap);
            solid(fill, 0xFF101028, 1f); cv.drawRect(listRect, fill);
            rowH = u * 0.075f;
            if (saves.isEmpty()) txt(cv, "NO SAVES YET", listRect.centerX(), listRect.centerY(), lineH, 0xFF6F6F6F, 255);
            cv.save();
            cv.clipRect(listRect);
            for (int i = 0; i < saves.size(); i++) {
                int id = saves.get(i);
                org.json.JSONObject o = saveInfo.get(id);
                RectF row = new RectF(listRect.left, listRect.top + i * rowH - listScroll, listRect.right, listRect.top + (i + 1) * rowH - listScroll);
                if (row.bottom < listRect.top || row.top > listRect.bottom) continue;
                if (id == selectedSave) { solid(fill, 0xFF2E3466, 1f); cv.drawRect(row, fill); solid(line, 0xFFFFFFFF, 0.8f); cv.drawRect(row, line); }
                gameIcon(cv, o, row.left + rowH * 0.5f, row.centerY(), rowH * 0.7f);
                String t = titleOf(o), d = (id == AUTO_SLOT ? "AUTO  " : "") + when(o.optLong("savedAt", 0));
                float dw = font_w(d, lineH * 0.75f);
                float tsz = lineH;
                float room = row.width() - rowH * 1.1f - dw - gap * 2;
                while (font_w(t, tsz) > room && tsz > lineH * 0.5f) tsz *= 0.9f;
                txt(cv, t, row.left + rowH * 1.05f + font_w(t, tsz) / 2f, row.centerY(), tsz, 0xFFFFFFFF, 255);
                txt(cv, d, row.right - gap - dw / 2f, row.centerY(), lineH * 0.75f, 0xFFC8C8C8, 255);
                solid(line, 0xFF303050, 1f); cv.drawLine(row.left, row.bottom, row.right, row.bottom, line);
                RectF hit = new RectF(row); hit.intersect(listRect);
                stateHits.add(hit); stateCodes.add(id);
            }
            cv.restore();
            line.setStrokeWidth(Math.max(2f, u * 0.006f));
            // under the list: the auto-save checkbox (a toggle: neutral, outlined and ticked when on)
            {
                RectF row = new RectF(listRect.left, btnTop, listRect.right, bodyBottom);
                float bs = btnH * 0.55f, bx = row.left + gap, by = row.centerY();
                RectF box = new RectF(bx, by - bs / 2f, bx + bs, by + bs / 2f);
                solid(fill, autoSavePause ? 0xFF2E3466 : 0xFF101028, 1f); cv.drawRect(box, fill);
                Paint o2 = new Paint(line); o2.setStrokeWidth(Math.max(3f, btnH * 0.06f)); o2.setColor(0xFFFFFFFF); o2.setAlpha(autoSavePause ? 255 : 150);
                cv.drawRect(box, o2);
                if (autoSavePause) {
                    android.graphics.Path tick = new android.graphics.Path();
                    tick.moveTo(box.left + bs * 0.2f, box.top + bs * 0.52f);
                    tick.lineTo(box.left + bs * 0.42f, box.top + bs * 0.74f);
                    tick.lineTo(box.left + bs * 0.8f, box.top + bs * 0.28f);
                    Paint tp = new Paint(o2); tp.setStrokeWidth(Math.max(3f, btnH * 0.08f)); tp.setStrokeCap(Paint.Cap.SQUARE);
                    cv.drawPath(tick, tp);
                }
                String al = "AUTO SAVE WHEN I PAUSE";
                float asz = lineH * 0.8f, room2 = row.right - (box.right + gap) - gap;
                while (font_w(al, asz) > room2 && asz > lineH * 0.4f) asz *= 0.9f;
                txt(cv, al, box.right + gap + font_w(al, asz) / 2f, by, asz, 0xFFFFFFFF, autoSavePause ? 255 : 200);
                stateHits.add(row); stateCodes.add(ST_AUTO);
            }
            // details of the selected save
            org.json.JSONObject o = selectedSave >= 0 ? saveInfo.get(selectedSave) : null;
            float dl = listRect.right + gap * 1.5f, dr = left + pw - gap;
            if (o != null) {
                float tw = dr - dl, th = Math.min(tw * 216f / 384f, (bodyBottom - bodyTop) * 0.5f);
                tw = th * 384f / 216f;
                RectF pic = new RectF(dl + (dr - dl - tw) / 2f, bodyTop, dl + (dr - dl + tw) / 2f, bodyTop + th);
                Bitmap b = thumbOf(selectedSave);
                if (b != null) { Paint p = new Paint(); p.setFilterBitmap(false); cv.drawBitmap(b, null, pic, p); }
                else { solid(fill, 0xFF101028, 1f); cv.drawRect(pic, fill); txt(cv, "NO PICTURE", pic.centerX(), pic.centerY(), lineH, 0xFF6F6F6F, 255); }
                solid(line, 0xFFFFFFFF, 0.6f); cv.drawRect(pic, line);
                float cxd = (dl + dr) / 2f, y = pic.bottom + lineH * 1.1f;
                String t = titleOf(o);
                float tsz = lineH * 1.2f;
                while (font_w(t, tsz) > dr - dl && tsz > lineH * 0.5f) tsz *= 0.9f;
                txt(cv, t, cxd, y, tsz, 0xFFFFFFFF, 255);
                y += lineH * 1.25f;
                txt(cv, kindOf(o), cxd, y, lineH * 0.8f, 0xFFC8C8C8, 255);
                y += lineH * 1.05f;
                // the players, each in their seat's colour
                org.json.JSONArray pl = o.optJSONArray("players");
                if (pl != null && pl.length() > 0) {
                    StringBuilder all = new StringBuilder();
                    for (int i = 0; i < pl.length(); i++) all.append(i > 0 ? "  " : "").append(pl.optJSONObject(i).optString("name", "?").toUpperCase());
                    float nsz = lineH * 0.8f;
                    while (font_w(all.toString(), nsz) > dr - dl && nsz > lineH * 0.4f) nsz *= 0.9f;
                    float nx = cxd - font_w(all.toString(), nsz) / 2f;
                    for (int i = 0; i < pl.length(); i++) {
                        org.json.JSONObject p = pl.optJSONObject(i);
                        String n = (i > 0 ? "  " : "") + p.optString("name", "?").toUpperCase();
                        int seat = p.optInt("seat", -1);
                        float w = font_w(n, nsz);
                        txt(cv, n, nx + w / 2f, y, nsz, seat >= 0 && seat < PLAYER_COLOR.length ? PLAYER_COLOR[seat] : 0xFFC8C8C8, 255);
                        nx += w;
                    }
                    y += lineH * 1.05f;
                }
                long gf = o.optLong("gameFrames", 0);
                txt(cv, gf > 0 ? clock(gf) + " INTO THE GAME" : "IN THE LIBRARY", cxd, y, lineH * 0.8f, 0xFFC8C8C8, 255);
                y += lineH * 1.05f;
                txt(cv, "SAVED " + when(o.optLong("savedAt", 0)), cxd, y, lineH * 0.75f, 0xFF8F8F8F, 255);
                float bw = (dr - dl - gap) / 2f;
                RectF ld = new RectF(dl, bodyBottom - btnH, dl + bw, bodyBottom);
                RectF del = new RectF(ld.right + gap, ld.top, dr, ld.bottom);
                boolean loadOk = !online || soloSession() || onlineSeat >= 0;
                button(cv, ld, "LOAD", BTN_NEUTRAL, loadOk, btnH, ST_LOAD);
                boolean armed = android.os.SystemClock.uptimeMillis() < deleteArmedUntil;
                button(cv, del, armed ? "TAP AGAIN" : "DELETE", armed ? BTN_DANGER : 0, true, btnH, ST_DELETE);
            } else if (!saves.isEmpty()) {
                txt(cv, "PICK A SAVE", (dl + dr) / 2f, (bodyTop + bodyBottom) / 2f, lineH, 0xFF6F6F6F, 255);
            }
        }

        // while a save loads the game isn't drawn (it's replaying flat out): cover it, and show
        // what's loading and how far it's got
        void drawRestore(Canvas cv, float u) {
            solid(fill, 0xFF000000, 1f);
            cv.drawRect(0, 0, getWidth(), getHeight(), fill);
            int pct = (stateStatus >> 8) & 0xff;
            float cx0 = getWidth() / 2f, cy0 = getHeight() / 2f;
            org.json.JSONObject o = saveInfo.get(pendingSlot);
            Bitmap b = thumbOf(pendingSlot);
            if (b != null) {
                float th = Math.min(u * 0.9f * 216f / 384f, cy0 - u * 0.12f), tw = th * 384f / 216f;
                RectF r = new RectF(cx0 - tw / 2f, cy0 - th - u * 0.06f, cx0 + tw / 2f, cy0 - u * 0.06f);
                Paint p = new Paint(); p.setAlpha(140);
                cv.drawBitmap(b, null, r, p);
            }
            float ty = cy0 + u * 0.03f;
            if (o != null) {
                String title = titleOf(o);
                float tw = font_w(title, u * 0.05f), isz = u * 0.05f;
                gameIcon(cv, o, cx0 - tw / 2f - isz * 0.7f, ty, isz);
                txt(cv, title, cx0 + isz * 0.25f, ty, u * 0.05f, 0xFFFFFFFF, 255);
                long gf = o.optLong("gameFrames", 0);
                txt(cv, kindOf(o) + "  -  " + (gf > 0 ? clock(gf) + " IN" : "LIBRARY") + "  -  " + when(o.optLong("savedAt", 0)), cx0, ty + u * 0.05f, u * 0.03f, 0xFFC8C8C8, 255);
            } else txt(cv, "LOADING", cx0, ty, u * 0.05f, 0xFFFFFFFF, 255);
            float bw = u * 0.9f, bh = u * 0.03f;
            RectF bar = new RectF(cx0 - bw / 2f, ty + u * 0.09f, cx0 + bw / 2f, ty + u * 0.09f + bh);
            solid(line, 0xFFFFFFFF, 0.8f); cv.drawRect(bar, line);
            solid(fill, 0xFF008456, 1f); cv.drawRect(bar.left + 2, bar.top + 2, bar.left + 2 + (bar.width() - 4) * pct / 100f, bar.bottom - 2, fill);
            txt(cv, "LOADING  " + pct + "%", cx0, bar.bottom + u * 0.04f, u * 0.035f, 0xFFC8C8C8, 255);
        }

        // the save-states button, left of the menu button: a little floppy disk
        void drawStatesButton(Canvas cv) {
            float x = cx(statesBtn), y = cy(statesBtn), r = radius(statesBtn);
            fill.setStyle(Paint.Style.FILL); fill.setColor(0xFF000000); fill.setAlpha(statesOpen ? 230 : 110);
            cv.drawCircle(x, y, r, fill);
            // the outlines overlap (shutter and label share the body's edges): drawn solid into one
            // layer that is faded as a whole, so every line has the same opacity
            float s = r * 0.5f;
            int layer = cv.saveLayerAlpha(x - r, y - r, x + r, y + r, statesOpen ? 255 : 150);
            line.setColor(0xFFFFFFFF); line.setAlpha(255);
            cv.drawRect(x - s, y - s, x + s, y + s, line);
            cv.drawRect(x - s * 0.5f, y - s, x + s * 0.4f, y - s * 0.35f, line);
            cv.drawRect(x - s * 0.6f, y + s * 0.15f, x + s * 0.6f, y + s, line);
            cv.restoreToCount(layer);
        }

        void drawGear(Canvas cv) {
            float x = cx(gear), y = cy(gear), r = radius(gear);
            fill.setStyle(Paint.Style.FILL); fill.setColor(0xFF000000); fill.setAlpha(editing ? 230 : 110);
            cv.drawCircle(x, y, r, fill);
            line.setColor(0xFFFFFFFF); line.setAlpha(editing ? 255 : 150);
            float w = r * 0.5f;
            for (int k = -1; k <= 1; k++) cv.drawLine(x - w, y + k * r * 0.32f, x + w, y + k * r * 0.32f, line);
        }

        void drawEditor(Canvas cv) {
            float u = unit();
            // dim the game a little so the editor reads clearly
            fill.setStyle(Paint.Style.FILL); fill.setColor(0xFF000000); fill.setAlpha(70);
            cv.drawRect(0, 0, getWidth(), getHeight(), fill);
            // outline every control, highlight the selected one with a resize handle
            for (Control c : controls) {
                float hw = halfW(c), hh = halfH(c);
                line.setColor(c == selected ? 0xFFE8EA4A : 0xFFFFFFFF); line.setAlpha(c == selected ? 255 : 120);
                tmp.set(cx(c) - hw, cy(c) - hh, cx(c) + hw, cy(c) + hh);
                cv.drawRect(tmp, line);
            }
            if (selected != null) {
                float[] h = handlePos(selected);
                fill.setColor(0xFFE8EA4A); fill.setAlpha(255);
                cv.drawCircle(h[0], h[1], u * 0.025f, fill);
            }
            // toolbar: sized to fit between the left edge and the menu button
            float maxW = cx(gear) - radius(gear) - u * 0.04f;
            float bh = u * 0.075f, gap = u * 0.01f;
            float[] widths = new float[TOOLBAR.length];
            float tw;
            while (true) {
                tw = 0;
                for (int t = 0; t < TOOLBAR.length; t++) { widths[t] = font_w(label(t), bh * 0.42f) + bh * 0.6f; tw += widths[t] + gap; }
                if (tw - gap <= maxW || bh < u * 0.04f) break;
                bh *= 0.92f; gap *= 0.92f;
            }
            float left = Math.max(u * 0.02f, (maxW - tw + gap) / 2f), top = u * 0.03f;
            text.setAlpha(255);
            for (int t = 0; t < TOOLBAR.length; t++) {
                RectF r = new RectF(left, top, left + widths[t], top + bh);
                toolbarRects[t] = r;
                boolean needsSel = t == TOOLBAR_SMALLER || t == TOOLBAR_BIGGER || t == TOOLBAR_HIDE;
                fill.setColor(BTN_NEUTRAL); fill.setAlpha(needsSel && selected == null ? 120 : 235);
                cv.drawRoundRect(r, bh * 0.2f, bh * 0.2f, fill);
                txt(cv, label(t), r.centerX(), r.centerY(), bh * 0.42f, 0xFFFFFFFF, needsSel && selected == null ? 120 : 255);
                left += widths[t] + gap;
            }
            String hint = selected == null ? "TAP A CONTROL TO SELECT IT. DRAG TO MOVE. PINCH OR DRAG THE YELLOW DOT TO RESIZE."
                    : "SELECTED: " + (selected.label.isEmpty() ? "D-PAD" : selected.type == Control.PLAYER ? "PLAYER SWITCH" : selected.label) + (selected.hidden ? " (HIDDEN)" : "");
            hint += "  -  OPACITY " + Math.round(opacity * 100) + "%";
            float hh = u * 0.032f;
            while (font_w(hint, hh) > getWidth() * 0.96f && hh > u * 0.012f) hh *= 0.9f;
            txt(cv, hint, getWidth() / 2f, top + bh + u * 0.045f, hh, 0xFFFFFFFF, 220);
        }

        String label(int t) {
            if (t == TOOLBAR_HIDE) return selected != null && selected.hidden ? "SHOW" : "HIDE";
            if (t == TOOLBAR_AUTOHIDE) return "AUTO-HIDE " + (autoHide ? "ON" : "OFF");
            if (t == TOOLBAR_DPAD) return "D-PAD " + (dpad4 ? "4-WAY" : "8-WAY");
            if (t == TOOLBAR_CHIPS) return "CHIPS " + (chipsColumn ? "COLUMN" : "ROW");
            return TOOLBAR[t];
        }
    }
}
