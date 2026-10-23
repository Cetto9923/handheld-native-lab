package org.ffrs.diagnostic;

/** Fake-clock/track protocol tests. They create no AudioTrack and play no sound. */
public final class AudioProbeSessionTest {
    private static void require(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    private static final class Clock implements AudioProbeSession.Clock {
        long now = 5000; boolean interruptOnSleep;
        public long nowMs() { return now; }
        public void sleepMs(long ms) throws InterruptedException {
            require(ms > 0 && ms <= 10, "bounded positive polling interval");
            if (interruptOnSleep) throw new InterruptedException("synthetic cancellation");
            now += ms;
        }
    }
    private static final class Track implements AudioProbeSession.Track {
        final Clock clock;
        int state = 1, playState = 1, written, maxWrite = 128, stops, releases;
        int writeError, sampleRate = 8000;
        long playStart, headBase;
        boolean stalled, zeroWrite, stopError, releaseError, playError, headError;
        String order = "";
        Track(Clock clock) { this.clock = clock; }
        public int state() { return state; }
        public int playState() { return playState; }
        public int sampleRate() { return sampleRate; }
        public int bufferFrames() { return 512; }
        public int head() {
            if (headError) throw new IllegalStateException("synthetic head failure");
            long frames = playState == 3 && !stalled ? Math.min(written, (clock.now - playStart) * sampleRate / 1000) : 0;
            return (int) (headBase + frames);
        }
        public AudioProbeResult.Device route() {
            if (playState != 3) return null;
            AudioProbeResult.Device device = new AudioProbeResult.Device(); device.id = 7; device.type = 2;
            return device;
        }
        public void play() {
            if (playError) throw new IllegalStateException("synthetic play failure");
            playState = 3; playStart = clock.now;
        }
        public int write(short[] data, int offset, int count) {
            require(offset == written && offset + count <= data.length, "partial write offsets");
            if (writeError != 0) return writeError;
            if (zeroWrite) return 0;
            int result = Math.min(count, maxWrite); written += result; return result;
        }
        public void stop() {
            stops++; order += "stop;";
            if (stopError) throw new IllegalStateException("synthetic stop failure");
            playState = 1;
        }
        public void release() {
            releases++; order += "release;";
            if (releaseError) throw new IllegalStateException("synthetic release failure");
            state = 0;
        }
    }
    private static AudioProbeResult run(Track track, Clock clock) {
        AudioProbeResult result = new AudioProbeResult();
        AudioProbeSession.run(result, () -> track, clock);
        require(track.releases == 1 && result.release_attempted, "release exactly once");
        require(!result.status.equals("PASS"), "no audibility PASS inference");
        return result;
    }
    public static void main(String[] args) {
        Clock c = new Clock(); Track t = new Track(c); AudioProbeResult r = run(t, c);
        require(r.status.equals("FRAME_PROGRESS_OBSERVED_NOT_AUDIBILITY_CONFIRMED"), "normal progress");
        require(r.written_frames == 2000 && r.head_advance_frames == 2000 && r.write_calls > 1, "partial writes complete");
        require(r.initialized && r.stop_returned && r.release_returned && r.state_after_release == 0, "actual cleanup states");
        require(t.order.equals("stop;release;") && r.routed_while_playing.id == 7, "cleanup order and observed route");
        require(r.routed_before_play == null && r.routed_after_stop == null, "inactive route unknown");
        c = new Clock(); t = new Track(c); t.stalled = true; r = run(t, c);
        require(r.deadline_reached && r.elapsed_ms == 2000 && r.head_advance_frames == 0, "stalled head deadline");
        c = new Clock(); t = new Track(c); t.zeroWrite = true; r = run(t, c);
        require(r.deadline_reached && r.elapsed_ms == 2000 && r.written_frames == 0, "zero-write deadline");
        c = new Clock(); t = new Track(c); t.writeError = -6; r = run(t, c);
        require(r.status.equals("FAIL_WRITE") && r.last_write_result == -6 && t.stops == 1, "negative write cleanup");
        c = new Clock(); t = new Track(c); t.state = 0; r = run(t, c);
        require(!r.initialized && !r.play_called && t.stops == 0, "uninitialized track released without stop");
        c = new Clock(); t = new Track(c); t.playError = true; r = run(t, c);
        require(r.status.equals("FAIL_TRACK_OPERATION") && t.stops == 1, "play exception cleanup");
        c = new Clock(); t = new Track(c); t.stopError = true; r = run(t, c);
        require(r.status.equals("FAIL_CLEANUP") && !r.stop_error.isEmpty() && r.release_returned, "stop failure still releases");
        c = new Clock(); t = new Track(c); t.releaseError = true; r = run(t, c);
        require(r.status.equals("FAIL_CLEANUP") && !r.release_error.isEmpty() && !r.release_returned, "release failure reported");
        c = new Clock(); t = new Track(c); t.headBase = 0xfffffff0L; r = run(t, c);
        require(r.head_advance_frames == 2000, "unsigned 32-bit head wrap");
        c = new Clock(); t = new Track(c); c.interruptOnSleep = true; r = run(t, c);
        require(r.interrupted && r.status.equals("CANCELLED") && r.release_returned, "interrupt cleanup");
        require(Thread.interrupted(), "interrupt flag retained"); // Clear only this test thread's flag.
        c = new Clock(); t = new Track(c); t.headError = true; r = run(t, c);
        require(r.status.equals("FAIL_TRACK_OPERATION") && !r.play_called && t.stops == 1, "head exception cleanup");
        c = new Clock(); r = new AudioProbeResult(); final Clock delayed = c;
        final Track delayedTrack = new Track(c);
        AudioProbeSession.run(r, () -> { delayed.now += 2001; return delayedTrack; }, c);
        require(r.status.equals("TIMEOUT_BEFORE_PLAY") && !r.play_called && r.release_returned, "late initialization never plays");
        c = new Clock(); r = new AudioProbeResult();
        AudioProbeSession.run(r, () -> { throw new IllegalStateException("synthetic construction failure"); }, c);
        require(r.status.equals("FAIL_TRACK_OPERATION") && !r.release_attempted, "no handle invented on constructor failure");
        require(AudioProbeSession.chooseSampleRate(48000, 44100) == 48000, "actual property preferred");
        require(AudioProbeSession.chooseSampleRate(0, 32000) == 32000, "actual native rate fallback");
        require(AudioProbeSession.chooseSampleRate(0, 0) == 44100, "named configuration fallback");
        for (int rate : new int[] {8000, 44100, 48000, 192000}) {
            short[] tone = AudioProbeSession.tone(rate);
            require(tone.length == rate / 4 && tone[0] == 0 && tone[tone.length - 1] == 0, "duration and ramps");
            int peak = 0; for (short sample : tone) peak = Math.max(peak, Math.abs((int) sample));
            require(peak > 0 && peak <= 327, "low-amplitude PCM bound");
        }
        System.out.println("HOST_FAKE_AUDIO_ONLY: partial/zero/negative writes, head stall/wrap, init/play/head failures, cancellation, delayed construction, cleanup failures/order, config and 0.25s amplitude/ramp checks passed. AudioTrack/device/audibility NOT_RUN.");
    }
}
