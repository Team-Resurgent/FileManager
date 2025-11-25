#include <exception>




// We are in the Xbox / RXDK build; exceptions are effectively unsupported here.
// Provide minimal stubs so the STL can link.

namespace std {

    // This is a function pointer the STL checks before throwing.
    // We define it so the linker can find it.
    void(__cdecl* _Raise_handler)(const exception&) = nullptr;

    // This is called whenever the STL wants to throw a std::exception-derived error.
    // Make it never return. You can customize what happens here.
    void _Throw(const exception& e)
    {
        // If someone installed a custom handler, call it.
        if (_Raise_handler) {
            _Raise_handler(e);
        }

        // Your own reporting (optional)
        // e.what() is usually a narrow string; but we can't safely call printf here
        // without dragging in more CRT, so keep it minimal.
        // If you have MyDebugPrint, you could do:
        // MyDebugPrint("std::_Throw called: %s\n", e.what());

        // Break into a debugger if attached
#ifdef _MSC_VER
        __debugbreak();
#endif

        // And never return
        for (;;)
            ;
    }

} // namespace std
