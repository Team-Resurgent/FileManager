#include <exception>

namespace std {

    // This is a function pointer the STL checks before throwing.
    // We define it so the linker can find it.
    void(__cdecl* _Raise_handler)(const exception&) = nullptr;

    // Stub C++ exception frame handler for RXDK builds.
    // This matches the real signature so the runtime doesn't blow the stack.

    extern "C" {

        // forward declarations only – we don't need the real structs
        struct _EXCEPTION_RECORD;
        struct _CONTEXT;
        struct _DISPATCHER_CONTEXT;

        // Copied from <excpt.h> (simplified)
        enum EXCEPTION_DISPOSITION {
            ExceptionContinueExecution,
            ExceptionContinueSearch,
            ExceptionNestedException,
            ExceptionCollidedUnwind
        };

        EXCEPTION_DISPOSITION __cdecl __CxxFrameHandler3(
            _EXCEPTION_RECORD* /*pExcept*/,
            void*              /*pFrame*/,
            _CONTEXT*          /*pContext*/,
            _DISPATCHER_CONTEXT* /*pDispatcher*/
        )
        {
            // We don't try to handle C++ exceptions, just tell the OS to keep looking.
            return ExceptionContinueSearch;
        }

    } // extern "C"

    // This is called whenever the STL wants to throw a std::exception-derived error.
    // Make it never return. You can customize what happens here.dd
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
