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

AudioCaptureHelperManager helper_manager;

static std::unordered_map<DWORD, DWORD> GetProcessParents(const std::set<DWORD> &pids)
{
	wil::unique_handle handle;
	*handle.put() = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	PROCESSENTRY32W info;
	info.dwSize = sizeof(PROCESSENTRY32W);

	bool ret = Process32FirstW(handle.get(), &info);

	std::unordered_map<DWORD, DWORD> parent_map;
	while (ret) {
		if (pids.contains(info.th32ProcessID))
			parent_map[info.th32ProcessID] = info.th32ParentProcessID;

		ret = Process32NextW(handle.get(), &info);
	}

	for (auto pid : pids) {
		if (parent_map.contains(pid))
			continue;

		parent_map[pid] = 0;
	}

	return parent_map;
}

std::set<DWORD>
AudioCapture::DeDuplicateCaptureList(const std::set<DWORD> &pids,
				     const std::set<DWORD> &exclude_pids = std::set<DWORD>())
{
	std::set<DWORD> all_pids = pids;
	all_pids.insert(exclude_pids.begin(), exclude_pids.end());

	auto parents = GetProcessParents(all_pids);

	std::set<DWORD> uncaptured_pids = pids;
	for (auto pid : exclude_pids)
		uncaptured_pids.erase(parents[pid]);

	std::set<DWORD> explicitly_captured_pids;
	std::set<DWORD> implicitly_captured_pids;

	while (!uncaptured_pids.empty()) {
		for (auto pid : uncaptured_pids) {
			if (uncaptured_pids.contains(parents[pid]))
				continue;

			explicitly_captured_pids.insert(pid);
		}

		for (auto pid : explicitly_captured_pids)
			uncaptured_pids.erase(pid);

		for (auto pid : uncaptured_pids) {
			if (!explicitly_captured_pids.contains(parents[pid]))
				continue;

			implicitly_captured_pids.insert(pid);
			uncaptured_pids.erase(pid);
		}
	}

	return explicitly_captured_pids;
}

void AudioCapture::StartCapture(const std::set<DWORD> &new_pids)
{
	for (auto pid : pids) {
		if (new_pids.contains(pid))
			continue;

		helper_manager.UnRegisterMixer(pid, &mixer.value());
	}

	for (auto new_pid : new_pids) {
		if (pids.contains(new_pid))
			continue;

		helper_manager.RegisterMixer(new_pid, &mixer.value());
	}

	pids = new_pids;
}

void AudioCapture::StopCapture()
{
	for (auto pid : pids)
		helper_manager.UnRegisterMixer(pid, &mixer.value());

	pids.clear();
}

void AudioCapture::WorkerUpdate()
{
	auto config_lock = config_section.lock();
	auto config_ = this->config;
	config_lock.reset();

	auto sessions = SessionMonitor::Instance()->GetSessions();

	std::set<DWORD> capture_pids;
	std::set<DWORD> exclude_pids;

	for (auto &[key, executable] : sessions) {
		// if (!config_.executables.contains(executable) ^ config_.exclude) {
		// 	exclude_pids.insert(key.pid);
		// 	continue;
		// }

		if (config_.mode == MODE_SESSION_EXCLUDE) {
			for (const auto& ps : config_.pattern) {
				std::regex pattern(ps);
				if (!std::regex_match(executable, pattern)) {
					capture_pids.insert(key.pid);
				}
			}
		}

		if (config_.mode == MODE_SESSION_INCLUDE) {
			for (const auto& ps : config_.pattern) {
				std::regex pattern(ps);
				if (std::regex_match(executable, pattern)) {
					capture_pids.insert(key.pid);
				}
			}
		}
	}

	if (capture_pids.empty()) {
		StopCapture();
		return;
	}

	StartCapture(AudioCapture::DeDuplicateCaptureList(
		capture_pids,  exclude_pids));
}

bool AudioCapture::Tick(const MSG &msg)
{
	bool shutdown = false;

	switch (msg.message) {
	case CaptureEvents::Shutdown:
		debug("shutting down");
		shutdown = true;

		break;

	case CaptureEvents::Update:
	case CaptureEvents::SessionAdded:
	case CaptureEvents::SessionExpired:
		WorkerUpdate();
		break;

	default:
		warn("unexpected event id, ignoring");
		break;
	}

	return shutdown;
}

void AudioCapture::Run()
{
	// Force message queue creation
	MSG msg;
	PeekMessageA(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

	worker_ready.SetEvent();

	// Before current thread is running, any message won't be sent successfully.
	// So here send Update again to make sure Tick() can be called once.
	PostThreadMessageA(worker_tid, CaptureEvents::Update, NULL, NULL);

	bool shutdown = false;
	while (!shutdown) {
		if (!GetMessage(&msg, reinterpret_cast<HWND>(-1), WM_USER, 0)) {
			debug("shutting down");
			shutdown = true;
		}

		shutdown = Tick(msg);
	}

	StopCapture();
}

void AudioCapture::Update(Settings *settings)
{
	AudioCaptureConfig new_config = {
		.mode = settings->mode,
		.pattern = std::move(settings->pattern),
	};


	// new_config.executables = GetExecutables(settings);

	auto lock = config_section.lock();
	config = std::move(new_config);
	lock.reset();

	PostThreadMessageA(worker_tid, CaptureEvents::Update, NULL, NULL);
}

//static void audio_capture_update(void *data, obs_data_t *settings)
//{
//	auto *ctx = static_cast<AudioCapture *>(data);
//	ctx->Update(settings);
//}

bool AudioCapture::IsUwpWindow(HWND window)
{
	wchar_t name[256] = {L'\0'};

	if (!GetClassNameW(window, name, sizeof(name) / sizeof(wchar_t)))
		return false;

	return wcscmp(name, L"ApplicationFrameWindow") == 0;
}

HWND AudioCapture::GetUwpActualWindow(HWND parent_window)
{
	DWORD parent_pid;
	HWND child_window;

	GetWindowThreadProcessId(parent_window, &parent_pid);
	child_window = FindWindowEx(parent_window, NULL, NULL, NULL);

	while (child_window != NULL) {
		DWORD child_pid;
		GetWindowThreadProcessId(child_window, &child_pid);

		if (child_pid != parent_pid)
			return child_window;

		child_window = FindWindowEx(parent_window, child_window, NULL, NULL);
	}

	return NULL;
}


AudioCapture::AudioCapture(IRecorder *source) : source{source}{

	mixer.emplace(source, helper_manager.GetFormat());

	worker_thread = std::thread(&AudioCapture::Run, this);
	worker_tid = GetThreadId(worker_thread.native_handle());

	SessionMonitor::Instance()->RegisterEvent(worker_tid, CaptureEvents::SessionAdded,
						  CaptureEvents::SessionExpired);

}

AudioCapture::~AudioCapture(){
	SessionMonitor::Instance()->UnRegisterEvent(worker_tid);

	if (!worker_thread.joinable())
		return;

	worker_ready.wait();
	PostThreadMessageA(worker_tid, CaptureEvents::Shutdown, NULL, NULL);
	worker_thread.join();
}

static void audio_capture_destroy(void *data)
{
	auto *ctx = static_cast<AudioCapture *>(data);
	delete ctx;
}


};