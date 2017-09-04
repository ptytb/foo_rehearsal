#include "stdafx.h"

// Sample initquit implementation. See also: initquit class documentation in relevant header.

extern static_api_ptr_t<playback_control> *g_playback_control;
//extern callInMainThreadHelper *mtHelper;
extern UINT_PTR g_timer;

class myinitquit : public initquit {
public:

	void on_init() {
		console::print("Rehearsal component: on_init() begin");
		g_playback_control = new static_api_ptr_t<playback_control>();
		//mtHelper = new callInMainThreadHelper();
		console::print("Rehearsal component: on_init() end");
	}

	void on_quit() {
		if (g_timer) {
			::KillTimer(NULL, g_timer);
		}
		console::print("Rehearsal component: on_quit()");
	}
};

static initquit_factory_t<myinitquit> g_myinitquit_factory;
