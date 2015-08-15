package de.rwth_aachen.comsys.audiosync.ui;

import android.content.Context;
import android.util.Log;
import android.view.ActionProvider;
import android.view.MenuItem;
import android.view.View;

import de.rwth_aachen.comsys.audiosync.R;

/**
 * Created by Jan on 15.08.2015.
 */
public class PlaybackModeActionProvider extends android.support.v4.view.ActionProvider {
    private Context mContext;

    public PlaybackModeActionProvider(Context context) throws Exception {
        super(context);

        mContext = context;
    }

    private boolean playLocal = true;

    public View onCreateActionView() {
        return null;
    }

    private MenuItem mItem;

    @Override
    public View onCreateActionView(MenuItem context) {
        mItem = context;
        return null;
    }

    @Override
    public boolean onPerformDefaultAction ()
    {
        playLocal = !playLocal;
        if(playLocal)
            mItem.setIcon(R.drawable.playmode_local);
        else
            mItem.setIcon(R.drawable.playmode_stream);
        return true;
    }
}
