// tcer.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "tcer.h"
#include "WindowFinder.h"

// Copy a string and return its length
size_t wcscpylen_s(WCHAR *strDestination, size_t numberOfElements, const WCHAR *strSource)
{
	size_t count = 0;
	while (((*strDestination++ = *strSource++) != 0) && (--numberOfElements > 0))
		++count;
	return count;
}

// Scan a string for the last occurrence of a character.
// Return index of the next position or 0 if no such character.
size_t wcsrchr_pos(const WCHAR* str, size_t start_pos, WCHAR c)
{
	size_t idx = start_pos;
	while (idx > 0)
	{
		if (str[--idx] == c)
			return idx + 1;
	}
	return 0;
}

void strip_file_data(WCHAR* elem)
{
	const WCHAR bad_chars[] = L"<>:|*\"";
	size_t pos = wcscspn(elem, bad_chars);
	// Full path, colon at the second position
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
	for (int i = 0; i < 4; ++i)
	{
		// Columns are space-delimited (size, date, time, attributes)
		len = wcsrchr_pos(elem, len - 1, L' ');
		if (len == 0)
			return;
	}
	elem[len - 1] = L'\0';
}

int APIENTRY wWinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPWSTR    lpCmdLine,
	int       nCmdShow
)
{
	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(nCmdShow);

	/*
		If this code is used somewhere else, keep in mind that when an error occurs,
		resources are not freed (program terminates, so all resources are freed
		by the system anyway).
	*/

	const size_t BUF_SZ = 1024;
	const size_t CMDLINE_BUF_SZ = 32 * 1024;
	WCHAR* msg_buf = new WCHAR[BUF_SZ];
	if (msg_buf == NULL)
	{
		MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}

	HWND tc_main_wnd;

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

		Algorythm:
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
	HMODULE ntdll = LoadLibrary(L"ntdll.dll");
	if (ntdll == NULL)
	{
		swprintf_s(msg_buf, BUF_SZ, L"Failed to load ntdll.dll (%d)", GetLastError());
		MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	tNtQueryInformationProcess fNtQueryInformationProcess = (tNtQueryInformationProcess)GetProcAddress(ntdll, "NtQueryInformationProcess");
	if (fNtQueryInformationProcess == NULL)
	{
		swprintf_s(msg_buf, BUF_SZ, L"Failed to find NtQueryInformationProcess (%d)", GetLastError());
		MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}

	// Get PID of the parent process (TC)
	PROCESS_BASIC_INFORMATION proc_info;
	ULONG ret_len;
	NTSTATUS query_res = fNtQueryInformationProcess(GetCurrentProcess(), ProcessBasicInformation, &proc_info, sizeof(proc_info), &ret_len);
	if (!NT_SUCCESS(query_res))
	{
		swprintf_s(msg_buf, BUF_SZ, L"NtQueryInformationProcess failed (0x%08x)", query_res);
		MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	FreeLibrary(ntdll);

	//////////////////////////////////////////////////////////////////////////
	// 2. Find the active file panel handle.

	// Find the main window of the TC instance found
#ifdef _DEBUG
	// DBG: When debugging, the debugger is parent; skip the PPID check
	tc_main_wnd = WindowFinder::FindWnd(NULL, false, L"TTOTAL_CMD", 0);
#else
	tc_main_wnd = WindowFinder::FindWnd(NULL, false, L"TTOTAL_CMD", proc_info.InheritedFromUniqueProcessId);
#endif
	if (tc_main_wnd == NULL)
	{
		MessageBox(NULL, L"Could not find parent TC window!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}

	// Find TC file panels
	tc_panels = WindowFinder::FindWnds(tc_main_wnd, true, L"TMyListBox", 0);
	if (tc_panels->GetLength() == 0)
	{
		MessageBox(NULL, L"Could not find panels in the TC window!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
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
		swprintf_s(msg_buf, BUF_SZ, L"Failed to get TC GUI thread information (%d)", GetLastError());
		MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	if (gti.hwndFocus == NULL)
	{
		MessageBox(NULL, L"Failed to determine active panel!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
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
		MessageBox(NULL, L"No TC panel is focused!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	if (GetWindowTextLength(tc_panel) != 0)
	{
		MessageBox(NULL, L"No TC file panel is focused!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}

	//////////////////////////////////////////////////////////////////////////
	// 3. Get current path from TC command line.

	WCHAR* tc_curpath_cmdline = new WCHAR[BUF_SZ];
	if (tc_curpath_cmdline == NULL)
	{
		MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	tc_curpath_cmdline[0] = L'\0';
	size_t tc_curpath_cmdline_len = 0;
	tc_panels = WindowFinder::FindWnds(tc_main_wnd, true, L"TMyPanel", 0);
	if (tc_panels->GetLength() == 0)
	{
		MessageBox(NULL, L"Failed to find TC command line!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	for (i = 0; i < tc_panels->GetLength(); ++i)
	{
		if (WindowFinder::FindWnd((*tc_panels)[i], true, L"TMyTabControl", 0) != NULL)
			continue;

		if (WindowFinder::FindWnd((*tc_panels)[i], true, L"THeaderClick", 0) != NULL)
			continue;

		HWND tc_cmdline = WindowFinder::FindWnd((*tc_panels)[i], true, L"TMyPanel", 0);
		if (tc_cmdline == NULL)
			continue;

		tc_curpath_cmdline_len = GetWindowText(tc_cmdline, tc_curpath_cmdline, BUF_SZ);
		if (tc_curpath_cmdline_len + 1 >= BUF_SZ)
		{
			swprintf_s(msg_buf, BUF_SZ, L"Too long path (%d characters)!", tc_curpath_cmdline_len);
			MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		if (tc_curpath_cmdline_len > 0)
			tc_curpath_cmdline[tc_curpath_cmdline_len - 1] = L'\\';
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
		swprintf_s(msg_buf, BUF_SZ, L"Invalid number of path bars (%d), should be 2!", tc_panels->GetLength());
		MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}

	// Get the coordinates of the active panel and path panels
	RECT tc_panel_rect, path_panel1_rect, path_panel2_rect;
	if (GetWindowRect(tc_panel, &tc_panel_rect) == 0)
	{
		swprintf_s(msg_buf, BUF_SZ, L"Failed to obtain the panel placement (%d)", GetLastError());
		MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	if ((GetWindowRect((*tc_panels)[0], &path_panel1_rect) == 0)
		||
		(GetWindowRect((*tc_panels)[1], &path_panel2_rect) == 0))
	{
		swprintf_s(msg_buf, BUF_SZ, L"Failed to obtain the path panel placement (%d)", GetLastError());
		MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
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
		MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	size_t tc_curpath_panel_len = GetWindowText(tc_path_panel, tc_curpath_panel, BUF_SZ);
	if (tc_curpath_panel_len + 1 >= BUF_SZ)
	{
		swprintf_s(msg_buf, BUF_SZ, L"Too long path (%d characters)!", tc_curpath_panel_len);
		MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	delete[] tc_curpath_panel;
	*/

	//////////////////////////////////////////////////////////////////////////
	// 5. Fetch the list of items to edit.

	// If file under cursor is selected, it will be used for selecting the editor
	// TODO: [HIGH] Implement getting editor by the focused element instead of the first selected
	bool focused_is_selected = false;

	// Get the focused item
	UINT focus_item_idx = (UINT)SendMessage(tc_panel, LB_GETCARETINDEX, 0, 0);

	// Get the number of selected elements and their indices
	size_t sel_items_num = SendMessage(tc_panel, LB_GETSELCOUNT, 0, 0);
	UINT* sel_items_idx = NULL;
	if (sel_items_num > 0)
	{
		sel_items_idx = new UINT[sel_items_num];
		if (sel_items_idx == NULL)
		{
			MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		size_t sel_items_num2 = SendMessage(tc_panel, LB_GETSELITEMS, sel_items_num, (LPARAM)sel_items_idx);
		if (sel_items_num2 < sel_items_num)
			sel_items_num = sel_items_num2;
	}

	// Get the elements themselves
	size_t invalid_paths = 0;

	WCHAR* focus_item_txt = NULL;
	size_t elem_len = SendMessage(tc_panel, LB_GETTEXTLEN, focus_item_idx, NULL);
	if ((elem_len != LB_ERR) && (elem_len > 0) && (elem_len + 1 < BUF_SZ))
	{
		focus_item_txt = new WCHAR[elem_len + 1];
		if (focus_item_txt == NULL)
		{
			MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		if (SendMessage(tc_panel, LB_GETTEXT, focus_item_idx, (LPARAM)focus_item_txt) == 0)
		{
			delete[] focus_item_txt;
			focus_item_txt = NULL;
		}
	}

	ArrayPtr<WCHAR>* sel_items_txt = new ArrayPtr<WCHAR>(sel_items_num);
	if (sel_items_txt == NULL)
	{
		MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
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
			MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		if (SendMessage(tc_panel, LB_GETTEXT, sel_items_idx[i], (LPARAM)tmp) == 0)
			delete[] tmp;
		else
		{
			sel_items_txt->Append(tmp);
			if (focus_item_idx == sel_items_idx[i])
				focused_is_selected = true;
		}
	}
	if (sel_items_idx != NULL)
		delete[] sel_items_idx;


	//////////////////////////////////////////////////////////////////////////
	// Translate list of items into list of paths                           //
	//////////////////////////////////////////////////////////////////////////

	// TODO: [HIGH] Support opening files directly from %TEMP%\_tc\ 
	// TODO: [HIGH] If errors (e.g. no panel has focus), just open lpCmdLine

	/*
		Current implementation:
		1) lpCmdLine =~ %TEMP%\_tc(_|)\.*
			Archive/FS-plugin/FTP/etc. -> open lpCmdLine, wait till editor close.
		2) tc_curpath_cmdline =~ \\\.*
			TempPanel FS-plugin -> open lpCmdLine, do not wait.
		3) lpCmdLine was just created
			Shift+F4 -> open lpCmdLine, do not wait.
		4) Otherwise
			Combine tc_curpath_cmdline with selected elements.
	*/

	bool WaitForTerminate = false;
	ArrayPtr<WCHAR>* edit_paths = new ArrayPtr<WCHAR>;
	if (edit_paths == NULL)
	{
		MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}

	size_t offset;
	size_t len;

	// Remove quotes from lpCmdLine (if present) and copy it into non-const buffer
	WCHAR* input_file = new WCHAR[BUF_SZ];
	if (input_file == NULL)
	{
		MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
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
			swprintf_s(msg_buf, BUF_SZ, L"Too long input path (>=%d characters)!", len);
			MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
	}

	// TODO: [MEDIUM] Get rid of code duplication
	// a. Check for archive/FS-plugin/FTP/etc.
	WCHAR* temp_dir = new WCHAR[BUF_SZ];
	if (temp_dir == NULL)
	{
		MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	size_t temp_dir_len = GetTempPath(BUF_SZ, temp_dir);
	if (temp_dir_len + 4 >= BUF_SZ)
	{
		swprintf_s(msg_buf, BUF_SZ, L"Too long TEMP path (%d characters)!", temp_dir_len);
		MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
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

	// b. Check for TemporaryPanel FS-plugin.
	if (edit_paths->GetLength() == 0)
	{
		if (wcsncmp(tc_curpath_cmdline, L"\\\\\\", 3) == 0)
		{
			edit_paths->Append(input_file);
			input_file = NULL;
		}
	}

	// c. Check for Shift+F4.
	// Heuristic algorithm: ftLastAccessTime == ftLastWriteTime, and both are almost current time.
	if (edit_paths->GetLength() == 0)
	{
		WIN32_FILE_ATTRIBUTE_DATA file_info;
		if (!GetFileAttributesEx(input_file, GetFileExInfoStandard, &file_info))
		{
			swprintf_s(msg_buf, BUF_SZ, L"Failed to get input file attributes (%d)", GetLastError());
			MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		if ((file_info.ftLastAccessTime.dwHighDateTime == file_info.ftLastWriteTime.dwHighDateTime) &&
			(file_info.ftLastAccessTime.dwLowDateTime == file_info.ftLastWriteTime.dwLowDateTime))
		{
			FILETIME local_time;
			GetSystemTimeAsFileTime(&local_time);
			ULARGE_INTEGER file_time64;
			ULARGE_INTEGER local_time64;
			file_time64.HighPart = file_info.ftLastWriteTime.dwHighDateTime;
			file_time64.LowPart = file_info.ftLastWriteTime.dwLowDateTime;
			local_time64.HighPart = local_time.dwHighDateTime;
			local_time64.LowPart = local_time.dwLowDateTime;
			// TODO: [HIGH] Make number of milliseconds (2000) configurable (move INI reading code upper)
			if (local_time64.QuadPart - file_time64.QuadPart < 2000 * 10000)
			{
				edit_paths->Append(input_file);
				input_file = NULL;
			}
		}
	}

	// d. Get files from panel
	if (edit_paths->GetLength() == 0)
	{
		for (i = 0; i < sel_items_num; ++i)
		{
			strip_file_data((*sel_items_txt)[i]);
			WCHAR* tmp = new WCHAR[BUF_SZ];
			if (tmp == NULL)
			{
				MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
				return 1;
			}
			wcscpy_s(tmp, BUF_SZ, tc_curpath_cmdline);
			len = wcscpylen_s(tmp + tc_curpath_cmdline_len, BUF_SZ - tc_curpath_cmdline_len, (*sel_items_txt)[i]);
			if ((len < BUF_SZ) && ((GetFileAttributes(tmp) & FILE_ATTRIBUTE_DIRECTORY) == 0))
				edit_paths->Append(tmp);
			else
				delete[] tmp;
		}
	}

	// e. Get focused element
	if ((edit_paths->GetLength() == 0) && (focus_item_txt != NULL))
	{
		// No elements found, probably switched directory too fast in TC.
		strip_file_data(focus_item_txt);
		WCHAR* tmp = new WCHAR[BUF_SZ];
		if (tmp == NULL)
		{
			MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		wcscpy_s(tmp, BUF_SZ, tc_curpath_cmdline);
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
		edit_paths->Append(input_file);
		input_file = NULL;
	}

	delete sel_items_txt;
	delete[] tc_curpath_cmdline;
	if (focus_item_txt != NULL)
		delete[] focus_item_txt;
	if (input_file != NULL)
		delete[] input_file;

	//////////////////////////////////////////////////////////////////////////
	// Find TCER configuration file                                         //
	//////////////////////////////////////////////////////////////////////////

	// Search for tcer.ini file: first own directory, then wincmd.ini directory
	WCHAR* ini_path = new WCHAR[BUF_SZ];
	WCHAR* ini_name = new WCHAR[BUF_SZ];
	if ((ini_path == NULL) || (ini_name == NULL))
	{
		MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	WCHAR* exe_path;
	if (_get_wpgmptr(&exe_path) != 0)
	{
		swprintf_s(msg_buf, BUF_SZ, L"Failed to find myself (%d)", GetLastError());
		MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	size_t path_len = wcscpylen_s(ini_path, BUF_SZ, exe_path);

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
			MessageBox(NULL, L"Too long INI file name!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		wcscpylen_s(ini_name + ini_name_len, BUF_SZ - ini_name_len, L".ini");
	}
	else
	{
		// Extension found, replace with 'ini'
		idx_dot -= idx_slash;
		if (idx_dot + 4 > BUF_SZ)
		{
			MessageBox(NULL, L"Too long INI file name!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		wcscpylen_s(ini_name + idx_dot, BUF_SZ - idx_dot, L"ini");
		ini_name_len = idx_dot + 3;
	}
	if (idx_slash + ini_name_len + 1 > BUF_SZ)
	{
		MessageBox(NULL, L"Too long path to INI!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	wcscpylen_s(ini_path + idx_slash, BUF_SZ - idx_slash, ini_name);

	// INI file path constructed, check the file existence
	if (GetFileAttributes(ini_path) == INVALID_FILE_ATTRIBUTES)
	{
		// File does not exist, search wincmd.ini directory
		path_len = GetEnvironmentVariable(L"COMMANDER_INI", ini_path, BUF_SZ);
		if (path_len == 0)
		{
			MessageBox(NULL, L"COMMANDER_INI variable undefined!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
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
				MessageBox(NULL, L"Too long path to myself!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
				return 1;
			}
			wcscpylen_s(ini_path + idx_slash, BUF_SZ - idx_slash, ini_name);
		}
		if (GetFileAttributes(ini_path) == INVALID_FILE_ATTRIBUTES)
		{
			MessageBox(NULL, L"Configuration file not found!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
	}
	delete[] ini_name;

	//////////////////////////////////////////////////////////////////////////
	// Read general TCER configuration                                      //
	//////////////////////////////////////////////////////////////////////////

	size_t MaxItems = GetPrivateProfileInt(L"Configuration", L"MaxItems", 256, ini_path);
//	size_t MaxShiftF4MSeconds = GetPrivateProfileInt(L"Configuration", L"MaxShiftF4MSeconds", 2000, ini_path);
	bool ClearSelection = (GetPrivateProfileInt(L"Configuration", L"ClearSelection", 1, ini_path) != 0);

	sel_items_num = edit_paths->GetLength();
	if ((MaxItems != 0) && (sel_items_num > MaxItems))
	{
		swprintf_s(msg_buf, BUF_SZ, L"%d files are to be opened!\nAre you sure you wish to continue?", sel_items_num);
		if (MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONWARNING | MB_OKCANCEL) == IDCANCEL)
			return 0;
	}

	//////////////////////////////////////////////////////////////////////////
	// Read TCER options for the first extension                            //
	//////////////////////////////////////////////////////////////////////////

	WCHAR* edit_path = (*edit_paths)[0];
	WCHAR* file_ext = new WCHAR[BUF_SZ];
	WCHAR* editor_path = new WCHAR[BUF_SZ];
	if ((file_ext == NULL) || (editor_path == NULL))
	{
		MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	path_len = wcslen(edit_path);
	idx_slash = wcsrchr_pos(edit_path, path_len, L'\\');
	idx_dot = wcsrchr_pos(edit_path, path_len, L'.');
	if ((idx_dot == 0) || ((idx_slash != 0) && (idx_dot < idx_slash)))
	{
		// Extension not found
		wcscpylen_s(file_ext, BUF_SZ, L"<nil>");
	}
	else
	{
		// Extension found
		wcscpylen_s(file_ext, BUF_SZ, edit_path + idx_dot);
	}

	// Get section name that describes the editor to use
	wcscpylen_s(editor_path, BUF_SZ, L"Program_");
	if (GetPrivateProfileString(L"Extensions", file_ext, NULL, editor_path + 8, BUF_SZ - 8, ini_path) == 0)
	{
		// No editor specified, use DefaultProgram
		wcscpylen_s(editor_path, BUF_SZ, L"DefaultProgram");
	}
	delete[] file_ext;

	// Read the efitor settings
	BOOL is_mdi = GetPrivateProfileInt(editor_path, L"MDI", 0, ini_path);
	WCHAR* tmp_buf = new WCHAR[BUF_SZ];
	if (tmp_buf == NULL)
	{
		MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	if (GetPrivateProfileString(editor_path, L"FullPath", NULL, tmp_buf, BUF_SZ, ini_path) == 0)
	{
		swprintf_s(msg_buf, BUF_SZ, L"INI key [%s]::FullPath is missing!", tmp_buf);
		MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	path_len = ExpandEnvironmentStrings(tmp_buf, editor_path, BUF_SZ);
	if ((path_len == 0) && (path_len > BUF_SZ))
	{
		swprintf_s(msg_buf, BUF_SZ, L"Failed to expand environment variables:\n'%s'", tmp_buf);
		MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}

	delete[] ini_path;
	delete[] tmp_buf;

	// Construct command lines taking into account the MDI setting and the maximum
	// allowed command line length CMDLINE_BUF_SZ (32k).
	ArrayPtr<WCHAR>* cmd_lines = new ArrayPtr<WCHAR>;
	if (cmd_lines == NULL)
	{
		MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	if (is_mdi)
	{
		// TODO: [MEDIUM] Optimize, get rid of code duplication
		WCHAR* cmd_line = new WCHAR[CMDLINE_BUF_SZ];
		if (cmd_line == NULL)
		{
			MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		size_t cur_pos = 0;
		cmd_line[cur_pos++] = L'"';
		cur_pos += wcscpylen_s(cmd_line + cur_pos, CMDLINE_BUF_SZ - cur_pos, editor_path);
		cmd_line[cur_pos++] = L'"';
		for (i = 0; i < edit_paths->GetLength(); ++i)
		{
			if (cur_pos + 4 > CMDLINE_BUF_SZ)
			{
				cmd_line[cur_pos++] = L'\0';
				cmd_lines->Append(cmd_line);
				cmd_line = new WCHAR[CMDLINE_BUF_SZ];
				if (cmd_line == NULL)
				{
					MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
					return 1;
				}
				cur_pos = 0;
				cmd_line[cur_pos++] = L'"';
				cur_pos += wcscpylen_s(cmd_line + cur_pos, CMDLINE_BUF_SZ - cur_pos, editor_path);
				cmd_line[cur_pos++] = L'"';
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
					MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
					return 1;
				}
				cur_pos = 0;
				cmd_line[cur_pos++] = L'"';
				cur_pos += wcscpylen_s(cmd_line + cur_pos, CMDLINE_BUF_SZ - cur_pos, editor_path);
				cmd_line[cur_pos++] = L'"';
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
		for (i = 0; i < edit_paths->GetLength(); ++i)
		{
			WCHAR* cmd_line = new WCHAR[BUF_SZ * 2 + 6];
			if (cmd_line == NULL)
			{
				MessageBox(NULL, L"Memory allocation error!", L"TC Edit Redirector", MB_ICONERROR | MB_OK);
				return 1;
			}
			size_t cur_pos = 0;
			cmd_line[cur_pos++] = L'"';
			cur_pos += wcscpylen_s(cmd_line + cur_pos, CMDLINE_BUF_SZ - cur_pos, editor_path);
			cmd_line[cur_pos++] = L'"';
			cmd_line[cur_pos++] = L' ';
			cmd_line[cur_pos++] = L'"';
			cur_pos += wcscpylen_s(cmd_line + cur_pos, CMDLINE_BUF_SZ - cur_pos, (*edit_paths)[i]);
			cmd_line[cur_pos++] = L'"';
			cmd_line[cur_pos++] = L'\0';
			cmd_lines->Append(cmd_line);
		}
	}
	delete edit_paths;
	delete[] editor_path;

	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	for (i = 0; i < cmd_lines->GetLength(); ++i)
	{
		ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
		ZeroMemory(&si, sizeof(STARTUPINFO));
		si.cb = sizeof(STARTUPINFO);
		if (CreateProcess(NULL, (*cmd_lines)[i], NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT, NULL, NULL, &si, &pi) == 0)
		{
			swprintf_s(msg_buf, BUF_SZ, L"Failed to start editor (%d)", GetLastError());
			MessageBox(NULL, msg_buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		if (ClearSelection)
		{
			// Send message into TC window to clear selection
			const int cm_ClearAll = 524;
			PostMessage(tc_main_wnd, WM_USER + 51, cm_ClearAll, 0);
		}
		// WaitForTerminate is set for single file only
		// TODO: [LOW] Make sure it works for multiple iterations (for the future)
		// TODO: [LOW] If several MDI instances are started, wait for each to terminate (otherwise the main instance may not get in time to open all the files)
		if (WaitForTerminate)
			WaitForSingleObject(pi.hProcess, INFINITE);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}

	delete cmd_lines;
	delete[] msg_buf;

	// TODO: [MEDIUM] Option to change editor's window placement (max, min)
	// TODO: [IDLE] Detect tree mode
	// TODO: [HIGH] Decide: If several dirs selected and no files -> open focused file or show error?
	// TODO: [IDLE] Accept lists of extensions in INI, not only single items
	// TODO: [LOW] Support virtual folders
	// TODO: [LOW] Allow associations not only by extension, but also by file masks
	// TODO: [HIGH] Add support for special command-line arguments for editors

	return 0;
}
