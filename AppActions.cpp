#include "AppActions.h"
#include "FileBrowserApp.h"
#include "FsUtil.h"
#include "XBInput.h"   // XBInput_GetInput, g_Gamepads

#include "xipslib.h"
#include "unzipLIB.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>   // toupper

/*
============================================================================
 AppActions
  - Centralized execution of menu actions for FileBrowserApp
  - Cancel-aware copy/move with a "press B twice to cancel" toast window
  - Uses FsUtil.* helpers for I/O and FileBrowserApp methods for UI refresh
============================================================================
*/

// ---- Cancel-aware progress context + thunk ---------------------------------
// CopyProgCtx: tracks progress, cancel state, and the 2-press B confirmation.
// The "toast window" behavior:
//   1) First B press shows "Press B again to cancel" and arms a window.
//   2) If the second B happens while the toast is still visible, we cancel.
//   3) If toast expires before second B, the next B just re-arms the window.
struct CopyProgCtx {
    FileBrowserApp* app;
    ULONGLONG base;        // bytes completed from previous items (offset)
    bool      canceled;    // set when user confirms cancel

    // confirmation state (toast window)
    bool      confirmArmed;   // true after the first B press
    DWORD     confirmUntil;   // snapshot of when the toast will expire (informational)
    bool      prevB;          // for rising-edge detection of B
};

// Progress callback used by file copy/move loops.
// - Pumps gamepad input to detect B presses
// - Updates the app's progress overlay
// - Implements the "press B twice" cancel logic
static bool CopyProgThunk(ULONGLONG done, ULONGLONG total, const char* label, void* user){
    CopyProgCtx* c = (CopyProgCtx*)user;

    // Poll controller to read B presses while copying
    XBInput_GetInput();
    const XBGAMEPAD& pad = g_Gamepads[0];
    const bool  bNow = (pad.bAnalogButtons[XINPUT_GAMEPAD_B] > 30);
    const DWORD now  = GetTickCount();

    // Paint progress (done is per-current-item, base is previous items)
    c->app->UpdateProgress(c->base + done, total, label);

    // Rising-edge B?
    if (bNow && !c->prevB){
        const DWORD statusUntil = c->app->StatusUntilMs();
        const bool  toastAlive  = (now < statusUntil);

        if (c->confirmArmed && toastAlive){
            // Second B while the toast is still up => cancel
            c->canceled = true;
            SetLastError(ERROR_OPERATION_ABORTED);
            c->prevB = bNow;
            return false; // abort copy/move
        } else {
            // First B: arm and show "press B again" toast
            c->confirmArmed = true;
            c->app->SetStatus("Press B again to cancel");
            c->confirmUntil = c->app->StatusUntilMs(); // snapshot (optional)
        }
    }

    // Disarm if toast has expired
    if (c->confirmArmed && now >= c->app->StatusUntilMs()){
        c->confirmArmed = false;
    }

    c->prevB = bNow;
    return true; // keep going
}

// ---- local helpers ----------------------------------------------------------

// Return 1 if both paths are on the same drive letter (case-insensitive).
static int SameDriveLetter(const char* a, const char* b){
    if (!a || !b || !a[0] || !b[0]) return 0;
    return toupper((unsigned char)a[0]) == toupper((unsigned char)b[0]);
}

// Ensure trailing backslash on non-empty strings (local, VC7.1-safe).
static void NormalizeSlashEndLocal(char* s, size_t cap){
    size_t n = s ? strlen(s) : 0;
    if (n && s[n-1] != '\\' && n+1 < cap){ s[n] = '\\'; s[n+1] = 0; }
}

// Case-insensitive: is `child` a subpath of `parent`?
static int IsSubPathCaseI(const char* parent, const char* child){
    char p[512], c[512];
    _snprintf(p, sizeof(p), "%s", parent ? parent : ""); p[sizeof(p)-1] = 0;
    _snprintf(c, sizeof(c), "%s", child  ? child  : ""); c[sizeof(c)-1] = 0;
    NormalizeSlashEndLocal(p, sizeof(p));
    NormalizeSlashEndLocal(c, sizeof(c));
    return _strnicmp(p, c, (int)strlen(p)) == 0; // child starts with parent?
}

