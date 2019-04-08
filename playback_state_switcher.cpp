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
#include <atlcoll.h>

#include <functional>
#include <list>
#include <cmath>
#include <random>
#include <sstream>
#include <fstream>

#include "json.hpp"
using namespace nlohmann;

#include "../../libcue/libcue.h"
extern "C" {
#include "../../libcue/time.h"
}
#define NOMINMAX
#include <re2/re2.h>
#include <re2/stringpiece.h>

enum { HDR_CHECK = 0, HDR_FROM, HDR_TO, HDR_TITLE, HDR_COUNT };

//callInMainThreadHelper *mtHelper;
extern static_api_ptr_t<playback_control> *g_playback_control;
static_api_ptr_t<metadb_handle> *g_metadb_handle;

void kill_timer();
void resume_timer();
void PlayCurrentListItemFromDatabase(CStringA track_id, int nItem);

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

static_api_ptr_t<playback_control> *m_playback_control;
static double m_loop_from = 0.0;
static double m_loop_to = 0.0;
static bool m_paused = false;
static bool m_stop_after = false;
static bool m_shuffle = false;
static bool m_shuffle_fake_only_loop = false;
static int m_shuffle_amount = -1;
static int m_shuffle_counter = 0;
static std::vector<int> m_shuffle_queue;
static int m_highlighted = -1;  // Exclude clicked item from shuffled queue
static int m_n_items = 0;
static CStringA m_track_id;
static bool m_is_data_updating = false;

