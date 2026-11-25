package org.openbeach.game;

import android.content.Intent;
import android.content.res.AssetManager;
import android.os.Build;
import android.os.Bundle;
import android.system.Os;
import android.util.Log;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** OpenBeach on Android (docs/build/android.md): SDL's activity, with SDL3 linked into the game's libmain.so. */
public class OpenBeachActivity extends SDLActivity {
    public static final String EXTRA_GAME_DIR = "org.openbeach.game.GAME_DIR";
    public static final String EXTRA_DEMO = "org.openbeach.game.DEMO";
    private static final String TAG = "OpenBeach";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= 28 && getWindow() != null) {
            WindowManager.LayoutParams params = getWindow().getAttributes();
            params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(params);
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"main"};
    }

    // SDL calls this on its main thread just before SDL_main. Starts the imported copy when available (from LauncherActivity or
    // directly via adb), otherwise unpacks and runs the bundled L0 demo.
    @Override
    protected String[] getArguments() {
        Intent intent = getIntent();
        boolean forceDemo = intent != null && intent.getBooleanExtra(EXTRA_DEMO, false);
        String explicitDir = intent != null ? intent.getStringExtra(EXTRA_GAME_DIR) : null;

        File game = null;
        if (!forceDemo) {
            if (explicitDir != null && LauncherActivity.hasBasePack(new File(explicitDir))) {
                game = new File(explicitDir);
            } else {
                game = LauncherActivity.findGameDataDir(this);
            }
        }

        try {
            // SDL sets the orientation from the window's size when it creates or fullscreens it, over the manifest's.
            Os.setenv("SDL_ORIENTATIONS", "LandscapeLeft LandscapeRight", true);
            if (game == null) {
                File demoDir = new File(getFilesDir(), "l0");
                copyDemo(demoDir);
                Os.setenv("OBL_PACK", "l0", true);
                Log.i(TAG, "game directory (demo) " + demoDir);
                return new String[] {"--game-dir", demoDir.getAbsolutePath(), "--pack", "l0", "--no-settings"};
            }
        } catch (Exception e) {
            Log.e(TAG, "the L0 demo could not be laid out", e);
        }
        if (game == null) game = new File(getFilesDir(), "l0");
        Log.i(TAG, "game directory " + game);
        return new String[] {"--game-dir", game.getAbsolutePath(), "--no-settings"};
    }

    // The demo's pack is in the APK's assets (assets/l0/packs/l0); it is copied once per installed version and its saves stay.
    private void copyDemo(File demo) throws Exception {
        String version = Long.toString(getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime);
        File marker = new File(demo, "packs/l0/.installed");
        if (marker.isFile() && new String(Files.readAllBytes(marker.toPath()), StandardCharsets.UTF_8).equals(version)) return;
        copyAsset(getAssets(), "l0/packs/l0", new File(demo, "packs/l0"));
        Files.write(marker.toPath(), version.getBytes(StandardCharsets.UTF_8));
    }

    private static void copyAsset(AssetManager assets, String path, File target) throws IOException {
        String[] children = assets.list(path);
        if (children != null && children.length > 0) {
            if (!target.isDirectory() && !target.mkdirs()) throw new IOException("cannot create " + target);
            for (String child : children) copyAsset(assets, path + "/" + child, new File(target, child));
            return;
        }
        try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
            byte[] buffer = new byte[65536];
            for (int n; (n = in.read(buffer)) > 0;) out.write(buffer, 0, n);
        }
    }

    /** Downloads an HTTPS resource up to maxBytes for mod catalogs (called via JNI from http_fetch.cpp). */
    public static byte[] fetchHttps(String urlString, int maxBytes) {
        if (urlString == null || !urlString.startsWith("https://") || maxBytes <= 0) return null;
        HttpURLConnection conn = null;
        try {
            URL url = new URL(urlString);
            conn = (HttpURLConnection) url.openConnection();
            conn.setInstanceFollowRedirects(false);
            conn.setConnectTimeout(15000);
            conn.setReadTimeout(30000);
            conn.setRequestProperty("User-Agent", "OpenBeach");
            if (conn.getResponseCode() != HttpURLConnection.HTTP_OK) return null;
            try (InputStream in = conn.getInputStream(); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
                byte[] buf = new byte[16384];
                int total = 0;
                for (int n; (n = in.read(buf)) > 0;) {
                    total += n;
                    if (total > maxBytes) return null;
                    out.write(buf, 0, n);
                }
                return out.toByteArray();
            }
        } catch (IOException e) {
            Log.w(TAG, "HTTPS fetch failed for " + urlString, e);
            return null;
        } finally {
            if (conn != null) conn.disconnect();
        }
    }
}
