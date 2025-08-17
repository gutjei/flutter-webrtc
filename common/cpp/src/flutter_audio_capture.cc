#include "flutter_webrtc_base.h"

#include "flutter_data_channel.h"
#include "flutter_peerconnection.h"
#include "flutter_media_stream.h"

#include "audio_capture/win/audio-capture.h"

#include "flutter_audio_capture.h"

namespace flutter_webrtc_plugin {

Muxer::Muxer(scoped_refptr<RTCAudioSource> source) :
    source(source) {}


void Muxer::save(const char* samples, size_t size) {
    buffer.insert(buffer.end(), samples, samples + size);

    const size_t frame_size_bytes = channels * (bits_per_sample / 8);
    const size_t buffer_size_bytes = frames_per_buffer * frame_size_bytes;

    while (buffer.size() >= buffer_size_bytes) {
        auto start = std::chrono::steady_clock::now();
        source->CaptureFrame(
            buffer.data(),
            bits_per_sample,
            sample_rate,
            channels,
            frames_per_buffer
        );

        buffer.erase(buffer.begin(), buffer.begin() + buffer_size_bytes);

        if (buffer.size() >= buffer_size_bytes) {
          auto end = std::chrono::steady_clock::now();
          auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
          if (elapsed.count() < 10) {
              std::this_thread::sleep_for(std::chrono::milliseconds(10) - elapsed);
          }
        }
    }
}

}  // namespace flutter_webrtc_plugin
