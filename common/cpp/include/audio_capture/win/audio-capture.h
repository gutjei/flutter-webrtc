#pragma once

#include <cstdio>
#include <optional>
#include <tuple>
#include <set>
#include <string>
#include <vector>

#include <windows.h>
#include <wil/resource.h>

#include "common.h"
#include "audio-capture-helper.h"
#include "session-monitor.h"
#include "../audio.h"
#include "../enums.h"

namespace flutter_webrtc_plugin {

enum CaptureEvents { Shutdown = WM_USER, Update, SessionAdded, SessionExpired };

struct AudioCaptureConfig {
	enum mode mode = MODE_SESSION_EXCLUDE;

	std::set<std::string> executables;

	std::vector<std::string> pattern;
};

class AudioCapture : public IAudioCapture {
private:
	std::thread worker_thread;
	DWORD worker_tid;
	wil::unique_event worker_ready{wil::EventOptions::ManualReset};

	wil::critical_section config_section;
	AudioCaptureConfig config;

	IRecorder *source;

	WAVEFORMATEX format{};
	std::optional<Mixer> mixer;

	std::set<DWORD> pids;

	void StartCapture(const std::set<DWORD> &new_pids);
	void StopCapture();

	void WorkerUpdate();

	bool Tick(const MSG &msg);
	void Run();

	std::set<std::string> GetExecutables(Settings *settings);
public:
	static std::set<DWORD> DeDuplicateCaptureList(const std::set<DWORD> &pids,
						      const std::set<DWORD> &exclude);


	std::tuple<std::string, std::string>
	MakeSessionOptionStrings(std::set<DWORD> pids, const std::string &executable, bool added);


	void Update(Settings *settings);

	bool IsUwpWindow(HWND window);
	HWND GetUwpActualWindow(HWND parent_window);

	void HotkeyStart();
	void HotkeyStop();

	explicit AudioCapture(IRecorder *source);
	~AudioCapture();
};

}