void begin_update();
void end_update();

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
		COMMAND_HANDLER_EX(IDC_IMPORT, BN_CLICKED, OnImport)
		COMMAND_HANDLER_EX(IDC_EXPORT, BN_CLICKED, OnExport)
		COMMAND_HANDLER_EX(IDC_FROM_CURRENT, BN_CLICKED, OnFromCurrentClicked)
		COMMAND_HANDLER_EX(IDC_TO_CURRENT, BN_CLICKED, OnToCurrentClicked)
		COMMAND_HANDLER_EX(IDC_CLEAR, BN_CLICKED, OnClearClicked)
		COMMAND_HANDLER_EX(IDC_LOOP_FROM, EN_CHANGE, OnLoopFromChanged)
		COMMAND_HANDLER_EX(IDC_LOOP_TO, EN_CHANGE, OnLoopToChanged)
		COMMAND_HANDLER_EX(IDC_ADD, BN_CLICKED, OnAdd)
		NOTIFY_HANDLER(IDC_LIST, NM_DBLCLK, OnNMDblclkList)
		NOTIFY_HANDLER(IDC_LIST, LVN_KEYDOWN, OnListKeyDown)
		NOTIFY_HANDLER(IDC_LIST, LVN_ITEMCHANGED, OnListItemChanged)
		COMMAND_HANDLER_EX(IDC_STOP_AFTER_FINISHED, BN_CLICKED, OnStopCheckbox)
		COMMAND_HANDLER_EX(IDC_SHUFFLE_INTERVALS, BN_CLICKED, OnShuffleCheckbox)
		COMMAND_HANDLER_EX(IDC_SHUFFLE_AMOUNT, EN_CHANGE, OnShuffleAmountChanged)
		CHAIN_MSG_MAP(CDialogResize<CPlaybackStateSwitcher>)
	END_MSG_MAP()

	BEGIN_DLGRESIZE_MAP(CPlaybackStateSwitcher)
		DLGRESIZE_CONTROL(IDOK, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_CLEAR, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_IMPORT, DLSZ_MOVE_Y)
		DLGRESIZE_CONTROL(IDC_EXPORT, DLSZ_MOVE_Y)
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

	void FireLooping();
	void PlayCurrentListItem(int nItem = -1);
	void PlayCurrentListItemFromDatabase(CStringA track_id, int nItem); 
	void BeforePlayInterval();

	int GetIntervalListItemCount() {
		return list->GetItemCount();
	}

	int GetSelectedItemIndex() {
		//return list->GetSelectedIndex();

		for (int i = 0, end = list->GetItemCount(); i < end; ++i) {
			if (list->GetItemState(i, LVIS_SELECTED) == LVIS_SELECTED) {
				return i;
			}
		}
		
		return -1;
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

private:
	std::unique_ptr<CCheckBox> cbStopAfterFinished;
	std::unique_ptr<CCheckBox> cbShuffleIntervals;

	void save_track_profile();
	void load_track_profile();	

	CStringA GetCurrentTrackId(); 
 
	// Playback callback methods. Before on_playback_new_track()
	void on_playback_starting(play_control::t_track_command p_command, bool p_paused) {
		kill_timer();
		m_paused = false;
		update_status();
	}

	void on_playback_new_track(metadb_handle_ptr p_track) {
		kill_timer();
		m_paused = false;
		m_track_id = GetCurrentTrackId();
		console::complain("foo_rehearsal load track_id", m_track_id.GetString());
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

		double position = (*m_playback_control)->playback_get_position();
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
	void OnImport(UINT, int, CWindow);
	void OnExport(UINT, int, CWindow);

	void ImportCUE(std::string filename);
	void ImportJSON(std::string filename);
	void ImportFromClipboard();

	void OnFromCurrentClicked(UINT, int, CWindow) {
		CString text;
		text.Format(L"%.3lf", (*m_playback_control)->playback_get_position());
		SetDlgItemText(IDC_LOOP_FROM, text);
	}

	void OnToCurrentClicked(UINT, int, CWindow) {
		CString text;
		text.Format(L"%.3lf", (*m_playback_control)->playback_get_position());
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

	CString format_time(double t, CString sep = L" : ", bool milliseconds = true) {
		double hours = int(t) / 3600;
		double mins = int(t - hours * 3600.0) / 60;
		double seconds_int;
		double seconds = modf(t, &seconds_int);
		seconds += int(seconds_int) % 60;
		CString text;
		if (milliseconds) {
			text.Format(L"%02.0lf%s%02.0lf%s%02.3lf", hours, sep, mins, sep, seconds);
		}
		else {
			text.Format(L"%02.0lf%s%02.0lf%s%02.0lf", hours, sep, mins, sep, seconds);
		}
		return text;
	}

	void util_format_time(int id_edit, int id_label) {
		CString text;
		GetDlgItemText(id_edit, text);
		double t = _wtof(text);
		text = format_time(t);
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

		int n = list->GetItemCount();
		list->AddItem(n, 0, L"");
		list->AddItem(n, 1, tFrom);
		list->AddItem(n, 2, tTo);
		list->AddItem(n, 3, tTitle);
		list->SetCheckState(n, true);

		save_track_profile();
	}

	void update_timer_range_from_window() {
		CString text;

		GetDlgItemText(IDC_LOOP_FROM, text);
		m_loop_from = _wtof(text);

		GetDlgItemText(IDC_LOOP_TO, text);
		m_loop_to = _wtof(text);
	}

	LRESULT OnStopCheckbox(UINT, int, CWindow) {
		int state = cbStopAfterFinished->GetCheck();
		m_stop_after = (state != BST_UNCHECKED);
		return FALSE;
	}

	LRESULT OnShuffleCheckbox(UINT, int, CWindow) {
		int state = cbShuffleIntervals->GetCheck();
		m_shuffle = (state != BST_UNCHECKED);
		m_shuffle_fake_only_loop = (state == BST_INDETERMINATE);
		return FALSE;
	}
	
	void OnShuffleAmountChanged(UINT, int, CWindow window) {
		if (window.IsWindowVisible()) {
			m_shuffle_amount = GetDlgItemInt(IDC_SHUFFLE_AMOUNT);
		}
	}

	BOOL OnInitDialog(CWindow, LPARAM);

	//friend void timerCallback(HWND hwnd, UINT uMsg, UINT timerId, DWORD dwTime);
 
public:
	CListViewCtrl *list = NULL;

	LRESULT OnNMDblclkList(int /*idCtrl*/, LPNMHDR pNMHDR, BOOL& /*bHandled*/);
	LRESULT OnListKeyDown(int /*idCtrl*/, LPNMHDR pNMHDR, BOOL& /*bHandled*/);
	LRESULT OnListItemChanged(int /*idCtrl*/, LPNMHDR pNMHDR, BOOL& /*bHandled*/);
};

void CPlaybackStateSwitcher::OnCancel(UINT, int, CWindow) {
	m_track_id = GetCurrentTrackId();
	current_dialog = NULL;
	g_windows[OWNER_SELF - 1] = NULL;
	DestroyWindow();	
}

std::function<bool(int)> is_checked = [](int x) {
	auto table = (*json_intervals)[m_track_id.GetBuffer()];
	auto table_it = table.cbegin();

	if (table_it == table.cend()) {
		return false;
	}

	auto record = *(table_it + x);
	bool checked = record[0].get<bool>();
	return checked;
};

static void timerCallback(HWND hwnd, UINT uMsg, UINT timerId, DWORD dwTime)
{
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

		++m_shuffle_counter;
		
		bool shuffleThresholdReached =
			m_shuffle
			&& m_shuffle_counter > 0
			&& (m_shuffle_amount <= 0 && m_shuffle_counter >= m_n_items
				|| m_shuffle_amount == m_shuffle_counter);

		if (shuffleThresholdReached && m_shuffle_amount == -1) {
			//console::complain(" ===== threshold! ====", ".... going new loop");
			m_shuffle_counter = 0;
			m_highlighted = -1;
			shuffleThresholdReached = false; 
		}

		if (m_stop_after || m_shuffle && shuffleThresholdReached) {
			kill_timer();
			(*g_playback_control)->pause(true);
			update_other();
		}

		bool useShuffleQueue = m_shuffle_amount <= m_n_items;

		if (m_shuffle && m_shuffle_queue.empty() && useShuffleQueue && !shuffleThresholdReached) {
			m_shuffle_queue.resize(m_n_items);
			//console::complain(("resize " + std::to_string(m_shuffle_queue.size())).c_str(), "");

			std::generate(m_shuffle_queue.begin(), m_shuffle_queue.end(),
				[n = 0]() mutable {
				return n++;
			});

			//console::complain(("highl " + std::to_string(m_highlighted)).c_str(), "");
			if (m_highlighted != -1) {
				m_shuffle_queue.erase(m_shuffle_queue.begin() + m_highlighted);
			}			

			using queue_t = decltype(m_shuffle_queue);
			queue_t queue_filtered;
			std::copy_if(m_shuffle_queue.begin(), m_shuffle_queue.end(), std::back_inserter(queue_filtered), is_checked); 

			//std::remove_if(m_shuffle_queue.begin(), m_shuffle_queue.end(), is_checked);
			//std::erase(...); // required!

			m_shuffle_queue.clear();
			std::reverse_copy(queue_filtered.begin(), queue_filtered.end(), std::back_inserter(m_shuffle_queue));

			if (!m_shuffle_fake_only_loop) {
				std::random_device rd;
				std::shuffle(m_shuffle_queue.begin(), m_shuffle_queue.end(), std::default_random_engine{ rd() });
			}
		}

		//std::string s;
		//std::for_each(m_shuffle_queue.begin(), m_shuffle_queue.end(),
		//	[&](const int piece) { s += " " + std::to_string(piece) + ", "; });
		//console::complain(("rehearsal " + std::to_string(m_shuffle_queue.size())).c_str(), s.c_str());
		
		if (m_shuffle && !m_stop_after && !shuffleThresholdReached) {
			//current_dialog->FireLooping();

			int nRandom;
			if (useShuffleQueue) {
				if (m_shuffle_queue.empty()) {
					kill_timer();
					console::complain("foo_rehearsal", "stopped because of empty shuffle queue");
					return;
				}
				nRandom = m_shuffle_queue.back();
				m_shuffle_queue.pop_back();
			}
			else {
				nRandom = rand() % m_n_items;
				auto nPreventInfiniteLoop = nRandom;
				while (!is_checked(nRandom)) {
					nRandom = (nRandom + 1) % m_n_items;
					if (nRandom == nPreventInfiniteLoop) {
						kill_timer();
						console::complain("foo_rehearsal", "stopped because of empty random queue");
						return;
					}
				}
			}

			if (current_dialog) {
				current_dialog->PlayCurrentListItem(nRandom);
			}
			else {
				PlayCurrentListItemFromDatabase(m_track_id, nRandom);
			}
		}
 
	}
}

void CPlaybackStateSwitcher::FireLooping() {
	update_timer_range_from_window();

	if (!m_paused && (*m_playback_control)->is_playing()) {
		(*m_playback_control)->playback_seek(m_loop_from); // Kills timer
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

std::string QueryUserCueSheetFilename(bool save = false)
{
	wchar_t szFile[MAX_PATH];

	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFilter = L"CUE Files\0*.cue\0JSON Chapters from ffprobe\0*.json\0\0";
	ofn.lpstrFile = szFile;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = L"Select CUE Sheet filename";
	ofn.Flags = save
		? OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST
		: OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST;

	if (save) {
		if (::GetSaveFileName(&ofn)) {
			CString name(ofn.lpstrFile);
			if (name.Right(4) != L".cue") {
				name += L".cue";
			}
			return CStringA(name);
		}

	}
	else {
		if (::GetOpenFileName(&ofn))
		{
			return CStringA(ofn.lpstrFile);
		}
	}
	return "";
}

CString CueTimeStampToSeconds(long t)
{
	int m, s, f;
	time_frame_to_msf(t, &m, &s, &f);
	CString value;
	value.Format(L"%.03lf", m * 60 + s + f / 75.0);
	return value;
}

void CPlaybackStateSwitcher::ImportCUE(std::string cue_sheet_filename)
{
	FILE *f = std::fopen(cue_sheet_filename.c_str(), "r");
	if (f == NULL) {
		return;
	}
 
	Cd *cue = cue_parse_file(f); 

	if (cue == NULL) {
		return;
	}

	int nTracks = cd_get_ntrack(cue);

	begin_update();
	for (size_t i = 0; i < nTracks; i++) {
		Track *track = cd_get_track(cue, i + 1);
		Cdtext *info = track_get_cdtext(track);
		long start = track_get_start(track);
		long length = track_get_length(track);

		list->AddItem(i, 0, L"");
		list->AddItem(i, 1, CueTimeStampToSeconds(start));
		list->AddItem(i, 2, CueTimeStampToSeconds(start + length));
		list->AddItem(i, 3, CString(cdtext_get(PTI_TITLE, info)));
		list->SetCheckState(i, true);
	} 
	end_update();

	cd_delete(cue);
	std::fclose(f);
	save_track_profile();
}

void CPlaybackStateSwitcher::ImportJSON(std::string filename)
{
	nlohmann::json json_chapters;

	std::ifstream of(filename);

	if (of.is_open()) {
		of >> json_chapters;
	} 
	of.close();

	auto chapters = json_chapters["chapters"];

	begin_update();
	int n = 0;
	for (json::const_iterator i = chapters.cbegin(); i != chapters.cend(); ++i, ++n)
	{ 
		auto start = (*i)["start_time"].get<std::string>();
		auto end = (*i)["end_time"].get<std::string>();

		list->AddItem(n, 0, L"");
		list->AddItem(n, 1, CString(start.c_str()));
		list->AddItem(n, 2, CString(end.c_str()));

		auto tags = (*i)["tags"];
		auto title = tags["title"].get<std::string>();
		list->AddItem(n, 3, CString(title.c_str()));

		list->SetCheckState(n, true);
	} 
	end_update(); 
	save_track_profile();
}

std::string GetClipboardText()
{
	// Try opening the clipboard
	if (!OpenClipboard(nullptr)) {
		return "";
	}

	// Get handle of clipboard object for ANSI text
	HANDLE hData = GetClipboardData(CF_TEXT);
	if (hData == nullptr)
		return "";

	// Lock the handle to get the actual text pointer
	char * pszText = static_cast<char*>(GlobalLock(hData));
	if (pszText == nullptr) {
		return "";
	}

	// Save text in a string class instance
	std::string text(pszText);

	// Release the lock
	GlobalUnlock(hData);

	// Release the clipboard
	CloseClipboard();

	return text;
}

void CPlaybackStateSwitcher::ImportFromClipboard() {
	CString HHMMSStoSeconds(CString hhmmss);
	CString mbtowc(CStringA s);

	begin_update();

	RE2::Options options(RE2::Quiet);
	RE2 re("((?:\\d+:)+\\d+)(?:[[:punct:]]|\\s)*((?:\\d+:)+\\d+)?(?:[[:punct:]]|\\s)*(.*)(?:\\n|$)", options);
	assert(re.ok()); // can check re.error() for details
	
	auto text = GetClipboardText();
	re2::StringPiece input(text);
	
	int n = 0;
	std::string from, to, title;
	while (RE2::FindAndConsume(&input, re, &from, &to, &title)) {
		list->AddItem(n, HDR_CHECK, L"");
		list->AddItem(n, HDR_FROM, HHMMSStoSeconds(mbtowc(from.c_str())));
		list->AddItem(n, HDR_TO, HHMMSStoSeconds(mbtowc(to.c_str())));
		list->AddItem(n, HDR_TITLE, mbtowc(title.c_str()));

		if (n > 0) {
			CString prevTo;
			list->GetItemText(n - 1, HDR_TO, prevTo);
			if (prevTo.IsEmpty()) {
				list->SetItemText(n - 1, HDR_TO, HHMMSStoSeconds(mbtowc(from.c_str())));
			}
		}

		++n;
	} 

	if (n > 0) {
		metadb_handle_ptr p_out;
		bool playing = (*g_playback_control)->get_now_playing(p_out);
		if (playing) {
			CString lastTo;
			list->GetItemText(n - 1, HDR_TO, lastTo);
			if (lastTo.IsEmpty()) {
				auto currentTrackLength = p_out->get_length();
				list->SetItemText(n - 1, HDR_TO, mbtowc(std::to_string(currentTrackLength).c_str()));
			}
		}
	}

	end_update();
	save_track_profile();
}

bool ends_with(std::string const &fullString, std::string const &ending) {
	if (fullString.length() >= ending.length()) {
		return (0 == fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
	}
	else {
		return false;
	}
}

void CPlaybackStateSwitcher::OnImport(UINT, int, CWindow)
{
	auto filename = QueryUserCueSheetFilename(); 

	if (ends_with(filename, ".cue")) {
		ImportCUE(filename);
	}
	else if (ends_with(filename, ".json")) {
		ImportJSON(filename);
	}
}

std::string SecondsToCueMMSSFF(std::string secs_s)
{
	double full_secs = std::stod(secs_s);
	int mins = int(full_secs) / 60;
	int seconds = int(full_secs) % 60;
	const auto frame = 1.0 / 75.0; // From CUE format standard
	double whole_sec;
	auto fractional_sec = std::modf(full_secs, &whole_sec);
	int frames = fractional_sec / frame;
	CStringA value;
	value.Format("%02d:%02d:%02d", mins, seconds, frames);
	return value.GetString();
}

CString HHMMSStoSeconds(CString hhmmss) { 
	CStringA wctomb(CString s);

	if (hhmmss.IsEmpty()) {
		return hhmmss;
	}

	int tokenPos = 0;
	std::list<std::string> tokens;
	
	while (tokenPos >= 0) {
		auto item = hhmmss.Tokenize(_T(":"), tokenPos);
		if (item.IsEmpty()) continue;
		tokens.push_back(wctomb(item).GetString());
	}

	int nToken = 0;
	double seconds = 0.0;

	for (auto item = tokens.rbegin(); item != tokens.rend(); ++item) {
		double x = std::stod(*item);
		seconds += x * std::pow(60, nToken);
		++nToken;
	}
	
	CString result;
	result.Format(L"%.3lf", seconds);
	return result;
}

void CPlaybackStateSwitcher::OnExport(UINT, int, CWindow)
{
	CString mbtowc(CStringA s);

	auto cue_sheet_filename = QueryUserCueSheetFilename(true); 
	if (cue_sheet_filename.empty()) {
		return;
	}

	auto table = (*json_intervals)[m_track_id.GetBuffer()];
	auto i = table.cbegin();
	auto end = table.cend();

	if (!table.is_array() || i == end) {
		console::complain("foo_rehearsal", "Nothing to export");
		return;
	}

	metadb_handle_ptr p_out;
	(*g_playback_control)->get_now_playing(p_out);

	std::wstringstream cue;
	pfc::string8 something;
	CString anything;

	cue << "PERFORMER";
	cue << " ";
	p_out->format_title_legacy(NULL, something, "\"%artist%\"", NULL); 
	cue << something;
	cue << std::endl;

	cue << "TITLE";
	cue << " ";
	p_out->format_title_legacy(NULL, something, "\"%title%\"", NULL); 
	cue << something;
	cue << std::endl;

	cue << "FILE";
	cue << " ";
	p_out->format_title_legacy(NULL, something, "\"%filename_ext%\"", NULL); 
	cue << something;
	cue << " ";
	cue << "WAVE";
	cue << std::endl;

	for (auto n = 1; i != end; ++i, ++n)
	{
		auto record = *i;

		anything.Format(L"  TRACK %02d AUDIO", n);
		cue << anything.GetString();
		cue << std::endl;
 
		auto title = record[HDR_TITLE].get<std::string>();
		anything.Format(L"    TITLE \"%s\"", mbtowc(title.c_str()));
		cue << anything.GetString();
		cue << std::endl;

		auto from = SecondsToCueMMSSFF(record[HDR_FROM].get<std::string>());
		anything.Format(L"    INDEX 01 %s", mbtowc(from.c_str()));
		cue << anything.GetString();
		cue << std::endl;
	}

	std::wofstream cue_file;
	cue_file.open(cue_sheet_filename.c_str(), std::ofstream::out);
	cue_file << cue.rdbuf();
}

BOOL CPlaybackStateSwitcher::OnInitDialog(CWindow, LPARAM) { 
	static static_api_ptr_t<playback_control> s_m_playback_control;
	m_playback_control = &s_m_playback_control;

	DlgResize_Init(true);
	update();	

	CString text;

	::SendMessage(GetDlgItem(IDC_SHUFFLE_SPIN_BUTTON), UDM_SETRANGE, 0, MAKELPARAM(999999, -1));

	text.Format(L"%.3lf", m_loop_from);
	SetDlgItemText(IDC_LOOP_FROM, text);

	text.Format(L"%.3lf", m_loop_to);
	SetDlgItemText(IDC_LOOP_TO, text);

	std::unique_ptr<CCheckBox> cbStopAfterFinished(new CCheckBox(GetDlgItem(IDC_STOP_AFTER_FINISHED)));
	cbStopAfterFinished->SetCheck(m_stop_after); 

	text.Format(L"%d", m_shuffle_amount);
	SetDlgItemText(IDC_SHUFFLE_AMOUNT, text);

	std::unique_ptr<CCheckBox> cbShuffleIntervals(new CCheckBox(GetDlgItem(IDC_SHUFFLE_INTERVALS)));
	cbShuffleIntervals->SetCheck(m_shuffle_fake_only_loop ? BST_INDETERMINATE : (m_shuffle));

	this->list = new CListViewCtrl(GetDlgItem(IDC_LIST));
	list->SetViewType(LVS_REPORT);
	list->SetExtendedListViewStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);

	list->AddColumn(L"†", 0);
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
	
	this->cbStopAfterFinished = std::move(cbStopAfterFinished);
	this->cbShuffleIntervals = std::move(cbShuffleIntervals);

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

void PlayCurrentListItemFromDatabase(CStringA track_id, int nItem)
{ 
	auto table = (*json_intervals)[track_id.GetBuffer()]; 
	auto table_it = table.cbegin();

	if (table_it == table.cend()) {
		kill_timer();
		return;
	}

	auto record = *(table_it + nItem);

	m_loop_from = std::stod(record[1].get<std::string>());
	m_loop_to =  std::stod(record[2].get<std::string>()); 

	if (g_timer_owner && g_timer_owner != TIMER_OWNER) {
		return;
	}

	kill_timer();
	//update_timer_range_from_window();
	(*m_playback_control)->playback_seek(m_loop_from);
	if ((*g_playback_control)->is_paused()) {
		(*g_playback_control)->pause(false);
	}
	resume_timer();
	update_other();
}

void CPlaybackStateSwitcher::PlayCurrentListItem(int nItem)
{
	if (list->GetItemCount() == 0) {
		return;
	}

	if (nItem == -1) {
		nItem = m_highlighted;
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
	update_timer_range_from_window();
	(*m_playback_control)->playback_seek(m_loop_from);
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
	m_n_items = GetIntervalListItemCount();
}

LRESULT CPlaybackStateSwitcher::OnNMDblclkList(int /*idCtrl*/, LPNMHDR pNMHDR, BOOL& /*bHandled*/)
{
	BeforePlayInterval();
	PlayCurrentListItem();
	return 0;
}

LRESULT CPlaybackStateSwitcher::OnListItemChanged(int /*idCtrl*/, LPNMHDR pNMHDR, BOOL& /*bHandled*/)
{
	if (m_is_data_updating) {
		return 0;
	}

	LPNMLISTVIEW lv = (LPNMLISTVIEW) pNMHDR;

	if (lv->iItem < 0) {
		return 0;
	}

	//if ( ((lv->uChanged & LVIF_STATE) == 0) ||
	//	((lv->uNewState & LVIS_SELECTED) == 0) )
	//{
	//	return 0;
	//}
 
	//console::complain("here!", std::to_string(lv->iSubItem).c_str());

	switch (lv->uNewState & LVIS_STATEIMAGEMASK)
	{
	case INDEXTOSTATEIMAGEMASK(BST_CHECKED + 1): // new state: checked
		//console::complain("\n Item %d has been checked", std::to_string(lv->iItem).c_str());
		save_track_profile();
		break;
	case INDEXTOSTATEIMAGEMASK(BST_UNCHECKED + 1): // new state: unchecked
		//console::complain("\n Item %d has been unchecked", std::to_string(lv->iItem).c_str());
		save_track_profile();
		break;
	}

	return 0;
}

LRESULT CPlaybackStateSwitcher::OnListKeyDown(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
{
	NMLVKEYDOWN* kd = (NMLVKEYDOWN*)pnmh;
	bool controlKey = GetAsyncKeyState(VK_CONTROL) & 0x8000;

	switch (kd->wVKey) {

	case VK_DELETE:
		//if (list->GetSelectedCount() == 1) {
		//	int n = list->GetSelectedIndex();
		//	list->DeleteItem(n);
		//}
		//else {
		//}
		LVITEM item;
		for (int i = 0, end = list->GetItemCount(); i < end; ) {
			if (list->GetItemState(i, LVIS_SELECTED) == LVIS_SELECTED) {
				list->DeleteItem(i);
				--end;
			}
			else {
				++i;
			}
		}
		//while (list->GetSelectedItem(&item)) {
		//	list->DeleteItem(item.iItem);
		//}
		save_track_profile();
		break;

	case 'a':
	case 'A': {
		if (controlKey) {
			list->SetRedraw(false);
			for (int i = 0; i < GetIntervalListItemCount(); ++i) {
				list->SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
			}
			list->SetRedraw(true);
		}

		break;
	}

	case 'v':
	case 'V': {
		ImportFromClipboard();
		break;
	}

	case VK_RETURN:
		BeforePlayInterval();
		PlayCurrentListItem();
		break;
	}

	return 0;
}

CString mbtowc(CStringA s) {
	wchar_t buf[MAX_PATH];
	MultiByteToWideChar(CP_UTF8, 0, s.GetBuffer(), -1, buf, 500);
	return CString(buf);
}

CStringA wctomb(CString s) {
	char buf[MAX_PATH];
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
		console::complain("foo_rehearsal", (std::string() + __FUNCTION__ + " interval not found").c_str());
		return;
	}

	auto table = (*json_intervals)[track_id.GetBuffer()];

	begin_update();

	int nRow = 0;
	for (auto i = table.cbegin(); i != table.cend(); ++i, ++nRow) {
		auto record = *i;

		list->AddItem(nRow, 0, L"");
		bool checked = bool(record[0]);
		list->SetCheckState(nRow, checked);

		for (size_t col = 1; col < size_t(HDR_COUNT); ++col) {			
			std::string text_a = record[col];
			CString text = mbtowc(text_a.c_str());
			list->AddItem(nRow, col, text);
		}
	}

	end_update();
}

CStringA CPlaybackStateSwitcher::GetCurrentTrackId()
{ 
	if (!(*g_playback_control)->is_playing()) {
		return "";
	}

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

	return track_id;
}

void CPlaybackStateSwitcher::save_track_profile()
{
	CStringA track_id = GetCurrentTrackId();

	if (list->GetItemCount() == 0) {
		json_intervals->erase(track_id.GetBuffer());
		return;
	}

	auto table = json::array();
	
	int count = list->GetItemCount();
	for (int i = 0; i < count; ++i) {
		auto record = json::array();
		CString text;

		bool checked = list->GetCheckState(i);
		record.push_back(checked);

		for (int col = 1; col < (int) HDR_COUNT; ++col) {
			list->GetItemText(i, col, text);
			auto text_mb = wctomb(text);
			record.push_back(text_mb.GetBuffer());
		}
		table.push_back(record);
	}

	(*json_intervals)[track_id.GetBuffer()] = table;
	m_n_items = count;
}

void kill_timer() {
	if (g_timer_owner && g_timer_owner != TIMER_OWNER) {
		return;
	}

	::KillTimer(NULL, g_timer);
	g_timer = 0;
	g_timer_owner = 0;

	if (current_dialog) {
		current_dialog->update_status();
	}
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

	if (current_dialog) {
		current_dialog->update_status();
	}
}

void begin_update() {
	m_is_data_updating = true;
	if (current_dialog) {
		current_dialog->list->SetRedraw(false);
	}
}

void end_update() {
	m_is_data_updating = false;
	if (current_dialog) {
		current_dialog->list->SetRedraw(true);
		current_dialog->list->UpdateWindow();
	}
}

