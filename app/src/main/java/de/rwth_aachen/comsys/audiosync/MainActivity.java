package de.rwth_aachen.comsys.audiosync;

import android.app.Activity;
import android.app.Fragment;
import android.net.nsd.NsdServiceInfo;
import android.os.Bundle;
import android.view.Menu;
import android.view.MenuItem;
import android.widget.Toast;

import java.util.Random;


public class MainActivity extends Activity implements MainActivityFragment.ICallbacks {
    private AudioCore mAudioCore;
    private NsdHelper mNSDHelper;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        mAudioCore = new AudioCore(this);
        mNSDHelper = new NsdHelper(this);
        mNSDHelper.initializeNsd();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        mAudioCore.cleanup();
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.menu_main, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // Handle action bar item clicks here. The action bar will
        // automatically handle clicks on the Home/Up button, so long
        // as you specify a parent activity in AndroidManifest.xml.
        int id = item.getItemId();

        //noinspection SimplifiableIfStatement
        if (id == R.id.action_settings) {
            return true;
        }

        return super.onOptionsItemSelected(item);
    }

    @Override
    protected void onPause() {
        super.onPause();
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
