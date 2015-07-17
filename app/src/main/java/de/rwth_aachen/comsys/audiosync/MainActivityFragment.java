package de.rwth_aachen.comsys.audiosync;

import android.app.Activity;
import android.app.Fragment;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.TextView;


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

    public void resetUI() {
        mStatusTV.setText("Select an action");
        mSendButton.setEnabled(true);
        mListenButton.setEnabled(true);
    }
}
