package de.amnoid.hallo;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;

public class HalloActivity extends Activity {
    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // setContentView(R.layout.main);
        View view = new MyView(this);
        setContentView(view);
    }
}