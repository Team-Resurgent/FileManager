// see: 

#if defined __cplusplus_cli
#define CALEETYPE __clrcall
#else
#define CALEETYPE __stdcall
#endif
#define __RELIABILITY_CONTRACT
#define SECURITYCRITICAL_ATTRIBUTE
#define ASSERT_UNMANAGED_CODE_ATTRIBUTE

#if defined __cplusplus_cli
#define CALLTYPE __clrcall 
#elif defined _M_IX86
#define CALLTYPE __thiscall
#else
#define CALLTYPE __stdcall
#endif

__RELIABILITY_CONTRACT
void CALEETYPE __ArrayUnwind(
	void* ptr,                // Pointer to array to destruct
	size_t      size,               // Size of each element (including padding)
	int         count,              // Number of elements in the array
	void(CALLTYPE* pDtor)(void*)    // The destructor to call
);

__RELIABILITY_CONTRACT
inline void CALEETYPE __ehvec_ctor(
	void* ptr,                // Pointer to array to destruct
	size_t      size,               // Size of each element (including padding)
	//  int         count,              // Number of elements in the array
	size_t      count,              // Number of elements in the array
	void(CALLTYPE* pCtor)(void*),   // Constructor to call
	void(CALLTYPE* pDtor)(void*)    // Destructor to call should exception be thrown
) {
	size_t i = 0;      // Count of elements constructed
	int success = 0;

	__try
	{
		// Construct the elements of the array
		for (; i < count; i++)
		{
			(*pCtor)(ptr);
			ptr = (char*)ptr + size;
		}
		success = 1;
	}
	__finally
	{
		if (!success)
			__ArrayUnwind(ptr, size, (int)i, pDtor);
	}
}

__RELIABILITY_CONTRACT
SECURITYCRITICAL_ATTRIBUTE
inline void CALEETYPE __ehvec_dtor(
	void* ptr,                // Pointer to array to destruct
	size_t      size,               // Size of each element (including padding)
	//  int         count,              // Number of elements in the array
	size_t      count,              // Number of elements in the array
	void(CALLTYPE* pDtor)(void*)    // The destructor to call
) {

	int success = 0;

	// Advance pointer past end of array
	ptr = (char*)ptr + size * count;

	__try
	{
		// Destruct elements
		while (count-- > 0)
		{
			ptr = (char*)ptr - size;
			(*pDtor)(ptr);
		}
		success = 1;
	}
	__finally
	{
		if (!success)
			__ArrayUnwind(ptr, size, (int)count, pDtor);
	}
}