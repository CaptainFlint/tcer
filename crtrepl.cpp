#include "stdafx.h"
#include "crtrepl.h"


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

// [lib] Searches for the first occurrence of any of 'control' characters in a string
size_t wcscspn(const wchar_t* string, const wchar_t* control)
{
	const wchar_t* str = string;
	const wchar_t* wcset;

	// 1st char in control string stops search
	while (*str)
	{
		for (wcset = control; *wcset; ++wcset)
		{
			if (*wcset == *str)
			{
				return (size_t)(str - string);
			}
		}
		++str;
	}
	return (size_t)(str - string);
}

// [lib] Copies bytes between buffers
errno_t memcpy_s(void* dest, size_t numberOfElements, const void* src, size_t count)
{
	if (count > numberOfElements)
		count = numberOfElements;
	while (count--)
	{
		*(BYTE*)dest = *(BYTE*)src;
		dest = (BYTE*)dest + 1;
		src = (BYTE*)src + 1;
	}
	return 0;
}

// [lib] Compare the specified number of characters of two strings
int wcsncmp(const WCHAR* string1, const WCHAR* string2, size_t count)
{
	if (!count)
		return 0;
	while ((--count) && *string1 && (*string1 == *string2))
	{
		++string1;
		++string2;
	}

	return (int)(*string1 - *string2);
}

#define __ascii_towlower(c) ( (((c) >= L'A') && ((c) <= L'Z')) ? ((c) | 0x20) : (c) )

// [lib] Compare the specified number of characters of two strings, case ignored
int _wcsnicmp(const WCHAR* string1, const WCHAR* string2, size_t count)
{
	if (!count)
		return 0;

	wchar_t f, l;

	do {
		f = __ascii_towlower(*string1);
		l = __ascii_towlower(*string2);
		++string1;
		++string2;
	} while ( (--count) && f && (f == l) );

	return (int)(f - l);
}

// Concatenates the two strings and one argument (limited swprintf replacement)
template <>
int swprintf_s(WCHAR* buffer, size_t sizeOfBuffer, const WCHAR* str1, WCHAR* arg1, const WCHAR* str2)
{
	size_t len = wcscpylen_s(buffer, sizeOfBuffer, str1);
	len += wcscpylen_s(buffer + len, sizeOfBuffer - len, arg1);
	len += wcscpylen_s(buffer + len, sizeOfBuffer - len, str2);
	return (int)len;
}

template <>
int swprintf_s(WCHAR* buffer, size_t sizeOfBuffer, const WCHAR* str1, size_t arg1, const WCHAR* str2)
{
	WCHAR val[21];  // Maximum length of 64-bit value is 20 characters
	WCHAR* res = val + 20;
	*--res = L'\0';
	if (arg1 == 0)
		*--res = L'0';
	else
		do
		{
			*--res = L'0' + (arg1 % 10L);
			arg1 /= 10L;
		} while (arg1 != 0);
 	return swprintf_s(buffer, sizeOfBuffer, str1, res, str2);
}

template <>
int swprintf_s(WCHAR* buffer, size_t sizeOfBuffer, const WCHAR* str1, ULONG arg1, const WCHAR* str2)
{
	return swprintf_s(buffer, sizeOfBuffer, str1, (size_t)arg1, str2);
}

// Special version for swprintf_s<ULONG> with hex format instead of decimal
int swprintf_s_hex(WCHAR* buffer, size_t sizeOfBuffer, const WCHAR* str1, ULONG arg1, const WCHAR* str2)
{
	WCHAR val[17] = L"0000000000000000";    // Maximum length of 64-bit value is 16 characters
	WCHAR* res = val + 16;
	if (arg1 == 0)
		res -= 8;
	else
	{
		do
		{
			BYTE d = arg1 % 16L;
			*--res = ((d < 10) ? (L'0' + d) : (L'a' - 10 + d));
			arg1 /= 16L;
		} while (arg1 != 0);
		res = val + ((res - val) / 8) * 8;
	}
 	return swprintf_s(buffer, sizeOfBuffer, str1, res, str2);
}
