#pragma once

#include <iostream>
#include <windows.h>
#include <cstdarg>  // для va_list и др.

#define do_log(level, format, ...) \
do_log_source(level, "[audio-capture] (%s) " format "\n", __func__, ##__VA_ARGS__)

namespace flutter_webrtc_plugin {

// Функция логирования
inline static void do_log_source(int level, const char *format, ...) {
  va_list args;
  va_start(args, format);

  // Используем vprintf для корректного форматированного вывода
  vprintf(format, args);

  va_end(args);
}

// Уровни логирования
constexpr int LOG_ERROR = 1;
constexpr int LOG_WARNING = 2;
constexpr int LOG_INFO = 3;
constexpr int LOG_DEBUG = 4;

} // namespace flutter_webrtc_plugin

// Упрощённые макросы
#define error(format, ...) flutter_webrtc_plugin::do_log(flutter_webrtc_plugin::LOG_ERROR, format, ##__VA_ARGS__)
#define warn(format, ...)  flutter_webrtc_plugin::do_log(flutter_webrtc_plugin::LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...)  flutter_webrtc_plugin::do_log(flutter_webrtc_plugin::LOG_INFO, format, ##__VA_ARGS__)
#define debug(format, ...) flutter_webrtc_plugin::do_log(flutter_webrtc_plugin::LOG_DEBUG, format, ##__VA_ARGS__)
