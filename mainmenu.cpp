#include "stdafx.h"

// {E7C94111-BC5C-4464-9723-C486BB1A10F4}
static const GUID g_mainmenu_group_id = { 0xe7c94111, 0xbc5c, 0x4464,{ 0x97, 0x23, 0xc4, 0x86, 0xbb, 0x1a, 0x10, 0xf4 } };


static mainmenu_group_popup_factory g_mainmenu_group(g_mainmenu_group_id, mainmenu_groups::playback,
	mainmenu_commands::sort_priority_dontcare, "Loop");

void RunPlaybackStateDemo(); //playback_state.cpp
void RunPlaybackStateSwitcher(); //playback_state_switcher.cpp

class mainmenu_commands_rehearsal : public mainmenu_commands {
public:
	enum {
		cmd_loop = 0,
		cmd_loop_switcher,
		cmd_total
	};
	
	t_uint32 get_command_count() {
		return cmd_total;
	}

	GUID get_command(t_uint32 p_index) {
		// {979211DA-1C3F-4F6B-9D2F-E04EE0670646}
		static const GUID guid_loop = { 0x979211da, 0x1c3f, 0x4f6b,{ 0x9d, 0x2f, 0xe0, 0x4e, 0xe0, 0x67, 0x6, 0x46 } };
		// {959FC715-A7C1-4502-A987-DD861CC8414C}
		static const GUID guid_loop_switcher = { 0x959fc715, 0xa7c1, 0x4502,{ 0xa9, 0x87, 0xdd, 0x86, 0x1c, 0xc8, 0x41, 0x4c } };

		switch(p_index) {
		case cmd_loop: return guid_loop;
		case cmd_loop_switcher: return guid_loop_switcher;
		default: uBugCheck(); // should never happen unless somebody called us with invalid parameters - bail
		}
	}

	void get_name(t_uint32 p_index, pfc::string_base & p_out) {
		switch(p_index) {
		case cmd_loop: p_out = "Interval..."; break;
		case cmd_loop_switcher: p_out = "Multiple intervals..."; break;
		default: uBugCheck(); // should never happen unless somebody called us with invalid parameters - bail
		}
	}

	bool get_description(t_uint32 p_index,pfc::string_base & p_out) {
		switch(p_index) {
		case cmd_loop: p_out = "Input the beginning and the end of the interval for looping"; return true;
		case cmd_loop_switcher: p_out = "Manage multiple intervals for looping"; return true;
		default: uBugCheck(); // should never happen unless somebody called us with invalid parameters - bail
		}
	}

	GUID get_parent() {
		return g_mainmenu_group_id;
	}

	void execute(t_uint32 p_index,service_ptr_t<service_base> p_callback) {
		switch(p_index) {

		case cmd_loop:
			RunPlaybackStateDemo();
			break;

		case cmd_loop_switcher:
			RunPlaybackStateSwitcher();
			break;

		default:
			uBugCheck(); // should never happen unless somebody called us with invalid parameters - bail
		}
	}
};

static mainmenu_commands_factory_t<mainmenu_commands_rehearsal> g_mainmenu_commands_sample_factory;
