#include "stdafx.h"
#include "resource.h"

CStringA wctomb(CString s);
CString mbtowc(CStringA s);
int json_intervals_get_count();

// Sample preferences interface: two meaningless configuration settings accessible through a preferences page and one accessible through advanced preferences.


// These GUIDs identify the variables within our component's configuration file.
// {2FA804BD-2B6F-465E-A28F-9EB732FAE180}
static const GUID guid_cfg_bogoSetting1 = { 0x2fa804bd, 0x2b6f, 0x465e,{ 0xa2, 0x8f, 0x9e, 0xb7, 0x32, 0xfa, 0xe1, 0x80 } };

// {FFD6F0D7-9D7F-4A86-B027-9C04C88D6015}
//static const GUID guid_cfg_bogoSetting2 = { 0xffd6f0d7, 0x9d7f, 0x4a86,{ 0xb0, 0x27, 0x9c, 0x4, 0xc8, 0x8d, 0x60, 0x15 } };



// This GUID identifies our Advanced Preferences branch (replace with your own when reusing code).
// {DD1E4EE4-47BC-4753-964F-35555B1AC676}
//static const GUID guid_advconfig_branch = { 0xdd1e4ee4, 0x47bc, 0x4753,{ 0x96, 0x4f, 0x35, 0x55, 0x5b, 0x1a, 0xc6, 0x76 } };


// This GUID identifies our Advanced Preferences setting (replace with your own when reusing code)
// as well as this setting's storage within our component's configuration file.
// {DA134D8E-3C2D-4349-9F68-D5E12A1720A2}
//static const GUID guid_cfg_bogoSetting3 = { 0xda134d8e, 0x3c2d, 0x4349,{ 0x9f, 0x68, 0xd5, 0xe1, 0x2a, 0x17, 0x20, 0xa2 } };


auto const default_cfg_bogoSetting1 = "%USERPROFILE%\\foo_rehearsal.json";
enum {
	default_cfg_bogoSetting2 = 666,
	default_cfg_bogoSetting3 = 42,
};

cfg_string cfg_bogoSetting1(guid_cfg_bogoSetting1, default_cfg_bogoSetting1);
//static cfg_uint cfg_bogoSetting2(guid_cfg_bogoSetting2, default_cfg_bogoSetting2);

//static advconfig_branch_factory g_advconfigBranch("Rehearsal", guid_advconfig_branch, advconfig_branch::guid_branch_tools, 0);
//static advconfig_integer_factory cfg_bogoSetting3("Bogo setting 3", guid_cfg_bogoSetting3, guid_advconfig_branch, 0, default_cfg_bogoSetting3, 0 /*minimum value*/, 9999 /*maximum value*/);

class CMyPreferences : public CDialogImpl<CMyPreferences>, public preferences_page_instance {
public:
	//Constructor - invoked by preferences_page_impl helpers - don't do Create() in here, preferences_page_impl does this for us
	CMyPreferences(preferences_page_callback::ptr callback) : m_callback(callback) {}

	//Note that we don't bother doing anything regarding destruction of our class.
	//The host ensures that our dialog is destroyed first, then the last reference to our preferences_page_instance object is released, causing our object to be deleted.


	//dialog resource ID
	enum { IDD = IDD_MYPREFERENCES };
	// preferences_page_instance methods (not all of them - get_wnd() is supplied by preferences_page_impl helpers)
	t_uint32 get_state();
	void apply();
	void reset();

	//WTL message map
	BEGIN_MSG_MAP(CMyPreferences)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDC_BOGO1, EN_CHANGE, OnEditChange)
		COMMAND_HANDLER_EX(IDC_BOGO2, EN_CHANGE, OnEditChange)
		COMMAND_HANDLER(IDC_BROWSE, BN_CLICKED, OnBnClickedBrowse)
	END_MSG_MAP()
private:
	BOOL OnInitDialog(CWindow, LPARAM);
	void OnEditChange(UINT, int, CWindow);
	bool HasChanged();
	void OnChanged();

	const preferences_page_callback::ptr m_callback;
