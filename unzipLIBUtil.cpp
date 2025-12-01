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

int ExtractCurrentFileWithProgress(UNZIP* zip, const char* dst, bool overwrite, ULONGLONG& inoutBytesDone, ULONGLONG totalBytes) {
    char fileName_InZip[512];
    char buf[512];
    unz_file_info fi;
    char pathSep[2];
    char* fileName_WithOutPath;
    char* pos;
    bool useFolderNames = true;
    int rc;
    char* writeFileName;
    char hold;
    bool skip = false;
    HANDLE file;
    DWORD bytesWritten = 0;

    // Check if the destination folder ends with an '\\'
    if (*(dst + strlen(dst) - 1) == '\\') {
        // Use no separator
        *pathSep = '\0';
    }
    else {
        // Use path separator
        strcpy(pathSep, "\\");
    }

    // Get information about the current file
    rc = zip->getFileInfo(&fi, buf, 512, NULL, 0, NULL, 0);
    if (rc != UNZ_OK) {
        return rc;
    }

    // Substitute '/' with '\'
    strreplall(buf, 512, "/", "\\");

    // Don't include the drive letter (if present) and the leading '\' (if present)
    if (buf[1] == ':' && buf[2] == '\\') {
        // Copy file name
        strcpy(fileName_InZip, (buf + 3));
    }
    else if (buf[1] == ':') {
        strcpy(fileName_InZip, (buf + 2));
    }
    else if (buf[0] == '\\') {
        strcpy(fileName_InZip, (buf + 1));
    }
    else {
        strcpy(fileName_InZip, buf);
    }

    // Set reference
    pos = (char*)fileName_WithOutPath = (char*)fileName_InZip;

    // Find filename part (without the path)
    while ((*pos) != '\0') {
        if (((*pos) == '/') || ((*pos) == '\\')) {
            // Set reference
            fileName_WithOutPath = (char*)(pos + 1);
        }

        // Increment position
        pos++;
    }

    // Is this a folder?
    if ((*fileName_WithOutPath) == '\0') {
        // Use folder names?
        if (useFolderNames) {
            // Compose file name
            sprintf(buf, "%s%s%s", dst, pathSep, fileName_InZip);

            // Substitute '/' with '\'
            strreplall(buf, 512, "/", "\\");

            // Create folder
            CreateDirectory(buf, NULL);
        }

        // Return OK
        return UNZ_OK;
    }

    // Do we have a buffer?
    if (unZipBuffer == NULL) {
        // Allocate buffer
        if ((unZipBuffer = (char*)malloc(65536)) == NULL) {
            // Return not OK
            return UNZ_INTERNALERROR;
        }
    }

    // Use folder names?
    if (useFolderNames) {
        // Use total file name
        writeFileName = fileName_InZip;
    }
    else {
        // Use file name only
        writeFileName = fileName_WithOutPath;
    }

    // Open the current file
    if ((rc = zip->openCurrentFile()) != UNZ_OK) {
        return rc;
    }

    // Compose file name
    sprintf(buf, "%s%s%s", dst, pathSep, writeFileName);

    // Check if file exists?
    if (!overwrite && rc == UNZ_OK) {
        // Open the local file
        file = CreateFile(buf, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

        // Check handle
        if (file != (HANDLE)INVALID_HANDLE_VALUE) {
            // File exists but don't overwrite. Close file
            CloseHandle(file);

            // Skip this file
            skip = true;
        }
    }

    // Skip this file?
    if (!skip && rc == UNZ_OK) {
        // Create the file
        file = CreateFile(buf, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

        // Check handle
        if (file == (HANDLE)INVALID_HANDLE_VALUE) {
            // File not created. Some zipfiles doesn't contain
            // folder alone before file
            if (useFolderNames && fileName_WithOutPath != (char*)fileName_InZip) {
                // Store character
                hold = *(fileName_WithOutPath - 1);

                // Terminate string
                *(fileName_WithOutPath - 1) = '\0';

                // Compose folder name
                sprintf(buf, "%s%s%s", overwrite, pathSep, writeFileName);

                // Create folder
                CreateDirectory(buf, NULL);

                // Restore file name
                *(fileName_WithOutPath - 1) = hold;

                // Compose folder name
                sprintf(buf, "%s%s%s", overwrite, pathSep, writeFileName);

                // Try to create the file
                file = CreateFile(buf, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            }
        }

        // Check handle
        if (file == (HANDLE)INVALID_HANDLE_VALUE) {
            // Return not OK
            return UNZ_ERRNO;
        }
    }

    // Check handle
    if (file != (HANDLE)INVALID_HANDLE_VALUE) {
        do {
            // Read the current file
            if ((rc = zip->readCurrentFile((uint8_t*)unZipBuffer, 65536)) < 0) {
                // Error reading zip file
                // Break out of loop
                break;
            }

            // Check return code
            if (rc > 0) {
                // Write to file
                if (WriteFile(file, unZipBuffer, (DWORD)rc, &bytesWritten, NULL) == false) {
                    // Error during write of file

                    // Set return status
                    rc = UNZ_ERRNO;

                    // Break out of loop
                    break;
                }
                else {
                    inoutBytesDone += bytesWritten;
                    if (CopyProgress::g_copyProgFn) {
                        if (!CopyProgress::g_copyProgFn(inoutBytesDone, totalBytes, NULL, CopyProgress::g_copyProgUser)) {
                            break; // canceled
                        }
                    }
                }
            }
        } while (rc > 0);

        // Close file
        CloseHandle(file);
    }

    if (rc == UNZ_OK) {
        // Close current file
        rc = zip->closeCurrentFile();
    }
    else {
        // Close current file (don't lose the error)
        zip->closeCurrentFile();
    }

    // Return status
    return rc;
}

void FreeUnZipBuffer() {
    free(unZipBuffer);
    unZipBuffer = NULL;
}