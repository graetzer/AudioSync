package de.rwth_aachen.comsys.audiosync;

import android.app.Activity;
import android.app.Fragment;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ListView;
import android.widget.TextView;
import java.util.ArrayList;


/**
 * A placeholder fragment containing a simple view.
 */
public class MainActivityFragment extends Fragment {
    private ICallbacks mCallbacks;
    private TextView mStatusTV;
    private Button mListenButton, mSendButton;

    public MainActivityFragment() {
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_main, container, false);
    }

    @Override
    public void onViewCreated(View view, Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        mStatusTV = (TextView) view.findViewById(R.id.textView_status);
        mListenButton = (Button) view.findViewById(R.id.button_listen);
        mSendButton = (Button) view.findViewById(R.id.button_send);
        mSendButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                mStatusTV.setText("Sending");
                mListenButton.setEnabled(false);
                mSendButton.setEnabled(false);
                mCallbacks.startSending();
            }
        });

        mListenButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                mStatusTV.setText("Listening");
                mListenButton.setEnabled(false);
                mSendButton.setEnabled(false);
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
    final ArrayList<AudioSource> rtpSourceList = new ArrayList<AudioSource>();

    public void updateStats() {
        while(!getActivity().isDestroyed()) {
            try {
                Thread.sleep(500);
            } catch (InterruptedException ex) {
                break;
            }

            rtpSourceList.clear();
            for (int i = 0; i < mAudioCore.getRtpSourceCount(); ++i) {
                AudioSource source = new AudioSource();

                source.name = mAudioCore.getRtpSourceName(i);
                source.jitter = mAudioCore.getRtpSourceJitter(i);
                source.timeOffset = mAudioCore.getRtpSourceTimeOffset(i);
                source.packetsLost = mAudioCore.getRtpSourcePacketsLost(i);

                rtpSourceList.add(source);
            }

            this.getActivity().runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    rtpSourceListAdapter.notifyDataSetChanged();
                }
            });
        }
    }

    @Override
    public void onAttach(Activity activity) {
        super.onAttach(activity);
        mCallbacks = (ICallbacks)activity;
    }

    interface ICallbacks {
        void startSending();
        void startListening();
    }

    AudioCore mAudioCore;

    public void setAudioCore(AudioCore core) {
        mAudioCore = core;
    }

    public void resetUI() {
        mStatusTV.setText("Select an action");
        mSendButton.setEnabled(true);
        mListenButton.setEnabled(true);
    }
}
