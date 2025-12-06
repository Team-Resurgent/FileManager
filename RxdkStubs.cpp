#include <exception>

namespace std {

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
            _EXCEPTION_RECORD*   /*pExcept*/,
            void*                /*pFrame*/,
            _CONTEXT*            /*pContext*/,
            _DISPATCHER_CONTEXT* /*pDispatcher*/
        ) {
            // We don't try to handle C++ exceptions, just tell the OS to keep looking.
            return ExceptionContinueSearch;
        }

    } // extern "C"

} // namespace std
