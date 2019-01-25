#include "stdafx.h"
#include "resource.h"
#include "timer_owner.h"

//#include "playable_location.h"
//#include "filesystem.h"

#include <Windows.h>

#include <atlbase.h>
#include <atlapp.h>
#include <atlcrack.h>
#include <atlwin.h>
#include <atlframe.h>

#include <functional>
#include <list>
#include <cmath>
#include <random>

#include "json.hpp"
using namespace nlohmann;

enum { HDR_CHECK = 0, HDR_FROM, HDR_TO, HDR_TITLE, HDR_COUNT };

//callInMainThreadHelper *mtHelper;
extern static_api_ptr_t<playback_control> *g_playback_control;
static_api_ptr_t<metadb_handle> *g_metadb_handle;

json *json_intervals = NULL;

int json_intervals_get_count() {
	return json_intervals->size();
}

extern UINT_PTR g_timer;
extern int g_timer_owner;
extern TimerOwner *g_windows[2];
#define OWNER_SELF 2

class CPlaybackStateSwitcher;
typedef CWindowAutoLifetime<ImplementModelessTracking<CPlaybackStateSwitcher> > dialog_t;
CPlaybackStateSwitcher *current_dialog = NULL;

static double m_loop_from = 0.0;
static double m_loop_to = 0.0;
static bool m_paused = false;
static bool m_stop_after = false;
static bool m_shuffle = false;
static int m_shuffle_amount = -1;
static int m_shuffle_counter = 0;
static std::vector<int> m_shuffle_queue;
static int m_highlighted = -1;  // Exclude clicked item from shuffled queue


void timerCallback(HWND hwnd, UINT uMsg, UINT timerId, DWORD dwTime);

void update_other() {
	TimerOwner *other = g_windows[OWNER_SELF % 2];
	if (other) {
		other->update_status();
	}
}

class CPlaybackStateSwitcher :
	public CDialogImpl<CPlaybackStateSwitcher>,
	public CDialogResize<CPlaybackStateSwitcher>,
	private play_callback_impl_base,
	public TimerOwner	
{

public:
	enum { IDD = IDD_LOOP_SWITCHER };

	BEGIN_MSG_MAP_EX(CPlaybackStateSwitcher)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnCancel)
		COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnAccept)
		COMMAND_HANDLER_EX(IDC_FROM_CURRENT, BN_CLICKED, OnFromCurrentClicked)
		COMMAND_HANDLER_EX(IDC_TO_CURRENT, BN_CLICKED, OnToCurrentClicked)
		COMMAND_HANDLER_EX(IDC_CLEAR, BN_CLICKED, OnClearClicked)
		COMMAND_HANDLER_EX(IDC_LOOP_FROM, EN_CHANGE, OnLoopFromChanged)
		COMMAND_HANDLER_EX(IDC_LOOP_TO, EN_CHANGE, OnLoopToChanged)
		COMMAND_HANDLER_EX(IDC_ADD, BN_CLICKED, OnAdd)
		NOTIFY_HANDLER(IDC_LIST, NM_DBLCLK, OnNMDblclkList)
		NOTIFY_HANDLER(IDC_LIST, LVN_KEYDOWN, OnListKeyDown)
		COMMAND_HANDLER_EX(IDC_STOP_AFTER_FINISHED, BN_CLICKED, OnStopCheckbox)
		COMMAND_HANDLER_EX(IDC_SHUFFLE_INTERVALS, BN_CLICKED, OnShuffleCheckbox)
		COMMAND_HANDLER_EX(IDC_SHUFFLE_AMOUNT, EN_CHANGE, OnShuffleAmountChanged)
		CHAIN_MSG_MAP(CDialogResize<CPlaybackStateSwitcher>)
	END_MSG_MAP()

	BEGIN_DLGRESIZE_MAP(CPlaybackStateSwitcher)
		DLGRESIZE_CONTROL(IDOK, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_CLEAR, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X | DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_LIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_BOX, DLSZ_SIZE_X | DLSZ_SIZE_Y)
		DLGRESIZE_CONTROL(IDC_STOP_AFTER_FINISHED, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_SHUFFLE_AMOUNT, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_SHUFFLE_INTERVALS, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_SHUFFLE_SPIN_BUTTON, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_LABEL_LOOP, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_ACTIVE, DLSZ_MOVE_Y)
	END_DLGRESIZE_MAP()

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

		if (m_paused) {
			return;
		}

		if (g_timer) {
			kill_timer();
		}

		g_timer = ::SetTimer(0, (UINT)0, 5, TIMERPROC(&timerCallback));
		g_timer_owner = TIMER_OWNER;

		if (g_timer == NULL) {
			console::complain("Rehearsal", "Failed to create timer");
		}

		update_status();
	}

	void FireLooping();
	void PlayCurrentListItem(int nItem = -1);

	void BeforePlayInterval();

	int GetIntervalListItemCount() {
		return list->GetItemCount();
	}

	int GetSelectedItemIndex() {
		return list->GetSelectedIndex();
	}

