package __APEX_PACKAGE__;

import android.os.Build;
import android.os.Debug;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;
import android.util.Log;
import android.view.Choreographer;
import android.view.FrameMetrics;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import android.view.Window;
import java.util.ArrayList;
import java.util.Arrays;
import org.json.JSONArray;
import org.json.JSONObject;

/** Explicit adb-only exercise of the real JUCE touch path and Component tree.
 * Choreographer intervals measure UI-thread callback cadence, NOT presented FPS.
 * No input simulation, file writes or polling occur in normal app launches. */
public final class ApexUiBenchmark implements Choreographer.FrameCallback {
    private static final String TAG = "APEX_UI_BENCH";
    private static final String[] PHASES = {"pan", "pinch", "unpinch", "idle", "reverse",
        "vertical-pan", "vertical-pinch", "vertical-unpinch"};
    private static final String[] COLD_SCROLL_PHASES = {
        "pan", "return", "revisit", "pinch", "post-x-pan", "post-x-return", "post-x-revisit",
        "unpinch", "idle", "reverse", "vertical-pan", "vertical-pinch",
        "post-y-pan", "post-y-return", "post-y-revisit", "vertical-unpinch"};
    private static final long PHASE_NS = 2500000000L;
    private final ApexImmersiveActivity activity;
    private final View decor;
    private final String renderer;
    private final String panReplay;
    private final boolean slowPanReplay, coldScrollReplay, prepareEdges;
    private final boolean leftRulerReplay, rulerAtlas;
    private final String[] phaseNames;
    private final float physicalScale;
    private final int left, top, width, height;
    private final MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[2];
    private final MotionEvent.PointerCoords[] coordinates = new MotionEvent.PointerCoords[2];
    private final long[] intervals = new long[2048];
    private final long[] inputWall = new long[2048];
    private final long[] inputCpu = new long[2048];
    private final long[] phaseStarts, phaseEnds;
    private final ArrayList<long[]> windowFrames = new ArrayList<>();
    private final ArrayList<long[]> rulerPhases = new ArrayList<>();
    private final HandlerThread metricsThread = new HandlerThread("APEX UI benchmark metrics");
    private final Window.OnFrameMetricsAvailableListener frameListener = this::recordWindowFrame;
    private final JSONArray phases = new JSONArray();
    private int phase = 0, samples = 0, eventCount = 0, moves = 0;
    private int droppedWindowFrames = 0;
    private long phaseStart = 0, lastFrame = 0, downTime = 0;
    private boolean stopped = false, touching = false, collectingMetrics = false;

    private static native void nativeUseFrameTiles(boolean enabled, boolean reduceMotion,
                                                 boolean framePaced, boolean suppressMasterPreview,
                                                 boolean reuseHorizontalBitmaps, boolean preciseArrangerDamage,
                                                 boolean retainTranslatedTimeline, boolean legacyFullTreeRaster,
                                                 boolean slowPanReplay, boolean legacyHorizontalFilter,
                                                 boolean prepareViewport, boolean directCanvas,
                                                 boolean optimizedCanvas, boolean rulerNumerals);
    private static native long[] nativeWindowDrawSamples(boolean finish);
    private static native long[] nativeRulerPaintStats();

