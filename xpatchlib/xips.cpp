#include "xips.h"
#include <String.h>
#include <StdLib.h>

int CheckIPS(FILE* fips) {
    if (fseek(fips, 0, SEEK_SET) != 0) return E_FSEEK_PATCH;

    char header[5];
    if ((fread(header, 1, 5, fips) != 5) || (memcmp(header, "PATCH", 5) != 0)) return E_NOT_IPS;

    for (;;) {
        char offset[3];
        if (fread(offset, 1, 3, fips) != 3) return E_BAD_PATCH;

        if (memcmp(offset, "EOF", 3) == 0) {
            fseek(fips, 0, SEEK_SET);
            return E_NO_ERROR;
        }

        char size[2];
        if (fread(size, 1, 2, fips) != 2) return E_BAD_PATCH;

        int s = ((unsigned char)size[0] << 8) | (unsigned char)size[1];

        //Is it RLE? Skip 2-byte count + 1-byte value
        if (s == 0) {
            if (fseek(fips, 3, SEEK_CUR) != 0) return E_BAD_PATCH;
        }
        else {
            if (fseek(fips, s, SEEK_CUR) != 0) return E_BAD_PATCH;
        }
    }

    return E_ERROR;
}

int ApplyIPS(const char* ips, const char* dst) {
    FILE* fips = fopen(ips, "rb");
    if (!fips) return E_FOPEN_PATCH;

    int check = CheckIPS(fips);
    if (check != E_NO_ERROR) {
        fclose(fips);
        return check;
    }

    //Double check
    char header[5];
    if (fread(header, 1, 5, fips) != 5 || memcmp(header, "PATCH", 5) != 0) {
        fclose(fips);
        return E_NOT_IPS;
    }

    FILE* fdst = fopen(dst, "rb+");
    if (!fdst) {
        fclose(fips);
        return E_FOPEN_DST;
    }

    for (;;) {
        char offset[3];
        fread(offset, 1, 3, fips);

        if (memcmp(offset, "EOF", 3) == 0) break;
        long o = ((unsigned char)offset[0] << 16) | ((unsigned char)offset[1] << 8) | (unsigned char)offset[2];

        char size[2];
        fread(size, 1, 2, fips);

        int s = ((unsigned char)size[0] << 8) | (unsigned char)size[1];

        //Is it RLE?
        if (s == 0) {
            char count[2];
            fread(count, 1, 2, fips);
            int c = (count[0] << 8) | count[1];

            char value;
            fread(&value, 1, 1, fips);

            if (fseek(fdst, o, SEEK_SET) != 0) {
                fclose(fips);
                fclose(fdst);
                return E_FSEEK_DST;
            }

            for (int i = 0; i < c; i++) {
                if (fputc(value, fdst) == EOF) {
                    fclose(fips);
                    fclose(fdst);
                    return E_FWRITE_DST;
                }
            }

            continue;
        }

        unsigned char* data = (unsigned char*)malloc(s * sizeof(char));
        if (data == NULL) {
            fclose(fips);
            fclose(fdst);
            return E_OUT_OF_MEMORY;
        }

        fread(data, 1, s, fips);

        if (fseek(fdst, o, SEEK_SET) != 0) {
            fclose(fips);
            fclose(fdst);
            return E_FSEEK_DST;
        }
        if (fwrite(data, 1, s, fdst) != s) {
            fclose(fips);
            fclose(fdst);
            return E_FWRITE_DST;
        }

        free(data);
    }

    fclose(fips);
    fclose(fdst);

    return E_NO_ERROR;
}