private:
	void save_track_profile();
	void load_track_profile();	

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

	// Playback callback methods. Before on_playback_new_track()
	void on_playback_starting(play_control::t_track_command p_command, bool p_paused) {
		m_paused = false;
		update_status();
		update();
	}

	void on_playback_new_track(metadb_handle_ptr p_track) {
		m_paused = false;
		kill_timer();
		load_track_profile();

		//if (m_shuffle) {
		//	BeforePlayInterval();
		//	list->SetItemState(0, LVIS_SELECTED, LVIS_SELECTED);
		//	//list->SetItemState(0, ~LVIS_SELECTED, LVIS_SELECTED);
		//	PlayCurrentListItem();
		//}
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

	void on_playback_dynamic_info(const file_info & p_info) { update(); }
	void on_playback_dynamic_info_track(const file_info & p_info) { update(); }
	void on_playback_time(double p_time) { update(); }
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

	void OnAdd(UINT, int, CWindow) {
		CString tFrom, tTo, tTitle;
		double from, to;
		CString record;

		GetDlgItemText(IDC_LOOP_FROM, tFrom);
		from = _wtof(tFrom);
		GetDlgItemText(IDC_LOOP_TO, tTo);
		to = _wtof(tTo);
		GetDlgItemText(IDC_TITLE, tTitle);

//		list->SetCheckState(0, true);
		int n = list->GetItemCount();
		list->AddItem(n, 0, L"");
		list->AddItem(n, 1, tFrom);
		list->AddItem(n, 2, tTo);
		list->AddItem(n, 3, tTitle);

		save_track_profile();
	}

	void update_timer() {
		CString text;

		GetDlgItemText(IDC_LOOP_FROM, text);
		m_loop_from = _wtof(text);

		GetDlgItemText(IDC_LOOP_TO, text);
		m_loop_to = _wtof(text);
	}

	LRESULT OnStopCheckbox(UINT, int, CWindow) {
		CCheckBox *cb = new CCheckBox(GetDlgItem(IDC_STOP_AFTER_FINISHED));
		int state = cb->GetCheck();
		m_stop_after = (state != BST_UNCHECKED);
		delete cb;
		return FALSE;
	}

	LRESULT OnShuffleCheckbox(UINT, int, CWindow) {
		CCheckBox *cb = new CCheckBox(GetDlgItem(IDC_SHUFFLE_INTERVALS));
		int state = cb->GetCheck();
		m_shuffle = (state != BST_UNCHECKED);
		delete cb;
		return FALSE;
	}
	
	void OnShuffleAmountChanged(UINT, int, CWindow) {
		m_shuffle_amount = GetDlgItemInt(IDC_SHUFFLE_AMOUNT);
	}

	BOOL OnInitDialog(CWindow, LPARAM);

	//friend void timerCallback(HWND hwnd, UINT uMsg, UINT timerId, DWORD dwTime);

	static_api_ptr_t<playback_control> m_playback_control;
	CListViewCtrl *list = NULL;

public:
	LRESULT OnNMDblclkList(int /*idCtrl*/, LPNMHDR pNMHDR, BOOL& /*bHandled*/);
	LRESULT OnListKeyDown(int /*idCtrl*/, LPNMHDR pNMHDR, BOOL& /*bHandled*/);
};

