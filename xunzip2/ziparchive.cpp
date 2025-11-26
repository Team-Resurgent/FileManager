
#include <xtl.h>
#include <stdio.h>

#include <malloc.h>
#include <sstream>
#include <fstream>
#include <conio.h>
#include <limits.h>
#include <locale>
#include <iostream>

using namespace std;

#include "ziparchive.h"

CZipArchive::CZipArchive(void) {
	m_pUnZipBuffer		= (void*)NULL;
	m_uiUnZipBufferSize	= 131072; // amount to malloc
}

CZipArchive::~CZipArchive(void) {
	if (m_pUnZipBuffer != (void*)NULL) {
		free(m_pUnZipBuffer);
		m_pUnZipBuffer = (void*)NULL;
	}
}

// Public
bool CZipArchive::ExtractFromFile(const char * pszSource, const char * pszDestinationFolder, const bool bUseFolderNames, const bool bOverwrite) {
	UNZIP zip;
    int rc;
	
	rc = zip.openZIP(pszSource, zipFile_Open, zipFile_Close, zipFile_Read, zipFile_Seek);
	if (rc != UNZ_OK) {
		zip.closeZIP();
		return false;
	}

	rc = ExtractZip(zip, pszDestinationFolder, bUseFolderNames, bOverwrite);

	zip.closeZIP();
	return (rc == UNZ_OK || rc == UNZ_END_OF_LIST_OF_FILE);
}

// Public
bool CZipArchive::ExtractFromMemory(uint8_t *pData, int iDataSize, const char * pszDestinationFolder, const bool bUseFolderNames, const bool bOverwrite) {
	UNZIP zip;
	int rc;

	rc = zip.openZIP(pData, iDataSize);
	if (rc != UNZ_OK) {
		zip.closeZIP();
		return false;
	}

	rc = ExtractZip(zip, pszDestinationFolder, bUseFolderNames, bOverwrite);

	zip.closeZIP();
	return (rc == UNZ_OK || rc == UNZ_END_OF_LIST_OF_FILE);
}

int CZipArchive::ExtractZip(UNZIP& zip, const char * pszDestinationFolder, const bool bUseFolderNames, const bool bOverwrite) {
	char szComment[256], szName[256];
	unz_file_info fi;
	int rc;

	// Use global comment as a zip sanity check
	rc = zip.getGlobalComment(szComment, sizeof(szComment));
	if (rc != UNZ_OK) {
		return rc;
	}

	// Ensure that the destination root folder exists
	CreateDirectory(pszDestinationFolder, NULL);

	zip.gotoFirstFile();
	rc = UNZ_OK;

	// Loop through all files
	while (rc == UNZ_OK) {
		// File record ok?
		rc = zip.getFileInfo(&fi, szName, sizeof(szName), NULL, 0, szComment, sizeof(szComment));
		if (rc == UNZ_OK) {
			// Extract the current file
			if ((rc = ExtractCurrentFile(zip, pszDestinationFolder, bUseFolderNames, bOverwrite)) != UNZ_OK) {
				// Error during extraction of file. Break out of loop
				break;
			}
		}
		rc = zip.gotoNextFile();
	}

	// Return status
	return rc;
}

char *strrepl(char *Str, size_t BufSiz, char *OldStr, char *NewStr) {
    int OldLen, NewLen;
    char *p, *q;

	if (NULL == (p = strstr(Str, OldStr))) {
        return Str;
	}

    OldLen = strlen(OldStr);
    NewLen = strlen(NewStr);

	if ((strlen(Str) + NewLen - OldLen + 1) > BufSiz) {
        return NULL;
	}

    memmove(q = p + NewLen, p + OldLen, strlen(p + OldLen)+1);
    memcpy(p, NewStr, NewLen);
    return q;
}

char *strreplall(char *Str, size_t BufSiz, char *OldStr, char *NewStr) {
	char *ret;
	size_t i;

	for (i = 0; i < BufSiz; i++) {
		ret = strrepl(Str, BufSiz, OldStr, NewStr);
	}

	return ret;
}

