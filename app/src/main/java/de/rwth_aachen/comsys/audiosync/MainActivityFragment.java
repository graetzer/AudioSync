package de.rwth_aachen.comsys.audiosync;

import android.app.Activity;
import android.app.Fragment;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ListView;
import android.widget.TextView;
import java.util.ArrayList;
import java.util.Arrays;


/**
 * A placeholder fragment containing a simple view.
 */
public class MainActivityFragment extends Fragment {
    private ICallbacks mCallbacks;
    private TextView mStatusTV;
    private Button mListenButton;

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
       /* mSendButton = (Button) view.findViewById(R.id.button_send);
        mSendButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                mStatusTV.setText("Sending");
                mListenButton.setEnabled(false);
                mSendButton.setEnabled(false);
                mCallbacks.startSending();
            }
        });*/

        mListenButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                mStatusTV.setText("Listening");
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
    final ArrayList<AudioDestination> rtpSourceList = new ArrayList<AudioDestination>();

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
        void startSending();
        void startListening();
    }

    AudioCore mAudioCore;

    public void setAudioCore(AudioCore core) {
        mAudioCore = core;
    }

    public void resetUI() {
        mStatusTV.setText("Select an action");
        //mSendButton.setEnabled(true);
        mListenButton.setEnabled(true);
    }
}
