#pragma once

#include "../audio.h"
#include "../enums.h"

namespace flutter_webrtc_plugin {

class AudioCapture : public IAudioCapture {
  private:
    IRecorder *source;
  public:
	void Update(Settings *settings);

	explicit AudioCapture(IRecorder *source);
	~AudioCapture();
};

}