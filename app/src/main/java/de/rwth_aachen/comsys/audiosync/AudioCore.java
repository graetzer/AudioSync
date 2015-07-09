package de.rwth_aachen.comsys.audiosync;

import android.content.Context;
import android.media.AudioManager;

/**
 * Created by simon on 27.06.15.
 */
public class AudioCore {

    public void setup(Context ctx) {
        AudioManager audioManager = (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
        String sr = audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE);
        String bs = audioManager.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER);
        initAudio(Integer.parseInt(sr), Integer.parseInt(bs));
    }

    private native void initAudio(int sample_rate, int buf_size);

    /**
     * Starts the server
     * Start sending data to clients, setup sntp server in response
     * mode on the specified port
     * @param path Path to a file
     */
    private native void startStreaming(String path);

    /**
     * Starts the clients
     * @param serverHost Hostname of the server
     */
    private native void startListening(String serverHost);
    
    /**
     * Stops the server/client if running
     */
    private native void stopPlayback();

    static {
        System.loadLibrary("AudioCore");
    }
}
