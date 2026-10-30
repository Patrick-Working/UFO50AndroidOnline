// Export the first 95 frames (ASCII 32-126, see scrInitFonts) of the given font sprites
// (FONT_SPRITES, comma list) as PNGs in OUT_DIR/<sprite>/<frame>.png. scripts/make_font.py
// packs them into one strip for the touch overlay.
using System.Linq;
using UndertaleModLib.Util;
string outDir = Environment.GetEnvironmentVariable("OUT_DIR");
var worker = new TextureWorker();
foreach (var name in Environment.GetEnvironmentVariable("FONT_SPRITES").Split(','))
{
    var spr = Data.Sprites.ByName(name);
    string dir = Path.Combine(outDir, name);
    Directory.CreateDirectory(dir);
    for (int i = 0; i < Math.Min(95, spr.Textures.Count); i++)
        worker.ExportAsPNG(spr.Textures[i].Texture, Path.Combine(dir, i + ".png"), null, true);
    Console.WriteLine($"FONT {name}: {spr.Width}x{spr.Height}, exported {Math.Min(95, spr.Textures.Count)} frames");
}
