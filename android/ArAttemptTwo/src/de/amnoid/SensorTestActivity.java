package de.amnoid;

import android.app.Activity;
import android.content.Context;
import android.graphics.PixelFormat;
import android.hardware.Camera;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Bundle;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Window;
import java.io.IOException;
import java.util.Date;

// Records, all with timestamps:
// * Accelerometer data (raw accel; processed gravity & linear accel)
// * Gyro data (raw; processed orientation)
// * Video frames
// XXX magnetic field data?
// XXX distance sensor?
public class SensorTestActivity extends Activity implements SensorEventListener, LocationListener { 
	private final SensorManager mSensorManager;
	private final Sensor mAccelerometer;
	private final Sensor mGyro;

    public SensorTestActivity() {
        mSensorManager = (SensorManager)getSystemService(SENSOR_SERVICE);
        mAccelerometer = mSensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
        mGyro= mSensorManager.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
        // TYPE_GYROSCOPE
        // TYPE_ACCELERATOR = TYPE_LINEAR_ACCELERATION + TYPE_GRAVITY
        // TYPE_MAGNETIC_FIELD
        // TYPE_ROTATION_VECTOR / TYPE_ORIENTATION
    }

    protected void onResume() {
        super.onResume();
        mSensorManager.registerListener(this, mAccelerometer, SensorManager.SENSOR_DELAY_FASTEST);
        mSensorManager.registerListener(this, mGyro, SensorManager.SENSOR_DELAY_FASTEST);
    }

    protected void onPause() {
        super.onPause();
        mSensorManager.unregisterListener(this);
    }

    public void onAccuracyChanged(Sensor sensor, int accuracy) {
    }

    public void onSensorChanged(SensorEvent event) {
    	// http://developer.android.com/reference/android/hardware/SensorEvent.html
    	if (event.sensor.getType() == Sensor.TYPE_ACCELEROMETER) {
    		// xyz linear accel
    	} else if (event.sensor.getType() == Sensor.TYPE_GYROSCOPE) {
    		// xyz radians / sec rotation speed
    	}
 
    	long sensorTimeStamp = event.timestamp;
        long cpuTimeStamp = System.nanoTime();
    }

    @Override
	protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);        
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        
        LocationManager lm = (LocationManager)getSystemService(Context.LOCATION_SERVICE);
        lm.requestLocationUpdates(
        		//LocationManager.GPS_PROVIDER,
        		LocationManager.NETWORK_PROVIDER,
        		/*min time between notifications*/1000L,
        		/*min distance between notifications*/500.0f,
        		this);

        setContentView(new Preview(this));
    }

	//@Override
	public void onLocationChanged(Location location) {
		if (location != null) {
			double lat = location.getLatitude();
			double lon = location.getLongitude();
			// speed, accuracy, altitude
		}
	}

	//@Override
	public void onProviderDisabled(String provider) {
	}

	//@Override
	public void onProviderEnabled(String provider) {		
	}

	//@Override
	public void onStatusChanged(String provider, int status, Bundle extras) {
		// TODO Auto-generated method stub
		
	}
}

class Preview extends SurfaceView implements SurfaceHolder.Callback, Camera.PreviewCallback {
    SurfaceHolder mHolder;
    Camera mCamera;
    
    Preview(Context context) {
        super(context);
        
        mHolder = getHolder();
        mHolder.addCallback(this);
    }

    public void surfaceCreated(SurfaceHolder holder) {
        mCamera = Camera.open();
        try {
           mCamera.setPreviewDisplay(holder);
        } catch (IOException exception) {
            mCamera.release();
            mCamera = null;
        }        
    }

    public void surfaceDestroyed(SurfaceHolder holder) {
        mCamera.stopPreview();
        mCamera.release();
        mCamera = null;
    }

    public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
    	mCamera.stopPreview();

    	// XXX don't hardcode
    	w = 800;
    	h = 480;
    	
        Camera.Parameters parameters = mCamera.getParameters();
        parameters.setPreviewSize(w, h); 
        mCamera.setParameters(parameters);
        PixelFormat p = new PixelFormat();
        PixelFormat.getPixelFormatInfo(parameters.getPreviewFormat(),p);
        int bufSize = (w*h*p.bitsPerPixel)/8;
        
        //Add three buffers to the buffer queue. I re-queue them once they are used in
        // onPreviewFrame, so we should not need many of them.
        byte[] buffer = new byte[bufSize];
        mCamera.addCallbackBuffer(buffer);                            
        buffer = new byte[bufSize];
        mCamera.addCallbackBuffer(buffer);
        buffer = new byte[bufSize];
        mCamera.addCallbackBuffer(buffer);
                                 
        mCamera.setPreviewCallbackWithBuffer(this);  // 30 fps for 800x480
        //mCamera.setPreviewCallback(this);  // 15 fps for 800x480
        mCamera.startPreview();
    }

    Date start;
    int fcount = 0;
	/**
	 * Demonstration of how to use onPreviewFrame. In this case I'm not
	 * processing the data, I'm just adding the buffer back to the buffer
	 * queue for re-use
	 */
	public void onPreviewFrame(byte[] data, Camera camera) {
		// TODO Auto-generated method stub
		if(start == null){
			start = new Date();
		}
		fcount++;
		if(fcount % 30 == 0){
			Date now = new Date();
			double ms = now.getTime() - start.getTime();
			Log.i("AR","fps:" + fcount/(ms/1000.0));
			start = now;
			fcount = 0;
		}
		
		//We are done with this buffer, so add it back to the pool
		mCamera.addCallbackBuffer(data);
	}

}
