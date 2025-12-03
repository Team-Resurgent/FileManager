#include "xpatchlib.h"
#include <StdLib.h>
#include <String.h>

int CreateBak(const char* src, bool ovr, int (*Upd)(unsigned long long)) {
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

    int rb;
    while ((rb = fread(buf, 1, 65536, fsrc))) {
        int wb = fwrite(buf, 1, rb, fdst);
        if (wb != rb) {
            free(buf);

            fclose(fsrc);
            fclose(fdst);

            return E_FWRITE_DST;
        }

        if (Upd) {
            if (Upd(wb) != 0) {
                break;
            }
        }
    }

    free(buf);

    fclose(fsrc);
    fclose(fdst);

    return E_NO_ERROR;
}

int RestoreBak(const char* src, bool ovr) {
    char dst[512];

    strcpy(dst, src);
    dst[strlen(src) - 4] = '\0';

    FILE* fdst = fopen(dst, "rb");
    bool exists = false;
    if (fdst) {
        exists = true;
        fclose(fdst);
    }

    if (!ovr && exists) return E_CANNOT_OVR;

    FILE* fsrc = fopen(src, "rb");
    if (!fsrc) return E_FOPEN_SRC;
    fclose(fsrc);

    if (exists) remove(dst);
    
    if (rename(src, dst) != 0) return E_ERROR;

    return E_NO_ERROR;
}

int ReadVarInt(FILE* fp, unsigned int* ui) {
    unsigned int r = 0;
    unsigned int s = 0;

    for (;;) {
        int b = fgetc(fp);
        if (b == EOF) return E_BAD_PATCH;

        r |= (unsigned int)(b & 0x7F) << s;
        if ((b & 0x80) == 0) {
            *ui = r;
            return E_NO_ERROR;
        }

        s += 7;
        if (s >= 32) return E_BAD_PATCH;
    }
}