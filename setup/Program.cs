// ExciteSetup.exe: installs the Excite PC launcher and opens it.
//
// The launcher does the rest: it asks for your Excite Truck and Excitebots disc images and builds
// each game on this PC from your own disc, with a progress bar (the finished games can't be
// shipped: they are translated from Nintendo's code). A disc image dropped on this exe is handed
// to the launcher.
using System.Diagnostics;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;

static class Setup
{
    const string Title = "Excite PC Setup";

    static readonly string LocalAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
    static readonly string InstallDir = Path.Combine(LocalAppData, "Programs", "ExcitePC");
    static readonly string LogFile = Path.Combine(LocalAppData, "ExcitePC", "setup-log.txt");

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int MessageBoxW(IntPtr owner, string text, string caption, uint type);
    const uint MB_OK = 0, MB_RETRYCANCEL = 5, MB_ICONERROR = 0x10, MB_ICONWARNING = 0x30;
    const int IDRETRY = 4;

    [STAThread]
    static int Main(string[] args)
    {
        try
        {
            Log($"setup {Assembly.GetExecutingAssembly().GetName().Version} started: {string.Join(" ", args)}");
            WaitForLauncherToClose();
            Install();
            MakeShortcuts();
            var start = new ProcessStartInfo(Path.Combine(InstallDir, "ExcitePC.exe"))
            {
                UseShellExecute = false,
                WorkingDirectory = InstallDir,
            };
            if (args.Length > 0) start.ArgumentList.Add(Path.GetFullPath(args[0]));
            Process.Start(start);
            Log("done");
            return 0;
        }
        catch (Exception e)
        {
            Log(e.ToString());
            MessageBoxW(IntPtr.Zero, $"Setup failed: {e.Message}\n\nLog: {LogFile}", Title, MB_OK | MB_ICONERROR);
            return 1;
        }
    }

    // The launcher's files can't be replaced while it runs.
    static void WaitForLauncherToClose()
    {
        while (Process.GetProcessesByName("ExcitePC").Length > 0)
        {
            int answer = MessageBoxW(IntPtr.Zero,
                "Excite PC is open. Close it (if a game is being set up, wait for it to finish first), then choose Retry.",
                Title, MB_RETRYCANCEL | MB_ICONWARNING);
            if (answer != IDRETRY) throw new OperationCanceledException("Excite PC was still open");
        }
    }

    // Unpacks the launcher and the games' build files into %LOCALAPPDATA%\Programs\ExcitePC.
    // The games themselves are installed next to it (Programs\ExciteTruck, Programs\Excitebots)
    // by the launcher and are left alone here.
    static void Install()
    {
        using Stream resource = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip")
            ?? throw new InvalidOperationException("the setup is incomplete (no payload)");
        Directory.CreateDirectory(InstallDir);
        string setupDir = Path.Combine(InstallDir, "setup");
        if (Directory.Exists(setupDir)) Directory.Delete(setupDir, true);   // old build files
        using var zip = new ZipArchive(resource, ZipArchiveMode.Read);
        zip.ExtractToDirectory(InstallDir, overwriteFiles: true);
        Log($"installed to {InstallDir}");
    }

    static void MakeShortcuts()
    {
        string exe = Path.Combine(InstallDir, "ExcitePC.exe");
        string desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
        string startMenu = Environment.GetFolderPath(Environment.SpecialFolder.Programs);
        // WScript.Shell makes .lnk files. Older setups put an "Excite Truck" shortcut to a launcher
        // inside the game folder, which is gone now: remove that one.
        string script = string.Join("; ", new[]
        {
            "$ErrorActionPreference = 'Stop'",
            "$shell = New-Object -ComObject WScript.Shell",
            $"foreach ($path in @('{Quote(Path.Combine(desktop, "Excite PC.lnk"))}', '{Quote(Path.Combine(startMenu, "Excite PC.lnk"))}')) {{ " +
                "$link = $shell.CreateShortcut($path); " +
                $"$link.TargetPath = '{Quote(exe)}'; $link.WorkingDirectory = '{Quote(InstallDir)}'; " +
                $"$link.IconLocation = '{Quote(exe)},0'; $link.Description = 'Excite Truck and Excitebots'; $link.Save() }}",
            $"$old = '{Quote(Path.Combine(desktop, "Excite Truck.lnk"))}'",
            "if (Test-Path -LiteralPath $old) { if ($shell.CreateShortcut($old).TargetPath -like '*ExciteTruckLauncher.exe') { Remove-Item -LiteralPath $old -Force } }",
        });
        var start = new ProcessStartInfo("powershell.exe")
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        foreach (string a in new[] { "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", script })
            start.ArgumentList.Add(a);
        using var process = Process.Start(start);
        if (process == null) return;
        string output = process.StandardOutput.ReadToEnd() + process.StandardError.ReadToEnd();
        process.WaitForExit();
        Log($"shortcuts: exit {process.ExitCode} {output.Trim()}");
    }

    static string Quote(string text) => text.Replace("'", "''");

    static void Log(string text)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(LogFile)!);
            File.AppendAllText(LogFile, $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} {text}{Environment.NewLine}");
        }
        catch { }
    }
}
