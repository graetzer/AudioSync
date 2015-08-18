package de.rwth_aachen.comsys.audiosync;

import android.app.Activity;
import android.app.Fragment;
import android.content.Intent;
import android.net.nsd.NsdServiceInfo;
import android.os.Bundle;
import android.view.Menu;
import android.view.MenuItem;
import android.widget.Toast;

import java.util.Random;

import de.rwth_aachen.comsys.audiosync.ui.BaseActivity;


public class MainActivity extends BaseActivity implements MainActivityFragment.ICallbacks {
    private AudioCore mAudioCore;
    private NsdHelper mNSDHelper;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        Intent i = new Intent(this.getApplicationContext(), MusicService.class);
        i.setAction(MusicService.ACTION_CMD);
        i.putExtra(MusicService.CMD_NAME, MusicService.CMD_STOP_CASTING);
        startService(i);

        mAudioCore = MusicService.mAudioCore;
        mNSDHelper = MusicService.mNSDHelper;

        Fragment frag = getFragmentManager().findFragmentById(R.id.fragment);
        if (frag instanceof MainActivityFragment) {
            ((MainActivityFragment)frag).setAudioCore(mAudioCore);
        }

        initializeToolbar();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
    }

    @Override
    public void onPause() {
        super.onPause();
        mNSDHelper.unregisterService();
        mAudioCore.stopPlaying();

        Fragment frag = getFragmentManager().findFragmentById(R.id.fragment);
        if (frag instanceof MainActivityFragment) {
            ((MainActivityFragment)frag).resetUI();
        }
    }

    @Override
    public void startSending() {
        // Choose a port over 5000 to avoid automatically assigned ports
        int port = 21212;//5000 + (int) (Math.random() * 10000);
        if (port % 2 != 0) port++;// RTP has to be an even port number
        mNSDHelper.registerService(port);
        mAudioCore.startPlaying(port);
    }

    @Override
    public void startListening() {
        // TODO it might be the easier solution to put format information in the Service attributes
        mNSDHelper.discoverServices(new NsdHelper.ServiceResolvedCallback() {
            @Override
            public void onServiceResolved(NsdServiceInfo serviceInfo) {
                String host = serviceInfo.getHost().getHostAddress();
                int port = serviceInfo.getPort();
                if (host != null && port % 2 == 0) {
                    mAudioCore.startReceiving(host, port);
                } else {
                    Toast.makeText(MainActivity.this, "Something is fishy, RTP ports must be even", Toast.LENGTH_LONG).show();
                }
            }
        });
    }
}
