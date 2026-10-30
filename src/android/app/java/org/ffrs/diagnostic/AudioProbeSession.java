package org.ffrs.diagnostic;

/** Nonblocking write/playback loop shared with fake-clock tests. All real tracks are released. */
public final class AudioProbeSession {
    public interface Clock { long nowMs(); void sleepMs(long ms) throws InterruptedException; }
    public interface Track {
        int state(); int playState(); int sampleRate(); int bufferFrames(); int head();
        AudioProbeResult.Device route();
        void play(); int write(short[] data, int offset, int count); void stop(); void release();
    }
    public interface Factory { Track create(); }

    public static int chooseSampleRate(int property, int nativeRate) {
        if (validRate(property)) return property;
        if (validRate(nativeRate)) return nativeRate;
        return 44100; // A named probe configuration, never an observed device capability.
    }
    private static boolean validRate(int rate) { return rate >= 8000 && rate <= 192000; }

    public static short[] tone(int rate) {
        if (!validRate(rate)) throw new IllegalArgumentException("Invalid track sample rate");
        short[] samples = new short[rate / 4]; // Exactly 0.25 s of mono PCM16 frames.
        int ramp = Math.max(1, rate / 200); // 5 ms ramps avoid abrupt sample edges.
        for (int i = 0; i < samples.length; i++) {
            double envelope = Math.min(1.0, Math.min((double) i / ramp, (double) (samples.length - 1 - i) / ramp));
            samples[i] = (short) Math.round(327.0 * envelope * Math.sin(2 * Math.PI * 660.0 * i / rate));
        }
        return samples;
    }

    private static AudioProbeResult.Device route(Track track, AudioProbeResult result) {
        try { return track.route(); }
        catch (RuntimeException failure) { result.route_error = failure.toString(); return null; }
    }

    public static void run(AudioProbeResult result, Factory factory, Clock clock) {
        Track track = null;
        long start = clock.nowMs();
        result.playback_started_ms = start;
        result.playback_status = "FAIL_INITIALIZATION";
        try {
            track = factory.create();
            result.state_after_create = track.state();
            result.initialized = result.state_after_create == 1; // AudioTrack.STATE_INITIALIZED
            if (!result.initialized) return;
            result.actual_sample_rate_hz = track.sampleRate();
            result.actual_buffer_frames = track.bufferFrames();
            short[] samples = tone(result.actual_sample_rate_hz);
            result.tone_frames = samples.length;
            result.routed_before_play = route(track, result);
            result.head_before = Integer.toUnsignedLong(track.head());
            result.head_last = result.head_before;
            if (clock.nowMs() - start >= result.deadline_ms) {
                result.deadline_reached = true; result.playback_status = "TIMEOUT_BEFORE_PLAY"; return;
            }
            if (Thread.currentThread().isInterrupted()) throw new InterruptedException("Audio probe cancelled");
            result.play_called = true;
            track.play();
            result.play_state_after_start = track.playState();
            if (result.play_state_after_start != 3) { result.playback_status = "FAIL_PLAY_STATE"; return; }
            while (clock.nowMs() - start < result.deadline_ms) {
                if (Thread.currentThread().isInterrupted()) throw new InterruptedException("Audio probe cancelled");
                if (result.written_frames < samples.length) {
                    int remaining = samples.length - result.written_frames;
                    result.last_write_result = track.write(samples, result.written_frames, remaining);
                    result.write_calls++;
                    if (result.last_write_result < 0 || result.last_write_result > remaining) {
                        result.playback_status = "FAIL_WRITE";
                        result.error = "AudioTrack.write returned " + result.last_write_result; return;
                    }
                    result.written_frames += result.last_write_result;
                }
                result.head_last = Integer.toUnsignedLong(track.head());
                result.head_advance_frames = (result.head_last - result.head_before) & 0xffffffffL;
                AudioProbeResult.Device current = route(track, result);
                if (current != null) result.routed_while_playing = current;
                if (clock.nowMs() - start >= result.deadline_ms) break;
                if (result.written_frames == samples.length && result.head_advance_frames >= samples.length) {
                    result.playback_status = "FRAME_PROGRESS_OBSERVED_NOT_AUDIBILITY_CONFIRMED"; return;
                }
                long remainingMs = result.deadline_ms - (clock.nowMs() - start);
                if (remainingMs > 0) clock.sleepMs(Math.min(10, remainingMs));
            }
            result.deadline_reached = true;
            result.playback_status = "TIMEOUT_WRITE_OR_PLAYBACK";
        } catch (InterruptedException failure) {
            result.interrupted = true; result.playback_status = "CANCELLED";
            result.error = failure.toString(); Thread.currentThread().interrupt();
        } catch (RuntimeException failure) {
            result.playback_status = "FAIL_TRACK_OPERATION"; result.error = failure.toString();
        } finally {
            if (track != null) {
                if (result.initialized) {
                    result.stop_attempted = true;
                    try {
                        track.stop(); result.stop_returned = true;
                        result.play_state_after_stop = track.playState();
                        result.routed_after_stop = route(track, result);
                    } catch (RuntimeException failure) { result.stop_error = failure.toString(); }
                }
                result.release_attempted = true;
                try {
                    track.release(); result.release_returned = true;
                    result.state_after_release = track.state();
                } catch (RuntimeException failure) { result.release_error = failure.toString(); }
            }
            result.elapsed_ms = Math.max(0, clock.nowMs() - start);
            result.status = result.playback_status;
            if (track != null && (!result.release_returned || result.state_after_release != 0
                    || (result.initialized && (!result.stop_returned || result.play_state_after_stop != 1))))
                result.status = "FAIL_CLEANUP";
        }
    }
}
