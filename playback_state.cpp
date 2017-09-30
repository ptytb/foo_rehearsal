#include "stdafx.h"
#include "resource.h"
#include "timer_owner.h"

#include <Windows.h>
#include <functional>

//callInMainThreadHelper *mtHelper;
static_api_ptr_t<playback_control> *g_playback_control;

UINT_PTR g_timer = 0;
int g_timer_owner = 0;
TimerOwner *g_windows[2] = { NULL, NULL };
#define OWNER_SELF 1


class CPlaybackStateDemo;
typedef CWindowAutoLifetime<ImplementModelessTracking<CPlaybackStateDemo> > dialog_t;
CPlaybackStateDemo *current_dialog = NULL;

static double m_loop_from = 0.0;
static double m_loop_to = 0.0;
static bool m_paused = false;

void timerCallback(HWND hwnd, UINT uMsg, UINT timerId, DWORD dwTime);

class CPlaybackStateDemo :
	public CDialogImpl<CPlaybackStateDemo>,
	private play_callback_impl_base,
	public TimerOwner {

public:
	enum {IDD = IDD_LOOP};

	BEGIN_MSG_MAP(CPlaybackStateDemo)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnCancel)
		COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnAccept)
		COMMAND_HANDLER_EX(IDC_FROM_CURRENT, BN_CLICKED, OnFromCurrentClicked)
		COMMAND_HANDLER_EX(IDC_TO_CURRENT, BN_CLICKED, OnToCurrentClicked)
		COMMAND_HANDLER_EX(IDC_CLEAR, BN_CLICKED, OnClearClicked)
		COMMAND_HANDLER_EX(IDC_LOOP_FROM, EN_CHANGE, OnLoopFromChanged)
		COMMAND_HANDLER_EX(IDC_LOOP_TO, EN_CHANGE, OnLoopToChanged)
	END_MSG_MAP()

	void kill_timer() {
		if (g_timer_owner && g_timer_owner != TIMER_OWNER) {
			return;
		}

		::KillTimer(NULL, g_timer);
		g_timer = 0;
		g_timer_owner = 0;

		update_status();
	}

	void resume_timer() {
		if (g_timer_owner && g_timer_owner != TIMER_OWNER) {
			return;
		}

		g_timer = ::SetTimer(0, (UINT) 0, 5, TIMERPROC(&timerCallback));
		g_timer_owner = TIMER_OWNER;
		if (g_timer == NULL) {
			console::complain("Rehearsal", "Failed to create timer");
		}
		update_status();
	}

private:

	void update_other() {
		TimerOwner *other = g_windows[OWNER_SELF % 2];
		if (other) {
			other->update_status();
		}
	}

	void update_status() {
		CString text;
		
		bool other_window = (g_timer_owner && g_timer_owner != TIMER_OWNER);
		::EnableWindow(GetDlgItem(IDOK), !other_window);
		::EnableWindow(GetDlgItem(IDC_CLEAR), !other_window);
		if (other_window) {
			text = L"Other window";
		}
		else if (g_timer || m_paused) {
			text.Format(L"%.3lf - %.3lf", m_loop_from, m_loop_to);
		}
		else {
			text = L"None";
		}
		
		if (m_paused) {
			text.Format(L"%s (Paused)", text);
		}
		
		SetDlgItemText(IDC_ACTIVE, text);
	}

	// Playback callback methods.
	void on_playback_starting(play_control::t_track_command p_command, bool p_paused) {
		m_paused = false;
		update_status();
		update();
	}
	
	void on_playback_new_track(metadb_handle_ptr p_track) {
		m_paused = false;
		kill_timer();
	}
		
	void on_playback_stop(play_control::t_stop_reason p_reason) {
		m_paused = false;
		kill_timer();
	}
	
	void on_playback_seek(double p_time) {
		if (g_timer_owner && g_timer_owner != TIMER_OWNER) {
			return;
		}

		double position = m_playback_control->playback_get_position();
		if (position > m_loop_to || position < m_loop_from) {
			kill_timer();
			update_other();
		}
	}
	
	void on_playback_pause(bool p_state) {
		if (p_state) { // Pause
			if (g_timer) {
				m_paused = true;
				kill_timer();
			}
		}
		else { // Unpause
			if (m_paused) {
				m_paused = false;
				resume_timer();				
			}
		}
	}

	void on_playback_edited(metadb_handle_ptr p_track) { kill_timer(); }

	void on_playback_dynamic_info(const file_info & p_info) {update();}
	void on_playback_dynamic_info_track(const file_info & p_info) {update();}
	void on_playback_time(double p_time) { update();}
	void on_volume_change(float p_new_val) {}

	void update();

	void OnCancel(UINT, int, CWindow);
	void OnAccept(UINT, int, CWindow);

	void OnFromCurrentClicked(UINT, int, CWindow) {
		CString text;
		text.Format(L"%.3lf", m_playback_control->playback_get_position());
		SetDlgItemText(IDC_LOOP_FROM, text);
	}

	void OnToCurrentClicked(UINT, int, CWindow) {
		CString text;
		text.Format(L"%.3lf", m_playback_control->playback_get_position());
		SetDlgItemText(IDC_LOOP_TO, text);
	}

	void OnClearClicked(UINT, int, CWindow) {
		if (g_timer_owner && g_timer_owner != TIMER_OWNER) {
			return;
		}

		m_paused = false;
		kill_timer();
		update_other();
	}

	void util_format_time(int id_edit, int id_label) {
		CString text;
		GetDlgItemText(id_edit, text);
		double t = _wtof(text);
		double hours = int(t) / 3600;
		double mins = int(t - hours * 3600.0) / 60;
		double seconds_int;
		double seconds = modf(t, &seconds_int);
		seconds += int(seconds_int) % 60;
		text.Format(L"%02.0lf : %02.0lf : %02.3lf", hours, mins, seconds);
		SetDlgItemText(id_label, text);
	}

	void OnLoopFromChanged(UINT, int, CWindow) {
		util_format_time(IDC_LOOP_FROM, IDC_HMS_FROM);
	}

	void OnLoopToChanged(UINT, int, CWindow) {
		util_format_time(IDC_LOOP_TO, IDC_HMS_TO);
	}

	void OnContextMenu(CWindow wnd, CPoint point);
	BOOL OnInitDialog(CWindow, LPARAM);

	//friend void timerCallback(HWND hwnd, UINT uMsg, UINT timerId, DWORD dwTime);

	static_api_ptr_t<playback_control> m_playback_control;
};

