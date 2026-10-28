// ExciteTruckSetup.exe: drag your Excite Truck disc image onto it.
// It unpacks the build script and the Excite Truck changes, then builds the game on this PC
// from your own disc (the finished game can't be shipped: it is translated from Nintendo's code).
using System.Diagnostics;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;

static class Setup
{
    static readonly string[] DiscExtensions = { ".wbfs", ".iso", ".rvz", ".ciso", ".wia", ".gcz", ".zip" };

    static readonly string LocalAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
    static readonly string SetupLog = Path.Combine(LocalAppData, "ExciteTruckSetup", "setup-log.txt");
    static readonly string BuildLog = Path.Combine(Path.GetPathRoot(Environment.SystemDirectory) ?? "C:\\", "ETBuild", "build-log.txt");

    static int Main(string[] args)
    {
        // Everything this window prints also goes to setup-log.txt, and a crash is logged and
        // waits for Enter instead of closing the window.
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(SetupLog)!);
            var log = new StreamWriter(SetupLog, append: true) { AutoFlush = true };
            log.WriteLine($"--- {DateTime.Now:yyyy-MM-dd HH:mm:ss} setup started: {string.Join(" ", args)}");
            Console.SetOut(new Tee(Console.Out, log));
        }
        catch { }
        AppDomain.CurrentDomain.UnhandledException += (_, e) =>
        {
            Console.WriteLine($"Setup crashed: {e.ExceptionObject}");
            Finish(1);
        };
        try { Console.Title = "Excite Truck Setup"; } catch { }
        Console.WriteLine("Excite Truck PC setup");
        Console.WriteLine("=====================");
        Console.WriteLine();
        try
        {
            return Run(args);
        }
        catch (Exception e)
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine($"Setup failed: {e.Message}");
            Console.ResetColor();
            Console.WriteLine(e.ToString());
            Console.WriteLine($"Log: {SetupLog}");
            return Finish(1);
        }
    }

    static int Run(string[] args)
    {
        string disc = args.Length > 0 ? args[0] : "";
        while (!IsDisc(disc))
        {
            if (disc.Length > 0) Console.WriteLine($"That isn't a disc image: {disc}");
            Console.WriteLine("Drag your Excite Truck (USA) disc image (.wbfs, .iso, .rvz or .zip) onto this window,");
            Console.WriteLine("then press Enter:");
            disc = (Console.ReadLine() ?? "").Trim().Trim('"');
            if (disc.Length == 0) return Finish(1);
        }
        disc = Path.GetFullPath(disc);
        string localAppData = LocalAppData;
        string gameDir = Path.Combine(localAppData, "Programs", "ExciteTruck");
        Console.WriteLine($"Disc:  {disc}");
        Console.WriteLine($"Game:  {gameDir}");
        Console.WriteLine($"Log:   {BuildLog}");
        Console.WriteLine();
        Console.WriteLine("The first build takes roughly 30 to 90 minutes and needs about 20 GB free on");
        Console.WriteLine($"{Path.GetPathRoot(Environment.SystemDirectory)} while it runs. You can use the PC meanwhile.");
        Console.WriteLine();

        string payloadDir = UnpackPayload(localAppData);
        string script = Path.Combine(payloadDir, "build-windows.ps1");
        var start = new ProcessStartInfo("powershell.exe")
        {
            UseShellExecute = false,
        };
        foreach (string a in new[] { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script,
                                     "-Disc", disc, "-Out", gameDir, "-Shortcut" })
            start.ArgumentList.Add(a);
        Console.WriteLine("Starting the build...");
        using var process = Process.Start(start) ?? throw new InvalidOperationException("could not start PowerShell");
        process.WaitForExit();
        Console.WriteLine($"Build finished with exit code {process.ExitCode}.");
        if (process.ExitCode != 0)
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine();
            Console.WriteLine("The build stopped. Send over the log so it can be fixed:");
            Console.WriteLine($"  {BuildLog}");
            Console.WriteLine($"Running setup again resumes where it stopped.");
            Console.ResetColor();
            return Finish(process.ExitCode);
        }

        Console.ForegroundColor = ConsoleColor.Green;
        Console.WriteLine();
        Console.WriteLine("All set. Start the game from the \"Excite Truck\" shortcut on your desktop.");
        Console.ResetColor();
        Console.WriteLine("Put your own MP3s in the \"Excite Truck Music\" folder (also on the desktop).");
        try { Process.Start(new ProcessStartInfo("explorer.exe", $"\"{gameDir}\"") { UseShellExecute = true }); } catch { }
        return Finish(0);
    }

    static bool IsDisc(string path)
    {
        if (path.Length == 0) return false;
        if (Directory.Exists(path)) return File.Exists(Path.Combine(path, "sys", "main.dol"));
        if (!File.Exists(path)) return false;
        string name = path.ToLowerInvariant();
        return DiscExtensions.Any(name.EndsWith) || System.Text.RegularExpressions.Regex.IsMatch(name, @"\.zip\.\d{3}$");
    }

    // Unpacks the embedded build script and Excite Truck changes once per setup version.
    static string UnpackPayload(string localAppData)
    {
        using Stream resource = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip")
            ?? throw new InvalidOperationException("the setup is incomplete (no payload)");
        using var buffer = new MemoryStream();
        resource.CopyTo(buffer);
        string hash = Convert.ToHexString(SHA256.HashData(buffer.ToArray()))[..12].ToLowerInvariant();
        string dir = Path.Combine(localAppData, "ExciteTruckSetup", hash);
        if (!File.Exists(Path.Combine(dir, ".complete")))
        {
            if (Directory.Exists(dir)) Directory.Delete(dir, true);
            buffer.Position = 0;
            using (var zip = new ZipArchive(buffer, ZipArchiveMode.Read))
                zip.ExtractToDirectory(dir);
            File.WriteAllText(Path.Combine(dir, ".complete"), hash);
        }
        return dir;
    }

    static int Finish(int code)
    {
        Console.WriteLine();
        Console.WriteLine("Press Enter to close.");
        try { Console.ReadLine(); } catch { }
        return code;
    }

    // Writes to the console and the log file at once.
    sealed class Tee : TextWriter
    {
        readonly TextWriter a, b;
        public Tee(TextWriter a, TextWriter b) { this.a = a; this.b = b; }
        public override System.Text.Encoding Encoding => a.Encoding;
        public override void Write(char value) { a.Write(value); try { b.Write(value); } catch { } }
        public override void Write(string? value) { a.Write(value); try { b.Write(value); } catch { } }
        public override void WriteLine(string? value) { a.WriteLine(value); try { b.WriteLine(value); } catch { } }
        public override void Flush() { a.Flush(); try { b.Flush(); } catch { } }
    }
}
