#include "stdafx.h"
#include "resource.h"

#include <Windows.h>
#include <functional>

//callInMainThreadHelper *mtHelper;
static_api_ptr_t<playback_control> *g_playback_control;
UINT_PTR g_timer = 0;

class CPlaybackStateDemo;
typedef CWindowAutoLifetime<ImplementModelessTracking<CPlaybackStateDemo> > dialog_t;
dialog_t *current_dialog = NULL;

static double m_loop_from = 0.0;
static double m_loop_to = 0.0;

void timerCallback(HWND hwnd, UINT uMsg, UINT timerId, DWORD dwTime);

class CPlaybackStateDemo : public CDialogImpl<CPlaybackStateDemo>, private play_callback_impl_base {

public:
	enum {IDD = IDD_LOOP};

	BEGIN_MSG_MAP(CPlaybackStateDemo)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnCancel)
		COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnAccept)
		COMMAND_HANDLER_EX(IDC_FROM_CURRENT, BN_CLICKED, OnFromCurrentClicked)
		COMMAND_HANDLER_EX(IDC_TO_CURRENT, BN_CLICKED, OnToCurrentClicked)
		COMMAND_HANDLER_EX(IDC_CLEAR, BN_CLICKED, OnClearClicked)
	END_MSG_MAP()

	void kill_timer() {
		::KillTimer(NULL, g_timer);
		g_timer = 0;
		SetDlgItemText(IDC_ACTIVE, L"None");
	}

private:

	// Playback callback methods.
	void on_playback_starting(play_control::t_track_command p_command, bool p_paused) { update(); }
	
	void on_playback_new_track(metadb_handle_ptr p_track) { kill_timer(); }
	
	void on_playback_stop(play_control::t_stop_reason p_reason) { kill_timer(); }
	
	void on_playback_seek(double p_time) {
		double position = m_playback_control->playback_get_position();
		if (position > m_loop_to || position < m_loop_from) {
			kill_timer();
		}
	}
	
	void on_playback_pause(bool p_state) { kill_timer(); }

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
		kill_timer();
	}

	void OnContextMenu(CWindow wnd, CPoint point);
	BOOL OnInitDialog(CWindow, LPARAM);

	//friend void timerCallback(HWND hwnd, UINT uMsg, UINT timerId, DWORD dwTime);

	static_api_ptr_t<playback_control> m_playback_control;
};

void CPlaybackStateDemo::OnCancel(UINT, int, CWindow) {
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
	CString text;
	
	GetDlgItemText(IDC_LOOP_FROM, text);
	m_loop_from = _wtof(text);
	
	GetDlgItemText(IDC_LOOP_TO, text);
	m_loop_to = _wtof(text);

	text.Format(L"%.3lf - %.3lf", m_loop_from, m_loop_to);
	SetDlgItemText(IDC_ACTIVE, text);

	if (g_timer) {
		kill_timer();
	}

	g_timer = ::SetTimer(0, (UINT) 0, 5, TIMERPROC(&timerCallback));
	if (g_timer == NULL) {
		console::complain("Rehearsal", "Failed to create timer");
	}

	m_playback_control->playback_seek(m_loop_from);
}


BOOL CPlaybackStateDemo::OnInitDialog(CWindow, LPARAM) {
	update();

	CString text;
	
	text.Format(L"%.3lf", m_loop_from);
	SetDlgItemText(IDC_LOOP_FROM, text);

	text.Format(L"%.3lf", m_loop_to);
	SetDlgItemText(IDC_LOOP_TO, text);

	if (g_timer) {
		text.Format(L"%.3lf - %.3lf", m_loop_from, m_loop_to);
	}
	else {
		text = L"None";
	}
	SetDlgItemText(IDC_ACTIVE, text);

	::ShowWindowCentered(*this,GetParent()); // Function declared in SDK helpers.
	return TRUE;
}

void CPlaybackStateDemo::update() {
}

void RunPlaybackStateDemo() {
	try {
		// ImplementModelessTracking registers our dialog to receive dialog messages thru main app loop's IsDialogMessage().
		// CWindowAutoLifetime creates the window in the constructor (taking the parent window as a parameter) and deletes the object when the window has been destroyed (through WTL's OnFinalMessage).
		current_dialog = new dialog_t(core_api::get_main_window());
		current_dialog = NULL;		
	} catch(std::exception const & e) {
		popup_message::g_complain("Dialog creation failure", e);
	}
}
