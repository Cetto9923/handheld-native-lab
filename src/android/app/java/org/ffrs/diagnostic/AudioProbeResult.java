package org.ffrs.diagnostic;

/** Typed observations for the explicit diagnostic tone, never a game/audio audibility verdict. */
public final class AudioProbeResult {
    public String status = "NOT_RUN", playback_status = "NOT_RUN", error = "", metadata_error = "", route_error = "";
    public String sample_rate_property_raw = "", frames_per_buffer_property_raw = "";
    public int property_sample_rate_hz, property_frames_per_buffer, native_output_sample_rate_hz;
    public String sample_rate_source = "";
    public int requested_sample_rate_hz, actual_sample_rate_hz, minimum_buffer_bytes, actual_buffer_frames;
    public int stream_volume_index = -1, stream_max_volume_index = -1;
    public boolean stream_volume_measured, stream_muted;
    public Device[] output_devices = new Device[0];
    public String output_devices_status = "unavailable";
    public Device routed_before_play, routed_while_playing, routed_after_stop;
    public long captured_elapsed_realtime_ns, playback_started_ms, elapsed_ms;
    public long deadline_ms = 2000;
    public boolean deadline_reached, interrupted, initialized, play_called;
    public int state_after_create = -1, play_state_after_start = -1, play_state_after_stop = -1, state_after_release = -1;
    public int tone_frames, written_frames, write_calls, last_write_result;
    public long head_before, head_last, head_advance_frames;
    public boolean stop_attempted, stop_returned, release_attempted, release_returned;
    public String stop_error = "", release_error = "";

    public static final class Device {
        public int id, type;
        public String product_name = "";
        public boolean is_sink;
        public int[] sample_rates = new int[0], channel_counts = new int[0], encodings = new int[0];
    }
}
