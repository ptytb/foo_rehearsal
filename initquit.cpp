#include "stdafx.h"
#include "json.hpp"

#include <fstream>

// Sample initquit implementation. See also: initquit class documentation in relevant header.

extern static_api_ptr_t<playback_control> *g_playback_control;
//extern callInMainThreadHelper *mtHelper;
extern UINT_PTR g_timer;
extern nlohmann::json *json_intervals;

void save_track_db();
void load_track_db();

class myinitquit : public initquit {
public:

	void on_init() {
		console::print("Rehearsal component: on_init() begin");
		g_playback_control = new static_api_ptr_t<playback_control>();
		//mtHelper = new callInMainThreadHelper();

		json_intervals = new nlohmann::json();
		load_track_db();
		
		console::print("Rehearsal component: on_init() end");
	}

	void on_quit() {
		if (g_timer) {
			::KillTimer(NULL, g_timer);
		}
		
		save_track_db();
		delete json_intervals;

		console::print("Rehearsal component: on_quit()");
	}
};

static initquit_factory_t<myinitquit> g_myinitquit_factory;

void save_track_db() {
	std::ofstream of(L"D:\\work\\foo_rehearsal.json");
	of << (*json_intervals);
	of.close();
}

void load_track_db() {
	std::ifstream of(L"D:\\work\\foo_rehearsal.json");
	if (of.is_open()) {
		of >> (*json_intervals);
	}
}