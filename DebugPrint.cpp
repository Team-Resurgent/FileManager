#include <xtl.h>      // for OutputDebugStringA on OG Xbox
#include <stdarg.h>
#include <stdio.h>

extern "C" void MyDebugPrint(const char* fmt, ...)
{
    CHAR buf[1024];

    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);

    if (n < 0) n = (int)sizeof(buf) - 1;
    if (n < 0) n = 0;
    buf[n] = '\0';

    // Strip any trailing CR/LF the caller might have added.
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = '\0';
    }

    // Append exactly one LF (no CR).
    if (n <= (int)sizeof(buf) - 2) {
        buf[n++] = '\n';
        buf[n]   = '\0';
    } else {
        buf[sizeof(buf) - 2] = '\n';
        buf[sizeof(buf) - 1] = '\0';
    }

    OutputDebugStringA(buf);
}
