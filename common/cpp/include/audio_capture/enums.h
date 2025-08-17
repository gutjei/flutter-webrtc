#pragma once

namespace flutter_webrtc_plugin {
  enum mode { MODE_SESSION_PID, MODE_SESSION_EXCLUDE, MODE_SESSION_INCLUDE };

  mode str_to_mode(const std::string& s);
};