int CZipArchive::ExtractCurrentFile(UNZIP& zip, const char * pszDestinationFolder, const bool bUseFolderNames, bool bOverwrite) {
	char szComment[256] = {0};
	char szFileName_InZip[1024] = {0};
	char szBuffer[1024];
	unz_file_info fi;
	char szPathSep[2];
	char * pszFileName_WithOutPath;
	char * pszPos;
	int rc;
	char * pszWriteFileName;
	char chHold;
	bool bSkip = false;
	HANDLE hFile;
	DWORD dwBytesWritten = 0;

	// Check if the destination folder ends with an '\\'
	if (*(pszDestinationFolder + strlen(pszDestinationFolder) - 1) == '\\') {
		// Use no separator
		*szPathSep = '\0';
	}
	else {
		// Use path separator
		strcpy(szPathSep, "\\");
	}

	// Get information about the current file
	rc = zip.getFileInfo(&fi, szBuffer, sizeof(szBuffer), NULL, 0, szComment, sizeof(szComment));
	if (rc != UNZ_OK) {
		return rc;
	}

	// Substitute '/' with '\'
	strreplall(szBuffer, 1024, "/", "\\");

	// Don't include the drive letter (if present) and the leading '\' (if present)
	if (szBuffer[1] == ':' && szBuffer[2] == '\\') {
		// Copy file name
		strcpy(szFileName_InZip, (szBuffer + 3));
	}
	else if (szBuffer[1] == ':') {
		strcpy(szFileName_InZip, (szBuffer + 2));
	}
	else if (szBuffer[0] == '\\') {
		strcpy(szFileName_InZip, (szBuffer + 1));
	}
	else {
		strcpy(szFileName_InZip, szBuffer);
	}

	// Set reference
	pszPos = (char*)pszFileName_WithOutPath = (char*)szFileName_InZip;

	// Find filename part (without the path)
	while ((*pszPos) != '\0') {
		if (((*pszPos) == '/') || ((*pszPos) == '\\')) {
			// Set reference
			pszFileName_WithOutPath = (char*)(pszPos + 1);
		}

		// Increment position
		pszPos++;
	}

	// Is this a folder?
	if ((*pszFileName_WithOutPath) == '\0') {
		// Use folder names?
		if (bUseFolderNames) {
			// Compose file name
			sprintf(szBuffer, "%s%s%s", pszDestinationFolder, szPathSep, szFileName_InZip);

			// Substitute '/' with '\'
			strreplall(szBuffer, 1024, "/", "\\");

			// Create folder
			CreateDirectory(szBuffer, NULL);
		}

		// Return OK
		return UNZ_OK;
	}

	// Do we have a buffer?
	if (m_pUnZipBuffer == (void*)NULL) {
		// Allocate buffer
		if ((m_pUnZipBuffer = (void*)malloc(m_uiUnZipBufferSize)) == (void*)NULL) {
			// Return not OK
			return UNZ_INTERNALERROR;
		}
	}

	// Use folder names?
	if (bUseFolderNames) {
		// Use total file name
		pszWriteFileName = szFileName_InZip;
	}
	else {
		// Use file name only
		pszWriteFileName = pszFileName_WithOutPath;
	}

	// Open the current file
	if ((rc = zip.openCurrentFile()) != UNZ_OK) {
		return rc;
	}

	// Compose file name
	sprintf(szBuffer, "%s%s%s", pszDestinationFolder, szPathSep, pszWriteFileName);

	// Check if file exists?
	if (!bOverwrite && rc == UNZ_OK) {
		// Open the local file
		hFile = CreateFile(szBuffer, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

		// Check handle
		if (hFile != (HANDLE)INVALID_HANDLE_VALUE) {
			// File exists but don't overwrite. Close file
			CloseHandle(hFile);

			// Skip this file
			bSkip = true;
		}
	}

	// Skip this file?
	if (!bSkip && rc == UNZ_OK) {
		// Create the file
		hFile = CreateFile(szBuffer, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		// Check handle
		if (hFile == (HANDLE)INVALID_HANDLE_VALUE) {
			// File not created. Some zipfiles doesn't contain
			// folder alone before file
			if (bUseFolderNames && pszFileName_WithOutPath != (char*)szFileName_InZip) {
				// Store character
				chHold = *(pszFileName_WithOutPath - 1);

				// Terminate string
				*(pszFileName_WithOutPath - 1) = '\0';

				// Compose folder name
				sprintf(szBuffer, "%s%s%s", pszDestinationFolder, szPathSep, pszWriteFileName);

				// Create folder
				CreateDirectory(szBuffer, NULL);

				// Restore file name
				*(pszFileName_WithOutPath - 1) = chHold;

				// Compose folder name
				sprintf(szBuffer, "%s%s%s", pszDestinationFolder, szPathSep, pszWriteFileName);

				// Try to create the file
				hFile = CreateFile(szBuffer, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			}
		}   

		// Check handle
		if (hFile == (HANDLE)INVALID_HANDLE_VALUE) {
			// Return not OK
			return UNZ_ERRNO;
		}
	}

	// Check handle
	if (hFile != (HANDLE)INVALID_HANDLE_VALUE) {
		do {
			// Read the current file
			if ((rc = zip.readCurrentFile((uint8_t*)m_pUnZipBuffer, m_uiUnZipBufferSize)) < 0) {
				// Error reading zip file
				// Break out of loop
				break;
			}

			// Check return code
			if (rc > 0) {
				// Write to file
				if (WriteFile(hFile, m_pUnZipBuffer, (DWORD)rc, &dwBytesWritten, NULL) == false) {
					// Error during write of file

					// Set return status
					rc = UNZ_ERRNO;

					// Break out of loop
					break;
				}
			}
		}
		while (rc > 0);

		// Close file
		CloseHandle(hFile);
	}

	if (rc == UNZ_OK) {
		// Close current file
		rc = zip.closeCurrentFile();
	}
	else {
		// Close current file (don't lose the error)
		zip.closeCurrentFile();
	}

	// Return status
	return rc;
}

// Callback functions needed by the unzipLIB to access the filesystem
void * zipFile_Open(const char *filename, int32_t *size) {
	FILE *f = fopen(filename, "rb");
	fseek(f, 0L, SEEK_END);
	*size = ftell(f);
	rewind(f);
	return (void *)f;
}

void zipFile_Close(void *p) {
	ZIPFILE *pzf = (ZIPFILE *)p;
	FILE *f = (FILE *)pzf->fHandle;

	if (f) {
		fclose(f);
	}
}

int32_t zipFile_Read(void *p, uint8_t *buffer, int32_t length) {
	ZIPFILE *pzf = (ZIPFILE *)p;
	FILE *f = (FILE *)pzf->fHandle;
	return fread(buffer, 1, length, f);
}

int32_t zipFile_Seek(void *p, int32_t position, int iType) {
	ZIPFILE *pzf = (ZIPFILE *)p;
	FILE *f = (FILE *)pzf->fHandle;
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
