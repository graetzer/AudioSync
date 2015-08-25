package de.rwth_aachen.comsys.audiosync;

import android.app.Fragment;
import android.content.Intent;
import android.net.nsd.NsdServiceInfo;
import android.os.Bundle;
import android.widget.Toast;

import de.rwth_aachen.comsys.audiosync.ui.BaseActivity;


public class StatisticsActivity extends BaseActivity implements StatisticsFragment.ICallbacks {
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

        mAudioCore = AudioCore.getInstance(this);
        mNSDHelper = MusicService.mNSDHelper;

        Fragment frag = getFragmentManager().findFragmentById(R.id.fragment);
        if (frag instanceof StatisticsFragment) {
            ((StatisticsFragment)frag).setAudioCore(mAudioCore);
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
        if (frag instanceof StatisticsFragment) {
            ((StatisticsFragment)frag).resetUI();
        }
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
                    mAudioCore.startListening(host, port);
                } else {
                    Toast.makeText(StatisticsActivity.this, "Something is fishy, RTP ports must be even", Toast.LENGTH_LONG).show();
                }
            }
        });
    }
}
