package de.rwth_aachen.comsys.audiosync.ui;

import android.content.Context;
import android.content.Intent;
import android.util.Log;
import android.view.ActionProvider;
import android.view.MenuItem;
import android.view.View;

import de.rwth_aachen.comsys.audiosync.MusicService;
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

    private static boolean playLocal = true;

    public View onCreateActionView() {
        return null;
    }

    private MenuItem mItem;
    boolean iconSet = false;

    @Override
    public View onCreateActionView(MenuItem context) {
        mItem = context;
        if(!iconSet) {
            updateIcon();
            iconSet = true;
        }
        return null;
    }

    private void updateIcon()
    {
        if(playLocal)
            mItem.setIcon(R.drawable.playmode_local);
        else
            mItem.setIcon(R.drawable.playmode_stream);
    }

    @Override
    public boolean onPerformDefaultAction ()
    {
        playLocal = !playLocal;

        Intent i = new Intent(mContext, MusicService.class);
        i.setAction(MusicService.ACTION_CMD);
        i.putExtra(MusicService.CMD_NAME, playLocal ? MusicService.CMD_STOP_CASTING : MusicService.CMD_START_CASTING);
        mContext.startService(i);

        updateIcon();
        return true;
    }
}
