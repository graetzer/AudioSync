package de.rwth_aachen.comsys.audiosync;

import android.app.Activity;
import android.app.Fragment;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.preference.PreferenceManager;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ListView;
import android.widget.SeekBar;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.Arrays;


/**
 * A placeholder fragment containing a simple view.
 */
public class StatisticsFragment extends Fragment {
    private ICallbacks mCallbacks;
    private Button mListenButton;
    private SharedPreferences mPrefs;
    private static final String PREF_DEVICE_LATENCY = "PREF_DEVICE_LATENCY";

    public StatisticsFragment() {
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_statistics, container, false);
    }

    @Override
    public void onViewCreated(View view, Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        mPrefs = PreferenceManager.getDefaultSharedPreferences(getActivity());
        int latencyMs = mPrefs.getInt(PREF_DEVICE_LATENCY, 0);
        AudioCore.getInstance(getActivity()).setDeviceLatency(latencyMs);

        mListenButton = (Button) view.findViewById(R.id.button_listen);
        final TextView numberTv = (TextView) view.findViewById(R.id.textView_latency);
        numberTv.setText("Device Latency: " + latencyMs + "ms");
        SeekBar seekbar = (SeekBar) view.findViewById(R.id.seekBar_latency);
        seekbar.setMax(500);// ms
        seekbar.setProgress(latencyMs);
        seekbar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                numberTv.setText("Device Latency: " + progress + "ms");
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {

            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                int progress = seekBar.getProgress();
                mPrefs.edit().putInt(PREF_DEVICE_LATENCY, progress).apply();
                AudioCore.getInstance(getActivity()).setDeviceLatency(progress);
            }
        });

        mListenButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                mListenButton.setEnabled(false);
                mCallbacks.startListening();
            }
        });

        rtpSourceListAdapter =
                new AudioSourcesAdapter(getActivity(), rtpSourceList);
        ListView rtpSourceListView = (ListView)view.findViewById(R.id.rtpSourceList);
        rtpSourceListView.setAdapter(rtpSourceListAdapter);

        Thread thread = new Thread(new Runnable() {
            @Override
            public void run() {
                updateStats();
            }});
        thread.start();
    }

    AudioSourcesAdapter rtpSourceListAdapter;
    final ArrayList<AudioDestination> rtpSourceList = new ArrayList<>();

    public void updateStats() {
        while(true) {
            Activity activity = getActivity();

            if(activity == null || activity.isDestroyed())
                break;

            try {
                Thread.sleep(1000);
            } catch (InterruptedException ex) {
                break;
            }

            if (mAudioCore != null) {
                AudioDestination dest[] = mAudioCore.getAudioDestinations();
                if (dest != null) {
                    rtpSourceList.clear();// Keep this even if there are no clients
                    rtpSourceList.addAll(Arrays.asList(dest));
                }
            }

            if(this.getActivity() != null) {
                this.getActivity().runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        rtpSourceListAdapter.notifyDataSetChanged();
                    }
                });
            }
        }
    }

    @Override
    public void onAttach(Activity activity) {
        super.onAttach(activity);
        mCallbacks = (ICallbacks)activity;
    }

    interface ICallbacks {
        void startListening();
    }

    AudioCore mAudioCore;

    public void setAudioCore(AudioCore core) {
        mAudioCore = core;
    }

    public void resetUI() {
        mListenButton.setEnabled(true);
    }
}
