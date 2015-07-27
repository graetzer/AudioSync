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

    @Override
    protected void finalize() throws Throwable {
        super.finalize();
        deinitAudio();
    }

    private void setup(Context ctx) {
        AudioManager audioManager = (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
        String sr = audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE);
        String bs = audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER);
        initAudio(Integer.parseInt(sr), Integer.parseInt(bs));

        mAssetManager = ctx.getAssets();
    }

    public void startPlaying(int portbase) {
        startStreaming(portbase, mAssetManager, "background.mp3");
        //startStreaming(portbase, mAssetManager, "mandelsson.mp3");
        /*Thread t = new Thread() {
            @Override
            public void run() {
                Log.d("AudioCore", "Started AudioCore thread");

            }
        };
        t.start();*/
    }

    public void cleanup() {
        Thread t = new Thread() {
            @Override
            public void run() {
                deinitAudio();
            }
        };
        t.start();
    }

    public void stopPlaying() {
        Thread t = new Thread() {
            @Override
            public void run() {
                stopServices();// TODO figure out a better name
            }
        };
        t.start();
    }

    private native void initAudio(int samplesPerSec, int framesPerBuffer);
    private native void deinitAudio();

    /**
     * Starts the server
     * Start sending data to clients, setup sntp server in response
     * mode on the specified port
     *
     * @param path Path to a file
     */
    private native void startStreaming(int portbase, AssetManager assetManager, String path);

    /**
     * Starts the clients
     *
     * @param serverHost Hostname of the server
     */
    public native void startReceiving(String serverHost, int portbase);

    /**
     * Stops the server/client if running. (Basically stop playing music)
     */
    private native void stopServices();

    /**
     * Returns the number of available RTPSources
     */
    public native int getRtpSourceCount();

    /**
     * Returns the number of available RTPSources
     */
    public native String getRtpSourceName(int index);
    /**
     * Returns the name of the rtp source
     */
    public native int getRtpSourceJitter(int index);
    /**
     * Returns the number of lost packets
     */
    public native int getRtpSourcePacketsLost(int index);
    /**
     * Returns the time offset in ms
     */
    public native int getRtpSourceTimeOffset(int index);
    /**
     * Returns true if the RTP Source sent data in the past
     */
    public native boolean getRtpSourceSender(int index);

    static {
        System.loadLibrary("AudioCore");
    }
}
