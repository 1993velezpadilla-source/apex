package __APEX_PACKAGE__;

import android.os.SystemClock;
import android.util.Log;
import android.view.Choreographer;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import java.util.Arrays;
import org.json.JSONArray;
import org.json.JSONObject;

/** Explicit adb-only exercise of the real JUCE touch path and Component tree.
 * Choreographer intervals measure UI-thread callback cadence, NOT presented FPS.
 * No input simulation, file writes or polling occur in normal app launches. */
public final class ApexUiBenchmark implements Choreographer.FrameCallback {
    private static final String TAG = "APEX_UI_BENCH";
    private static final String[] PHASES = {"pan", "pinch", "idle", "reverse"};
    private static final long PHASE_NS = 2500000000L;
    private final ApexImmersiveActivity activity;
    private final View decor;
    private final int left, top, width, height;
    private final MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[2];
    private final MotionEvent.PointerCoords[] coordinates = new MotionEvent.PointerCoords[2];
    private final long[] intervals = new long[2048];
    private final JSONArray phases = new JSONArray();
    private int phase = 0, samples = 0, eventCount = 0;
    private long phaseStart = 0, lastFrame = 0, downTime = 0;
    private boolean stopped = false, touching = false;

    public ApexUiBenchmark(ApexImmersiveActivity owner, int x, int y, int w, int h) {
        activity = owner;
        decor = owner.getWindow().getDecorView();
        left = x; top = y; width = w; height = h;
        for (int i = 0; i < 2; ++i) {
            properties[i] = new MotionEvent.PointerProperties();
            properties[i].id = i;
            properties[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
            coordinates[i] = new MotionEvent.PointerCoords();
            coordinates[i].pressure = coordinates[i].size = 1.0f;
        }
    }

    public void start() {
        Log.i(TAG, "READY " + left + "," + top + "," + width + "," + height);
        if (width < 80 || height < 80) {
            fail(new IllegalStateException("No real visible timeline body"));
            return;
        }
        // Let the host capture a baseline frame and let initial peak jobs settle.
        decor.postDelayed(() -> Choreographer.getInstance().postFrameCallback(this), 1500);
    }

    public void stop() {
        stopped = true;
        Choreographer.getInstance().removeFrameCallback(this);
        if (touching) send(MotionEvent.ACTION_CANCEL, phase == 1 ? 2 : 1);
        touching = false;
    }

    private void send(int action, int count) {
        MotionEvent event = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), action,
            count, properties, coordinates, 0, 0, 1.0f, 1.0f, 0, 0,
            InputDevice.SOURCE_TOUCHSCREEN, 0);
        decor.dispatchTouchEvent(event);
        event.recycle();
        ++eventCount;
    }

    private void position(double progress) {
        if (phase == 1) {
            final double spread = 0.13 + progress * 0.20;
            coordinates[0].x = (float)(left + width * (0.5 - spread));
            coordinates[1].x = (float)(left + width * (0.5 + spread));
            coordinates[0].y = (float)(top + height * (0.5 - spread * 0.3));
            coordinates[1].y = (float)(top + height * (0.5 + spread * 0.3));
        } else {
            coordinates[0].x = (float)(left + width *
                (phase == 3 ? 0.22 + progress * 0.55 : 0.82 - progress * 0.55));
            coordinates[0].y = top + height * 0.82f;
        }
    }

    private JSONObject state() throws Exception {
        return new JSONObject(activity.nativeNavigationState());
    }

    @Override public void doFrame(long frameTimeNanos) {
        if (stopped) return;
        try {
            if (phaseStart == 0) {
                phaseStart = frameTimeNanos;
                downTime = SystemClock.uptimeMillis();
                samples = 0;
                position(0);
                JSONObject entry = new JSONObject();
                entry.put("phase", PHASES[phase]);
                entry.put("before", state());
                phases.put(entry);
                if (phase != 2) {
                    send(MotionEvent.ACTION_DOWN, 1);
                    if (phase == 1)
                        send(MotionEvent.ACTION_POINTER_DOWN | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT), 2);
                    touching = true;
                }
            } else if (lastFrame != 0 && samples < intervals.length) {
                intervals[samples++] = frameTimeNanos - lastFrame;
            }
            lastFrame = frameTimeNanos;
            final double progress = Math.min(1.0, (frameTimeNanos - phaseStart) / (double)PHASE_NS);
            if (phase != 2) {
                position(progress);
                send(MotionEvent.ACTION_MOVE, phase == 1 ? 2 : 1);
            }
            if (progress >= 1.0) {
                if (touching) {
                    if (phase == 1)
                        send(MotionEvent.ACTION_POINTER_UP | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT), 2);
                    send(MotionEvent.ACTION_UP, 1);
                    touching = false;
                }
                final long[] sorted = Arrays.copyOf(intervals, samples);
                Arrays.sort(sorted);
                JSONObject entry = phases.getJSONObject(phase);
                entry.put("after", state());
                entry.put("ui_callback_samples", samples);
                entry.put("ui_interval_p50_ms", percentile(sorted, 0.50));
                entry.put("ui_interval_p95_ms", percentile(sorted, 0.95));
                entry.put("ui_interval_p99_ms", percentile(sorted, 0.99));
                entry.put("ui_interval_worst_ms", percentile(sorted, 1.0));
                ++phase;
                phaseStart = lastFrame = 0;
                if (phase == PHASES.length) {
                    stopped = true;
                    JSONObject result = new JSONObject();
                    result.put("renderer_requested", activity.getIntent().getStringExtra("apex_renderer"));
                    result.put("scene", activity.getIntent().getStringExtra("apex_scene"));
                    result.put("metric", "UI Choreographer callback intervals; not presented FPS");
                    result.put("events", eventCount);
                    result.put("phases", phases);
                    Log.i(TAG, "RESULT " + result);
                    return;
                }
            }
            Choreographer.getInstance().postFrameCallback(this);
        } catch (Exception error) { fail(error); }
    }

    private double percentile(long[] sorted, double fraction) {
        if (sorted.length == 0) return 0;
        return sorted[(int)((sorted.length - 1) * fraction)] / 1000000.0;
    }

    private void fail(Exception error) {
        stop();
        Log.e(TAG, "RESULT {\"error\":\"" + error.getClass().getSimpleName() + "\"}", error);
    }
}
