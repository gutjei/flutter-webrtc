#pragma once

#include "enums.h"

namespace flutter_webrtc_plugin {

  class IRecorder {
  public:
    virtual void save(const char* samples, size_t size) = 0;
    virtual void flush() = 0;
  };

  struct Settings {
	mode mode = MODE_SESSION_EXCLUDE;
	std::vector<std::string> pattern;
  };

  class IAudioCapture {
    public:
     virtual void Update(Settings *settings) = 0;
  };

};
