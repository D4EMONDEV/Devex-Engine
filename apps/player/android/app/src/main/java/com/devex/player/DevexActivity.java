package com.devex.player;

import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

// The activity of a Devex game: SDL's, with the libraries of the engine, and the files of the engine
// (shaders, resources) extracted from assets/devex into devex/ of the internal storage, where the
// engine looks for them. The package of the game stays in the APK, read where it is stored.
public class DevexActivity extends SDLActivity {
    private static final String TAG = "Devex";

    @Override
    protected String[] getLibraries() {
        // SDL is linked into the engine library.
        return new String[] {"c++_shared", "devex-engine", "main"};
    }

    // The orientation is the manifest's, which the export sets, rather than SDL's choice from the
    // size of the window.
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        extractEngineFiles();
        super.onCreate(savedInstanceState);
    }

    // Once per installed version: the files of an update replace those of the previous one.
    private void extractEngineFiles() {
        final File target = new File(getFilesDir(), "devex");
        final File stamp = new File(target, ".version");
        String version;
        try {
            final PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            version = info.versionCode + "-" + info.lastUpdateTime;
        } catch (Exception exception) {
            version = "unknown";
        }
        if (version.equals(readText(stamp))) {
            return;
        }
        deleteTree(target);
        try {
            copyTree(getAssets(), "devex", target);
            writeText(stamp, version);
        } catch (IOException exception) {
            Log.e(TAG, "Cannot extract the files of the engine", exception);
        }
    }

    private static void copyTree(AssetManager assets, String path, File target) throws IOException {
        final String[] children = assets.list(path);
        if (children == null || children.length == 0) {
            // A file: the asset manager lists no children for it.
            target.getParentFile().mkdirs();
            try (InputStream input = assets.open(path); OutputStream output = new FileOutputStream(target)) {
                final byte[] buffer = new byte[64 * 1024];
                for (int read; (read = input.read(buffer)) > 0; ) {
                    output.write(buffer, 0, read);
                }
            }
            return;
        }
        target.mkdirs();
        for (String child : children) {
            copyTree(assets, path + "/" + child, new File(target, child));
        }
    }

    private static void deleteTree(File file) {
        final File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        file.delete();
    }

    private static String readText(File file) {
        try (InputStream input = new FileInputStream(file)) {
            return new String(input.readAllBytes(), StandardCharsets.UTF_8);
        } catch (IOException exception) {
            return null;
        }
    }

    private static void writeText(File file, String text) throws IOException {
        try (OutputStream output = new FileOutputStream(file)) {
            output.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }
}
