// Pack and sign the APK without the Android SDK or Java: the runner APK's files (copied as they
// are) plus the patched game, stored uncompressed and aligned like zipalign does, signed with
// APK Signature Scheme v2 (what Android 7+ checks). The signing key is made on first use and kept
// in KEY_FILE: keep it, Android only installs an update signed with the same key.
//
// Env: RUNNER_APK, GAME_DIR, OUT_APK, KEY_FILE.  Runs inside UndertaleModCli (any data file loaded).
// Without GAME_DIR it repacks the runner alone (the "runner kit" the quick build starts from).
using System.Linq;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;

string runnerPath = Environment.GetEnvironmentVariable("RUNNER_APK");
string gameDir = Environment.GetEnvironmentVariable("GAME_DIR");
if (string.IsNullOrEmpty(gameDir)) gameDir = null;
string outPath = Environment.GetEnvironmentVariable("OUT_APK");
string keyPath = Environment.GetEnvironmentVariable("KEY_FILE");
const string KeyPassword = "ufo50android";   // a local key for personal builds

// ---------------------------------------------------------------- zip reading (the runner) ---
var runner = File.ReadAllBytes(runnerPath);
ushort U16(byte[] b, long o) => (ushort)(b[o] | b[o + 1] << 8);
uint U32(byte[] b, long o) => (uint)(b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24);
long eocd = -1;
for (long i = runner.Length - 22; i >= Math.Max(0, runner.Length - 66000); i--)
    if (U32(runner, i) == 0x06054b50) { eocd = i; break; }
if (eocd < 0) throw new Exception("runner APK: no end of central directory");
int count = U16(runner, eocd + 10);
long cdPos = U32(runner, eocd + 16);

var entries = new List<Entry>();
for (int k = 0; k < count; k++)
{
    if (U32(runner, cdPos) != 0x02014b50) throw new Exception("runner APK: bad central directory");
    var e = new Entry
    {
        Flags = (ushort)(U16(runner, cdPos + 8) & ~8), Method = U16(runner, cdPos + 10),
        Time = U16(runner, cdPos + 12), Date = U16(runner, cdPos + 14), Crc = U32(runner, cdPos + 16),
        CSize = U32(runner, cdPos + 20), USize = U32(runner, cdPos + 24),
    };
    int nlen = U16(runner, cdPos + 28), xlen = U16(runner, cdPos + 30), clen = U16(runner, cdPos + 32);
    e.ExtAttr = U32(runner, cdPos + 38);
    long lho = U32(runner, cdPos + 42);
    e.Name = System.Text.Encoding.UTF8.GetString(runner, (int)cdPos + 46, nlen);
    e.SrcData = lho + 30 + U16(runner, lho + 26) + U16(runner, lho + 28);
    cdPos += 46 + nlen + xlen + clen;
    entries.Add(e);
}

// ---------------------------------------------------------------- what goes in ---------------
// Old signatures go (this APK gets a new one); the placeholder game and options are replaced.
bool IsOldSignature(string n) => n.StartsWith("META-INF/") &&
    (n.EndsWith(".SF") || n.EndsWith(".RSA") || n.EndsWith(".DSA") || n.EndsWith(".EC") || n == "META-INF/MANIFEST.MF");
string runnerIni = "";
var keep = new List<Entry>();
foreach (var e in entries)
{
    if (e.Name == "assets/options.ini")
    {
        runnerIni = e.Method == 0 ? System.Text.Encoding.UTF8.GetString(runner, (int)e.SrcData, (int)e.CSize)
            : new StreamReader(new System.IO.Compression.DeflateStream(new MemoryStream(runner, (int)e.SrcData, (int)e.CSize), System.IO.Compression.CompressionMode.Decompress)).ReadToEnd();
        continue;
    }
    if (IsOldSignature(e.Name) || (gameDir != null && e.Name == "assets/game.droid")) continue;
    keep.Add(e);
}
var skipNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { "ufo50.exe", "steam_api64.dll", "Steamworks_x64.dll", "data.win", "options.ini" };
var skipExt = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { ".exe", ".dll", ".pdb", ".log" };
var added =new List<(string name, string path)>();
if (gameDir != null) added.Add(("assets/game.droid", Path.Combine(gameDir, "data.win")));
foreach (var f in gameDir == null ? Enumerable.Empty<string>() : Directory.GetFiles(gameDir, "*", SearchOption.AllDirectories).OrderBy(x => x, StringComparer.Ordinal).AsEnumerable())
{
    string rel = Path.GetRelativePath(gameDir, f).Replace('\\', '/');
    if (rel.StartsWith(".") || skipNames.Contains(Path.GetFileName(f)) || skipExt.Contains(Path.GetExtension(f))) continue;
    added.Add(("assets/" + rel.ToLowerInvariant(), f));   // the Android runner lowercases asset paths
}
var addedNames = new HashSet<string>(added.Select(a => a.name));
keep.RemoveAll(e => addedNames.Contains(e.Name));
// The runner's [LLVM-Android] NDKDir is a build machine path the game never reads: drop it.
runnerIni = string.Join("\n", runnerIni.Replace("\r", "").Split('\n').Where(l => !l.StartsWith("NDKDir=")));
string gameIni = gameDir != null && File.Exists(Path.Combine(gameDir, "options.ini")) ? File.ReadAllText(Path.Combine(gameDir, "options.ini")) : "";
byte[] iniBytes = System.Text.Encoding.UTF8.GetBytes(runnerIni.TrimEnd() + "\n" + gameIni);

