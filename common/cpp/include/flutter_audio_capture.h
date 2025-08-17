#pragma once

#include "audio_capture/audio.h"

#ifndef FLUTTER_WEBRTC_RTC_AUDIO_CAPTURE_HXX
#define FLUTTER_WEBRTC_RTC_AUDIO_CAPTURE_HXX

namespace flutter_webrtc_plugin {

  class Muxer : public IRecorder {
  private:
    scoped_refptr<RTCAudioSource> source;
    std::vector<char> buffer;

    const size_t channels = 2;
    const int sample_rate = 48000;
    const int bits_per_sample = 16;
    const size_t frames_per_buffer = 480; // 10ms at 48kHz

    size_t bytes_per_frame() const {
      return channels * (bits_per_sample / 8);
    }
  public:
    Muxer(scoped_refptr<RTCAudioSource> source);
    void save(const char* samples, size_t size);
    void flush() {};
    ~Muxer();
  };

};

#endif