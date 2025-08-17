#include <string>

#include "audio_capture/enums.h"

namespace flutter_webrtc_plugin {

  mode str_to_mode(const std::string& s) {
      if (s == "pid")     return MODE_SESSION_PID;
      if (s == "exclude") return MODE_SESSION_EXCLUDE;
      if (s == "include") return MODE_SESSION_INCLUDE;
      return MODE_SESSION_PID;
  }

};