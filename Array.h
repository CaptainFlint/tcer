#pragma once

template <class T>
class Array
{
protected:
	size_t alloc_sz;
	size_t length;
	T* data;

public:
	Array(size_t len = 0);
	~Array();

	size_t GetLength() const { return length; }
	const T& operator [](size_t idx) const { _ASSERT((idx < length) && (data != NULL)); return data[idx]; }
	T& operator [](size_t idx) { _ASSERT((idx < length) && (data != NULL)); return data[idx]; }
	bool Append(T elem);
};

template <class T> Array<T>::Array(size_t len)
{
	alloc_sz = len;
	length = 0;
	if (len != 0)
		data = new T[len];
	else
		data = NULL;
}

template <class T> Array<T>::~Array()
{
	if (data != NULL)
		delete[] data;
}

template <class T> bool Array<T>::Append(T elem)
{
	if ((length == alloc_sz) || (alloc_sz == 0) || (data == NULL))
	{
		size_t new_alloc_sz = (alloc_sz == 0) ? 32 : alloc_sz * 2;
		T* new_data = new T[new_alloc_sz];
		if (new_data == NULL)
			return false;
		if (length != 0)
			memcpy_s(new_data, new_alloc_sz * sizeof(T), data, length * sizeof(T));
		if (data != NULL)
			delete[] data;
		data = new_data;
		alloc_sz = new_alloc_sz;
	}
	data[length++] = elem;
	return true;
}


template <class T>
class ArrayPtr : public Array<T*>
{
public:
	ArrayPtr(size_t len = 0) : Array<T*>(len) {}
	~ArrayPtr();
};

template <class T> ArrayPtr<T>::~ArrayPtr()
{
	if (data != NULL)
	{
		for (size_t i = 0; i < length; ++i)
			delete[] data[i];
	}
}
