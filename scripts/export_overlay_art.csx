// The touch overlay's art, taken from your copy of the game (nothing is shipped):
//   pfo_font.png      sFontDefault, the first 95 frames (ASCII 32-126, see scrInitFonts) in one strip
//   pfo_keyicons.png  sKeyIcons, every frame in one strip (frame number = Windows key code)
//   pfo_vinputs.png   sVirtualInputs, every frame in one strip (the I / II / SYS lettering)
// Usage: OUT_DIR=<game folder> UndertaleModCli load <data.win> -s export_overlay_art.csx
using System.Linq;
using ImageMagick;
using UndertaleModLib.Util;

string outDir = Environment.GetEnvironmentVariable("OUT_DIR");
string tmp = Path.Combine(Path.GetTempPath(), "ufo50_overlay_art_" + Environment.ProcessId);
var worker = new TextureWorker();

void Strip(string sprite, int maxFrames, string file)
{
    var spr = Data.Sprites.ByName(sprite);
    int n = Math.Min(maxFrames, spr.Textures.Count);
    string dir = Path.Combine(tmp, sprite);
    Directory.CreateDirectory(dir);
    var cells = new List<MagickImage>();
    for (int i = 0; i < n; i++)
    {
        string p = Path.Combine(dir, i + ".png");
        worker.ExportAsPNG(spr.Textures[i].Texture, p, null, true);   // padded to the sprite's full size
        cells.Add(new MagickImage(p));
    }
    int w = (int)cells[0].Width, h = (int)cells[0].Height;
    using var strip = new MagickImage(MagickColors.Transparent, (uint)(w * n), (uint)h);
    for (int i = 0; i < n; i++) strip.Composite(cells[i], i * w, 0, CompositeOperator.Copy);
    strip.Format = MagickFormat.Png32;
    strip.Write(Path.Combine(outDir, file));
    foreach (var c in cells) c.Dispose();
    Console.WriteLine($"ART {file}: {n} cells of {w}x{h}");
}

Directory.CreateDirectory(outDir);
Strip("sFontDefault", 95, "pfo_font.png");
Strip("sKeyIcons", int.MaxValue, "pfo_keyicons.png");
Strip("sVirtualInputs", int.MaxValue, "pfo_vinputs.png");
worker.Dispose();
try { Directory.Delete(tmp, true); } catch { }
