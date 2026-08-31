package io.tacklebox.wallet;

import android.os.Bundle;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

/**
 * Thin wrapper over SDL3's activity. SDL loads libSDL3.so and libmain.so
 * (the TackleBox native build) and forwards lifecycle, touch, and text input.
 */
public class TackleBoxActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // A wallet's screen has no business in the recents carousel or on
        // screenshots taken by other apps.
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_SECURE,
                             WindowManager.LayoutParams.FLAG_SECURE);
    }
}
