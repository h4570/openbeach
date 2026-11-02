using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
class CaptureHost {
    static int Main(string[] args) {
        string dir = args[Array.IndexOf(args, "--game-dir") + 1];
        if (Array.IndexOf(args, "--standalone-host") < 0) {
            var start = new ProcessStartInfo(Process.GetCurrentProcess().MainModule.FileName,
                "--standalone-host --game-dir \"" + dir + "\"");
            start.UseShellExecute = false;
            Process.Start(start);
            for (int i = 0; i < 100 && !File.Exists(Path.Combine(dir, "ready")); ++i) Thread.Sleep(50);
            return Environment.GetEnvironmentVariable("CAPTURE_FAIL") == "1" ? 7 : 0;
        }
        File.WriteAllText(Path.Combine(dir, "BeachLife.ini"), "[Sound]\nPlayListCount=0\n");
        File.WriteAllText(Path.Combine(dir, "ready"), "ready");
        Console.WriteLine("host output");
        Thread.Sleep(60000);
        return 0;
    }
}
