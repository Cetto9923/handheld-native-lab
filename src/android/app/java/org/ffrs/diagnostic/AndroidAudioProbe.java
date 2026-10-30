package org.ffrs.diagnostic;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.SystemClock;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Invoked only by the explicit tone button. Never changes system volume, routing or recording. */
public final class AndroidAudioProbe {
    private static AudioProbeResult.Device device(AudioDeviceInfo info) {
        if (info == null) return null;
        AudioProbeResult.Device out = new AudioProbeResult.Device();
        out.id = info.getId(); out.type = info.getType(); out.product_name = String.valueOf(info.getProductName());
        out.is_sink = info.isSink(); out.sample_rates = info.getSampleRates();
        out.channel_counts = info.getChannelCounts(); out.encodings = info.getEncodings();
        return out;
    }
    private static int positive(String value) {
        try { int n = Integer.parseInt(value); return n > 0 ? n : 0; }
        catch (RuntimeException ignored) { return 0; }
    }
    public static AudioProbeResult run(Context context) {
        AudioProbeResult result = new AudioProbeResult();
        result.captured_elapsed_realtime_ns = SystemClock.elapsedRealtimeNanos();
        AudioManager manager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        if (manager != null) {
            try {
                String rate = manager.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE);
                String frames = manager.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER);
                result.sample_rate_property_raw = rate == null ? "" : rate;
                result.frames_per_buffer_property_raw = frames == null ? "" : frames;
                result.property_sample_rate_hz = positive(rate);
                result.property_frames_per_buffer = positive(frames);
            } catch (RuntimeException failure) { result.metadata_error += " Properties: " + failure.toString(); }
            try {
                AudioDeviceInfo[] devices = manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS);
                result.output_devices = new AudioProbeResult.Device[devices.length];
                for (int i = 0; i < devices.length; i++) result.output_devices[i] = device(devices[i]);
                result.output_devices_status = "measured_android_api";
            } catch (RuntimeException failure) { result.metadata_error += " Output devices: " + failure.toString(); }
            try {
                result.stream_volume_index = manager.getStreamVolume(AudioManager.STREAM_MUSIC);
                result.stream_max_volume_index = manager.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
                result.stream_muted = manager.isStreamMute(AudioManager.STREAM_MUSIC);
                result.stream_volume_measured = true;
            } catch (RuntimeException failure) { result.metadata_error += " Volume query: " + failure.toString(); }
        } else result.metadata_error = "AudioManager unavailable";
        try { result.native_output_sample_rate_hz = AudioTrack.getNativeOutputSampleRate(AudioManager.STREAM_MUSIC); }
        catch (RuntimeException failure) { result.metadata_error += " Native rate: " + failure.toString(); }
        result.requested_sample_rate_hz = AudioProbeSession.chooseSampleRate(result.property_sample_rate_hz,
                result.native_output_sample_rate_hz);
        result.sample_rate_source = result.property_sample_rate_hz == result.requested_sample_rate_hz
                ? "AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE"
                : result.native_output_sample_rate_hz == result.requested_sample_rate_hz
                ? "AudioTrack.getNativeOutputSampleRate(STREAM_MUSIC)"
                : "diagnostic_fallback_configuration_44100_not_a_device_measurement";
        try {
            result.minimum_buffer_bytes = AudioTrack.getMinBufferSize(result.requested_sample_rate_hz,
                    AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
            if (result.minimum_buffer_bytes <= 0) {
                result.status = result.playback_status = "FAIL_UNSUPPORTED_PROBE_CONFIGURATION";
                result.error = "getMinBufferSize returned " + result.minimum_buffer_bytes;
                return result;
            }
            AudioProbeSession.run(result, () -> {
                final AudioTrack track = new AudioTrack.Builder()
                        .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build())
                        .setAudioFormat(new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                                .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).setSampleRate(result.requested_sample_rate_hz).build())
                        .setTransferMode(AudioTrack.MODE_STREAM).setBufferSizeInBytes(result.minimum_buffer_bytes).build();
                return new AudioProbeSession.Track() {
                    public int state() { return track.getState(); }
                    public int playState() { return track.getPlayState(); }
                    public int sampleRate() { return track.getSampleRate(); }
                    public int bufferFrames() { return track.getBufferSizeInFrames(); }
                    public int head() { return track.getPlaybackHeadPosition(); }
                    public AudioProbeResult.Device route() { return device(track.getRoutedDevice()); }
                    public void play() { track.play(); }
                    public int write(short[] data, int offset, int count) {
                        return track.write(data, offset, count, AudioTrack.WRITE_NON_BLOCKING);
                    }
                    public void stop() { track.stop(); }
                    public void release() { track.release(); }
                };
            }, new AudioProbeSession.Clock() {
                public long nowMs() { return SystemClock.elapsedRealtime(); }
                public void sleepMs(long ms) throws InterruptedException { Thread.sleep(ms); }
            });
        } catch (RuntimeException failure) {
            result.status = result.playback_status = "FAIL_AUDIO_SETUP"; result.error = failure.toString();
        }
        return result;
    }
    private static JSONObject deviceJson(AudioProbeResult.Device device) throws JSONException {
        JSONObject out = new JSONObject();
        out.put("status", device == null ? "unavailable_at_query_time" : "measured_android_api");
        if (device != null) {
            out.put("id", device.id); out.put("type", device.type); out.put("product_name", device.product_name);
            out.put("is_sink", device.is_sink); out.put("sample_rates", new JSONArray(device.sample_rates));
            out.put("channel_counts", new JSONArray(device.channel_counts)); out.put("encodings", new JSONArray(device.encodings));
        }
        return out;
    }
    public static String toJson(AudioProbeResult r) throws JSONException {
        JSONObject out = new JSONObject();
        out.put("schema", "ffrs.android.explicit_audio_probe.v1");
        out.put("scope", "explicit_diagnostic_tone_only_not_game_audio");
        out.put("status", r.status); out.put("playback_status", r.playback_status);
        out.put("audibility_confirmation", "NOT_CONFIRMED_REQUIRES_HUMAN"); out.put("game_audio", "NOT_RUN");
        out.put("error", r.error); out.put("metadata_error", r.metadata_error); out.put("route_error", r.route_error);
        out.put("captured_elapsed_realtime_ns", r.captured_elapsed_realtime_ns);
        JSONObject props = new JSONObject();
        props.put("source", "AudioManager.getProperty");
        props.put("sample_rate_raw", r.sample_rate_property_raw); props.put("frames_per_buffer_raw", r.frames_per_buffer_property_raw);
        props.put("sample_rate_hz", r.property_sample_rate_hz); props.put("frames_per_buffer", r.property_frames_per_buffer);
        props.put("native_output_sample_rate_hz", r.native_output_sample_rate_hz); out.put("properties", props);
        out.put("output_devices_source", "AudioManager.getDevices(GET_DEVICES_OUTPUTS)_not_selected_routing");
        out.put("output_devices_status", r.output_devices_status);
        JSONArray devices = new JSONArray(); for (AudioProbeResult.Device d : r.output_devices) devices.put(deviceJson(d));
        out.put("output_devices", devices);
        out.put("route_source", "AudioTrack.getRoutedDevice_current_track_only_null_when_not_playing");
        out.put("routed_before_play", deviceJson(r.routed_before_play));
        out.put("routed_while_playing", deviceJson(r.routed_while_playing)); out.put("routed_after_stop", deviceJson(r.routed_after_stop));
        JSONObject volume = new JSONObject(); volume.put("measured", r.stream_volume_measured);
        volume.put("stream", "STREAM_MUSIC"); volume.put("index", r.stream_volume_index);
        volume.put("max_index", r.stream_max_volume_index); volume.put("muted", r.stream_muted);
        volume.put("changed_by_probe", false); out.put("volume", volume);
        JSONObject config = new JSONObject(); config.put("sample_rate_source", r.sample_rate_source);
        config.put("requested_sample_rate_hz", r.requested_sample_rate_hz); config.put("actual_sample_rate_hz", r.actual_sample_rate_hz);
        config.put("format", "PCM16_MONO"); config.put("duration_ms", 250); config.put("peak_pcm16", 327);
        config.put("frequency_hz", 660); config.put("ramp_ms", 5); config.put("minimum_buffer_bytes", r.minimum_buffer_bytes);
        config.put("actual_buffer_frames", r.actual_buffer_frames); out.put("configuration", config);
        out.put("initialized", r.initialized); out.put("state_after_create", r.state_after_create);
        out.put("play_called", r.play_called); out.put("play_state_after_start", r.play_state_after_start);
        out.put("tone_frames", r.tone_frames); out.put("write_mode", "WRITE_NON_BLOCKING");
        out.put("written_frames", r.written_frames); out.put("write_calls", r.write_calls); out.put("last_write_result", r.last_write_result);
        out.put("head_before", r.head_before); out.put("head_last", r.head_last); out.put("head_advance_frames", r.head_advance_frames);
        out.put("deadline_ms", r.deadline_ms); out.put("deadline_reached", r.deadline_reached); out.put("elapsed_ms", r.elapsed_ms);
        out.put("deadline_scope", "nonblocking_write_and_playback_polling_including_track_creation_elapsed_platform_call_latency_not_preemptible");
        out.put("interrupted", r.interrupted);
        JSONObject cleanup = new JSONObject(); cleanup.put("stop_attempted", r.stop_attempted); cleanup.put("stop_returned", r.stop_returned);
        cleanup.put("play_state_after_stop", r.play_state_after_stop); cleanup.put("stop_error", r.stop_error);
        cleanup.put("release_attempted", r.release_attempted); cleanup.put("release_returned", r.release_returned);
        cleanup.put("state_after_release", r.state_after_release); cleanup.put("release_error", r.release_error);
        out.put("cleanup", cleanup);
        return out.toString();
    }
}
