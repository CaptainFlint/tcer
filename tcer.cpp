// tcer.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "tcer.h"
#include "WindowFinder.h"
#include <commctrl.h>

// Path to system directory
WCHAR DllPath[MAX_PATH];
size_t DllPathLen;

// Strips the Full View data: size, date, time, attributes
void strip_file_data(WCHAR* elem)
{
	const WCHAR bad_chars[] = L"<>:|*\"";
	size_t pos = wcscspn(elem, bad_chars);
	// Full path, colon at the second position => searching for the next bad character
	if (pos == 1)
		pos = 2 + wcscspn(elem + 2, bad_chars);
	// Not found
	if (elem[pos] == L'\0')
		return;
	// Custom columns (separated from file name by space and '>')
	if (elem[pos] == L'>')
	{
		if (pos > 1)
			elem[pos - 1] = L'\0';
		return;
	}
	// Full view mode
	size_t len = wcslen(elem);
	// Try to determine GetTextMode scheme
	size_t delim_pos = wcscspn(elem, L"\t\x0d");
	if (elem[delim_pos] == L'\0')
	{
		// No special delimiters found => probably GetTextMode=0: columns are space-delimited (name size date time attributes)
		for (int i = 0; i < 4; ++i)
		{
			len = wcsrchr_pos(elem, len - 1, L' ');
			if (len == 0)
				return;
		}
		if ((elem[len] < L'0') || (elem[len] > L'9'))
		{
			// The Size value does not start with digit => it contains unit name delimited by space from the value,
			// and only the unit was stripped, so now strip the value
			len = wcsrchr_pos(elem, len - 1, L' ');
			if (len == 0)
				return;
		}
	}
	else
	{
		if (elem[delim_pos] == L'\t')
		{
			// Check if this is GetTextMode=4 or 5
			size_t delim_pos2 = wcscspn(elem, L"\x0d");
			if (elem[delim_pos2] == L'\x0d')
			{
				// It is (Name:\t{name}<CR>Size:\t{size}<CR>Date:\t{date+time}<CR>Attrs:\t{attributes}, or same with <CR><LF>).
				// Move the file name (substring between the first \t and \r chars) to the beginning of the text.
				memcpy_s(elem, len + 1, elem + delim_pos + 1, (delim_pos2 - delim_pos) * sizeof(WCHAR));
				len = delim_pos2 - delim_pos;
			}
			else
				// No, it's GetTextMode=1: columns are tab-delimited (name<TAB>size<TAB>date+time<TAB>attributes) => just cut by the first tab
				len = delim_pos + 1;
		}
		else if (elem[delim_pos] == L'\x0d')
		{
			// GetTextMode=2: columns are \r-delimited (name<CR>size<CR>date+time<CR>attributes)
			// GetTextMode=3: columns are \r\n-delimited (name<CR><LF>size<CR><LF>date time<CR><LF>attributes)
			// In both cases \r is the the first non-filename character
			len = delim_pos + 1;
		}
	}
	elem[len - 1] = L'\0';
}


typedef void (WINAPI *tGetNativeSystemInfo)(LPSYSTEM_INFO lpSystemInfo);
typedef BOOL (WINAPI *tIsWow64Process)(HANDLE hProcess, PBOOL Wow64Process);

enum BITNESS
{
	PROCESS_X32 = 0,
	PROCESS_X64 = 1
};

// Checks whether process is 32- or 64-bit
BITNESS GetProcessBitness(DWORD pid)
{
	BITNESS res = PROCESS_X32;

	// Protection from loading DLL from current path
	wcscpylen_s(DllPath + DllPathLen, MAX_PATH - DllPathLen, L"kernel32.dll");
	HMODULE kernel32 = LoadLibrary(DllPath);

	if (kernel32 == NULL)
		return res;
	tGetNativeSystemInfo fGetNativeSystemInfo = (tGetNativeSystemInfo)GetProcAddress(kernel32, "GetNativeSystemInfo");
	tIsWow64Process fIsWow64Process = (tIsWow64Process)GetProcAddress(kernel32, "IsWow64Process");
	if ((fGetNativeSystemInfo != NULL) && (fIsWow64Process != NULL))
	{
		SYSTEM_INFO si;
		fGetNativeSystemInfo(&si);
		if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
		{
			HANDLE pHandle = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
			if (pHandle != NULL)
			{
				BOOL is64;
				// IsWow64Process returns FALSE for 64-bit process on 64-bit system
				if (fIsWow64Process(pHandle, &is64) && !is64)
					res = PROCESS_X64;
				CloseHandle(pHandle);
			}
		}
	}
	FreeLibrary(kernel32);
	return res;
}


HANDLE ProcessHeap;

#ifdef _DEBUG

