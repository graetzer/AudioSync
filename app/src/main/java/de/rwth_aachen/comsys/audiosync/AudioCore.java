package de.rwth_aachen.comsys.audiosync;

import android.content.Context;
import android.content.res.AssetManager;
import android.media.AudioManager;
import android.util.Log;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/**
 * @author simon@graetzer.org
 */
public class AudioCore {
    private static final String TAG = AudioCore.class.getSimpleName();
    private AssetManager mAssetManager;
    private ExecutorService mPool = Executors.newSingleThreadExecutor();
    private AudioCore(Context ctx) {
        setup(ctx.getApplicationContext());
    }

    private static AudioCore instance;
    public static AudioCore getInstance(Context ctx) {
        if (instance == null) instance = new AudioCore(ctx);
        return instance;
    }

    @Override
    protected void finalize() throws Throwable {
        super.finalize();
        deinitAudio();
    }

    private void setup(Context ctx) {
        mAssetManager = ctx.getAssets();
        AudioManager audioManager = (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
        final String sr = audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE);
        final String bs = audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER);

        mPool.execute(new Runnable() {
            @Override
            public void run() {
                initAudio(Integer.parseInt(sr), Integer.parseInt(bs));
            }
        });
    }

    public void startPlayingFile(final int portbase, final String path) {
        mPool.submit(new Runnable() {
            @Override
            public void run() {
                startStreamingUri(portbase, path);
            }
        });
    }

    public void startPlayingAsset(int portbase, String name) {
        startStreamingAsset(portbase, mAssetManager, name);
    }

    public void startListening(final String serverHost, final int portbase) {
        mPool.submit(new Runnable() {
            @Override
            public void run() {
                startReceiving(serverHost, portbase);
            }
        });
    }

    public void cleanup() {
        mPool.execute(new Runnable() {
            @Override
            public void run() {
                deinitAudio();
            }
        });
    }

    public void stopPlaying() {
        Future<?> task = mPool.submit(new Runnable() {
            @Override
            public void run() {
                stopServices();// TODO figure out a better name
            }
        });
    }

    public native void setDeviceLatency(long latencyMs);

    private native void initAudio(int samplesPerSec, int framesPerBuffer);
    private native void deinitAudio();

    /**
     * Starts the server
     * Start sending data to clients, setup sntp server in response
     * mode on the specified port
     *
     * @param name Name of a file
     */
    private native void startStreamingAsset(int portbase, AssetManager assetManager, String name);

    private native void startStreamingUri(int portbase, String path);

    /**
     * Starts the clients
     *
     * @param serverHost Hostname of the server
     */
    private native void startReceiving(String serverHost, int portbase);

    /**
     * Stops the server/client if running. (Basically stop playing music)
     */
    private native void stopServices();

    /**
     * Returns the number of available RTPSources
     */
    public native AudioDestination[] getAudioDestinations();

    /**
     * Return current presentation time in milliseconds
     * */
    public native long getCurrentPresentationTime();

    public native boolean isRunning();

    public native boolean isSending();

    public void pauseStreaming() {
        mPool.submit(new Runnable() {
            @Override
            public void run() {
                pauseSending();
            }
        });
    }

    /**
     * Only possible if sending
     */
    private native void pauseSending();

    static {
        System.loadLibrary("AudioCore");
    }
}