// ---------------------------------------------------------------- zip writing ----------------
uint[] crcTable = Enumerable.Range(0, 256).Select(n => { uint c = (uint)n; for (int j = 0; j < 8; j++) c = (c & 1) != 0 ? 0xEDB88320 ^ (c >> 1) : c >> 1; return c; }).ToArray();
uint Crc(uint crc, byte[] b, int len) { crc = ~crc; for (int i = 0; i < len; i++) crc = crcTable[(crc ^ b[i]) & 0xff] ^ (crc >> 8); return ~crc; }

Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outPath)));
string partPath = outPath + ".part";
var central = new MemoryStream();
var cw = new BinaryWriter(central);
int written = 0;
using (var fs = new FileStream(partPath, FileMode.Create, FileAccess.ReadWrite, FileShare.None, 1 << 20))
{
    var w = new BinaryWriter(fs);
    void Header(Entry e, long offset, byte[] name)
    {
        // stored data aligned to 4 bytes (4096 for native libraries), as zipalign -p does
        int align = e.Method != 0 ? 0 : e.Name.EndsWith(".so") ? 4096 : 4;
        int extra = 0;
        if (align > 0) { long dataAt = offset + 30 + name.Length + 6; extra = 6 + (int)((align - dataAt % align) % align); }
        w.Write(0x04034b50u); w.Write((ushort)(e.Method == 0 ? 10 : 20)); w.Write(e.Flags); w.Write(e.Method);
        w.Write(e.Time); w.Write(e.Date); w.Write(e.Crc); w.Write(e.CSize); w.Write(e.USize);
        w.Write((ushort)name.Length); w.Write((ushort)extra); w.Write(name);
        if (extra > 0) { w.Write((ushort)0xD935); w.Write((ushort)(extra - 4)); w.Write((ushort)align); w.Write(new byte[extra - 6]); }
        cw.Write(0x02014b50u); cw.Write((ushort)0x031E); cw.Write((ushort)(e.Method == 0 ? 10 : 20)); cw.Write(e.Flags); cw.Write(e.Method);
        cw.Write(e.Time); cw.Write(e.Date); cw.Write(e.Crc); cw.Write(e.CSize); cw.Write(e.USize);
        cw.Write((ushort)name.Length); cw.Write((ushort)0); cw.Write((ushort)0); cw.Write((ushort)0); cw.Write((ushort)0);
        cw.Write(e.ExtAttr); cw.Write((uint)offset); cw.Write(name);
        written++;
    }
    foreach (var e in keep)
    {
        Header(e, fs.Position, System.Text.Encoding.UTF8.GetBytes(e.Name));
        fs.Write(runner, (int)e.SrcData, (int)e.CSize);
    }
    var buf = new byte[1 << 20];
    void AddStored(string name, Stream src)
    {
        // CRC first (one read), then header and data
        uint crc = 0; long len = 0; int n;
        src.Position = 0;
        while ((n = src.Read(buf, 0, buf.Length)) > 0) { crc = Crc(crc, buf, n); len += n; }
        var e = new Entry { Name = name, Method = 0, Flags = 0x0800, Time = 0, Date = 0x21, Crc = crc, CSize = (uint)len, USize = (uint)len, ExtAttr = 0x81A40000 };
        Header(e, fs.Position, System.Text.Encoding.UTF8.GetBytes(name));
        src.Position = 0;
        while ((n = src.Read(buf, 0, buf.Length)) > 0) fs.Write(buf, 0, n);
    }
    foreach (var (name, path) in added) using (var s = File.OpenRead(path)) AddStored(name, s);
    AddStored("assets/options.ini", new MemoryStream(iniBytes));
    w.Flush();
}