    public ApexUiBenchmark(ApexImmersiveActivity owner, int x, int y, int w, int h) {
        activity = owner;
        decor = owner.getWindow().getDecorView();
        renderer = owner.getIntent().getStringExtra("apex_renderer");
        final String requestedPan = owner.getIntent().getStringExtra("apex_pan_replay");
        panReplay = requestedPan == null ? "normal" : requestedPan;
        slowPanReplay = !"normal".equals(panReplay);
        coldScrollReplay = owner.getIntent().getBooleanExtra("apex_cold_scroll_replay", false);
        prepareEdges = owner.getIntent().getBooleanExtra("apex_prepare_edges", true);
        leftRulerReplay = "ruler-left".equals(owner.getIntent().getStringExtra("apex_scene"));
        rulerAtlas = owner.getIntent().getBooleanExtra("apex_ruler_atlas", false);
        phaseNames = coldScrollReplay ? COLD_SCROLL_PHASES : PHASES;
        phaseStarts = new long[phaseNames.length];
        phaseEnds = new long[phaseNames.length];
        physicalScale = owner.getResources().getDisplayMetrics().density;
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
        final boolean optimizedCanvas = "optimized-canvas".equals(renderer);
        final boolean directCanvas = optimizedCanvas || "direct-canvas".equals(renderer);
        nativeUseFrameTiles(!"int-array".equals(renderer), "tiles".equals(renderer),
            directCanvas || "frame-paced".equals(renderer),
            activity.getIntent().getBooleanExtra("apex_master_preview_suppressed", false),
            activity.getIntent().getBooleanExtra("apex_horizontal_bitmap_reuse", false),
            activity.getIntent().getBooleanExtra("apex_precise_arranger_damage", false),
            activity.getIntent().getBooleanExtra("apex_retain_translated_timeline", false),
            activity.getIntent().getBooleanExtra("apex_legacy_full_tree_raster", false),
            slowPanReplay, "threshold-filter".equals(panReplay), prepareEdges,
            directCanvas, optimizedCanvas, rulerAtlas);
        if (decor.isHardwareAccelerated()) {
            metricsThread.start();
            activity.getWindow().addOnFrameMetricsAvailableListener(frameListener,
                new Handler(metricsThread.getLooper()));
            collectingMetrics = true;
        }
        // Let the host capture a baseline frame and let initial peak jobs settle.
        decor.postDelayed(() -> Choreographer.getInstance().postFrameCallback(this), 1500);
    }

    public void stop() {
        stopped = true;
        Choreographer.getInstance().removeFrameCallback(this);
        if (touching) send(MotionEvent.ACTION_CANCEL, isPinch() ? 2 : 1);
        touching = false;
        if (collectingMetrics) {
            activity.getWindow().removeOnFrameMetricsAvailableListener(frameListener);
            metricsThread.quitSafely();
            collectingMetrics = false;
        }
    }

    private void recordWindowFrame(Window window, FrameMetrics metrics, int dropped) {
        synchronized (windowFrames) {
            windowFrames.add(new long[] { metrics.getMetric(FrameMetrics.VSYNC_TIMESTAMP),
                metrics.getMetric(FrameMetrics.DRAW_DURATION), metrics.getMetric(FrameMetrics.TOTAL_DURATION),
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ? metrics.getMetric(FrameMetrics.GPU_DURATION) : -1,
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ? metrics.getMetric(FrameMetrics.DEADLINE) : -1 });
            droppedWindowFrames += dropped;
        }
    }