// Basename (pointer into input string), e.g., "E:\A\B\C" -> "C".
static const char* BaseNameOf(const char* path){
    const char* s = path ? strrchr(path, '\\') : 0;
    return s ? (s+1) : path;
}

// ------------------------------------------------------------------
// unzipLIB filesystem callbacks (for openZIP in ACT_UNZIP)
// ------------------------------------------------------------------
void* zipFile_Open(const char* filename, int32_t* size) {
    FILE* f = fopen(filename, "rb");
    fseek(f, 0L, SEEK_END);
    *size = ftell(f);
    rewind(f);
    return (void*)f;
}

void zipFile_Close(void* p) {
    ZIPFILE* pzf = (ZIPFILE*)p;
    FILE* f = (FILE*)pzf->fHandle;

    if (f) {
        fclose(f);
    }
}

int32_t zipFile_Read(void* p, uint8_t* buffer, int32_t length) {
    ZIPFILE* pzf = (ZIPFILE*)p;
    FILE* f = (FILE*)pzf->fHandle;
    return fread(buffer, 1, length, f);
}

int32_t zipFile_Seek(void* p, int32_t position, int iType) {
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
// ExtractCurrentFile & helper methods courtesy of CrunchBite
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

void* m_pUnZipBuffer = (void*)NULL;
unsigned int m_uiUnZipBufferSize = 131072;
int ExtractCurrentFile(UNZIP* zip, const char* pszDestinationFolder, const bool bUseFolderNames, bool bOverwrite) {

    char szFileName_InZip[512];
    char szBuffer[512];
    unz_file_info fi;
    char szPathSep[2];
    char* pszFileName_WithOutPath;
    char* pszPos;
    int rc;
    char* pszWriteFileName;
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
    rc = zip->getFileInfo(&fi, szBuffer, 1024, NULL, 0, NULL, 0);
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
    if ((rc = zip->openCurrentFile()) != UNZ_OK) {
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
            if ((rc = zip->readCurrentFile((uint8_t*)m_pUnZipBuffer, m_uiUnZipBufferSize)) < 0) {
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
        } while (rc > 0);

        // Close file
        CloseHandle(hFile);
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

namespace AppActions {

// Collects selected sources for copy/move/delete:
// - If any items are marked, returns all marked (excluding "..").
// - Else returns the single current selection (if not "..").
// - Writes full paths (dir + name) into 'out'.
static void GatherMarkedOrSelectedFullPaths(const Pane& src, std::vector<std::string>& out) {
    if (src.mode != 1 || src.items.empty()) return;

    bool any = false;
    for (size_t i=0; i<src.items.size(); ++i) {
        const Item& it = src.items[i];
        if (it.marked && !it.isUpEntry) {
            char full[512]; JoinPath(full, sizeof(full), src.curPath, it.name);
            out.push_back(full);
            any = true;
        }
    }
    if (!any) {  // fall back to single selection
        const Item& sel = src.items[src.sel];
        if (!sel.isUpEntry) {
            char full[512]; JoinPath(full, sizeof(full), src.curPath, sel.name);
            out.push_back(full);
        }
    }
}

// Main dispatcher: runs the action the user picked from the context menu.
void Execute(Action act, FileBrowserApp& app) {
    Pane& src = app.m_pane[app.m_active];
	Pane& dst = app.m_pane[1 - app.m_active];

    const Item* sel = NULL;
	const Item* sel2 = NULL;
    if (!src.items.empty()) sel = &src.items[src.sel];
	if (!dst.items.empty()) sel2 = &dst.items[dst.sel];

    // Full path of selection (if any). Also supports drive-list selection.
    char srcFull[512] = "";
	char dstFull[512] = "";
	const char* ext = NULL;
	const char* ext2 = NULL;
    if (sel) {
        if (src.mode == 1 && !sel->isUpEntry) {
			if (!sel->isDir) ext = GetExtension(sel->name);
            // Normal directory listing: dir + name
            JoinPath(srcFull, sizeof(srcFull), src.curPath, sel->name);
            srcFull[sizeof(srcFull)-1] = 0;
        } else if (src.mode == 0 && sel->isDir && !sel->isUpEntry) {
            // Drive list: item name is already something like "E:\"
            _snprintf(srcFull, sizeof(srcFull), "%s", sel->name);
            srcFull[sizeof(srcFull)-1] = 0;
            NormalizeDirA(srcFull); // ensure trailing slash
        }
    }
	if (sel2) {
		if (dst.mode == 1 && !sel2->isUpEntry) {
			if (!sel2->isDir) ext2 = GetExtension(sel2->name);
            // Normal directory listing: dir + name
            JoinPath(dstFull, sizeof(dstFull), dst.curPath, sel2->name);
            dstFull[sizeof(dstFull)-1] = 0;
        } else if (dst.mode == 0 && sel2->isDir && !sel2->isUpEntry) {
            // Drive list: item name is already something like "E:\"
            _snprintf(dstFull, sizeof(dstFull), "%s", sel2->name);
            dstFull[sizeof(dstFull)-1] = 0;
            NormalizeDirA(dstFull); // ensure trailing slash
        }
	}

    switch (act)
    {
    // ---- Open / Enter / Launch ------------------------------------------------
    case ACT_OPEN:
        if (sel){
            if (sel->isUpEntry) { app.UpOne(src); }
            else if (sel->isDir) { app.EnterSelection(src); }
            else if (HasXbeExt(sel->name)){
                char full[512]; JoinPath(full, sizeof(full), src.curPath, sel->name);
                if (!LaunchXbeA(full)) app.SetStatusLastErr("Launch failed");
            }
        }
        break;

    // ---- Copy -----------------------------------------------------------------
	case ACT_COPY:
	{
		if (src.mode != 1) { app.SetStatus("Open a folder"); break; }

		// Resolve destination (other pane preferred)
		char dstDir[512];
		if (!app.ResolveDestDir(dstDir, sizeof(dstDir))) { app.SetStatus("Pick a destination"); break; }
		if ((dstDir[0]=='D'||dstDir[0]=='d') && dstDir[1]==':'){ app.SetStatus("Cannot copy to D:\\"); break; }
		NormalizeDirA(dstDir);
		if (!CanWriteHereA(dstDir)){ app.SetStatusLastErr("Dest not writable"); break; }

		// Gather sources
		std::vector<std::string> srcs;
		GatherMarkedOrSelectedFullPaths(src, srcs);
		if (srcs.empty()) { app.SetStatus("Nothing to copy"); break; }

		// Compute total bytes for progress bar
		ULONGLONG total=0;
		for (size_t i=0;i<srcs.size();++i) total += DirSizeRecursiveA(srcs[i].c_str());

		// --- preflight free-space check on destination (kept from your version) ---
		{
			ULONGLONG freeB=0, totB=0;
			GetDriveFreeTotal(dstDir, freeB, totB);
			if (total > freeB){
				char need[64], have[64];
				FormatSize(total, need, sizeof(need));
				FormatSize(freeB, have, sizeof(have));
				app.SetStatus("Not enough space: need %s, have %s", need, have);
				break;
			}
		}
		// --- end preflight ---

		// Begin progress + set callback
		app.BeginProgress(total, srcs[0].c_str(), "Copying...");
		CopyProgCtx ctx = { &app, 0, false, false, 0, false };
		SetCopyProgressCallback(CopyProgThunk, &ctx);

		ULONGLONG base = 0;           // cumulative bytes completed
		char lastDstTop[512] = {0};   // for cancel cleanup

		// NEW: track results so the final toast reflects reality
		size_t copiedOk = 0, failed = 0, skipped = 0;

		for (size_t i=0;i<srcs.size();++i){
			const char* sp = srcs[i].c_str();

			// Compute top-level destination path (dstDir\basename(sp)) for cleanup
			const char* bn = BaseNameOf(sp);
			JoinPath(lastDstTop, sizeof(lastDstTop), dstDir, bn);

			ULONGLONG thisSize = DirSizeRecursiveA(sp);
			ctx.base = base;

			// --- prevent copying a folder into its own subfolder (or itself) ---
			if (IsSubPathCaseI(sp, lastDstTop)) {
				app.SetStatus("Cannot copy a folder into its own subfolder");
				++skipped;
				continue; // skip this item, proceed to next
			}

			// --- per-item free-space check (extra safety) ---
			{
				ULONGLONG freeB=0, totB=0;
				GetDriveFreeTotal(lastDstTop, freeB, totB); // any path on dest volume is fine
				if (thisSize > freeB) {
					char need[64], have[64];
					FormatSize(thisSize, need, sizeof(need));
					FormatSize(freeB,   have, sizeof(have));
					app.SetStatus("Not enough space for %s: need %s, have %s", bn, need, have);
					break; // stop the whole batch (change to 'continue;' to try remaining items)
				}
			}
			// --- end per-item check ---

			if (!CopyRecursiveWithProgressA(sp, dstDir, total)){
				if (ctx.canceled){
					// Remove partial destination of the current item, then stop
					DeleteRecursiveA(lastDstTop);
					break;
				}
				// Non-cancel failure
				++failed;
			} else {
				base += thisSize;
				++copiedOk;
			}
		}

		// End progress and clear callback
		SetCopyProgressCallback(NULL, NULL);
		app.EndProgress();

		if (ctx.canceled){
			app.SetStatus("Copy canceled (%u done, %u skipped, %u failed)",
						(unsigned)copiedOk, (unsigned)skipped, (unsigned)failed);
			app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);
			break;
		}

		// If we copied into the currently displayed dest folder, refresh it
		Pane& dstp = app.m_pane[1 - app.m_active];
		if (dstp.mode==1 && _stricmp(dstp.curPath, dstDir)==0) ListDirectory(dstp.curPath, dstp.items);

		// Clear marks and refresh both panes
		for (size_t i=0;i<src.items.size();++i) src.items[i].marked=false;
		app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);

		// Final toast that reflects what actually happened
		if (failed==0 && skipped==0) {
			app.SetStatus("Copied %u item(s)", (unsigned)copiedOk);
		} else {
			app.SetStatus("Copied %u, %u skipped, %u failed",
						(unsigned)copiedOk, (unsigned)skipped, (unsigned)failed);
		}
		break;
}


    // ---- Move -----------------------------------------------------------------
	case ACT_MOVE:
	{
		if (src.mode != 1) { app.SetStatus("Open a folder"); break; }

		char dstDir[512];
		if (!app.ResolveDestDir(dstDir, sizeof(dstDir))) { app.SetStatus("Pick a destination"); break; }
		if ((dstDir[0]=='D'||dstDir[0]=='d') && dstDir[1]==':'){ app.SetStatus("Cannot move to D:\\"); break; }
		NormalizeDirA(dstDir);
		if (!CanWriteHereA(dstDir)){ app.SetStatusLastErr("Dest not writable"); break; }

		std::vector<std::string> srcs;
		GatherMarkedOrSelectedFullPaths(src, srcs);
		if (srcs.empty()) { app.SetStatus("Nothing to move"); break; }

		// Total bytes for progress
		ULONGLONG total=0;
		for (size_t i=0;i<srcs.size();++i) total += DirSizeRecursiveA(srcs[i].c_str());

		// --- preflight space only when cross-volume (move will copy+delete) ---
		{
			const bool sameVol = SameDriveLetter(src.curPath, dstDir) != 0;
			if (!sameVol){
				ULONGLONG freeB=0, totB=0;
				GetDriveFreeTotal(dstDir, freeB, totB);
				if (total > freeB){
					char need[64], have[64];
					FormatSize(total, need, sizeof(need));
					FormatSize(freeB, have, sizeof(have));
					app.SetStatus("Not enough space: need %s, have %s", need, have);
					break;
				}
			}
		}
		// --- end preflight ---

		app.BeginProgress(total, srcs[0].c_str(), "Moving...");
		CopyProgCtx ctx = { &app, 0, false, false, 0, false };
		SetCopyProgressCallback(CopyProgThunk, &ctx);

		size_t movedOk = 0, failed = 0, skipped = 0;  // NEW: track results
		ULONGLONG base = 0;

		for (size_t i=0;i<srcs.size();++i){
			const char* sp = srcs[i].c_str();
			ULONGLONG thisSize = DirSizeRecursiveA(sp);
			ctx.base = base;

			// Destination top (dstDir\basename(sp))
			const char* baseName = BaseNameOf(sp);
			char dstTop[512]; JoinPath(dstTop, sizeof(dstTop), dstDir, baseName);

			// --- NEW: prevent moving a folder into its own subfolder (or onto itself)
			if (IsSubPathCaseI(sp, dstTop)) {
				app.SetStatus("Cannot move a folder into its own subfolder");
				++skipped;
				continue; // skip this item and go on
			}

			BOOL doneThis = FALSE;

			// --- NEW: per-item free-space check only if not a fast same-volume rename
			const BOOL canFastRename =
				SameDriveLetter(sp, dstDir) && !IsSubPathCaseI(sp, dstTop);

			if (!canFastRename) {
				ULONGLONG freeB=0, totB=0;
				GetDriveFreeTotal(dstTop, freeB, totB);
				if (thisSize > freeB) {
					char need[64], have[64];
					FormatSize(thisSize, need, sizeof(need));
					FormatSize(freeB,   have, sizeof(have));
					app.SetStatus("Not enough space to move %s: need %s, have %s", baseName, need, have);
					break; // stop the whole batch (use 'continue;' if you prefer to try remaining items)
				}
			}
			// --- end NEW ---

			// Fast path: same drive and not moving into own subfolder -> MoveFileA
			if (SameDriveLetter(sp, dstDir) && !IsSubPathCaseI(sp, dstTop)) {
				if (MoveFileA(sp, dstTop)) {
					doneThis = TRUE;  // instant rename/move within volume
				}
			}

			// Fallback: copy -> delete original (only delete if copy succeeded)
			if (!doneThis) {
				if (!CopyRecursiveWithProgressA(sp, dstDir, total)){
					if (ctx.canceled){
						// Clean partial dest and stop
						DeleteRecursiveA(dstTop);
						break;
					}
					// Non-cancel failure
					++failed;
				} else {
					if (DeleteRecursiveA(sp)) {
						doneThis = TRUE;
					} else {
						// Optional: consider removing dstTop if source delete failed
						++failed;
					}
				}
			}

			if (doneThis) ++movedOk;
			base += thisSize;
		}

		SetCopyProgressCallback(NULL, NULL);
		app.EndProgress();

		if (ctx.canceled){
			app.SetStatus("Move canceled (%u done, %u skipped, %u failed)",
						(unsigned)movedOk, (unsigned)skipped, (unsigned)failed);
			// On cancel we keep originals; clear marks and refresh UI
			for (size_t i=0;i<src.items.size();++i) src.items[i].marked=false;
			if (src.mode==1) ListDirectory(src.curPath, src.items);
			{
				Pane& dstp = app.m_pane[1 - app.m_active];
				if (dstp.mode==1 && _stricmp(dstp.curPath, dstDir)==0) ListDirectory(dstp.curPath, dstp.items);
			}
			app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);
			break;
		}

		// Normal completion: refresh both panes, clear marks
		for (size_t i=0;i<src.items.size();++i) src.items[i].marked=false;
		if (src.mode==1) ListDirectory(src.curPath, src.items);
		{
			Pane& dstp = app.m_pane[1 - app.m_active];
			if (dstp.mode==1 && _stricmp(dstp.curPath, dstDir)==0) ListDirectory(dstp.curPath, dstp.items);
		}
		app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);

		// NEW: accurate final toast
		if (failed==0 && skipped==0) {
			app.SetStatus("Moved %u item(s)", (unsigned)movedOk);
		} else {
			app.SetStatus("Moved %u, %u skipped, %u failed",
						(unsigned)movedOk, (unsigned)skipped, (unsigned)failed);
		}
		break;
	}


    // ---- Delete ---------------------------------------------------------------
    case ACT_DELETE:
    {
        if (src.mode != 1) { app.SetStatus("Open a folder"); break; }

        std::vector<std::string> srcs;
        GatherMarkedOrSelectedFullPaths(src, srcs);
        if (srcs.empty()) { app.SetStatus("Nothing to delete"); break; }

        int ok=0;
        for (size_t i=0;i<srcs.size();++i) if (DeleteRecursiveA(srcs[i].c_str())) ++ok;

        // Clear marks, refresh, and toast result
        for (size_t i=0;i<src.items.size();++i) src.items[i].marked=false;
        if (src.mode==1) ListDirectory(src.curPath, src.items);
        app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);
        app.SetStatus("Deleted %d / %d", ok, (int)srcs.size());
        break;
    }

    // ---- Rename (opens modal OSK) --------------------------------------------
    case ACT_RENAME:
        if (sel && src.mode==1 && !sel->isUpEntry){
            app.BeginRename(src.curPath, sel->name);
        } else {
            app.SetStatus("Open a folder and select an item");
        }
        break;

    // ---- Make new folder ------------------------------------------------------
    case ACT_MKDIR:
    {
        char baseDir[512] = {0};

        if (src.mode == 1) {
            _snprintf(baseDir, sizeof(baseDir), "%s", src.curPath);
        } else if (!src.items.empty()) {
            const Item& di = src.items[src.sel];
            if (di.isDir && !di.isUpEntry) {
                _snprintf(baseDir, sizeof(baseDir), "%s", di.name); // drive root, e.g. "E:\"
            }
        }
        baseDir[sizeof(baseDir)-1] = 0;
        if (!baseDir[0]) { app.SetStatus("Open a folder or select a drive first"); break; }

        if ((baseDir[0]=='D' || baseDir[0]=='d') && baseDir[1]==':') {
            app.SetStatus("Cannot create on D:\\ (read-only)"); break;
        }

        NormalizeDirA(baseDir);
        if (!CanWriteHereA(baseDir)) { app.SetStatusLastErr("Dest not writable"); break; }

        // Auto-name NewFolder[/N] without clobbering existing names
        char nameBuf[64];
        char target[512];
        int idx = 0;
        for (;;){
            if (idx == 0) _snprintf(nameBuf, sizeof(nameBuf), "NewFolder");
            else          _snprintf(nameBuf, sizeof(nameBuf), "NewFolder%d", idx);
            nameBuf[sizeof(nameBuf)-1]=0;

            JoinPath(target, sizeof(target), baseDir, nameBuf);

            if (!DirExistsA(target)) {
                if (CreateDirectoryA(target, NULL)) {
                    app.SetStatus("Created %s", nameBuf);
                } else {
                    app.SetStatusLastErr("Create folder failed");
                }
                break;
            }
            if (++idx > 999){ app.SetStatus("Create folder failed (names exhausted)"); break; }
        }

        app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);

        // If we created inside the active folder, select the new dir
        if (src.mode == 1 && _stricmp(src.curPath, baseDir) == 0) {
            app.SelectItemInPane(src, nameBuf);
        }
        break;
    }

    // ---- Calculate size -------------------------------------------------------
    case ACT_CALCSIZE:
    {
        if (!sel) break;
        if (!srcFull[0]) {
            // Try to derive from selection directly (covers any future cases)
            char tmpPath[512] = "";
            if (src.mode == 0 && sel->isDir && !sel->isUpEntry) {
                _snprintf(tmpPath, sizeof(tmpPath), "%s", sel->name);
                tmpPath[sizeof(tmpPath)-1] = 0;
                NormalizeDirA(tmpPath);
            }
            if (!tmpPath[0]) { app.SetStatus("Open a folder or select a drive"); break; }
            ULONGLONG bytes = DirSizeRecursiveA(tmpPath);
            char tmp[64]; FormatSize(bytes, tmp, sizeof(tmp));
            app.SetStatus("%s", tmp);
            break;
        }

        ULONGLONG bytes = DirSizeRecursiveA(srcFull);
        char tmp[64]; FormatSize(bytes, tmp, sizeof(tmp));
        app.SetStatus("%s", tmp);
        break;
    }

    // ---- Go to root (or back to drive list) ----------------------------------
    case ACT_GOROOT:
        if (src.mode == 1){
            if (!IsDriveRoot(src.curPath)){
                char root[4] = { src.curPath[0], ':', '\\', 0 };
                strncpy(src.curPath, root, sizeof(src.curPath)-1);
                src.curPath[3] = 0;
                src.sel = 0; src.scroll = 0;
                ListDirectory(src.curPath, src.items);
            } else {
                // Already at root -> switch to drive list
                src.mode = 0; src.curPath[0] = 0; src.sel = 0; src.scroll = 0;
                BuildDriveItems(src.items);
            }
        } else {
            // In drive list: refresh it
            BuildDriveItems(src.items);
        }
        break;

    // ---- Marking operations ---------------------------------------------------
    case ACT_MARK_ALL:
    {
        Pane& p = app.m_pane[app.m_active];
        if (p.mode==1 && !p.items.empty()){
            int n=0;
            for (size_t i=0; i<p.items.size(); ++i){
                if (!p.items[i].isUpEntry && !p.items[i].marked){
                    p.items[i].marked = true; ++n;
                }
            }
            if (n>0) app.SetStatus("Marked %d item%s", n, (n==1?"":"s"));
            else     app.SetStatus("All already marked");
        }
        break;
    }

    case ACT_INVERT_MARKS:
    {
        Pane& p = app.m_pane[app.m_active];
        if (p.mode==1 && !p.items.empty()){
            int toggled=0;
            for (size_t i=0;i<p.items.size(); ++i){
                if (!p.items[i].isUpEntry){
                    p.items[i].marked = !p.items[i].marked;
                    ++toggled;
                }
            }
            app.SetStatus("Inverted marks (%d)", toggled);
        }
        break;
    }

    case ACT_CLEAR_MARKS:
    {
        Pane& p = app.m_pane[app.m_active];
        if (p.mode==1 && !p.items.empty()){
            int cleared=0;
            for (size_t i=0;i<p.items.size(); ++i){
                if (p.items[i].marked){ p.items[i].marked=false; ++cleared; }
            }
            app.SetStatus(cleared ? "Cleared %d" : "No marks", cleared);
        }
        break;
    }

    // ---- Format cache (X/Y/Z + clear E:\CACHE) --------------------------------
    case ACT_FORMAT_CACHE:
    {
        app.SetStatus("Formatting cache partitions (X/Y/Z)...");
        const bool ok = FormatCacheXYZ(0, true);  // 0 => default 16KiB; also clears E:\CACHE
        if (!ok) { app.SetStatus("Format cache failed"); break; }

        // If either pane is browsing X:, Y:, or Z:, refresh it
        for (int p = 0; p < 2; ++p) {
            Pane& pane = app.m_pane[p];
            if (pane.mode == 1 && pane.curPath[0]) {
                char dl = (char)toupper((unsigned char)pane.curPath[0]);
                if (dl == 'X' || dl == 'Y' || dl == 'Z') {
                    ListDirectory(pane.curPath, pane.items);
                }
            }
        }
        app.RefreshPane(app.m_pane[0]);
        app.RefreshPane(app.m_pane[1]);
        app.SetStatus("Formatted X/Y/Z and E:\\CACHE");
        break;
    }

    // ---- Switch active pane ---------------------------------------------------
    case ACT_SWITCHMEDIA:
        app.m_active = 1 - app.m_active;
        break;
	
	case ACT_APPLYIPS:
		if (ext && _stricmp(ext, "ips") == 0 && ext2 && _stricmp(ext2, "xbe") == 0) {
			if (applyIPS(srcFull, dstFull) == E_NO_ERROR) app.SetStatus("Patch applied");
			else app.SetStatus("Patch failed");
		}
		break;

	case ACT_CREATEBAK:
	{
		if (ext && _stricmp(ext, "xbe") == 0) {
			switch (createBak(srcFull, false)) {
			case E_NO_ERROR: 
				app.SetStatus("Bak created");
				break;
			case E_CANNOT_OVR: 
				app.SetStatus("Bak already exists");
				break;
			default: 
				app.SetStatus("Bak failed");
			}
		}
		app.RefreshPane(app.m_pane[0]);
        app.RefreshPane(app.m_pane[1]);
		break;
	}
	case ACT_RESTOREBAK:
		if (ext && _stricmp(ext, "bak") == 0) {
			if (restoreBak(srcFull, true) == E_NO_ERROR) app.SetStatus("Bak restored");
			else app.SetStatus("Restore failed");
		}
		app.RefreshPane(app.m_pane[0]);
        app.RefreshPane(app.m_pane[1]);
		break;

    case ACT_UNZIPHERE:
    case ACT_UNZIPTO:

		if (ext && _stricmp(ext, "zip") == 0) {

			// Resolve destination
			char dstDir[512];
            if (act == ACT_UNZIPHERE) {
                if (!app.ResolveSrcDir(dstDir, sizeof(dstDir))) {
                    app.SetStatus("Pick a destination");
                    break;
                }
                if ((dstDir[0] == 'D' || dstDir[0] == 'd') && dstDir[1] == ':') {
                    app.SetStatus("Cannot extract to D:\\");
                    break;
                }
                NormalizeDirA(dstDir);
                if (!CanWriteHereA(dstDir)) {
                    app.SetStatusLastErr("Dest not writable");
                    break;
                }
            }
            else if (act == ACT_UNZIPTO) {
                if (!app.ResolveDestDir(dstDir, sizeof(dstDir))) {
                    app.SetStatus("Pick a destination");
                    break;
                }
                if ((dstDir[0] == 'D' || dstDir[0] == 'd') && dstDir[1] == ':') {
                    app.SetStatus("Cannot extract to D:\\");
                    break;
                }
                NormalizeDirA(dstDir);
                if (!CanWriteHereA(dstDir)) {
                    app.SetStatusLastErr("Dest not writable");
                    break;
                }
            }

			UNZIP* zip = new UNZIP;

			if (zip->openZIP(srcFull, zipFile_Open, zipFile_Close, zipFile_Read, zipFile_Seek) != UNZ_OK) {
				zip->closeZIP();
				app.SetStatus("Bad zip file");
				break;
			}

			int rc = zip->gotoFirstFile();

			unz_file_info fi;
			ULONGLONG total = 0;
            char szName[512];

			// Compute total bytes for progress bar
			while (rc == UNZ_OK) {
				rc = zip->getFileInfo(&fi, szName, 512, NULL, 0, NULL, 0);
				if (rc == UNZ_OK) {
					total += fi.uncompressed_size;
					rc = zip->gotoNextFile();
				}
			}

			if (total == 0) {
				app.SetStatus("Bad zip file");
				break;
			}

			// --- preflight free-space check on destination ---
			ULONGLONG freeB = 0, totB = 0;
			GetDriveFreeTotal(dstDir, freeB, totB);
			if (total > freeB) {
				zip->closeZIP();
				char need[64], have[64];
				FormatSize(total, need, sizeof(need));
				FormatSize(freeB, have, sizeof(have));
				app.SetStatus("Not enough space: need %s, have %s", need, have);
				break;
			}
			// --- end preflight ---

            if ((rc = zip->gotoFirstFile()) == UNZ_OK) {
                rc = zip->getFileInfo(&fi, szName, 512, NULL, 0, NULL, 0);
            }

            // Begin progress + set callback
            app.BeginProgress(total, szName, "Extracting...");
            CopyProgCtx ctx = { &app, 0, false, false, 0, false };
            SetCopyProgressCallback(CopyProgThunk, &ctx);

            ULONGLONG base = 0; // cumulative bytes completed
			size_t extractedOk = 0, skipped = 0;
            

			while (rc == UNZ_OK) {

                ctx.base = base;
                if (ctx.canceled) break;

				if ((rc = ExtractCurrentFile(zip, dstDir, true, false)) != UNZ_OK) skipped += 1;
				else extractedOk += 1;

                base += fi.uncompressed_size;

                if ((rc = zip->gotoNextFile()) == UNZ_OK) {
                    rc = zip->getFileInfo(&fi, szName, 512, NULL, 0, NULL, 0);
                }

                if (CopyProgress::g_copyProgFn) {
                    if (!CopyProgress::g_copyProgFn(base, total, szName, CopyProgress::g_copyProgUser)) {
                        break; // canceled
                    }
                }

			} //end while

            // End progress and clear callback
            SetCopyProgressCallback(NULL, NULL);
            app.EndProgress();

            if (ctx.canceled) {
                while (rc == UNZ_OK) {
                    ++skipped;
                    rc = zip->gotoNextFile();
                }
                app.SetStatus("Extraction canceled (%u extracted, %u skipped)", (unsigned)extractedOk, (unsigned)skipped);
                break;
            }
            else {
                // Final toast that reflects what actually happened
                app.SetStatus("%u extracted, %u skipped", (unsigned)extractedOk, (unsigned)skipped);
            }

			zip->closeZIP();

		}

        app.RefreshPane(app.m_pane[0]);
        app.RefreshPane(app.m_pane[1]);
		break;

    } // switch
}

} // namespace AppActions
