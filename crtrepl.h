#pragma once

#include <emmintrin.h>

// zeromem: template function for zeroing the memory area
// Requires 16-byte aligning in SSE mode and 4-byte aligning in non-SSE
#if (_M_IX86_FP == 2) || defined _M_X64
// All 64-bit processors have SSE2 support
template <class T>
__forceinline void zeromem(T* dst)
{
	zeromem_sz<(sizeof(T) + 15) / 16>(dst);
}

template<size_t sz>
__forceinline void zeromem_sz(void* dst)
{
    _mm_store_si128((__m128i*)dst, _mm_setzero_si128());
    zeromem_sz<sz - 1>((BYTE*)dst + 16);
}

template<>
__forceinline void zeromem_sz<0>(void* dst)
{
	UNREFERENCED_PARAMETER(dst);
}
#elif (_M_IX86_FP == 0)
template <class T>
__forceinline void zeromem(T* dst)
{
	__asm
	{
		mov edi, dst
		mov ecx, SIZE T / 4
		xor eax, eax
		rep stosd
	}
}
#else
#error Unsupported SSE option.
#endif

// Class for wrapping into blocks with size multiple of 16-byte
#pragma pack(1)
template <class T>
class AlignWrapper
{
public:
	T data;
	BYTE align[16 - sizeof(T) % 16];
	operator T*() { return &data; }
};
#pragma pack()


// Allocation replacement functions
extern HANDLE ProcessHeap;

__declspec(restrict) __declspec(noalias) __forceinline void* __cdecl malloc(size_t n)
{
    return HeapAlloc(ProcessHeap, 0, n);
}

__declspec(restrict) __declspec(noalias) __forceinline void* __cdecl realloc(void* p, size_t n)
{
    if (p == NULL)
		return malloc(n);
    return HeapReAlloc(ProcessHeap, 0, p, n);
}

__declspec(noalias) __forceinline void __cdecl free(void* p)
{
    if (p == NULL)
		return;
    HeapFree(ProcessHeap, 0, p);
}

__forceinline void* __cdecl operator new(size_t n)
{
    return HeapAlloc(ProcessHeap, 0, n);
}

__forceinline void* __cdecl operator new[](size_t n)
{
    return HeapAlloc(ProcessHeap, 0, n);
}

__forceinline void __cdecl operator delete(void* p)
{
    if (p == NULL)
		return;
    HeapFree(ProcessHeap, 0, p);
}

__forceinline void __cdecl operator delete[](void* p)
{
    if (p == NULL)
		return;
    HeapFree(ProcessHeap, 0, p);
}

// Copy a string and return its length
size_t wcscpylen_s(WCHAR *strDestination, size_t numberOfElements, const WCHAR *strSource);

// Scan a string for the last occurrence of a character.
// Return index of the next position or 0 if no such character.
size_t wcsrchr_pos(const WCHAR* str, size_t start_pos, WCHAR c);

// [lib] Searches for the first occurrence of any of 'control' characters in a string
size_t wcscspn(const wchar_t* string, const wchar_t* control);

// [lib] Copies bytes between buffers
errno_t memcpy_s(void* dest, size_t numberOfBytes, const void* src, size_t count);

// [lib] Compare the specified number of characters of two strings
int wcsncmp(const WCHAR* string1, const WCHAR* string2, size_t count);

// [lib] Compare the specified number of characters of two strings, case ignored
int _wcsnicmp(const WCHAR* string1, const WCHAR* string2, size_t count);

// Concatenates the two strings and one argument (limited swprintf replacement)
template <class T>
int swprintf_s(WCHAR* buffer, size_t sizeOfBuffer, const WCHAR* str1, T arg1, const WCHAR* str2);

// Special version for swprintf_s<ULONG> with hex format instead of decimal
int swprintf_s_hex(WCHAR* buffer, size_t sizeOfBuffer, const WCHAR* str1, ULONG arg1, const WCHAR* str2);
