package de.amnoid.hallo;

import java.util.ArrayList;
import java.util.List;

import android.animation.ObjectAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View;

public class MyView extends View {

	private Path currentPath_;
	private List<Path> paths_ = new ArrayList<Path>();
	private Paint paint_;
	private GestureDetector detector_;

	public MyView(Context context) {
		super(context);
		setBackgroundColor(0xFFFFFFFF);

		paint_ = new Paint();
        paint_.setAntiAlias(true);
        paint_.setColor(Color.RED);
        paint_.setStyle(Paint.Style.STROKE);

        detector_ = new GestureDetector(context, new GestureDetector.SimpleOnGestureListener() {
        	@Override
			public boolean onDoubleTap(MotionEvent event) {
        		handleDoubleTap();
        		return true;
        	}
        });
	}

	private void handleDoubleTap() {
		// Just give some visual feedback for now.
		// XXX serialize model object and send it somewhere for storage of training data.
		ObjectAnimator.ofFloat(this, "alpha", 0f, 1f).setDuration(100).start();
	}

	@Override
	public boolean onTouchEvent(MotionEvent event) {
		if (detector_.onTouchEvent(event))
			return true;

		switch (event.getActionMasked()) {
		case MotionEvent.ACTION_DOWN:
			currentPath_ = new Path();
			currentPath_.moveTo(event.getX(), event.getY());
			break;
		case MotionEvent.ACTION_MOVE:
			  // XXX should look at get(Historical)EventTime()
			final int historySize = event.getHistorySize();
			 for (int h = 0; h < historySize; h++)
				 currentPath_.lineTo(event.getHistoricalX(h), event.getHistoricalY(h));
			 currentPath_.lineTo(event.getX(), event.getY());
			 break;
		case MotionEvent.ACTION_UP:
			// XXX add final point to currentPath_?
			if (currentPath_ != null)
				paths_.add(currentPath_);
			currentPath_ = null;
			break;
		case MotionEvent.ACTION_CANCEL:
			currentPath_ = null;
			break;
		}
		invalidate();  // XXX overly eager
		return true;
	}

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        for (Path p : paths_)
        	canvas.drawPath(p, paint_);
        if (currentPath_ != null)
        	canvas.drawPath(currentPath_, paint_);
    }
}