// ---------------------------------------------------------------- APK Signature Scheme v2 ----
X509Certificate2 cert;
if (File.Exists(keyPath)) cert = new X509Certificate2(File.ReadAllBytes(keyPath), KeyPassword, X509KeyStorageFlags.Exportable | X509KeyStorageFlags.EphemeralKeySet);
else
{
    using var newKey = RSA.Create(2048);
    var req = new CertificateRequest("CN=UFO 50 Android personal build", newKey, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
    using var made = req.CreateSelfSigned(DateTimeOffset.UtcNow.AddDays(-1), DateTimeOffset.UtcNow.AddYears(50));
    Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(keyPath)));
    File.WriteAllBytes(keyPath, made.Export(X509ContentType.Pfx, KeyPassword));
    cert = new X509Certificate2(File.ReadAllBytes(keyPath), KeyPassword, X509KeyStorageFlags.Exportable | X509KeyStorageFlags.EphemeralKeySet);
    Console.WriteLine("APK made a new signing key: " + keyPath);
}
var rsa = cert.GetRSAPrivateKey();

long entriesEnd = new FileInfo(partPath).Length;
byte[] cd = central.ToArray();
byte[] EocdWith(long cdOffset)
{
    var m = new MemoryStream(); var bw = new BinaryWriter(m);
    bw.Write(0x06054b50u); bw.Write((ushort)0); bw.Write((ushort)0); bw.Write((ushort)written); bw.Write((ushort)written);
    bw.Write((uint)cd.Length); bw.Write((uint)cdOffset); bw.Write((ushort)0);
    return m.ToArray();
}

// digest: 1 MB chunks of (entries, central directory, end record pointing at the signing block)
var chunkDigests = new MemoryStream();
int chunks = 0;
using (var sha = SHA256.Create())
{
    var chunk = new byte[1 << 20];
    void Chunk(byte[] data, int len)
    {
        var pre = new byte[5]; pre[0] = 0xa5; BitConverter.GetBytes(len).CopyTo(pre, 1);
        sha.Initialize(); sha.TransformBlock(pre, 0, 5, null, 0); sha.TransformFinalBlock(data, 0, len);
        chunkDigests.Write(sha.Hash); chunks++;
    }
    void Section(Stream s) { int n; while ((n = ReadFull(s, chunk)) > 0) Chunk(chunk, n); }
    using (var fs = File.OpenRead(partPath)) Section(fs);
    Section(new MemoryStream(cd));
    Section(new MemoryStream(EocdWith(entriesEnd)));
}
int ReadFull(Stream s, byte[] b) { int t = 0, n; while (t < b.Length && (n = s.Read(b, t, b.Length - t)) > 0) t += n; return t; }
var top = new MemoryStream(); top.WriteByte(0x5a); top.Write(BitConverter.GetBytes(chunks)); chunkDigests.Position = 0; chunkDigests.CopyTo(top);
byte[] digest = SHA256.HashData(top.ToArray());

byte[] LP(params byte[][] parts) { var m = new MemoryStream(); foreach (var p in parts) m.Write(p); var body = m.ToArray(); var r = new byte[4 + body.Length]; BitConverter.GetBytes(body.Length).CopyTo(r, 0); body.CopyTo(r, 4); return r; }
byte[] U32b(uint v) => BitConverter.GetBytes(v);
const uint RsaPkcs1Sha256 = 0x0103;
byte[] digestsSeq = LP(LP(U32b(RsaPkcs1Sha256), LP(digest)));
byte[] certsSeq = LP(LP(cert.RawData));
byte[] signedData = Concat(digestsSeq, certsSeq, LP());   // no additional attributes
byte[] signature = rsa.SignData(signedData, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
byte[] signer = Concat(LP(signedData), LP(LP(U32b(RsaPkcs1Sha256), LP(signature))), LP(rsa.ExportSubjectPublicKeyInfo()));
byte[] v2Value = LP(LP(signer));
byte[] Concat(params byte[][] parts) { var m = new MemoryStream(); foreach (var p in parts) m.Write(p); return m.ToArray(); }

var block = new MemoryStream(); var bwb = new BinaryWriter(block);
long pairLen = 4 + v2Value.Length;
long blockSize = 8 + pairLen + 8 + 16;           // everything after the first size field
bwb.Write((ulong)blockSize); bwb.Write((ulong)pairLen); bwb.Write(0x7109871au); bwb.Write(v2Value);
bwb.Write((ulong)blockSize); bwb.Write(System.Text.Encoding.ASCII.GetBytes("APK Sig Block 42"));
byte[] blockBytes = block.ToArray();

using (var fs = new FileStream(partPath, FileMode.Append, FileAccess.Write))
{
    fs.Write(blockBytes); fs.Write(cd); fs.Write(EocdWith(entriesEnd + blockBytes.Length));
}
if (File.Exists(outPath)) File.Delete(outPath);
File.Move(partPath, outPath);
Console.WriteLine($"APK {outPath}: {written} files, {new FileInfo(outPath).Length / 1000000} MB, signed");

class Entry
{
    public string Name; public ushort Flags, Method, Time, Date; public uint Crc, CSize, USize, ExtAttr; public long SrcData;
}
