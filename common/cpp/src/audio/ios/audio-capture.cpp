#include <cstdint>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <set>
#include <unordered_map>
#include <regex>

#include <windows.h>
#include <stringapiset.h>
#include <processthreadsapi.h>
#include <mmreg.h>
#include <audiopolicy.h>
#include <audioclientactivationparams.h>
#include <tlhelp32.h>

#include <winuser.h>

#include "wil/result.h"
#include "wil/result_macros.h"

#include "audio_capture/win/audio-capture.h"
#include "audio_capture/win/audio-capture-helper-manager.h"

namespace flutter_webrtc_plugin {

void AudioCapture::Update(Settings *settings) {}


AudioCapture::AudioCapture(IRecorder *source) : source{source}{}
AudioCapture::~AudioCapture(){}


};