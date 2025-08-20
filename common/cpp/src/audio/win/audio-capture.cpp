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

DWORD GetCurrentPID() {
    return GetCurrentProcessId();
}

	std::set<DWORD> GetChildProcesses(DWORD parentPID) {
    std::set<DWORD> children;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return children;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hSnapshot, &pe)) {
        do {
            if (pe.th32ParentProcessID == parentPID) {
                children.insert(pe.th32ProcessID);
            }
        } while (Process32Next(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return children;
}


void AudioCapture::WorkerUpdate()
{
	auto config_lock = config_section.lock();
	auto config_ = this->config;
	config_lock.reset();

	auto sessions = SessionMonitor::Instance()->GetSessions();
	std::set<DWORD> capture_pids;

    DWORD current_pid = GetCurrentPID();
    std::set<DWORD> current_pids = GetChildProcesses(current_pid);
    current_pids.insert(current_pid);

	for (auto &[key, executable] : sessions) {
        if (current_pids.contains(key.pid)) {
        	continue;
        }

        if (config_.mode == MODE_SESSION_PID && key.pid == config_.pid) {
        	capture_pids.insert(key.pid);
        	continue;
        }

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

	StartCapture(capture_pids);
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
      	.pid = static_cast<DWORD>(settings->pid),
	};

	auto lock = config_section.lock();
	config = std::move(new_config);
	lock.reset();

	PostThreadMessageA(worker_tid, CaptureEvents::Update, NULL, NULL);
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

};