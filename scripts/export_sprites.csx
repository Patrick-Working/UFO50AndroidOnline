// Export every frame of the given sprites (SPRITES, comma list) to OUT_DIR/<sprite>/<frame>.png.
var worker = new UndertaleModLib.Util.TextureWorker();
string outDir = Environment.GetEnvironmentVariable("OUT_DIR");
foreach (var name in Environment.GetEnvironmentVariable("SPRITES").Split(','))
{
    var spr = Data.Sprites.ByName(name);
    string dir = Path.Combine(outDir, name);
    Directory.CreateDirectory(dir);
    for (int i = 0; i < spr.Textures.Count; i++)
        worker.ExportAsPNG(spr.Textures[i].Texture, Path.Combine(dir, i + ".png"), null, true);
    Console.WriteLine($"SPR {name}: {spr.Width}x{spr.Height}, {spr.Textures.Count} frames");
}
