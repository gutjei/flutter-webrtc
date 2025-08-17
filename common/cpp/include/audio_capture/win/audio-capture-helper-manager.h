#pragma once

#include <unordered_map>
#include <tuple>
#include <set>

#include <windows.h>

#include <wil/resource.h>

#include "common.h"
#include "audio-capture-helper.h"

namespace flutter_webrtc_plugin {

class AudioCaptureHelperManager {
private:
	wil::critical_section helpers_section;
	std::unordered_map<DWORD, AudioCaptureHelper> helpers;

	WAVEFORMATEX format;

public:
	AudioCaptureHelperManager()
	{
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nChannels = 2;
		format.nSamplesPerSec = 48000;

		format.nBlockAlign = format.nChannels * sizeof(int16_t);
		format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
		format.wBitsPerSample = CHAR_BIT * sizeof(int16_t);
		format.cbSize = 0;
	};

	~AudioCaptureHelperManager() = default;

	WAVEFORMATEX GetFormat() { return format; }

	void RegisterMixer(DWORD pid, Mixer *mixer)
	{
		auto lock = helpers_section.lock();

		try {
			auto [it, inserted] = helpers.try_emplace(pid, mixer, format, pid);
			if (!inserted)
				it->second.RegisterMixer(mixer);
		} catch (wil::ResultException e) {
			error("failed to create helper... update Windows?");
			error("%s", e.what());
		}
	};

	void UnRegisterMixer(DWORD pid, Mixer *mixer)
	{
		auto lock = helpers_section.lock();

		auto it = helpers.find(pid);
		if (it == helpers.end())
			return;

		auto remove_helper = it->second.UnRegisterMixer(mixer);
		if (remove_helper)
			helpers.erase(it);
	};
};

};