#include "unzipLIBUtil.h"

char* unZipBuffer = NULL;

// ------------------------------------------------------------------
// unzipLIB filesystem callbacks
// ------------------------------------------------------------------
void* ZipFile_Open(const char* filename, int32_t* size) {
    FILE* f = fopen(filename, "rb");
    fseek(f, 0L, SEEK_END);
    *size = ftell(f);
    rewind(f);
    return (void*)f;
}

void ZipFile_Close(void* p) {
    ZIPFILE* pzf = (ZIPFILE*)p;
    FILE* f = (FILE*)pzf->fHandle;

    if (f) {
        fclose(f);
    }
}

int32_t ZipFile_Read(void* p, uint8_t* buffer, int32_t length) {
    ZIPFILE* pzf = (ZIPFILE*)p;
    FILE* f = (FILE*)pzf->fHandle;
    return fread(buffer, 1, length, f);
}

int32_t ZipFile_Seek(void* p, int32_t position, int iType) {
    ZIPFILE* pzf = (ZIPFILE*)p;
    FILE* f = (FILE*)pzf->fHandle;
    long l = 0;

    if (iType == SEEK_SET) {
        return fseek(f, position, SEEK_SET);
    }
    else if (iType == SEEK_END) {
        return fseek(f, position + pzf->iSize, SEEK_END);
    }
    else { // SEEK_CUR
        l = ftell(f);
    }

    return fseek(f, l + position, SEEK_CUR);
}

// ------------------------------------------------------------------
// ExtractCurrentFileWithProgress & helper functions (CrunchBite)
// ------------------------------------------------------------------
char* strrepl(char* Str, size_t BufSiz, char* OldStr, char* NewStr) {
    int OldLen, NewLen;
    char* p, * q;

    if (NULL == (p = strstr(Str, OldStr))) {
        return Str;
    }

    OldLen = strlen(OldStr);
    NewLen = strlen(NewStr);

    if ((strlen(Str) + NewLen - OldLen + 1) > BufSiz) {
        return NULL;
    }

    memmove(q = p + NewLen, p + OldLen, strlen(p + OldLen) + 1);
    memcpy(p, NewStr, NewLen);
    return q;
}

char* strreplall(char* Str, size_t BufSiz, char* OldStr, char* NewStr) {
    char* ret;
    size_t i;

    for (i = 0; i < BufSiz; i++) {
        ret = strrepl(Str, BufSiz, OldStr, NewStr);
    }

    return ret;
}

int ExtractCurrentFileWithProgress(UNZIP* zip, const char* dst, bool overwrite) {
    char pathSep[2];    

    // Check if the destination folder ends with an '\\'
    if (*(dst + strlen(dst) - 1) == '\\') *pathSep = '\0';
    else strcpy(pathSep, "\\");

    char buf[512];
    unz_file_info fi;
    int rc;

    // Get information about the current file
    rc = zip->getFileInfo(&fi, buf, 512, NULL, 0, NULL, 0);
    if (rc != UNZ_OK) return rc;

    // Substitute '/' with '\'
    strreplall(buf, 512, "/", "\\");

    char fileName_InZip[512];

    // Don't include the drive letter (if present) and the leading '\' (if present)
    if (buf[1] == ':' && buf[2] == '\\') strcpy(fileName_InZip, (buf + 3));
    else if (buf[1] == ':') strcpy(fileName_InZip, (buf + 2));
    else if (buf[0] == '\\') strcpy(fileName_InZip, (buf + 1));
    else strcpy(fileName_InZip, buf);
    
    char* fileName_WithOutPath;
    char* pos;

    pos = (char*)fileName_WithOutPath = (char*)fileName_InZip;

    // Find filename part (without the path)
    while ((*pos) != '\0') {
        if (((*pos) == '/') || ((*pos) == '\\')) fileName_WithOutPath = (char*)(pos + 1);
        pos++;
    }

    // Is this a folder?
    if ((*fileName_WithOutPath) == '\0') {
        sprintf(buf, "%s%s%s", dst, pathSep, fileName_InZip);
        strreplall(buf, 512, "/", "\\");
        CreateDirectory(buf, NULL);
        return UNZ_OK;
    }

    // Do we have a buffer?
    if (unZipBuffer == NULL) {
        if ((unZipBuffer = (char*)malloc(65536)) == NULL) return UNZ_INTERNALERROR;
    }

    char* writeFileName = fileName_InZip;

    // Open the current file
    if ((rc = zip->openCurrentFile()) != UNZ_OK) return rc;

    // Compose file name
    sprintf(buf, "%s%s%s", dst, pathSep, writeFileName);

    HANDLE file;
    bool skip = false;

    // Check if file exists?
    if (!overwrite && rc == UNZ_OK) {
        file = CreateFile(buf, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != (HANDLE)INVALID_HANDLE_VALUE) {
            CloseHandle(file);
            skip = true;
        }
    }

    char hold;

    //Create the file and directories
    if (!skip && rc == UNZ_OK) {
        file = CreateFile(buf, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

        if (file == (HANDLE)INVALID_HANDLE_VALUE) {
            if (fileName_WithOutPath != (char*)fileName_InZip) {
                hold = *(fileName_WithOutPath - 1);
                *(fileName_WithOutPath - 1) = '\0';
                sprintf(buf, "%s%s%s", overwrite, pathSep, writeFileName);
                CreateDirectory(buf, NULL);
                *(fileName_WithOutPath - 1) = hold;
                sprintf(buf, "%s%s%s", overwrite, pathSep, writeFileName);
                file = CreateFile(buf, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            }
        }

        if (file == (HANDLE)INVALID_HANDLE_VALUE) return UNZ_ERRNO;
    }

    DWORD bytesWritten = 0;

    // Read and write the current file
    if (file != (HANDLE)INVALID_HANDLE_VALUE) {
        do {
            if ((rc = zip->readCurrentFile((uint8_t*)unZipBuffer, 65536)) < 0) break;

            if (rc > 0) {
                if (!WriteFile(file, unZipBuffer, (DWORD)rc, &bytesWritten, NULL)) {
                    rc = UNZ_ERRNO;
                    break;
                }
                else {
                    if (CopyProgress::g_copyProgFn) {
                        if (!CopyProgress::g_copyProgFn(-(LONGLONG)bytesWritten, NULL, NULL, CopyProgress::g_copyProgUser)) {
                            break; // canceled
                        }
                    }
                }
            }
        } while (rc > 0);

        CloseHandle(file);
    }

    // Close current file (don't lose the error)
    if (rc == UNZ_OK) rc = zip->closeCurrentFile();
    else zip->closeCurrentFile();

    return rc;
}

void FreeUnZipBuffer() {
    free(unZipBuffer);
    unZipBuffer = NULL;
}