int WINAPI wWinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPWSTR    lpCmdLine,
	int       nCmdShow
)
{
	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(nCmdShow);

#else

int wWinMainCRTStartup()
{
	LPWSTR lpCmdLine;

#endif

	InitCommonControls();

	/*
		If this code is used somewhere else, keep in mind that when an error occurs,
		resources are not freed (program terminates, so all resources are freed
		by the system anyway).
	*/

	ProcessHeap = GetProcessHeap();
	const WCHAR* const MsgBoxTitle = L"TC Edit Redirector";

	const size_t BUF_SZ = 1024;
	const size_t CMDLINE_BUF_SZ = 32 * 1024;
	WCHAR* msg_buf = new WCHAR[BUF_SZ];
	if (msg_buf == NULL)
	{
		MessageBox(NULL, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}

	DllPathLen = GetSystemDirectory(DllPath, MAX_PATH);
	if (DllPathLen == 0)
	{
		swprintf_s(msg_buf, BUF_SZ, L"Failed to get system directory (", GetLastError(), L")");
		MessageBox(NULL, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}
	else if (DllPathLen > MAX_PATH - 13)
	{
		MessageBox(NULL, L"Too long path to system directory!", MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}
	DllPath[DllPathLen++] = L'\\';

	HWND tc_main_wnd = NULL;

	ArrayHWND* tc_panels;
	size_t i;

	//////////////////////////////////////////////////////////////////////////
	// Get the list of selected items from TC file panel                    //
	//////////////////////////////////////////////////////////////////////////

	/*
		This part is the most performance-critical, because while this is working,
		the user may change the cursor position, selection or even open another path.
		So the very first thing we do is getting all the information we need from
		TC window and its child windows/panels.

		Algorithm:
		1. Find window of the TC instance which initiated this TCER process:
		  a) get parent PID;
		  b) find TTOTAL_CMD window that belongs to the PID.
		2. Find the active file panel handle:
		  a) find two TMyListBox'es (file panels);
		  b) get the focused child window of the TC's GUI thread;
		  c) compare to TMyListBox'es found.
		3. Get current path from TC command line (needed for archives and FS plugins):
		  a) find all TMyPanel's that are direct children of the main window;
		  b) search for the one that has child TMyPanel but doesn't have TMyTabControl and THeaderClick
			 (for situations when tabs are on and off, respectively);
		  c) the remaining control is Command Line, get the path from its child TMyPanel.
		4. Get active panel title:
		  TMyListBox and TPathPanel are independent, so we cannot get it from parent-child relationship
		  a) find all TPathPanel's that are children of the main window;
		  b) get the coordinates of the active panel;
		  c) check if left edges of TPathPanel are equal:
		    d1) yes: vertical layout; get the one which is above the listbox;
			d2) no: horizontal layout; get the one whose left edge is closer to the listbox'es left edge.
		5. Fetch the list of items to edit:
		  a) get the list of selected items;
		  b) if the list is empty, get the focused item.
	*/

	//////////////////////////////////////////////////////////////////////////
	// 1. Find window of the TC instance which initiated this TCER process.

	// Obtain address of NtQueryInformationProcess needed for getting PPID
	// Protection from loading DLL from current path
	wcscpylen_s(DllPath + DllPathLen, MAX_PATH - DllPathLen, L"ntdll.dll");
	HMODULE ntdll = LoadLibrary(DllPath);
	if (ntdll == NULL)
	{
		swprintf_s(msg_buf, BUF_SZ, L"Failed to load ntdll.dll (", GetLastError(), L")");
		MessageBox(NULL, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}
	tNtQueryInformationProcess fNtQueryInformationProcess = (tNtQueryInformationProcess)GetProcAddress(ntdll, "NtQueryInformationProcess");
	if (fNtQueryInformationProcess == NULL)
	{
		swprintf_s(msg_buf, BUF_SZ, L"Failed to find NtQueryInformationProcess (", GetLastError(), L")");
		MessageBox(NULL, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}

	// Get PID of the parent process (TC)
	PROCESS_BASIC_INFORMATION proc_info;
	ULONG ret_len;
	NTSTATUS query_res = fNtQueryInformationProcess(GetCurrentProcess(), ProcessBasicInformation, &proc_info, sizeof(proc_info), &ret_len);
	if (!NT_SUCCESS(query_res))
	{
		swprintf_s_hex(msg_buf, BUF_SZ, L"NtQueryInformationProcess failed (0x", query_res, L")");
		MessageBox(NULL, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}
	FreeLibrary(ntdll);

	BITNESS BitnessTC = GetProcessBitness((DWORD)proc_info.InheritedFromUniqueProcessId);

	// Window class names for 32- and 64-bit TC controls
	const WCHAR* ListBoxClass[2] = { L"TMyListBox",  L"LCLListBox"  };
	const WCHAR* PanelClass[2]   = { L"TMyPanel",    L"Window"      };
	const WCHAR* CmdLineClass[2] = { L"TMyComboBox", L"LCLComboBox" };

	//////////////////////////////////////////////////////////////////////////
	// 2. Find the active file panel handle.

#ifdef _DEBUG
	// DBG: When debugging, the debugger is parent; skip the PPID checks
	proc_info.InheritedFromUniqueProcessId = 0;
#endif

	// First, check if we are started from the Find Files dialog
	bool parent_find_files = false;
	tc_main_wnd = WindowFinder::FindWnd(NULL, false, L"TFindFile", proc_info.InheritedFromUniqueProcessId);
	if (tc_main_wnd != NULL)
	{
		// Yes - remember this important fact
		parent_find_files = true;
	}
	else
	{
		// No - find the main window of the TC instance
		tc_main_wnd = WindowFinder::FindWnd(NULL, false, L"TTOTAL_CMD", proc_info.InheritedFromUniqueProcessId);
		if (tc_main_wnd == NULL)
		{
			MessageBox(NULL, L"Could not find parent TC window!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
	}

	// Various variables calculated when TCER is started from the main TC window:
	// Current path from TC command line
	WCHAR* tc_curpath_cmdline = NULL;
	size_t tc_curpath_cmdline_len = 0;
	// List of selected items in the TC file panel
	ArrayPtr<WCHAR>* sel_items_txt = NULL;
	size_t sel_items_num = 0;
	// Focused item in the TC file panel
	WCHAR* focus_item_txt = NULL;
	// Item to be used for selecting the appropriate editor
	size_t active_item = 0;

	// Work with TC file panel - only if started from the main TC window
	if (!parent_find_files)
	{
		// Find TC file panels
		tc_panels = WindowFinder::FindWnds(tc_main_wnd, true, ListBoxClass[BitnessTC], 0);
		if (tc_panels->GetLength() == 0)
		{
			MessageBox(tc_main_wnd, L"Could not find panels in the TC window!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}

		// Determine the focused panel
		HWND tc_panel = NULL;
		GUITHREADINFO gti;
		gti.cbSize = sizeof(gti);
		SetForegroundWindow(tc_main_wnd);
		DWORD tid = GetWindowThreadProcessId(tc_main_wnd, NULL);
#ifdef _DEBUG
		// DBG: Wait, so that application windows had time to switch and set focus
		Sleep(500);
#endif
		if (!GetGUIThreadInfo(tid, &gti))
		{
			swprintf_s(msg_buf, BUF_SZ, L"Failed to get TC GUI thread information (", GetLastError(), L")");
			MessageBox(tc_main_wnd, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		if (gti.hwndFocus == NULL)
		{
			MessageBox(tc_main_wnd, L"Failed to determine active panel!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		for (i = 0; i < tc_panels->GetLength(); ++i)
		{
			if ((*tc_panels)[i] == gti.hwndFocus)
			{
				tc_panel = gti.hwndFocus;
				break;
			}
		}
		delete tc_panels;
		if (tc_panel == NULL)
		{
			MessageBox(tc_main_wnd, L"No TC panel is focused!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		if (GetWindowTextLength(tc_panel) != 0)
		{
			MessageBox(tc_main_wnd, L"No TC file panel is focused!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}

		//////////////////////////////////////////////////////////////////////////
		// 3. Get current path from TC command line.

		tc_curpath_cmdline = new WCHAR[BUF_SZ];
		if (tc_curpath_cmdline == NULL)
		{
			MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		tc_curpath_cmdline[0] = L'\0';
		tc_curpath_cmdline_len = 0;
		tc_panels = WindowFinder::FindWnds(tc_main_wnd, true, PanelClass[BitnessTC], 0);
		if (tc_panels->GetLength() == 0)
		{
			MessageBox(tc_main_wnd, L"Failed to find TC command line!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		for (i = 0; i < tc_panels->GetLength(); ++i)
		{
			if (WindowFinder::FindWnd((*tc_panels)[i], true, CmdLineClass[BitnessTC], 0) == NULL)
				continue;

			HWND tc_cmdline = WindowFinder::FindWnd((*tc_panels)[i], true, PanelClass[BitnessTC], 0);
			if (tc_cmdline == NULL)
				continue;

			tc_curpath_cmdline_len = GetWindowText(tc_cmdline, tc_curpath_cmdline, BUF_SZ);
			if (tc_curpath_cmdline_len + 1 >= BUF_SZ)
			{
				swprintf_s(msg_buf, BUF_SZ, L"Too long path (", tc_curpath_cmdline_len, L" characters)!");
				MessageBox(tc_main_wnd, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
				ExitProcess(1);
			}
			if (tc_curpath_cmdline_len > 0)
			{
				if ((tc_curpath_cmdline_len >= 2) && (tc_curpath_cmdline[tc_curpath_cmdline_len - 2] == L'\\'))
				{
					// The path ends with backslash (probably disk root) => just remove the angle bracket
					tc_curpath_cmdline[--tc_curpath_cmdline_len] = L'\0';
				}
				else
				{
					// Path without trailing backslash => replace the angle bracket with backslash
					tc_curpath_cmdline[tc_curpath_cmdline_len - 1] = L'\\';
				}
			}
			break;
		}
		delete tc_panels;

		//////////////////////////////////////////////////////////////////////////
		// 4. Get active panel title.

		/* Code reserved for future use!

		// Find the two TPathPanel's that are children of the main window
		tc_panels = WindowFinder::FindWnds(tc_main_wnd, false, L"TPathPanel", 0);
		if (tc_panels->GetLength() != 2)
		{
			swprintf_s(msg_buf, BUF_SZ, L"Invalid number of path bars (", tc_panels->GetLength(), L"), should be 2!");
			MessageBox(tc_main_wnd, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}

		// Get the coordinates of the active panel and path panels
		RECT tc_panel_rect, path_panel1_rect, path_panel2_rect;
		if (GetWindowRect(tc_panel, &tc_panel_rect) == 0)
		{
			swprintf_s(msg_buf, BUF_SZ, L"Failed to obtain the panel placement (", GetLastError(), L")");
			MessageBox(tc_main_wnd, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		if ((GetWindowRect((*tc_panels)[0], &path_panel1_rect) == 0)
			||
			(GetWindowRect((*tc_panels)[1], &path_panel2_rect) == 0))
		{
			swprintf_s(msg_buf, BUF_SZ, L"Failed to obtain the path panel placement (", GetLastError(), L")");
			MessageBox(tc_main_wnd, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}

		// Find the active path panel
		HWND tc_path_panel;
		if (path_panel1_rect.left == path_panel2_rect.left)
		{
			// Vertical panels placement
			if (abs(tc_panel_rect.top - path_panel1_rect.top) < abs(tc_panel_rect.top - path_panel2_rect.top))
				tc_path_panel = (*tc_panels)[0];
			else
				tc_path_panel = (*tc_panels)[1];
		}
		else
		{
			// Horizontal panels placement
			if (abs(tc_panel_rect.left - path_panel1_rect.left) < abs(tc_panel_rect.left - path_panel2_rect.left))
				tc_path_panel = (*tc_panels)[0];
			else
				tc_path_panel = (*tc_panels)[1];
		}
		delete tc_panels;

		WCHAR* tc_curpath_panel = new WCHAR[BUF_SZ];
		if (tc_curpath_panel == NULL)
		{
			MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		size_t tc_curpath_panel_len = GetWindowText(tc_path_panel, tc_curpath_panel, BUF_SZ);
		if (tc_curpath_panel_len + 1 >= BUF_SZ)
		{
			swprintf_s(msg_buf, BUF_SZ, L"Too long path (", tc_curpath_panel_len, L" characters)!");
			MessageBox(tc_main_wnd, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		delete[] tc_curpath_panel;
		*/

		//////////////////////////////////////////////////////////////////////////
		// 5. Fetch the list of items to edit.

		// Active item: the one to be used for choosing the editor.
		// If focused item is selected it will be used, the first file otherwise.
		active_item = 0;

		// Get the focused item
		UINT focus_item_idx = (UINT)SendMessage(tc_panel, LB_GETCARETINDEX, 0, 0);

		// Get the number of selected elements and their indices
		sel_items_num = SendMessage(tc_panel, LB_GETSELCOUNT, 0, 0);
		UINT* sel_items_idx = NULL;
		if (sel_items_num > 0)
		{
			sel_items_idx = new UINT[sel_items_num];
			if (sel_items_idx == NULL)
			{
				MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
				ExitProcess(1);
			}
			size_t sel_items_num2 = SendMessage(tc_panel, LB_GETSELITEMS, sel_items_num, (LPARAM)sel_items_idx);
			if (sel_items_num2 < sel_items_num)
				sel_items_num = sel_items_num2;
		}

		// Get the elements themselves
		size_t invalid_paths = 0;

		size_t elem_len = SendMessage(tc_panel, LB_GETTEXTLEN, focus_item_idx, NULL);
		if ((elem_len != LB_ERR) && (elem_len > 0) && (elem_len + 1 < BUF_SZ))
		{
			focus_item_txt = new WCHAR[elem_len + 1];
			if (focus_item_txt == NULL)
			{
				MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
				ExitProcess(1);
			}
			if (SendMessage(tc_panel, LB_GETTEXT, focus_item_idx, (LPARAM)focus_item_txt) == 0)
			{
				delete[] focus_item_txt;
				focus_item_txt = NULL;
			}
		}

		sel_items_txt = new ArrayPtr<WCHAR>(sel_items_num);
		if (sel_items_txt == NULL)
		{
			MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		for (i = 0; i < sel_items_num; ++i)
		{
			elem_len = SendMessage(tc_panel, LB_GETTEXTLEN, sel_items_idx[i], NULL);
			if ((elem_len == LB_ERR) || (elem_len + 1 >= BUF_SZ))
			{
				++invalid_paths;
				continue;
			}
			WCHAR* tmp = new WCHAR[elem_len + 1];
			if (tmp == NULL)
			{
				MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
				ExitProcess(1);
			}
			if (SendMessage(tc_panel, LB_GETTEXT, sel_items_idx[i], (LPARAM)tmp) == 0)
				delete[] tmp;
			else
			{
				sel_items_txt->Append(tmp);
				if (focus_item_idx == sel_items_idx[i])
					active_item = sel_items_txt->GetLength() - 1;   // Remember the active item index
			}
		}
		if (sel_items_idx != NULL)
			delete[] sel_items_idx;
	}


	//////////////////////////////////////////////////////////////////////////
	// Find TCER configuration file                                         //
	//////////////////////////////////////////////////////////////////////////

	// Search for tcer.ini file: first own directory, then wincmd.ini directory
	WCHAR* ini_path = new WCHAR[BUF_SZ];
	WCHAR* ini_name = new WCHAR[BUF_SZ];
	if ((ini_path == NULL) || (ini_name == NULL))
	{
		MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}
	if (GetModuleFileName(NULL, ini_path, BUF_SZ) == 0)
	{
		swprintf_s(msg_buf, BUF_SZ, L"Failed to find myself (", GetLastError(), L")");
		MessageBox(tc_main_wnd, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}
	size_t path_len = wcslen(ini_path);

	size_t idx_slash, idx_dot;
	idx_slash = wcsrchr_pos(ini_path, path_len, L'\\');
	idx_dot = wcsrchr_pos(ini_path, path_len, L'.');
	size_t ini_name_len = path_len - idx_slash;
	wcscpylen_s(ini_name, BUF_SZ, ini_path + idx_slash);
	if ((idx_dot == 0) || ((idx_slash != 0) && (idx_dot < idx_slash)))
	{
		// Extension not found, append '.ini'
		if (ini_name_len + 5 > BUF_SZ)
		{
			MessageBox(tc_main_wnd, L"Too long INI file name!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		wcscpylen_s(ini_name + ini_name_len, BUF_SZ - ini_name_len, L".ini");
	}
	else
	{
		// Extension found, replace with 'ini'
		idx_dot -= idx_slash;
		if (idx_dot + 4 > BUF_SZ)
		{
			MessageBox(tc_main_wnd, L"Too long INI file name!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		wcscpylen_s(ini_name + idx_dot, BUF_SZ - idx_dot, L"ini");
		ini_name_len = idx_dot + 3;
	}
	if (idx_slash + ini_name_len + 1 > BUF_SZ)
	{
		MessageBox(tc_main_wnd, L"Too long path to INI!", MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}
	wcscpylen_s(ini_path + idx_slash, BUF_SZ - idx_slash, ini_name);

	// INI file path constructed, check the file existence
	if (GetFileAttributes(ini_path) == INVALID_FILE_ATTRIBUTES)
	{
		// File does not exist, search wincmd.ini directory
		path_len = GetEnvironmentVariable(L"COMMANDER_INI", ini_path, BUF_SZ);
		if (path_len == 0)
		{
			MessageBox(tc_main_wnd, L"COMMANDER_INI variable undefined!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		idx_slash = wcsrchr_pos(ini_path, path_len, L'\\');
		if (idx_slash == 0)
		{
			// File name only, replace it with INI file name
			wcscpylen_s(ini_path, BUF_SZ, ini_name);
		}
		else
		{
			// Keep path, replace file name
			if (idx_slash + ini_name_len + 1 > BUF_SZ)
			{
				MessageBox(tc_main_wnd, L"Too long path to myself!", MsgBoxTitle, MB_ICONERROR | MB_OK);
				ExitProcess(1);
			}
			wcscpylen_s(ini_path + idx_slash, BUF_SZ - idx_slash, ini_name);
		}
		if (GetFileAttributes(ini_path) == INVALID_FILE_ATTRIBUTES)
		{
			MessageBox(tc_main_wnd, L"Configuration file not found!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
	}
	delete[] ini_name;


	//////////////////////////////////////////////////////////////////////////
	// Read general TCER configuration                                      //
	//////////////////////////////////////////////////////////////////////////

	size_t MaxItems = GetPrivateProfileInt(L"Configuration", L"MaxItems", 256, ini_path);
	size_t MaxShiftF4MSeconds = GetPrivateProfileInt(L"Configuration", L"MaxShiftF4MSeconds", 2000, ini_path);
	bool ClearSelection = (GetPrivateProfileInt(L"Configuration", L"ClearSelection", 1, ini_path) != 0);


	//////////////////////////////////////////////////////////////////////////
	// Translate list of items into list of paths                           //
	//////////////////////////////////////////////////////////////////////////

	// TODO: [1:HIGH] Support opening files directly from %TEMP%\_tc\

	/*
		Current implementation:
		1) Started from Find Files -> use lpCmdLine.
		2) lpCmdLine =~ %TEMP%\_tc(_|)\.*
			Archive/FS-plugin/FTP/etc. -> open lpCmdLine, wait till editor close.
		3) tc_curpath_cmdline =~ \\\.*
			TempPanel FS-plugin -> open lpCmdLine, do not wait.
		4) lpCmdLine was just created
			Shift+F4 -> open lpCmdLine, do not wait.
		5) Otherwise
			Combine tc_curpath_cmdline with selected elements.
		6) If no elements selected, open the focused file.
		7) If it fails, open the file supplied via command line.
	*/

#ifndef _DEBUG
	// CRT code did not run to prepare command line arguments, so do it now
	lpCmdLine = GetCommandLine();

	// Remove the TCER executable from comand line
	bool InsideQuotes = false;
	while ((*lpCmdLine > L' ') || (*lpCmdLine && InsideQuotes))
	{
		// Flip the InsideQuotes if current character is a double quote
		if (*lpCmdLine == L'"')
			InsideQuotes = !InsideQuotes;
		++lpCmdLine;
	}

	// Skip past any white space preceeding the second token.
	while (*lpCmdLine && (*lpCmdLine <= L' '))
		++lpCmdLine;
#endif

	bool WaitForTerminate = false;
	ArrayPtr<WCHAR>* edit_paths = new ArrayPtr<WCHAR>;
	if (edit_paths == NULL)
	{
		MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}

	size_t offset;
	size_t len;

	// Remove quotes from lpCmdLine (if present) and copy it into non-const buffer
	WCHAR* input_file = new WCHAR[BUF_SZ];
	if (input_file == NULL)
	{
		MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}
	input_file[0] = L'\0';
	if ((lpCmdLine != NULL) && (lpCmdLine[0] != L'\0'))
	{
		offset = ((lpCmdLine[0] == L'"') ? 1 : 0);
		len = wcscpylen_s(input_file, BUF_SZ, lpCmdLine + offset);
		if (input_file[len - 1] == L'"')
			input_file[len - 1] = L'\0';
		if (len >= BUF_SZ)
		{
			swprintf_s(msg_buf, BUF_SZ, L"Too long input path (>=", len, L" characters)!");
			MessageBox(tc_main_wnd, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
	}

	// a. Check for Find Files being our parent
	if (parent_find_files)
	{
		edit_paths->Append(input_file);
		input_file = NULL;      // Memory block now is controlled by ArrayPtr, forget about it
		WaitForTerminate = false;
	}

	// TODO: [3:MEDIUM] Get rid of code duplication
	// b. Check for archive/FS-plugin/FTP/etc.
	if (edit_paths->GetLength() == 0)
	{
		WCHAR* temp_dir = new WCHAR[BUF_SZ];
		if (temp_dir == NULL)
		{
			MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		size_t temp_dir_len = GetTempPath(BUF_SZ, temp_dir);
		if (temp_dir_len + 4 >= BUF_SZ)
		{
			swprintf_s(msg_buf, BUF_SZ, L"Too long TEMP path (", temp_dir_len, L" characters)!");
			MessageBox(tc_main_wnd, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		temp_dir[temp_dir_len++] = L'_';
		temp_dir[temp_dir_len++] = L't';
		temp_dir[temp_dir_len++] = L'c';
		if (_wcsnicmp(input_file, temp_dir, temp_dir_len) == 0)
		{
			if ((input_file[temp_dir_len] == L'\\')
				||
				((input_file[temp_dir_len] == L'_') && (input_file[temp_dir_len + 1] == L'\\')))
			{
				edit_paths->Append(input_file);
				input_file = NULL;      // Memory block now is controlled by ArrayPtr, forget about it
				// FS-plugins do not need to wait; archives/FTP/LPT/etc. do need.
				WaitForTerminate = (wcsncmp(tc_curpath_cmdline, L"\\\\\\", 3) != 0);
			}
		}
		delete[] temp_dir;
	}

	// c. Check for TemporaryPanel FS-plugin.
	if (edit_paths->GetLength() == 0)
	{
		if (wcsncmp(tc_curpath_cmdline, L"\\\\\\", 3) == 0)
		{
			edit_paths->Append(input_file);
			input_file = NULL;
		}
	}

	// TODO: [1:HIGH] If TC panel is not autorefreshed (NoReread) the new file does not appear, and TCER opens the file under cursor instead
	// c. Check for Shift+F4.
	/*
		Using timestamp values of the input file.
		Timestamp resolutions for different file systems:
		NTFS:
			CreationTime   - 100 nanoseconds
			LastWriteTime  - 100 nanoseconds
			LastAccessTime - 100 nanoseconds
			(LastAccessTime is updated at a 60 minute granularity; in Vista/Server08 updates to LastAccessTime
			are disabled by default and are updated only when the file is closed.)
		FAT:
			CreationTime   - 10 milliseconds
			LastWriteTime  - 2 seconds
			LastAccessTime - 1 day
		exFAT:
			CreationTime   - 10 milliseconds
			LastWriteTime  - 10 milliseconds
			LastAccessTime - 2 seconds

		So, check whether CreationTime is close enough to the current system time.
	*/
	if (edit_paths->GetLength() == 0)
	{
		WIN32_FILE_ATTRIBUTE_DATA file_info;
		if (GetFileAttributesEx(input_file, GetFileExInfoStandard, &file_info))
		{
			FILETIME local_time;
			GetSystemTimeAsFileTime(&local_time);
			ULARGE_INTEGER file_time64;
			ULARGE_INTEGER local_time64;
			file_time64.HighPart = file_info.ftCreationTime.dwHighDateTime;
			file_time64.LowPart = file_info.ftCreationTime.dwLowDateTime;
			local_time64.HighPart = local_time.dwHighDateTime;
			local_time64.LowPart = local_time.dwLowDateTime;
			// Consider the file just created if it is older than MaxShiftF4MSeconds milliseconds
			// (2000 milliseconds by default)
			if (local_time64.QuadPart - file_time64.QuadPart < MaxShiftF4MSeconds * 10000)
			{
				edit_paths->Append(input_file);
				input_file = NULL;
			}
		}
	}

	// e. Get files from panel
	if (edit_paths->GetLength() == 0)
	{
		for (i = 0; i < sel_items_num; ++i)
		{
			strip_file_data((*sel_items_txt)[i]);
			WCHAR* tmp = new WCHAR[BUF_SZ];
			if (tmp == NULL)
			{
				MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
				ExitProcess(1);
			}
			wcscpylen_s(tmp, BUF_SZ, tc_curpath_cmdline);
			len = wcscpylen_s(tmp + tc_curpath_cmdline_len, BUF_SZ - tc_curpath_cmdline_len, (*sel_items_txt)[i]);
			if ((len < BUF_SZ) && ((GetFileAttributes(tmp) & FILE_ATTRIBUTE_DIRECTORY) == 0))
			{
				edit_paths->Append(tmp);
				// Update the active item index in case some bad items did not pass the test
				if (active_item == i)
					active_item = edit_paths->GetLength() - 1;
			}
			else
			{
				delete[] tmp;
				// The active item is bad; drop it and use the first item
				if (active_item == i)
					active_item = 0;
			}
		}
		if ((sel_items_num > 0) && (edit_paths->GetLength() == 0))
		{
			MessageBox(tc_main_wnd, L"None of the selected elements could be opened!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
	}

	// e. Get focused element
	if ((edit_paths->GetLength() == 0) && (focus_item_txt != NULL))
	{
		// No elements found, probably switched directory too fast in TC.
		active_item = 0;
		strip_file_data(focus_item_txt);
		WCHAR* tmp = new WCHAR[BUF_SZ];
		if (tmp == NULL)
		{
			MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		wcscpylen_s(tmp, BUF_SZ, tc_curpath_cmdline);
		len = wcscpylen_s(tmp + tc_curpath_cmdline_len, BUF_SZ - tc_curpath_cmdline_len, focus_item_txt);
		if ((len < BUF_SZ) && ((GetFileAttributes(tmp) & FILE_ATTRIBUTE_DIRECTORY) == 0))
			edit_paths->Append(tmp);
		else
			delete[] tmp;
	}

	// f. Get the file from command line.
	if (edit_paths->GetLength() == 0)
	{
		// No elements found, probably switched directory too fast in TC.
		active_item = 0;
		edit_paths->Append(input_file);
		input_file = NULL;
	}

	// Check the final active item index
	if (active_item >= edit_paths->GetLength())
		active_item = 0;

	delete sel_items_txt;
	delete[] tc_curpath_cmdline;
	if (focus_item_txt != NULL)
		delete[] focus_item_txt;
	if (input_file != NULL)
		delete[] input_file;

	// Check for the number of files to be opened
	// (in case Ctrl+A, F4 was accidentally pressed in a huge directory)
	sel_items_num = edit_paths->GetLength();
	if ((MaxItems != 0) && (sel_items_num > MaxItems))
	{
		swprintf_s(msg_buf, BUF_SZ, L"", sel_items_num, L" files are to be opened!\nAre you sure you wish to continue?");
		if (MessageBox(tc_main_wnd, msg_buf, MsgBoxTitle, MB_ICONWARNING | MB_OKCANCEL) == IDCANCEL)
			ExitProcess(0);
	}


	//////////////////////////////////////////////////////////////////////////
	// Read TCER options for the 'active file' extension                    //
	//////////////////////////////////////////////////////////////////////////

	WCHAR* edit_path = (*edit_paths)[active_item];
	WCHAR* file_ext = new WCHAR[BUF_SZ];
	WCHAR* ini_section = new WCHAR[BUF_SZ];
	WCHAR* editor_path = new WCHAR[2 * BUF_SZ];
	if ((file_ext == NULL) || (ini_section == NULL) || (editor_path == NULL))
	{
		MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}
	// Extract the active file extension
	path_len = wcslen(edit_path);
	idx_slash = wcsrchr_pos(edit_path, path_len, L'\\');
	idx_dot = wcsrchr_pos(edit_path, path_len, L'.');
	size_t file_ext_len;
	if ((idx_dot == 0) || ((idx_slash != 0) && (idx_dot < idx_slash)))
	{
		// Extension not found
		file_ext_len = wcscpylen_s(file_ext, BUF_SZ, L"<nil>");
	}
	else
	{
		// Extension found
		file_ext_len = wcscpylen_s(file_ext, BUF_SZ, edit_path + idx_dot);
	}

	// Get section name that describes the editor to use
	WCHAR* all_exts = new WCHAR[32767];
	if (GetPrivateProfileSection(L"Extensions", all_exts, 32767, ini_path) == 0)
	{
		// No extensions specified, use DefaultProgram
		wcscpylen_s(ini_section, BUF_SZ, L"DefaultProgram");
	}
	else
	{
		WCHAR* exts_block = all_exts;
		bool found = false;
		// INI section lines are 0-byte-delimited, and the whole section ends with two 0-bytes
		while (*exts_block != L'\0')
		{
			WCHAR* pos = exts_block;
			// Checking the list of extensions in the current line...
			while (*pos != L'=')
			{
				if ((_wcsnicmp(pos, file_ext, file_ext_len) == 0) && ((pos[file_ext_len] == L',') || (pos[file_ext_len] == L'=')))
				{
					// Found the correct extension => store the application name and exit the loop
					found = true;
					pos += wcscspn(pos, L"=") + 1;
					wcscpylen_s(ini_section, BUF_SZ, L"Program_");
					wcscpylen_s(ini_section + 8, BUF_SZ - 8, pos);
					break;
				}
				// Go to the next extension
				pos += wcscspn(pos, L",=");
				if (*pos == L',')
					++pos;
			}
			if (found)
				break;
			else
			{
				// Go to the next line
				pos += wcscspn(pos, L"") + 1;
				exts_block = pos;
			}
		}
		if (!found)
		{
			// No editor specified, use DefaultProgram
			wcscpylen_s(ini_section, BUF_SZ, L"DefaultProgram");
		}
	}
	delete[] all_exts;
	delete[] file_ext;

	// Read the editor settings
	BOOL is_mdi = GetPrivateProfileInt(ini_section, L"MDI", 0, ini_path);
	WCHAR* tmp_buf = new WCHAR[BUF_SZ];
	if (tmp_buf == NULL)
	{
		MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}
	tmp_buf[0] = L'"';
	path_len = GetPrivateProfileString(ini_section, L"FullPath", NULL, tmp_buf + 1, BUF_SZ - 1, ini_path);
	if (path_len == 0)
	{
		swprintf_s(msg_buf, BUF_SZ, L"INI key [", ini_section, L"]::FullPath is missing!");
		MessageBox(tc_main_wnd, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}
	path_len = ExpandEnvironmentStrings(tmp_buf, editor_path, 2 * BUF_SZ);
	if ((path_len == 0) && (path_len > 2 * BUF_SZ - 1))
	{
		swprintf_s(msg_buf, BUF_SZ, L"Failed to expand environment variables:\n'", tmp_buf, L"'");
		MessageBox(tc_main_wnd, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}
	editor_path[path_len - 1] = L'"';
	editor_path[path_len] = L'\0';

	len = GetPrivateProfileString(ini_section, L"CommandLineArgs", NULL, editor_path + path_len + 1, static_cast<DWORD>(BUF_SZ - path_len - 1), ini_path);
	if (len != 0)
	{
		editor_path[path_len] = L' ';
		path_len += len + 1;
		if (path_len >= 2 * BUF_SZ)
		{
			MessageBox(tc_main_wnd, L"Too long editor path and/or args!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
	}

	delete[] ini_path;
	delete[] tmp_buf;
	delete[] ini_section;

	// Construct command lines taking into account the MDI setting and the maximum
	// allowed command line length CMDLINE_BUF_SZ (32k).
	ArrayPtr<WCHAR>* cmd_lines = new ArrayPtr<WCHAR>;
	if (cmd_lines == NULL)
	{
		MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
		ExitProcess(1);
	}
	size_t cur_pos;
	if (is_mdi)
	{
		// If MDI editor is a secondary instance it will terminate immediately.
		// If it is a first instance, it may open another file and continue to live
		// even when the original file is closed. Therefore waiting for the process
		// termination is useless in either case.
		WaitForTerminate = false;

		// Cycle all the elements to edit, concatenate them into command line.
		// As soon as command line length exceeds CMDLINE_BUF_SZ, roll back the latest
		// added item, insert the command line into the execution queue and start
		// constructing the new command line starting from the currently processed file.
		// TODO: [3:MEDIUM] Optimize, get rid of code duplication
		WCHAR* cmd_line = new WCHAR[CMDLINE_BUF_SZ];
		if (cmd_line == NULL)
		{
			MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		cur_pos = wcscpylen_s(cmd_line, CMDLINE_BUF_SZ, editor_path);
		for (i = 0; i < edit_paths->GetLength(); ++i)
		{
			if (cur_pos + 4 > CMDLINE_BUF_SZ)
			{
				cmd_line[cur_pos++] = L'\0';
				cmd_lines->Append(cmd_line);
				cmd_line = new WCHAR[CMDLINE_BUF_SZ];
				if (cmd_line == NULL)
				{
					MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
					ExitProcess(1);
				}
				cur_pos = wcscpylen_s(cmd_line, CMDLINE_BUF_SZ, editor_path);
			}
			size_t cur_pos_bak = cur_pos;
			cmd_line[cur_pos++] = L' ';
			cmd_line[cur_pos++] = L'"';
			cur_pos += wcscpylen_s(cmd_line + cur_pos, CMDLINE_BUF_SZ - cur_pos, (*edit_paths)[i]);
			if (cur_pos + 2 >= CMDLINE_BUF_SZ)
			{
				cur_pos = cur_pos_bak;
				cmd_line[cur_pos++] = L'\0';
				cmd_lines->Append(cmd_line);
				cmd_line = new WCHAR[CMDLINE_BUF_SZ];
				if (cmd_line == NULL)
				{
					MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
					ExitProcess(1);
				}
				cur_pos = wcscpylen_s(cmd_line, CMDLINE_BUF_SZ, editor_path);
				cmd_line[cur_pos++] = L' ';
				cmd_line[cur_pos++] = L'"';
				cur_pos += wcscpylen_s(cmd_line + cur_pos, CMDLINE_BUF_SZ - cur_pos, (*edit_paths)[i]);
			}
			cmd_line[cur_pos++] = L'"';
		}
		cmd_line[cur_pos++] = L'\0';
		cmd_lines->Append(cmd_line);
	}
	else
	{
		// Cycle all the elements to edit, concatenate each item separately
		// with the editor path/args and insert these single-filed command lines
		// into the execution queue.
		const size_t sdi_buf_sz = BUF_SZ * 3 + 6;
		for (i = 0; i < edit_paths->GetLength(); ++i)
		{
			WCHAR* cmd_line = new WCHAR[sdi_buf_sz];
			if (cmd_line == NULL)
			{
				MessageBox(tc_main_wnd, L"Memory allocation error!", MsgBoxTitle, MB_ICONERROR | MB_OK);
				ExitProcess(1);
			}
			cur_pos = wcscpylen_s(cmd_line, sdi_buf_sz, editor_path);
			cmd_line[cur_pos++] = L' ';
			cmd_line[cur_pos++] = L'"';
			cur_pos += wcscpylen_s(cmd_line + cur_pos, sdi_buf_sz - cur_pos, (*edit_paths)[i]);
			cmd_line[cur_pos++] = L'"';
			cmd_line[cur_pos++] = L'\0';
			cmd_lines->Append(cmd_line);
		}
	}
	delete edit_paths;
	delete[] editor_path;

	// Run all the prepared command lines
	__declspec(align(16)) AlignWrapper<PROCESS_INFORMATION> pi;
	__declspec(align(16)) AlignWrapper<STARTUPINFO> si;
	PROCESS_INFORMATION* pi_ptr = (PROCESS_INFORMATION*)pi;
	STARTUPINFO* si_ptr = (STARTUPINFO*)si;
	for (i = 0; i < cmd_lines->GetLength(); ++i)
	{
		zeromem(&pi);
		zeromem(&si);
		si_ptr->cb = sizeof(STARTUPINFO);
		if (CreateProcess(NULL, (*cmd_lines)[i], NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT, NULL, NULL, si_ptr, pi_ptr) == 0)
		{
			swprintf_s(msg_buf, BUF_SZ, L"Failed to start editor (", GetLastError(), L")");
			MessageBox(tc_main_wnd, msg_buf, MsgBoxTitle, MB_ICONERROR | MB_OK);
			ExitProcess(1);
		}
		if (ClearSelection)
		{
			// Send message into TC window to clear selection
			const int cm_ClearAll = 524;
			PostMessage(tc_main_wnd, WM_USER + 51, cm_ClearAll, 0);
		}
		// WaitForTerminate is set for single file only
		// TODO: [5:LOW] Make sure it works for multiple iterations (for the future)
		// TODO: [5:LOW] If several MDI instances are started, wait for each to terminate (otherwise the main instance may not get in time to open all the files)
		if (WaitForTerminate)
			WaitForSingleObject(pi_ptr->hProcess, INFINITE);
		CloseHandle(pi_ptr->hProcess);
		CloseHandle(pi_ptr->hThread);
	}

	delete cmd_lines;
	delete[] msg_buf;

	// TODO: [3:MEDIUM] Option to change editor's window placement (max, min)
	// TODO: [9:IDLE] Detect tree mode
	// TODO: [9:IDLE] Accept lists of extensions in INI, not only single items
	// TODO: [5:LOW] Support virtual folders
	// TODO: [5:LOW] Allow associations not only by extension, but also by file masks + prioritize them by order
	// TODO: [3:MEDIUM] Reuse dynamic memory instead of delete/new
	// TODO: [5:LOW] Support opening files from one bunch in different editors

	ExitProcess(0);
}
