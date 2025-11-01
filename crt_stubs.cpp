// crt_stubs.cpp - Missing C++ runtime function implementations for Xbox SDK
// This file provides stub implementations for C++ runtime functions that
// Visual Studio 2022's STL expects but are not available in the Xbox runtime libraries

#include <xtl.h>
#include <exception>
#include <cstring>
#include <new>

// Sized delete operator (C++14 feature) - must NOT be in extern "C" block
// The Xbox runtime only has the unsized delete, so we forward to it
void operator delete(void* ptr, unsigned int size) noexcept
{
    // Call the standard delete that the Xbox runtime provides
    ::operator delete(ptr);
}

// Forward declarations to ensure proper linkage
extern "C" void __invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t);
extern "C" void __invalid_parameter_noinfo_noreturn();
extern "C" void ___std_exception_copy(const void*, void*);
extern "C" void ___std_exception_destroy(void*);
extern "C" void ___std_terminate();
extern "C" int __CxxFrameHandler3(void*, void*, void*, void*);

extern "C" {

// Invalid parameter handler - called when invalid parameters are passed to CRT functions
// For Xbox, we'll implement a simple version that does nothing (no validation)
// Note: This needs to match the exact signature expected by the MSVC runtime
void __invalid_parameter(
    const wchar_t* expression,
    const wchar_t* function,
    const wchar_t* file,
    unsigned int line,
    uintptr_t reserved)
{
    // In debug builds, you might want to break here or log
    // For now, just return (no parameter validation on Xbox)
}

// Simplified invalid parameter handler used in Release builds
void __invalid_parameter_noinfo_noreturn()
{
    // No-op for Xbox
}

// Exception handling support functions
void ___std_exception_copy(const void* from, void* to)
{
    // Simple memcpy for exception objects
    if (from && to)
    {
        memcpy(to, from, sizeof(std::exception));
    }
}

void ___std_exception_destroy(void* exception_object)
{
    // Nothing to destroy for basic exceptions
    // The Xbox runtime doesn't need complex cleanup
}

// C++ exception frame handler - needed for exception handling
// This is a complex function, but for Xbox we can provide a minimal stub
int __CxxFrameHandler3(void*, void*, void*, void*)
{
    // Minimal stub - return value indicates exception not handled
    return 0;
}

// std::terminate implementation - called when exception handling fails
void ___std_terminate()
{
    // On Xbox, we can't properly handle terminate
    // Just infinite loop or halt
    while (1) {}
}

} // extern "C"

// Create linker aliases to map the decorated symbol names to the expected names
// The compiled symbols have extra underscores due to cdecl calling convention
#pragma comment(linker, "/alternatename:__invalid_parameter=___invalid_parameter")
#pragma comment(linker, "/alternatename:__invalid_parameter_noinfo_noreturn=___invalid_parameter_noinfo_noreturn")
#pragma comment(linker, "/alternatename:___std_exception_copy=____std_exception_copy")
#pragma comment(linker, "/alternatename:___std_exception_destroy=____std_exception_destroy")
#pragma comment(linker, "/alternatename:___std_terminate=____std_terminate")

// Exception throwing helper for length errors - must be in std namespace
namespace std {

void _Xlength_error(const char* message)
{
    // On Xbox, we can't throw exceptions properly, so we'll just terminate
    // In a real implementation, you might want to handle this differently
    // For now, do nothing or implement basic error handling
}

void _Xout_of_range(const char* message)
{
    // Out of range error - similar handling to length error
    // For now, do nothing or implement basic error handling
}

// Thread-safe locking mechanism (std::_Lockit)
// For single-threaded Xbox applications, these can be no-ops
_Lockit::_Lockit(int kind) noexcept
{
    // No-op for single-threaded or if threading isn't needed
    // On Xbox, most games run single-threaded or manage their own threading
}

_Lockit::~_Lockit() noexcept
{
    // No-op destructor
}

// System error handling functions
char const* _Syserror_map(int errcode)
{
    // Simple error message for Xbox
    return "System error";
}

int _Winerror_map(int errcode)
{
    // Map Windows error to generic error code
    return errcode;
}

} // namespace std

// Additional C runtime functions for error/filesystem support
extern "C" {

// System error message allocation/deallocation (using __stdcall for correct decoration)
char* __stdcall __std_system_error_allocate_message(unsigned long error_code, char const* message)
{
    // Simple allocation - just return a static string for Xbox
    static char error_buffer[256];
    if (message) {
        strncpy(error_buffer, message, sizeof(error_buffer) - 1);
        error_buffer[sizeof(error_buffer) - 1] = '\0';
    } else {
        strcpy(error_buffer, "System error");
    }
    return error_buffer;
}

void __stdcall __std_system_error_deallocate_message(char* message)
{
    // No-op since we use static buffer
}

// Filesystem code page function
unsigned int __stdcall __std_fs_code_page()
{
    // Return UTF-8 code page (65001) or ANSI (0)
    return 0; // ANSI for Xbox
}

// Filesystem string conversion - narrow to wide
int __stdcall __std_fs_convert_narrow_to_wide(
    unsigned int code_page,
    const char* narrow_str,
    int narrow_len,
    wchar_t* wide_str,
    int wide_len)
{
    // Simple narrow to wide conversion for Xbox
    if (wide_str == nullptr || wide_len == 0) {
        // Return required size
        return narrow_len + 1;
    }
    
    int count = 0;
    for (int i = 0; i < narrow_len && count < wide_len - 1; ++i, ++count) {
        wide_str[count] = static_cast<wchar_t>(narrow_str[i]);
    }
    wide_str[count] = L'\0';
    return count;
}

// Filesystem string conversion - wide to narrow
int __stdcall __std_fs_convert_wide_to_narrow(
    unsigned int code_page,
    const wchar_t* wide_str,
    int wide_len,
    char* narrow_str,
    int narrow_len)
{
    // Simple wide to narrow conversion for Xbox
    if (narrow_str == nullptr || narrow_len == 0) {
        // Return required size
        return wide_len + 1;
    }
    
    int count = 0;
    for (int i = 0; i < wide_len && count < narrow_len - 1; ++i, ++count) {
        narrow_str[count] = static_cast<char>(wide_str[i]);
    }
    narrow_str[count] = '\0';
    return count;
}

// Reverse algorithm helper for trivially swappable 4-byte types
void __std_reverse_trivially_swappable_4(void* first, void* last)
{
    // Reverse elements that are 4 bytes each (like int, float, etc.)
    char* begin = static_cast<char*>(first);
    char* end = static_cast<char*>(last);
    
    if (end <= begin) return;
    
    end -= 4; // Move to last element
    
    while (begin < end) {
        // Swap 4 bytes
        char temp[4];
        temp[0] = begin[0]; temp[1] = begin[1]; temp[2] = begin[2]; temp[3] = begin[3];
        begin[0] = end[0]; begin[1] = end[1]; begin[2] = end[2]; begin[3] = end[3];
        end[0] = temp[0]; end[1] = temp[1]; end[2] = temp[2]; end[3] = temp[3];
        
        begin += 4;
        end -= 4;
    }
}

} // extern "C"

