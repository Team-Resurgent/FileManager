#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xipslib.h"

static unsigned char _PATCH[] = { 0x50,0x41,0x54,0x43,0x48 };
static unsigned char _EOF[] = { 0x45,0x4F,0x46 };

int CheckIPS(const char* ips) {
    FILE* f = fopen(ips, "rb");
    if (!f) return E_FOPEN_IPS;

    char header[5];
    if ((fread(header, 1, 5, f) != 5) || (memcmp(header, _PATCH, 5) != 0)) {
        fclose(f);
        return E_NOT_IPS;
    }

    for (;;) {
        char offset[3];
        if (fread(offset, 1, 3, f) != 3) {
            fclose(f);
            return E_BAD_IPS;
        }

        if (memcmp(offset, _EOF, 3) == 0) {
            fclose(f);
            return E_NO_ERROR;
        }

        char size[2];
        if (fread(size, 1, 2, f) != 2) {
            fclose(f);
            return E_BAD_IPS;
        }

        int s = ((unsigned char)size[0] << 8) | (unsigned char)size[1];

        if (fseek(f, s, SEEK_CUR) != 0) {
            fclose(f);
            return E_BAD_IPS;
        }
    }
}

int CreateBak(const char* src, bool ovr) {
    char dst[1024];

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
    while ((c = fread(buf, 1, sizeof(buf), fsrc))) {
        if (fwrite(buf, 1, c, fdst) != c) {
            free(buf);
            return E_FWRITE_DST;
        }
    }

    fclose(fsrc);
    fclose(fdst);

    free(buf);

    return E_NO_ERROR;
}

int RestoreBak(const char* src, bool ovr) {
    char dst[1024];

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
    
    if (rename(src, dst) != 0) return E_REN_ERROR;

    return E_NO_ERROR;
}

int ApplyIPS(const char* ips, const char* src) {
    int check = CheckIPS(ips);
    if (check != E_NO_ERROR) return check;

    FILE* fips = fopen(ips, "rb");
    if (!fips) return E_FOPEN_IPS;

    char header[5];
    if (fread(header, 1, 5, fips) != 5 || memcmp(header, _PATCH, 5) != 0) {
        fclose(fips);
        return E_NOT_IPS;
    }

    FILE* fsrc = fopen(src, "rb+");
    if (!fsrc) {
        fclose(fips);
        return E_FOPEN_SRC;
    }

    for (;;) {
        char offset[3];
        if (fread(offset, 1, 3, fips) != 3) {
            fclose(fips);
            fclose(fsrc);
            return E_BAD_IPS;
        }

        if (memcmp(offset, _EOF, 3) == 0) break;
        long o = ((unsigned char)offset[0] << 16) | ((unsigned char)offset[1] << 8) | (unsigned char)offset[2];

        char size[2];
        if (fread(size, 1, 2, fips) != 2) {
            fclose(fips);
            fclose(fsrc);
            return E_BAD_IPS;
        }

        int s = ((unsigned char)size[0] << 8) | (unsigned char)size[1];
        unsigned char* data = (unsigned char*)malloc(s * sizeof(char));
        if (data == NULL) {
            fclose(fips);
            fclose(fsrc);
            return E_OUT_OF_MEMORY;
        }

        if (fread(data, 1, s, fips) != s) {
            fclose(fips);
            fclose(fsrc);
            return E_BAD_IPS;
        }
        if (fseek(fsrc, o, SEEK_SET) != 0) {
            fclose(fips);
            fclose(fsrc);
            return E_FSEEK_SRC;
        }
        if (fwrite(data, 1, s, fsrc) != s) {
            fclose(fips);
            fclose(fsrc);
            return E_FWRITE_DST;
        }

        free(data);
    }

    fclose(fips);
    fclose(fsrc);

    return E_NO_ERROR;
}
