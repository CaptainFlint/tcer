#include "stdafx.h"
#include "WindowFinder.h"

// Window enumeration callback function
// lparam is a pointer to SearchData structure
BOOL CALLBACK WindowFinder::EnumWindowProc(HWND hwnd, LPARAM lparam)
{
	_ASSERT(hwnd != NULL);
	_ASSERT(lparam != NULL);
	SearchData* data = (SearchData*)lparam;

	// Check parent window
	if (data->wnd_parent != NULL)
	{
		if (GetAncestor(hwnd, GA_PARENT) != data->wnd_parent)
			return TRUE;
	}
	// Check PID
	if (data->wnd_pid != 0)
	{
		DWORD pid;
		GetWindowThreadProcessId(hwnd, &pid);
		if (pid != data->wnd_pid)
			return TRUE;
	}
	// Check class name
	if (data->wnd_class != NULL)
	{
		WCHAR class_name[CLASS_BUF_SZ];
		if (GetClassName(hwnd, class_name, CLASS_BUF_SZ) == 0)
			return TRUE;
		if (wcsncmp(class_name, data->wnd_class, CLASS_BUF_SZ) != 0)
			return TRUE;
	}

	if (data->find_all)
	{
		data->res_wnds->Append(hwnd);
		return TRUE;
	}
	else
	{
		data->res_wnd = hwnd;
		return FALSE;
	}
}


// Find window that is a direct child of 'parent', has class name 'wnd_class'
// and belongs to process 'pid'.
// If either of the arguments is NULL or 0 it is ignored.
HWND WindowFinder::FindWnd(HWND parent, bool direct_child, const WCHAR* wclass, ULONG_PTR pid)
{
	SearchData data;
	data.find_all = false;
	data.wnd_parent = (direct_child ? parent : NULL);
	data.wnd_class = wclass;
	data.wnd_pid = pid;
	data.res_wnd = NULL;
	EnumChildWindows(parent, EnumWindowProc, (LPARAM)&data);
	return data.res_wnd;
}

// Find all windows that are direct children of 'parent', have class name 'wnd_class'
// and belong to process 'pid'.
// If either of the arguments is NULL or 0 it is ignored.
ArrayHWND* WindowFinder::FindWnds(HWND parent, bool direct_child, const WCHAR* wclass, ULONG_PTR pid)
{
	SearchData data;
	data.find_all = true;
	data.wnd_parent = (direct_child ? parent : NULL);
	data.wnd_class = wclass;
	data.wnd_pid = pid;
	data.res_wnds = new ArrayHWND;
	EnumChildWindows(parent, EnumWindowProc, (LPARAM)&data);
	return data.res_wnds;
}