public:
	LRESULT OnBnClickedBrowse(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
};

BOOL CMyPreferences::OnInitDialog(CWindow, LPARAM) {
	CStringA text(cfg_bogoSetting1);
	SetDlgItemText(IDC_BOGO1, mbtowc(text));

	CString count_s;
	count_s.Format(L"%d", json_intervals_get_count());
	SetDlgItemText(IDC_TRACKS, count_s);

//	SetDlgItemInt(IDC_BOGO2, cfg_bogoSetting2, FALSE);

	return FALSE;
}

void CMyPreferences::OnEditChange(UINT, int, CWindow) {
	// not much to do here
	OnChanged();
}

t_uint32 CMyPreferences::get_state() {
	t_uint32 state = preferences_state::resettable;
	if (HasChanged()) state |= preferences_state::changed;
	return state;
}

void CMyPreferences::reset() {
	CStringA text(default_cfg_bogoSetting1);
	SetDlgItemText(IDC_BOGO1, CString(text));

	SetDlgItemInt(IDC_BOGO2, default_cfg_bogoSetting2, FALSE);
	OnChanged();
}

void CMyPreferences::apply() {
	CString text;
	GetDlgItemText(IDC_BOGO1, text);
	CStringA text_a = wctomb(text);
	cfg_bogoSetting1.set_string(text_a.GetBuffer(), text_a.GetLength());

//	cfg_bogoSetting2 = GetDlgItemInt(IDC_BOGO2, NULL, FALSE);

	OnChanged(); //our dialog content has not changed but the flags have - our currently shown values now match the settings so the apply button can be disabled
}

bool CMyPreferences::HasChanged() {
	CString text;
	GetDlgItemText(IDC_BOGO1, text);
	CStringA text_a = wctomb(text);

	//returns whether our dialog content is different from the current configuration (whether the apply button should be enabled or not)
	return text_a != cfg_bogoSetting1;
//		|| GetDlgItemInt(IDC_BOGO2, NULL, FALSE) != cfg_bogoSetting2;
}

void CMyPreferences::OnChanged() {
	//tell the host that our state has changed to enable/disable the apply button appropriately.
	m_callback->on_state_changed();
}

class preferences_page_myimpl : public preferences_page_impl<CMyPreferences> {
	// preferences_page_impl<> helper deals with instantiation of our dialog; inherits from preferences_page_v3.
public:
	const char * get_name() { return "Rehearsal"; }
	GUID get_guid() {
		// This is our GUID. Replace with your own when reusing the code.
		// {FA269CB1-09DB-48CC-A876-3624443B9023}
		static const GUID guid = { 0xfa269cb1, 0x9db, 0x48cc,{ 0xa8, 0x76, 0x36, 0x24, 0x44, 0x3b, 0x90, 0x23 } };
		return guid;
	}
	GUID get_parent_guid() { return guid_tools; }
};

static preferences_page_factory_t<preferences_page_myimpl> g_preferences_page_myimpl_factory;




LRESULT CMyPreferences::OnBnClickedBrowse(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	TCHAR filename[MAX_PATH];

	CStringA text(cfg_bogoSetting1);
	CString path = mbtowc(text);
	TCHAR path_w[500];
	ExpandEnvironmentStrings(path, path_w, 500);
	StrCpyW(filename, path_w);
	PathRemoveFileSpec(path_w);
	TCHAR title[500];
	GetFileTitle(filename, title, 500);
	
	OPENFILENAME ofn;
//	ZeroMemory(&filename, sizeof(filename));
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;  // If you have a window to center over, put its HANDLE here
	ofn.lpstrFilter = L"JSON Files\0*.json\0\0";
	ofn.lpstrFile = title;
	ofn.lpstrInitialDir = path_w;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = L"Select database filename";
	ofn.Flags = OFN_DONTADDTORECENT | OFN_OVERWRITEPROMPT;

	if (GetSaveFileName(&ofn))
	{
		CString name(title);
		if (name.Right(5) != L".json") {
			name += L".json";
		}
		SetDlgItemText(IDC_BOGO1, name);
	}
	
	return 0;
}
