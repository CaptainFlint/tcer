#pragma once

#include "Array.h"

const int CLASS_BUF_SZ = 256;

typedef Array<HWND> ArrayHWND;

class WindowFinder
{
private:
	// Static class, no instances allowed
	WindowFinder(void) {};
	~WindowFinder(void) {};

	// Internal search instance data
	struct SearchData
	{
		// Variables for temporary storing search criteria
		bool find_all;
		HWND wnd_parent;
		const WCHAR* wnd_class;
		ULONG_PTR wnd_pid;

		// Found window(s)
		union
		{
			HWND res_wnd;
			ArrayHWND* res_wnds;
		};
	};

	// Window enumeration callback function.
	// lparam is a pointer to SearchData structure.
	static BOOL CALLBACK EnumWindowProc(HWND hwnd, LPARAM lparam);

public:
	// Find first window that is a direct child of 'parent', has class name 'wnd_class'
	// and belongs to process 'pid'.
	// If either of the arguments is NULL or 0 it is ignored.
	static HWND FindWnd(HWND parent, const WCHAR* wclass, ULONG_PTR pid);

	// Find all windows that are direct children of 'parent', have class name 'wnd_class'
	// and belong to process 'pid'.
	// If either of the arguments is NULL or 0 it is ignored.
	static ArrayHWND* FindWnds(HWND parent, const WCHAR* wclass, ULONG_PTR pid);
};