void CPlaybackStateDemo::OnCancel(UINT, int, CWindow) {
	current_dialog = NULL;
	g_windows[OWNER_SELF - 1] = NULL;
	DestroyWindow();
}

static void timerCallback(HWND hwnd, UINT uMsg, UINT timerId, DWORD dwTime) {
	// some CODE for calling from thread other than main
	// for threaded implementation instead of timers

	//struct seeker {
	//	static void fire() {
	//		double current = (*g_playback_control)->playback_get_position();
	//		if (current >= m_loop_to) {
	//			(*g_playback_control)->playback_seek(m_loop_from);
	//		}
	//		
			//CString text;
			//text.Format(L"now at %.3lf", (*g_playback_control)->playback_get_position());
			//std::function<void()> f = std::bind(console::info, (char*)text.GetBuffer());
			//mtHelper.add(f);
		//}
	//};
	//std::function<void()> g = std::bind(seeker::fire);
	//mtHelper->add(g);

	double current = (*g_playback_control)->playback_get_position();
	if (current >= m_loop_to) {
		(*g_playback_control)->playback_seek(m_loop_from);
	}
}

void CPlaybackStateDemo::OnAccept(UINT, int, CWindow) {
	if (g_timer_owner && g_timer_owner != TIMER_OWNER) {
		return;
	}

	CString text;

	if (g_timer) {
		kill_timer();
	}

	GetDlgItemText(IDC_LOOP_FROM, text);
	m_loop_from = _wtof(text);
	
	GetDlgItemText(IDC_LOOP_TO, text);
	m_loop_to = _wtof(text);
	
	if (!m_paused && m_playback_control->is_playing()) {
		m_playback_control->playback_seek(m_loop_from); // Kills timer
		resume_timer();
	}	

	update_status();
	update_other();
}


BOOL CPlaybackStateDemo::OnInitDialog(CWindow, LPARAM) {
	update();

	CString text;
	
	text.Format(L"%.3lf", m_loop_from);
	SetDlgItemText(IDC_LOOP_FROM, text);

	text.Format(L"%.3lf", m_loop_to);
	SetDlgItemText(IDC_LOOP_TO, text);


	current_dialog = this;
	g_windows[OWNER_SELF - 1] = this;

	update_status();
	::ShowWindowCentered(*this, GetParent()); // Function declared in SDK helpers.
	return TRUE;
}

void CPlaybackStateDemo::update() {
}

void RunPlaybackStateDemo() {
	try {
		// ImplementModelessTracking registers our dialog to receive dialog messages thru main app loop's IsDialogMessage().
		// CWindowAutoLifetime creates the window in the constructor (taking the parent window as a parameter) and deletes the object when the window has been destroyed (through WTL's OnFinalMessage).
		if (current_dialog) {
			current_dialog->BringWindowToTop();
			return;
		}
		new dialog_t(core_api::get_main_window());
	} catch(std::exception const & e) {
		popup_message::g_complain("Dialog creation failure", e);
	}
}
