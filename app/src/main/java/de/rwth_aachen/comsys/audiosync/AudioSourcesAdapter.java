package de.rwth_aachen.comsys.audiosync;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.TextView;

import java.util.ArrayList;

/**
 * Created by Jan on 27.07.2015.
 */
public class AudioSourcesAdapter extends ArrayAdapter<AudioDestination> {
    public AudioSourcesAdapter(Context context, ArrayList<AudioDestination> users) {
        super(context, 0, users);
    }

    @Override
    public View getView(int position, View convertView, ViewGroup parent) {
        // Get the data item for this position
        AudioDestination source = getItem(position);
        // Check if an existing view is being reused, otherwise inflate the view
        if (convertView == null) {
            convertView = LayoutInflater.from(getContext()).inflate(R.layout.item_rtpsource, parent, false);
        }

        if(source == null)
            return convertView;

        TextView tvName = (TextView)convertView.findViewById(R.id.rtpSourceName);
        TextView tvJitter = (TextView)convertView.findViewById(R.id.rtpJitter);
        TextView tvPacketLoss = (TextView)convertView.findViewById(R.id.rtpLostPackets);
        TextView tvTimeOffset = (TextView)convertView.findViewById(R.id.rtpTimeOffset);

        tvName.setText(source.name);
        tvJitter.setText(String.valueOf(source.jitter));
        tvPacketLoss.setText(String.valueOf(source.packetsLost));
        tvTimeOffset.setText(String.valueOf(source.timeOffset));
        // Lookup view for data population
       // TextView tvName = (TextView) convertView.findViewById(R.id.tvName);
        //TextView tvHome = (TextView) convertView.findViewById(R.id.tvHome);
        // Populate the data into the template view using the data object
        //tvName.setText(user.name);
       // tvHome.setText(user.hometown);
        // Return the completed view to render on screen
        return convertView;
    }
}