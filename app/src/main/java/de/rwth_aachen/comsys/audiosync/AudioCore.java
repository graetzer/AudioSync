package de.rwth_aachen.comsys.audiosync;

import android.content.Context;
import android.content.res.AssetManager;
import android.media.AudioManager;
import android.util.Log;

/**
 * Created by simon on 27.06.15.
 */
public class AudioCore {
    private AssetManager mAssetManager;

    public AudioCore(Context ctx) {
        setup(ctx.getApplicationContext());
    }

    private void setup(Context ctx) {
        AudioManager audioManager = (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
        String sr = audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE);
        String bs = audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER);
        initAudio(Integer.parseInt(sr), Integer.parseInt(bs));

        mAssetManager = ctx.getAssets();
    }

    public void startPlaying() {

        Thread t = new Thread() {
            @Override
            public void run() {
                Log.d("AudioCore", "Started AudioCore thread");
                startStreaming(mAssetManager, "background.mp3");
            }
        };
        t.start();
    }

    private native void initAudio(int samplesPerSec, int framesPerBuffer);

    /**
     * Starts the server
     * Start sending data to clients, setup sntp server in response
     * mode on the specified port
     * @param path Path to a file
     */
    private native void startStreaming(AssetManager assetManager, String path);

    /**
     * Starts the clients
     * @param serverHost Hostname of the server
     */
    private native void startListening(String serverHost);
    
    /**
     * Stops the server/client if running
     */
    public native void stopPlayback();

    static {
        System.loadLibrary("AudioCore");
    }
}
