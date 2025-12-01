#include "xipslibUtil.h"

// ------------------------------------------------------------------
// CreateBak function with added prog (src-dev)
// ------------------------------------------------------------------
int CreateBakWithProgress(const char* src, bool ovr, ULONGLONG& inoutBytesDone, ULONGLONG totalBytes) {
    char dst[512];
    strcpy(dst, src);
    strcat(dst, ".bak");

    if (!ovr) {
        FILE* fdst = fopen(dst, "rb");
        bool exists = false;
        if (fdst) {
            exists = true;
            fclose(fdst);
        }
        if (exists) return E_CANNOT_OVR;
    }

    FILE* fsrc = fopen(src, "rb");
    if (!fsrc) return E_FOPEN_SRC;

   FILE* fdst = fopen(dst, "wb");
    if (!fdst) {
        fclose(fsrc);
        return E_FOPEN_DST;
    }

    char* buf = (char*)malloc(sizeof(char)*65536);
    if (buf == NULL) return E_OUT_OF_MEMORY;

    int c;
    while ((c = fread(buf, 1, 65536, fsrc))) {
        int rb = fwrite(buf, 1, c, fdst);
        if (rb != c) {
            free(buf);

            fclose(fsrc);
            fclose(fdst);

            return E_FWRITE_DST;
        }
        inoutBytesDone += rb;
        if (CopyProgress::g_copyProgFn) {
            if (!CopyProgress::g_copyProgFn(inoutBytesDone, totalBytes, NULL, CopyProgress::g_copyProgUser)) {
                break; // canceled
            }
        }
    }

    free(buf);

    fclose(fsrc);
    fclose(fdst);

    return E_NO_ERROR;
}