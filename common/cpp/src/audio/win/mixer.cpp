#include "audio_capture/win/mixer.h"
#include "wil/result_macros.h"
#include <basetsd.h>
#include <memory>
#include <processthreadsapi.h>
#include <winbase.h>
#include <winuser.h>
#include <chrono>
#include <ctime>

namespace flutter_webrtc_plugin {


template<typename T> T Mixer::RoundToNearest(T x, T m)
{
	return ((x + m / 2) / m) * m;
}

UINT64 Mixer::GetCurrentTimestamp()
{
	LARGE_INTEGER timestamp_ticks;
	static LARGE_INTEGER frequency = {0};

	if (frequency.QuadPart == 0)
		QueryPerformanceFrequency(&frequency);

	QueryPerformanceCounter(&timestamp_ticks);
	return timestamp_ticks.QuadPart * (10000000 / frequency.QuadPart);
}

std::size_t Mixer::DurationToFrames(UINT64 duration)
{
	UINT64 duration_ns = duration * 100;
	return (duration_ns * format.nSamplesPerSec) / 1000000000;
}

UINT64 Mixer::FramesToDuration(std::size_t frames)
{
	UINT64 duration_ns = (frames * 1000000000) / format.nSamplesPerSec;
	return duration_ns / 100;
}

void Mixer::ProcessInput(UINT64 input_timestamp, std::vector<int16_t> &input_buffer)
{
	if (mix.empty()) {
		mix_timestamp = input_timestamp;
		mix = std::move(input_buffer);

		return;
	}

	if (input_timestamp < mix_timestamp) {
		warn("late mix input packet - increase cutoff_end?");
		return;
	}

	auto offset = format.nChannels * DurationToFrames(input_timestamp - mix_timestamp);

	if (offset + input_buffer.size() > mix.size())
		mix.resize(offset + input_buffer.size());

	for (std::size_t i = 0; i < input_buffer.size(); ++i)
		mix[offset + i] += input_buffer[i];
}

void Mixer::ProcessInput()
{
	auto lock = input_section.lock();

	while (!input_queue.empty()) {
		auto &[input_timestamp, input_buffer] = input_queue.front();
		ProcessInput(input_timestamp, input_buffer);
		input_queue.pop();
	}
}

std::size_t Mixer::TimestampToMixOffset(UINT64 timestamp)
{
	if (timestamp < mix_timestamp)
		return 0;

	return DurationToFrames(timestamp - mix_timestamp);
}

std::tuple<std::size_t, std::size_t> Mixer::CalculateCutoff(UINT64 timestamp)
{

	if (timestamp < cutoff_end)
		return {0, 0};

	if (timestamp < cutoff_start)
		return {0, TimestampToMixOffset(timestamp - cutoff_end)};

	auto start = TimestampToMixOffset(timestamp - cutoff_start);
	auto end = TimestampToMixOffset(timestamp - cutoff_end);

	return {start, end};
}

void Mixer::Tick()
{
	ProcessInput();

	UINT64 current_timestamp = GetCurrentTimestamp();
	auto [start, end] = CalculateCutoff(current_timestamp);
	auto frames = end - start;

    if (frames > max_frames_throttle) {
      return;
    }

    if (frames == 0) {
      return;
    }

	if (mix.empty()) {
		std::vector<char> silence(frames * format.nBlockAlign, 0);
		output->save(silence.data(), silence.size());
		mix_timestamp += FramesToDuration(end);
		return;
	}

	if (start * format.nChannels >= mix.size()) {
		mix.clear();
		return;
	}

    start = min(mix.size() / format.nChannels, start);
    end = min(mix.size() / format.nChannels, end);

	output->save(reinterpret_cast<const char *>(mix.data() + start * format.nChannels), (end - start)*format.nBlockAlign);

	std::vector<int16_t> new_mix(mix.begin() + end * format.nChannels, mix.end());
	mix = std::move(new_mix);

	mix_timestamp += FramesToDuration(end);
}
void Mixer::Run()
{
	// Force message queue creation
	MSG msg;
	PeekMessageA(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

	worker_ready.SetEvent();

	bool shutdown = false;
	while (!shutdown) {
		if (!GetMessage(&msg, reinterpret_cast<HWND>(-1), WM_USER, 0)) {
			debug("shutting down");
			shutdown = true;
		}

		switch (msg.message) {
		case MixerEvents::Shutdown:
			debug("shutting down");
			shutdown = true;
			break;

		case MixerEvents::Tick:
			Tick();
			break;;
		}
	}
}

void Mixer::SubmitPacket(UINT64 timestamp, int16_t *data, UINT32 num_frames)
{
	auto lock = input_section.lock();

	auto &[_, buffer] = input_queue.emplace(timestamp, 0);
	buffer.assign(data, data + num_frames * format.nChannels);

	lock.reset();
}

static void timer_thread(DWORD worker_tid, UINT64 tick_interval) {
	HANDLE hTimer = CreateWaitableTimerExW(
		nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

	if (!hTimer) {
        error("Failed to create waitable timer");
		return;
	}

	LARGE_INTEGER liDueTime{};
	liDueTime.QuadPart = -static_cast<LONGLONG>(tick_interval) * 10000;

	if (!SetWaitableTimer(
			hTimer,
			&liDueTime,
			static_cast<LONG>(tick_interval),
			nullptr,
			nullptr,
			FALSE)) {
        error("Failed to set waitable timer");
		CloseHandle(hTimer);
		return;
			}

	while (true) {
		DWORD wait = WaitForSingleObject(hTimer, INFINITE);
		if (wait != WAIT_OBJECT_0)
			break;

		if (!PostThreadMessageW(worker_tid, MixerEvents::Tick, 0, 0)) {
			break;
		}
	}

	CancelWaitableTimer(hTimer);
	CloseHandle(hTimer);
}


Mixer::Mixer(IRecorder *output, WAVEFORMATEX format) : output{output}, format{format}
{
	worker_thread = std::thread(&Mixer::Run, this);
    worker_tid = GetThreadId(HANDLE(worker_thread.native_handle()));

    SetThreadPriority(HANDLE(worker_thread.native_handle()), THREAD_PRIORITY_HIGHEST);

    timer_thread_handle = std::thread(timer_thread, worker_tid, tick_interval);
}

Mixer::~Mixer()
{
	worker_ready.wait();
    PostThreadMessageW(worker_tid, MixerEvents::Shutdown, 0, 0);
    worker_thread.join();

    if (timer_thread_handle.joinable())
        timer_thread_handle.join();
}

};