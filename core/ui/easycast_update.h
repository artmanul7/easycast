// EasyCast 0.2.8 (Claude): updates from GitHub Releases inside the emulator (Settings > About).
// The work is done by the launcher's helper (EASYCAST_UPDATER = runtime/<v>/easycast_update.py, set by run.py):
// --check / --install print "STEP <text>" progress lines and a last "RESULT <state> <version> <message>" line.
// It runs in a background thread, so the menu stays responsive. The launcher also runs "--auto" (once a day) at start;
// its result (userdata/update.json) gives a short notice when a newer version exists.
#pragma once
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

namespace easycast_update {

struct State {
	std::mutex lock;
	std::string status;		// last STEP / RESULT text for the screen
	std::string result;		// newer | latest | installed | error (empty while running)
	std::string version;	// version named by the last RESULT
	std::atomic<bool> busy { false };
};

inline State &state() { static State s; return s; }

inline const char *tool() { return std::getenv("EASYCAST_UPDATER"); }
inline const char *current() { const char *v = std::getenv("EASYCAST_VERSION"); return v ? v : "?"; }

inline void run(const char *arg)
{
	State &s = state();
	if (tool() == nullptr || s.busy.exchange(true))
		return;
	{
		std::lock_guard<std::mutex> g(s.lock);
		s.result.clear();
		s.status = arg[2] == 'c' ? "Checking GitHub..." : "Starting the update...";
	}
	std::thread([arg]() {
		State &s = state();
		std::string cmd = std::string("python3 '") + tool() + "' " + arg + " 2>&1";
		FILE *p = popen(cmd.c_str(), "r");
		char line[512];
		bool got = false;
		while (p != nullptr && std::fgets(line, sizeof(line), p) != nullptr) {
			std::string l(line);
			while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
			std::lock_guard<std::mutex> g(s.lock);
			if (l.rfind("STEP ", 0) == 0) {
				s.status = l.substr(5);
			} else if (l.rfind("RESULT ", 0) == 0) {
				std::string r = l.substr(7);
				size_t a = r.find(' '), b = a == std::string::npos ? a : r.find(' ', a + 1);
				s.result = r.substr(0, a);
				s.version = a == std::string::npos ? "" : r.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
				s.status = b == std::string::npos ? r : r.substr(b + 1);
				got = true;
			}
		}
		if (p != nullptr) pclose(p);
		if (!got) {
			std::lock_guard<std::mutex> g(s.lock);
			s.result = "error";
			s.status = "The update helper did not answer";
		}
		s.busy = false;
	}).detach();
}

// one line at start-up when the launcher's daily check found a newer version
inline bool newerKnown(std::string &ver)
{
	const char *root = std::getenv("EASYCAST_ROOT");
	if (root == nullptr) return false;
	std::string path = std::string(root) + "/userdata/update.json";
	FILE *f = std::fopen(path.c_str(), "r");
	if (f == nullptr) return false;
	char buf[512] = {};
	size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
	std::fclose(f);
	std::string j(buf, n);
	if (j.find("\"newer\": true") == std::string::npos) return false;
	size_t k = j.find("\"latest\": \"");
	if (k == std::string::npos) return false;
	k += 11;
	ver = j.substr(k, j.find('"', k) - k);
	return true;
}

} // namespace easycast_update