void CPlaybackStateSwitcher::OnCancel(UINT, int, CWindow) {
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

		int nItems = current_dialog->GetIntervalListItemCount();

		++m_shuffle_counter;
		
		bool shuffleThresholdReached =
			m_shuffle
			&& m_shuffle_counter > 0
			&& (m_shuffle_amount == 0 && m_shuffle_counter == nItems
				|| m_shuffle_amount == m_shuffle_counter);

		if (m_stop_after || m_shuffle || shuffleThresholdReached) {
			current_dialog->kill_timer();
			(*g_playback_control)->pause(true);
			update_other();
		}

		bool useShuffle = m_shuffle_amount <= nItems;

		if (m_shuffle_queue.empty() && useShuffle && !shuffleThresholdReached) {
			m_shuffle_queue.resize(nItems);

			std::generate(m_shuffle_queue.begin(), m_shuffle_queue.end(), [n = 0]() mutable {
				return n++;
			});

			if (m_highlighted != -1) {
				m_shuffle_queue.erase(m_shuffle_queue.begin() + m_highlighted);
			}			

			std::random_device rd;
			std::shuffle(m_shuffle_queue.begin(), m_shuffle_queue.end(), std::default_random_engine{ rd() });
		}
		
		if (m_shuffle && !m_stop_after && !shuffleThresholdReached) {
			//current_dialog->FireLooping();

			int nRandom;
			if (useShuffle) {
				nRandom = m_shuffle_queue.back();
				m_shuffle_queue.pop_back();
			}
			else {
				nRandom = rand() % nItems;
			}

			current_dialog->PlayCurrentListItem(nRandom);
		}

	}
}

void CPlaybackStateSwitcher::FireLooping() {
	update_timer();

	if (!m_paused && m_playback_control->is_playing()) {
		m_playback_control->playback_seek(m_loop_from); // Kills timer
		resume_timer();
	}

	update_status();
	update_other();
}

void CPlaybackStateSwitcher::OnAccept(UINT, int, CWindow) {
	if (g_timer_owner && g_timer_owner != TIMER_OWNER) {
		return;
	}

	FireLooping();
}

BOOL CPlaybackStateSwitcher::OnInitDialog(CWindow, LPARAM) {
	DlgResize_Init(true);
	update();	

	CString text;

	::SendMessage(GetDlgItem(IDC_SHUFFLE_SPIN_BUTTON), UDM_SETRANGE, 0, MAKELPARAM(999999, 0));

	text.Format(L"%.3lf", m_loop_from);
	SetDlgItemText(IDC_LOOP_FROM, text);

	text.Format(L"%.3lf", m_loop_to);
	SetDlgItemText(IDC_LOOP_TO, text);

	this->list = new CListViewCtrl(GetDlgItem(IDC_LIST));
	list->SetViewType(LVS_REPORT);
	list->SetExtendedListViewStyle(LVS_EX_FULLROWSELECT);

	list->AddColumn(L"", 0);
	list->AddColumn(L"From", 1);
	list->AddColumn(L"To", 2);
	list->AddColumn(L"Title", 3);

	list->SetColumnWidth(0, 25);
	list->SetColumnWidth(1, 64);
	list->SetColumnWidth(2, 64);
	list->SetColumnWidth(3, 192);

	current_dialog = this;
	g_windows[OWNER_SELF - 1] = this;

	update_status();
	load_track_profile();
	
	::ShowWindowCentered(*this, GetParent()); // Function declared in SDK helpers.
	
	return TRUE;
}

void CPlaybackStateSwitcher::update() {
}

void RunPlaybackStateSwitcher() {
	try {
		// ImplementModelessTracking registers our dialog to receive dialog messages thru main app loop's IsDialogMessage().
		// CWindowAutoLifetime creates the window in the constructor (taking the parent window as a parameter) and deletes the object when the window has been destroyed (through WTL's OnFinalMessage).
		if (current_dialog) {
			current_dialog->BringWindowToTop();
			return;
		}
		new dialog_t(core_api::get_main_window());
	}
	catch (std::exception const & e) {
		popup_message::g_complain("Dialog creation failure", e);
	}
}

