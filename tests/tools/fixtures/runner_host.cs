// A no-game launcher/host fixture for the PowerShell timeout and slot-isolation tests.
using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Threading;

class RunnerHost
{
    static int Main(string[] args)
    {
        bool host = Array.IndexOf(args, "--standalone-host") >= 0;
        string dir = args[Array.IndexOf(args, "--game-dir") + 1];
        string script = args[Array.IndexOf(args, "--script") + 1];
        string mode = Path.GetFileNameWithoutExtension(script);
        if (!host)
        {
            if (mode == "startup-hang") Thread.Sleep(Timeout.Infinite);
            var info = new ProcessStartInfo(Assembly.GetExecutingAssembly().Location,
                "--standalone-host --game-dir \"" + dir + "\" --script \"" + script + "\"");
            info.UseShellExecute = false;
            using (var child = Process.Start(info))
            {
                // Inherit the redirected output, as the real standalone host does.
                if (Array.IndexOf(args, "--no-wait") >= 0) return 0;
                child.WaitForExit();
                return child.ExitCode;
            }
        }
        File.WriteAllText(Path.Combine(dir, "BeachLife.ini"), "host changed the ini");
        if (Environment.GetEnvironmentVariable("OBL_RUNNER_FIXTURE") != null)
        {
            if (File.Exists(Path.Combine(dir, "progress.json"))) return 9;
            File.WriteAllText(Path.Combine(dir, "progress.json"), "host won a scenario");
        }
        File.WriteAllText(Path.Combine(dir, "fixture.env"), Environment.GetEnvironmentVariable("OBL_RUNNER_FIXTURE") ?? "");
        string log = Path.Combine(dir, "openbeach.log");
        if (mode == "hang")
        {
            File.WriteAllText(log, "autotest: FAILED original code reached @00401000 fixture trap\n");
            Thread.Sleep(Timeout.Infinite);
        }
        if (mode == "exit7")
        {
            File.WriteAllText(log, "FatalError: fixture exit\n");
            return 7;
        }
        File.WriteAllText(log, "autotest: summary passed=1 failed=0\nautotest: completed\n");
        return mode == "unexpected7" ? 7 : 0;
    }
}
