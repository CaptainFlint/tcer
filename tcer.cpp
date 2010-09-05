// tcer.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "tcer.h"
#include "WindowFinder.h"

const size_t BUF_SZ = 1024;
const size_t CMDLINE_BUF_SZ = 32 * 1024;
WCHAR buf[BUF_SZ];

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
size_t wcsrchr_pos(WCHAR* str, size_t start_pos, WCHAR c)
{
	size_t idx = start_pos;
	while (idx > 0)
	{
		if (str[--idx] == c)
			return idx + 1;
	}
	return 0;
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

	HWND tc_wnd;

	ArrayHWND* tc_panels;
	size_t i;

	//////////////////////////////////////////////////////////////////////////
	// Get the list of selected items from TC file panel                    //
	//////////////////////////////////////////////////////////////////////////

	/*
		Algorythm:
		1. Find window of the TC instance which initiated this TCER process:
		  a) get parent PID;
		  b) find TTOTAL_CMD window that belongs to the PID.
		2. Find the active file panel handle:
		  a) find two TMyListBox'es (file panels);
		  b) get the focused child window of the TC's GUI thread;
		  c) compare to TMyListBox'es found.
		3. Get current path:
		  TMyListBox and TPathPanel are independent, so we cannot know which TPathPanel is active, so
		  a) find all TMyPanel's that are direct children of the main window;
		  b) search for the one that has child TMyPanel but doesn't have TMyTabControl and THeaderClick
			 (for situations when tabs are on and off, respectively);
		  c) the remaining control is Command Line, get the path from its child TMyPanel.
		4. Fetch the list of items to edit:
		  a) get the list of selected items;
		  b) if the list is empty, get the focused item.
	*/

	//////////////////////////////////////////////////////////////////////////
	// 1. Find window of the TC instance which initiated this TCER process.

	// Obtain address of NtQueryInformationProcess needed for getting PPID
	HMODULE ntdll = LoadLibrary(L"ntdll.dll");
	if (ntdll == NULL)
	{
		swprintf_s(buf, BUF_SZ, L"Failed to load ntdll.dll (%d)", GetLastError());
		MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	tNtQueryInformationProcess fNtQueryInformationProcess = (tNtQueryInformationProcess)GetProcAddress(ntdll, "NtQueryInformationProcess");
	if (fNtQueryInformationProcess == NULL)
	{
		FreeLibrary(ntdll);
		swprintf_s(buf, BUF_SZ, L"Failed to find NtQueryInformationProcess (%d)", GetLastError());
		MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}

	// Get PID of the parent process (TC)
	PROCESS_BASIC_INFORMATION proc_info;
	ULONG ret_len;
	NTSTATUS query_res = fNtQueryInformationProcess(GetCurrentProcess(), ProcessBasicInformation, &proc_info, sizeof(proc_info), &ret_len);
	if (!NT_SUCCESS(query_res))
	{
		FreeLibrary(ntdll);
		swprintf_s(buf, BUF_SZ, L"NtQueryInformationProcess failed (0x%08x)", query_res);
		MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	FreeLibrary(ntdll);

	//////////////////////////////////////////////////////////////////////////
	// 2. Find the active file panel handle.

	// Find the main window of the TC instance found
	// TODO: Return back TC PID
	tc_wnd = WindowFinder::FindWnd(NULL, L"TTOTAL_CMD", /*proc_info.InheritedFromUniqueProcessId*/0);
	if (tc_wnd == NULL)
	{
		swprintf_s(buf, BUF_SZ, L"Could not find parent TC window!");
		MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}

	// Find TC file panels
	tc_panels = WindowFinder::FindWnds(tc_wnd, L"TMyListBox", 0);
	if (tc_panels->GetLength() == 0)
	{
		swprintf_s(buf, BUF_SZ, L"Could not find panels in the TC window!");
		MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}

	// Determine the focused panel
	HWND tc_panel = NULL;
	GUITHREADINFO gti;
	gti.cbSize = sizeof(gti);
	SetForegroundWindow(tc_wnd);
	DWORD tid = GetWindowThreadProcessId(tc_wnd, NULL);
	// TODO: Remove debug code
//#ifdef _DEBUG
	Sleep(1000);
//#endif
	if (!GetGUIThreadInfo(tid, &gti))
	{
		swprintf_s(buf, BUF_SZ, L"Failed to get TC GUI thread information (%d)", GetLastError());
		MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	if (gti.hwndFocus == NULL)
	{
		swprintf_s(buf, BUF_SZ, L"Failed to determine active panel!");
		MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
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
	if (tc_panel == NULL)
	{
		swprintf_s(buf, BUF_SZ, L"No TC panel is focused!");
		MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	delete tc_panels;
	if (GetWindowTextLength(tc_panel) != 0)
	{
		swprintf_s(buf, BUF_SZ, L"No TC file panel is focused!");
		MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}

	//////////////////////////////////////////////////////////////////////////
	// 3. Get current path.

	WCHAR tc_curpath[BUF_SZ] = L"";
	tc_panels = WindowFinder::FindWnds(tc_wnd, L"TMyPanel", 0);
	if (tc_panels->GetLength() == 0)
	{
		swprintf_s(buf, BUF_SZ, L"Failed to find TC command line!");
		MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	for (i = 0; i < tc_panels->GetLength(); ++i)
	{
		HWND tc_cmdline = NULL;
		ArrayHWND* child_elems;
		size_t child_elems_num;

		child_elems = WindowFinder::FindWnds((*tc_panels)[i], L"TMyPanel", 0);
		child_elems_num = child_elems->GetLength();
		if (child_elems_num > 0)
			tc_cmdline = (*child_elems)[0];
		delete child_elems;
		if (child_elems_num == 0)
			continue;

		child_elems = WindowFinder::FindWnds((*tc_panels)[i], L"TMyTabControl", 0);
		child_elems_num = child_elems->GetLength();
		delete child_elems;
		if (child_elems_num != 0)
			continue;

		child_elems = WindowFinder::FindWnds((*tc_panels)[i], L"THeaderClick", 0);
		child_elems_num = child_elems->GetLength();
		delete child_elems;
		if (child_elems_num != 0)
			continue;

		int len = GetWindowTextLength(tc_cmdline);
		if (len > BUF_SZ)
		{
			swprintf_s(buf, BUF_SZ, L"Too long path (%d characters)!", len);
			MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		GetWindowText(tc_cmdline, tc_curpath, BUF_SZ);
		break;
	}
	delete tc_panels;
	if (tc_curpath[0] == L'\0')
	{
		swprintf_s(buf, BUF_SZ, L"Failed to get path from TC command line!");
		MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}

	//////////////////////////////////////////////////////////////////////////
	// 4. Fetch the list of items to edit.

	// Get the number of elements and their indices
	size_t sel_items_num = SendMessage(tc_panel, LB_GETSELCOUNT, 0, 0);
	// TODO: Make the number customizable and the check - optional
	if (sel_items_num > 256)
	{
		swprintf_s(buf, BUF_SZ, L"%d items selected!\nAre you sure you wish to continue?", sel_items_num);
		if (MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONWARNING | MB_OKCANCEL) == IDCANCEL)
			return 0;
	}
	UINT* sel_items = NULL;
	if (sel_items_num == 0)
	{
		sel_items_num = 1;
		sel_items = new UINT[1];
		if (sel_items == NULL)
		{
			swprintf_s(buf, BUF_SZ, L"Memory allocation error when fetching selected elements!");
			MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		sel_items[0] = SendMessage(tc_panel, LB_GETCARETINDEX, 0, 0);
	}
	else
	{
		sel_items = new UINT[sel_items_num];
		if (sel_items == NULL)
		{
			swprintf_s(buf, BUF_SZ, L"Memory allocation error when fetching selected elements!");
			MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		size_t sel_items_num2 = SendMessage(tc_panel, LB_GETSELITEMS, sel_items_num, (LPARAM)sel_items);
		if (sel_items_num2 < sel_items_num)
			sel_items_num = sel_items_num2;
	}

	// Get the elements themselves (with full path)
	size_t tc_curpath_len = wcslen(tc_curpath);
	tc_curpath[tc_curpath_len - 1] = L'\\';
	ArrayPtr<WCHAR> edit_paths;
	size_t invalid_paths = 0;
	for (i = 0; i < sel_items_num; ++i)
	{
		WCHAR elem[BUF_SZ];
		size_t elem_len = SendMessage(tc_panel, LB_GETTEXT, sel_items[i], (LPARAM)elem);
		if (tc_curpath_len + elem_len >= BUF_SZ)
		{
			++invalid_paths;
			continue;
		}
		WCHAR* tmp = new WCHAR[BUF_SZ];
		wcscpy_s(tmp, BUF_SZ, tc_curpath);
		wcscpy_s(tmp + tc_curpath_len, BUF_SZ - tc_curpath_len, elem);
		if ((GetFileAttributes(tmp) & FILE_ATTRIBUTE_DIRECTORY) == 0)
			edit_paths.Append(tmp);
		else
			delete[] tmp;
	}
	delete[] sel_items;
	if (edit_paths.GetLength() == 0)
	{
		// No elements found, probably switched directory too fast in TC.
		// Get the file from command line.
		WCHAR* tmp = new WCHAR[BUF_SZ];
		size_t offset = ((lpCmdLine[0] == L'"') ? 1 : 0);
		size_t len = wcscpylen_s(tmp, BUF_SZ, lpCmdLine + offset);
		if (tmp[len - 1] == L'"')
			tmp[len - 1] = L'\0';
		edit_paths.Append(tmp);
	}

	//////////////////////////////////////////////////////////////////////////
	// Find TCER configuration file                                         //
	//////////////////////////////////////////////////////////////////////////

	// Search for tcer.ini file: first own directory, then wincmd.ini directory
	WCHAR ini_path[BUF_SZ];
	WCHAR ini_name[BUF_SZ];
	WCHAR* exe_path;
	if (_get_wpgmptr(&exe_path) != 0)
	{
		swprintf_s(buf, BUF_SZ, L"Failed to find myself (%d)", GetLastError());
		MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	size_t path_len = wcscpylen_s(ini_path, BUF_SZ, exe_path);

	size_t idx_slash, idx_dot;
	idx_slash = wcsrchr_pos(ini_path, path_len, L'\\');
	idx_dot = wcsrchr_pos(ini_path, path_len, L'.');
	size_t ini_name_len = path_len - idx_slash;
	wcscpy_s(ini_name, BUF_SZ, exe_path + idx_slash);
	if ((idx_dot == 0) || ((idx_slash != 0) && (idx_dot < idx_slash)))
	{
		// Extension not found, append '.ini'
		if (ini_name_len + 5 > BUF_SZ)
		{
			swprintf_s(buf, BUF_SZ, L"Too long INI file name!");
			MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		wcscpy_s(ini_name + ini_name_len, BUF_SZ - ini_name_len, L".ini");
	}
	else
	{
		// Extension found, replace with 'ini'
		idx_dot -= idx_slash;
		if (idx_dot + 4 > BUF_SZ)
		{
			swprintf_s(buf, BUF_SZ, L"Too long INI file name!");
			MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		wcscpy_s(ini_name + idx_dot, BUF_SZ - idx_dot, L"ini");
		ini_name_len = idx_dot + 3;
	}
	if (idx_slash + ini_name_len + 1 > BUF_SZ)
	{
		swprintf_s(buf, BUF_SZ, L"Too long path to INI!");
		MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	wcscpy_s(ini_path + idx_slash, BUF_SZ - idx_slash, ini_name);

	// INI file path constructed, check the file existence
	if (GetFileAttributes(ini_path) == INVALID_FILE_ATTRIBUTES)
	{
		// File does not exist, search wincmd.ini directory
		path_len = GetEnvironmentVariable(L"COMMANDER_INI", ini_path, BUF_SZ);
		if (path_len == 0)
		{
			swprintf_s(buf, BUF_SZ, L"COMMANDER_INI variable undefined!");
			MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		idx_slash = wcsrchr_pos(ini_path, path_len, L'\\');
		if (idx_slash == 0)
		{
			// File name only, replace it with INI file name
			wcscpy_s(ini_path, BUF_SZ, ini_name);
		}
		else
		{
			// Keep path, replace file name
			if (idx_slash + ini_name_len + 1 > BUF_SZ)
			{
				swprintf_s(buf, BUF_SZ, L"Too long path to myself!");
				MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
				return 1;
			}
			wcscpy_s(ini_path + idx_slash, BUF_SZ - idx_slash, ini_name);
		}
		if (GetFileAttributes(ini_path) == INVALID_FILE_ATTRIBUTES)
		{
			swprintf_s(buf, BUF_SZ, L"Configuration file not found!");
			MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// Read TCER options for the first extension                            //
	//////////////////////////////////////////////////////////////////////////

	WCHAR* edit_path = edit_paths[0];
	WCHAR file_ext[BUF_SZ];
	path_len = wcslen(edit_path);
	idx_slash = wcsrchr_pos(edit_path, path_len, L'\\');
	idx_dot = wcsrchr_pos(edit_path, path_len, L'.');
	if ((idx_dot == 0) || ((idx_slash != 0) && (idx_dot < idx_slash)))
	{
		// Extension not found
		wcscpy_s(file_ext, BUF_SZ, L"<nil>");
	}
	else
	{
		// Extension found
		wcscpy_s(file_ext, BUF_SZ, edit_path + idx_dot);
	}

	// Get section name that describes the editor to use
	wcscpy_s(buf, BUF_SZ, L"Program_");
	if (GetPrivateProfileString(L"Extensions", file_ext, NULL, buf + 8, BUF_SZ - 8, ini_path) == 0)
	{
		// No editor specified, use DefaultProgram
		wcscpy_s(buf, BUF_SZ, L"DefaultProgram");
	}

	// Read the efitor settings
	WCHAR editor_path[BUF_SZ];
	if (GetPrivateProfileString(buf, L"FullPath", NULL, editor_path, BUF_SZ, ini_path) == 0)
	{
		swprintf_s(buf, BUF_SZ, L"INI key [%s]::FullPath is missing!", buf);
		MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
		return 1;
	}
	BOOL is_mdi = GetPrivateProfileInt(buf, L"MDI", 0, ini_path);

	ArrayPtr<WCHAR> cmd_lines;
	if (is_mdi)
	{
		// TODO: Optimize
		WCHAR* cmd_line = new WCHAR[CMDLINE_BUF_SZ];
		size_t cur_pos = 0;
		cmd_line[cur_pos++] = L'"';
		cur_pos += wcscpylen_s(cmd_line + cur_pos, CMDLINE_BUF_SZ - cur_pos, editor_path);
		cmd_line[cur_pos++] = L'"';
		size_t cmd_line_args_pos = cur_pos;
		for (i = 0; i < edit_paths.GetLength(); ++i)
		{
			if (cur_pos + 4 > CMDLINE_BUF_SZ)
			{
				cmd_line[cur_pos++] = L'\0';
				cmd_lines.Append(cmd_line);
				cmd_line = new WCHAR[CMDLINE_BUF_SZ];
				cur_pos = 0;
				cmd_line[cur_pos++] = L'"';
				cur_pos += wcscpylen_s(cmd_line + cur_pos, CMDLINE_BUF_SZ - cur_pos, editor_path);
				cmd_line[cur_pos++] = L'"';
			}
			size_t cur_pos_bak = cur_pos;
			cmd_line[cur_pos++] = L' ';
			cmd_line[cur_pos++] = L'"';
			cur_pos += wcscpylen_s(cmd_line + cur_pos, CMDLINE_BUF_SZ - cur_pos, edit_paths[i]);
			if (cur_pos + 2 >= CMDLINE_BUF_SZ)
			{
				cur_pos = cur_pos_bak;
				cmd_line[cur_pos++] = L'\0';
				cmd_lines.Append(cmd_line);
				cmd_line = new WCHAR[CMDLINE_BUF_SZ];
				cur_pos = 0;
				cmd_line[cur_pos++] = L'"';
				cur_pos += wcscpylen_s(cmd_line + cur_pos, CMDLINE_BUF_SZ - cur_pos, editor_path);
				cmd_line[cur_pos++] = L'"';
				cmd_line[cur_pos++] = L' ';
				cmd_line[cur_pos++] = L'"';
				cur_pos += wcscpylen_s(cmd_line + cur_pos, CMDLINE_BUF_SZ - cur_pos, edit_paths[i]);
			}
			cmd_line[cur_pos++] = L'"';
		}
		cmd_line[cur_pos++] = L'\0';
		cmd_lines.Append(cmd_line);
	}
	else
	{
		for (i = 0; i < edit_paths.GetLength(); ++i)
		{
			WCHAR* cmd_line = new WCHAR[BUF_SZ * 2 + 6];
			size_t cur_pos = 0;
			cmd_line[cur_pos++] = L'"';
			cur_pos += wcscpylen_s(cmd_line + cur_pos, CMDLINE_BUF_SZ - cur_pos, editor_path);
			cmd_line[cur_pos++] = L'"';
			cmd_line[cur_pos++] = L' ';
			cmd_line[cur_pos++] = L'"';
			cur_pos += wcscpylen_s(cmd_line + cur_pos, CMDLINE_BUF_SZ - cur_pos, edit_paths[i]);
			cmd_line[cur_pos++] = L'"';
			cmd_line[cur_pos++] = L'\0';
			cmd_lines.Append(cmd_line);
		}
	}

	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	for (i = 0; i < cmd_lines.GetLength(); ++i)
	{
		ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
		ZeroMemory(&si, sizeof(STARTUPINFO));
		si.cb = sizeof(STARTUPINFO);
		if (CreateProcess(NULL, cmd_lines[i], NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT, NULL, NULL, &si, &pi) == 0)
		{
			swprintf_s(buf, BUF_SZ, L"Failed to start editor (%d)", GetLastError());
			MessageBox(NULL, buf, L"TC Edit Redirector", MB_ICONERROR | MB_OK);
			return 1;
		}
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}

	// TODO: Branch view
	// TODO: Find results
	// TODO: Brief/full/custom/thumbs view modes
	// TODO: (?) Detect tree mode
	// TODO: Archives support (incl. waiting for process termination)
	// TODO: (?) Detect type from file under cursor, not from first selected (what if file under cursor is not selected?)
	// TODO: Environment vars in paths
	// TODO: Change editor's window placement (max, min)
	return 0;
}