void CPlaybackStateSwitcher::PlayCurrentListItem(int nItem) {
	if (nItem == -1) {
		nItem = list->GetSelectedIndex();
	}

	CString text;

	list->GetItemText(nItem, HDR_FROM, text);
	SetDlgItemText(IDC_LOOP_FROM, text);

	list->GetItemText(nItem, HDR_TO, text);
	SetDlgItemText(IDC_LOOP_TO, text);

	list->GetItemText(nItem, HDR_TITLE, text);
	SetDlgItemText(IDC_TITLE, text);

	if (g_timer_owner && g_timer_owner != TIMER_OWNER) {
		return;
	}

	kill_timer();
	update_timer();
	m_playback_control->playback_seek(m_loop_from);
	if ((*g_playback_control)->is_paused()) {
		(*g_playback_control)->pause(false);
	}
	resume_timer();
	update_other();
}

void CPlaybackStateSwitcher::BeforePlayInterval() {
	m_shuffle_queue.clear();
	m_shuffle_counter = 0;
	m_highlighted = GetSelectedItemIndex();
}

LRESULT CPlaybackStateSwitcher::OnNMDblclkList(int /*idCtrl*/, LPNMHDR pNMHDR, BOOL& /*bHandled*/)
{
	BeforePlayInterval();
	PlayCurrentListItem();
	return 0;
}

LRESULT CPlaybackStateSwitcher::OnListKeyDown(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
{
	NMLVKEYDOWN* kd = (NMLVKEYDOWN*)pnmh;

	switch (kd->wVKey) {

	case VK_DELETE:
		if (list->GetSelectedCount() == 1) {
			int n = list->GetSelectedIndex();
			list->DeleteItem(n);
		}
		else {
			LVITEM item;
			while (list->GetSelectedItem(&item)) {
				list->DeleteItem(item.iItem);
			}
		}
		save_track_profile();
		break;

	case VK_RETURN:
		BeforePlayInterval();
		PlayCurrentListItem();
		break;
	}

	return 0;
}

CString mbtowc(CStringA s) {
	wchar_t buf[500];
	MultiByteToWideChar(CP_UTF8, 0, s.GetBuffer(), -1, buf, 500);
	return CString(buf);
}

CStringA wctomb(CString s) {
	char buf[500];
	WideCharToMultiByte(CP_UTF8, 0, s.GetBuffer(), -1, buf, 500, 0, 0);
	return CStringA(buf);
}

void CPlaybackStateSwitcher::load_track_profile()
{
	list->DeleteAllItems();

	metadb_handle_ptr p_out;
	bool playing = (*g_playback_control)->get_now_playing(p_out);
	if (!playing) {
		return;
	}

	CStringA path_a(p_out->get_path());
	CStringA track_id;
	track_id.Format("%s#%d", path_a, p_out->get_subsong_index());

	if (json_intervals->find(track_id.GetBuffer()) == json_intervals->end()) {
		//MessageBox(L"1");
		return;
	}

	auto table = (*json_intervals)[track_id.GetBuffer()];

	for (auto i = table.cbegin(); i != table.cend(); ++i) {
		int n = list->GetItemCount();
		list->AddItem(n, 0, L"");
		for (int col = 1; col < HDR_COUNT; ++col) {			
			auto record = *i;
			std::string text_a = record[col - 1];
			CString text = mbtowc(text_a.c_str());
			list->AddItem(n, col, text);
		}
	}
}

void CPlaybackStateSwitcher::save_track_profile() {
	metadb_handle_ptr p_out;
	(*g_playback_control)->get_now_playing(p_out);
	CStringA path_a(p_out->get_path());
	CString path(mbtowc(path_a));
	
	if (path.Find(L"file://") == 0) {
		path.Delete(0, 7);
	}

	path += ".json";

	CStringA track_id;
	track_id.Format("%s#%d", path_a, p_out->get_subsong_index());

	if (list->GetItemCount() == 0) {
		json_intervals->erase(track_id.GetBuffer());
		return;
	}

	auto table = json::array();
	
	for (int i = 0; i < list->GetItemCount(); ++i) {
		auto record = json::array();
		CString text;
		for (int col = 1; col < HDR_COUNT; ++col) {
			list->GetItemText(i, col, text);
			record.push_back(wctomb(text).GetBuffer());
		}
		table.push_back(record);
	}

	(*json_intervals)[track_id.GetBuffer()] = table;
}