    private void send(int action, int count) {
        MotionEvent event = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), action,
            count, properties, coordinates, 0, 0, 1.0f, 1.0f, 0, 0,
            InputDevice.SOURCE_TOUCHSCREEN, 0);
        if (action == MotionEvent.ACTION_MOVE && moves < inputWall.length) {
            final long cpu = Debug.threadCpuTimeNanos();
            final long start = System.nanoTime();
            decor.dispatchTouchEvent(event);
            inputWall[moves] = System.nanoTime() - start;
            inputCpu[moves] = Debug.threadCpuTimeNanos() - cpu;
            ++moves;
        } else {
            decor.dispatchTouchEvent(event);
        }
        event.recycle();
        ++eventCount;
    }

    private String currentPhase() { return phaseNames[phase]; }
    private boolean isPinch() { return currentPhase().endsWith("pinch"); }

    private float panCoordinate(double start, double span, int direction, double progress) {
        if (!slowPanReplay)
            return (float)(start + direction * progress * span);
        // Move past drag slop before replaying exactly one logical pixel per
        // event. The initial fractional position exercises viewport truncation.
        final double origin = Math.floor(start / physicalScale) + 0.55;
        final double steps = moves == 0 ? 0.0 : Math.min(27 + moves, span / physicalScale);
        return (float)((origin + direction * steps) * physicalScale);
    }

    private void position(double progress) {
        final String name = currentPhase();
        if (name.startsWith("vertical-") && isPinch()) {
            final double spread = 0.13 + ("vertical-pinch".equals(name) ? progress : 1.0 - progress) * 0.20;
            coordinates[0].x = coordinates[1].x = (float)(left + width * 0.55);
            coordinates[0].y = (float)(top + height * (0.5 - spread));
            coordinates[1].y = (float)(top + height * (0.5 + spread));
        } else if ("vertical-pan".equals(name)) {
            coordinates[0].x = (float)(left + width * 0.82);
            coordinates[0].y = panCoordinate(top + height * 0.82, height * 0.55, -1, progress);
        } else if (isPinch()) {
            final double spread = 0.13 + ("pinch".equals(name) ? progress : 1.0 - progress) * 0.20;
            coordinates[0].x = (float)(left + width * (0.5 - spread));
            coordinates[1].x = (float)(left + width * (0.5 + spread));
            coordinates[0].y = coordinates[1].y = (float)(top + height * 0.5);
        } else {
            final boolean returning = "reverse".equals(name) || name.endsWith("return");
            final int direction = (returning ? 1 : -1) * (leftRulerReplay ? -1 : 1);
            // Finish the swipe before lifting. A stationary tail makes the
            // revisit's start/end match the first traversal despite flings.
            final double travel = coldScrollReplay ? Math.min(1.0, progress / 0.78) : progress;
            coordinates[0].x = panCoordinate(left + width * (direction > 0 ? 0.22 : 0.82),
                width * 0.55, direction, travel);
            coordinates[0].y = top + height * 0.82f;
        }
    }

    private JSONObject state(boolean finishPhase) throws Exception {
        return new JSONObject(activity.nativeNavigationState(finishPhase));
    }

    @Override public void doFrame(long frameTimeNanos) {
        if (stopped) return;
        try {
            final String name = currentPhase();
            if (phaseStart == 0) {
                phaseStart = frameTimeNanos;
                phaseStarts[phase] = frameTimeNanos;
                downTime = SystemClock.uptimeMillis();
                samples = 0;
                moves = 0;
                position(0);
                JSONObject entry = new JSONObject();
                entry.put("phase", name);
                entry.put("before", state(false));
                nativeWindowDrawSamples(false);
                phases.put(entry);
                if (!"idle".equals(name)) {
                    send(MotionEvent.ACTION_DOWN, 1);
                    if (isPinch())
                        send(MotionEvent.ACTION_POINTER_DOWN | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT), 2);
                    touching = true;
                }
            } else if (lastFrame != 0 && samples < intervals.length) {
                intervals[samples++] = frameTimeNanos - lastFrame;
            }
            lastFrame = frameTimeNanos;
            final double duration = name.startsWith("vertical-") ? 1_500_000_000L : PHASE_NS;
            final double progress = Math.min(1.0, (frameTimeNanos - phaseStart) / duration);
            if (!"idle".equals(name)) {
                position(progress);
                send(MotionEvent.ACTION_MOVE, isPinch() ? 2 : 1);
            }
            if (progress >= 1.0) {
                if (touching) {
                    if (isPinch())
                        send(MotionEvent.ACTION_POINTER_UP | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT), 2);
                    send(MotionEvent.ACTION_UP, 1);
                    touching = false;
                }
                final long[] sorted = Arrays.copyOf(intervals, samples);
                Arrays.sort(sorted);
                JSONObject entry = phases.getJSONObject(phase);
                entry.put("after", state(true));
                addPeerTimings(entry, nativeWindowDrawSamples(true));
                rulerPhases.add(nativeRulerPaintStats());
                entry.put("ui_callback_samples", samples);
                entry.put("ui_interval_p50_ms", percentile(sorted, 0.50));
                entry.put("ui_interval_p95_ms", percentile(sorted, 0.95));
                entry.put("ui_interval_p99_ms", percentile(sorted, 0.99));
                entry.put("ui_interval_worst_ms", percentile(sorted, 1.0));
                entry.put("inputWallMs", summarize(inputWall, moves));
                entry.put("inputCpuMs", summarize(inputCpu, moves));
                phaseEnds[phase] = frameTimeNanos;
                ++phase;
                phaseStart = lastFrame = 0;
                if (phase == phaseNames.length) {
                    decor.postDelayed(this::finish, 500);
                    return;
                }
                if (coldScrollReplay && ("pinch".equals(name) || "vertical-pinch".equals(name))) {
                    // Give the new scale the same settling interval in both
                    // variants. No pan during this interval visits the region.
                    decor.postDelayed(() -> Choreographer.getInstance().postFrameCallback(this), 500);
                    return;
                }
                if ("pinch".equals(name) && "deep".equals(activity.getIntent().getStringExtra("apex_scene"))) {
                    Log.i(TAG, "PEAK horizontal");
                    decor.postDelayed(() -> Choreographer.getInstance().postFrameCallback(this), 1800);
                    return;
                }
            }
            Choreographer.getInstance().postFrameCallback(this);
        } catch (Exception error) { fail(error); }
    }

    private void finish() {
        if (stopped) return;
        stop();
        try {
            synchronized (windowFrames) {
                final long[] draw = new long[windowFrames.size()];
                final long[] total = new long[windowFrames.size()];
                final long[] gpu = new long[windowFrames.size()];
                for (int i = 0; i < phaseStarts.length; ++i) {
                    int count = 0, gpuCount = 0, knownDeadlines = 0, missedDeadlines = 0;
                    for (long[] frame : windowFrames) {
                        if (frame[0] < phaseStarts[i] || frame[0] > phaseEnds[i]) continue;
                        draw[count] = frame[1];
                        total[count++] = frame[2];
                        if (frame[3] >= 0) gpu[gpuCount++] = frame[3];
                        if (frame[4] > 0 && frame[2] >= 0) {
                            ++knownDeadlines;
                            if (frame[2] >= frame[4]) ++missedDeadlines;
                        }
                    }
                    JSONObject entry = phases.getJSONObject(i);
                    // Android truncates individual logcat records around 4 KiB.
                    // Keep window timings separate from native gesture state.
                    JSONObject windowMetrics = new JSONObject();
                    windowMetrics.put("phase", entry.getString("phase"));
                    windowMetrics.put("windowDrawMs", summarize(draw, count));
                    windowMetrics.put("windowTotalMs", summarize(total, count));
                    windowMetrics.put("windowGpuMs", summarize(gpu, gpuCount));
                    windowMetrics.put("windowKnownDeadlines", knownDeadlines);
                    windowMetrics.put("windowMissedDeadlines", missedDeadlines);
                    final long[] labels = rulerPhases.get(i);
                    JSONObject ruler = new JSONObject();
                    ruler.put("seen", labels[0]);
                    ruler.put("labels", labels[1]);
                    ruler.put("atlas", labels[2]);
                    ruler.put("builds", labels[3]);
                    JSONArray first = new JSONArray();
                    for (int bar = 0; bar < 7; ++bar) {
                        JSONArray sample = new JSONArray();
                        sample.put(labels[4 + bar * 2] / 1.0e6);
                        sample.put(labels[5 + bar * 2] / 1.0e6);
                        first.put(sample);
                    }
                    ruler.put("first_ms", first);
                    windowMetrics.put("ruler", ruler);
                    Log.i(TAG, "PHASE " + entry);
                    Log.i(TAG, "WINDOW " + windowMetrics);
                }
            }
            JSONObject result = new JSONObject();
            result.put("renderer_requested", renderer);
            result.put("ruler_atlas", rulerAtlas);
            result.put("hardware_canvas", decor.isHardwareAccelerated());
            result.put("scene", activity.getIntent().getStringExtra("apex_scene"));
            result.put("master_preview_suppressed",
                activity.getIntent().getBooleanExtra("apex_master_preview_suppressed", false));
            result.put("horizontal_bitmap_reuse",
                activity.getIntent().getBooleanExtra("apex_horizontal_bitmap_reuse", false));
            result.put("precise_arranger_damage",
                activity.getIntent().getBooleanExtra("apex_precise_arranger_damage", false));
            result.put("retain_translated_timeline",
                activity.getIntent().getBooleanExtra("apex_retain_translated_timeline", false));
            result.put("legacy_full_tree_raster",
                activity.getIntent().getBooleanExtra("apex_legacy_full_tree_raster", false));
            result.put("pan_replay", panReplay);
            result.put("cold_scroll_replay", coldScrollReplay);
            result.put("prepare_edges", prepareEdges);
            result.put("metric", "UI Choreographer callback intervals; not presented FPS");
            result.put("events", eventCount);
            result.put("droppedWindowFrames", droppedWindowFrames);
            Log.i(TAG, "RESULT " + result);
        } catch (Exception error) { fail(error); }
    }

    private void addPeerTimings(JSONObject entry, long[] samples) throws Exception {
        final int stride = 11;
        if (samples.length % stride != 0) throw new IllegalArgumentException("Invalid APEX peer sample stride");
        final int count = samples.length / stride;
        final long[] wall = new long[count];
        final long[] cpu = new long[count];
        final long[] raster = new long[count];
        final long[] rasterCpu = new long[count];
        final long[] rasterWait = new long[count];
        final long[] transfer = new long[count];
        int tiledFrames = 0;
        int reducedTimelineFrames = 0, scrollTileFrames = 0, directFrames = 0, optimizedFrames = 0;
        long skippedChromePixels = 0;
        long paintedPixels = 0, updatedTilePixels = 0, reusedTilePixels = 0;
        int retainedScrollFrames = 0, multiRasterFrames = 0;
        long retainedScrollPixels = 0;
        for (int i = 0; i < count; ++i) {
            final int start = i * stride;
            wall[i] = samples[start];
            cpu[i] = samples[start + 1];
            raster[i] = samples[start + 2];
            rasterCpu[i] = samples[start + 5];
            rasterWait[i] = Math.max(0, raster[i] - rasterCpu[i]);
            transfer[i] = Math.max(0, wall[i] - raster[i]);
            paintedPixels += samples[start + 3];
            updatedTilePixels += samples[start + 6];
            reusedTilePixels += samples[start + 7];
            final long retained = samples[start + 8];
            if (retained > 0) ++retainedScrollFrames;
            retainedScrollPixels += retained;
            if (samples[start + 9] > 1) ++multiRasterFrames;
            if (samples[start + 4] != 0) ++tiledFrames;
            if (samples[start + 4] == 2) ++reducedTimelineFrames;
            if (samples[start + 4] == 3) ++scrollTileFrames;
            if (samples[start + 4] >= 4) ++directFrames;
            if (samples[start + 4] == 5) ++optimizedFrames;
            skippedChromePixels += samples[start + 10];
        }
        entry.put("peerWallMs", summarize(wall, count));
        entry.put("peerCpuMs", summarize(cpu, count));
        entry.put("peerRasterMs", summarize(raster, count));
        entry.put("peerRasterCpuMs", summarize(rasterCpu, count));
        // Wall minus thread CPU is scheduling/blocking time, not drawing instructions.
        entry.put("peerRasterWaitMs", summarize(rasterWait, count));
        entry.put("peerTransferMs", summarize(transfer, count));
        entry.put("peerTiledFrames", tiledFrames);
        entry.put("peerReducedTimelineFrames", reducedTimelineFrames);
        entry.put("peerScrollTileFrames", scrollTileFrames);
        entry.put("peerDirectFrames", directFrames);
        entry.put("peerOptimizedFrames", optimizedFrames);
        entry.put("peerMultiRasterFrames", multiRasterFrames);
        entry.put("peerRetainedScrollFrames", retainedScrollFrames);
        entry.put("peerRetainedScrollPixels", retainedScrollPixels);
        if (count != 0) {
            entry.put("peerMeanSkippedChromePixels", skippedChromePixels / count);
            entry.put("peerMeanPixels", paintedPixels / count);
            entry.put("peerMeanUpdatedTilePixels", updatedTilePixels / count);
            entry.put("peerMeanReusedTilePixels", reusedTilePixels / count);
        }
    }

    private JSONObject summarize(long[] values, int count) throws Exception {
        final long[] sorted = Arrays.copyOf(values, count);
        Arrays.sort(sorted);
        JSONObject summary = new JSONObject();
        summary.put("count", count);
        summary.put("p50", percentile(sorted, 0.50));
        summary.put("p95", percentile(sorted, 0.95));
        summary.put("worst", percentile(sorted, 1.0));
        return summary;
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
