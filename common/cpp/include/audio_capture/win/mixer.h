#pragma once

#include <thread>
#include <queue>
#include <vector>

#include <windows.h>
#include <wil/resource.h>

#include "common.h"
#include "../audio.h"

namespace flutter_webrtc_plugin {

namespace MixerEvents {
enum MixerEvents {
	Shutdown = WM_USER,
	Tick,
};
};

class Mixer {
private:
	static const UINT64 ms_in_ts = 10000;

	static const UINT64 cutoff_start = 40 * ms_in_ts;
	static const UINT64 cutoff_end = 20 * ms_in_ts;

	static const DWORD tick_interval = 10;
	static const UINT64 max_frames_throttle = 2000;

	IRecorder *output{};

	std::thread worker_thread;
	std::thread timer_thread_handle;
	DWORD worker_tid;
	wil::unique_event worker_ready{wil::EventOptions::ManualReset};

	HANDLE timer{};

	WAVEFORMATEX format;

	wil::critical_section input_section;
	std::queue<std::tuple<UINT64, std::vector<int16_t>>> input_queue;

	UINT64 mix_timestamp{};
	std::vector<int16_t> mix;

	template<typename T> static T RoundToNearest(T x, T m);

	UINT64 GetCurrentTimestamp();
	std::size_t DurationToFrames(UINT64 duration);
	UINT64 FramesToDuration(std::size_t frames);

	std::size_t TimestampToMixOffset(UINT64 timestamp);
	std::tuple<std::size_t, std::size_t> CalculateCutoff(UINT64 timestamp);

	void ProcessInput(UINT64 input_timestamp, std::vector<int16_t> &input_buffer);
	void ProcessInput();

	void Tick();
	void Run();

public:
	void SubmitPacket(UINT64 timestamp, int16_t *data, UINT32 num_frames);

	Mixer(IRecorder *output, WAVEFORMATEX format);
	~Mixer();
};

};