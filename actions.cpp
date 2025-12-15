#include "actions.h"

#include "main.h"
#include "xips.h"
#include "xpatchlibUtils.h"
#include "unzipLIBUtils.h"

// The "toast window" behavior:
//   1) First B press shows "Press B again to cancel" and arms a window.
//   2) If the second B happens while the toast is still visible, we cancel.
//   3) If toast expires before second B, the next B just re-arms the window.
struct CopyProgCtx {
    ULONGLONG base;     // bytes completed from previous items (offset)
    bool canceled;      // set when user confirms cancel

    bool confirmArmed;  // true after the first B press
    DWORD confirmUntil; // snapshot of when the toast will expire (informational)
    bool prevB;         // for rising-edge detection of B
};

// Progress callback used by file copy/move loops
static bool CopyProgThunk(LONGLONG done, LONGLONG total, const char* label, void* user) {
    CopyProgCtx* ctx = (CopyProgCtx*)user;
    FileBrowserApp& app = FileBrowserApp::Get();

    if (done == LLONG_MIN) done = app.m_prog.done;
    else if (done < 0) {
        done = _abs64(done);
        done += app.m_prog.done;
        done += ctx->base;
    }
    else done += ctx->base;

    total = (total == LLONG_MIN) ? 0 : total;

    // Poll controller to read B presses while copying
    XBInput_GetInput();
    const XBGAMEPAD& pad = g_Gamepads[0];
    const bool  bNow = (pad.bAnalogButtons[XINPUT_GAMEPAD_B] > 30);
    const DWORD now  = GetTickCount();

    app.UpdateProgress(done, total, label);

    // Rising-edge B?
    if (bNow && !ctx->prevB){
        const DWORD statusUntil = app.StatusUntilMs();
        const bool  toastAlive  = (now < statusUntil);

        if (ctx->confirmArmed && toastAlive){
            // Second B while the toast is still up => cancel
            ctx->canceled = true;
            SetLastError(ERROR_OPERATION_ABORTED);
            ctx->prevB = bNow;
            return false; // abort copy/move
        } else {
            // First B: arm and show "press B again" toast
            ctx->confirmArmed = true;
            app.SetStatus("Press \x81 again to cancel");
            ctx->confirmUntil = app.StatusUntilMs(); // snapshot (optional)
        }
    }

    // Disarm if toast has expired
    if (ctx->confirmArmed && now >= app.StatusUntilMs()) ctx->confirmArmed = false;

    ctx->prevB = bNow;
    return true; // continue
}

// Return 1 if both paths are on the same drive letter (case-insensitive)
static int SameDriveLetter(const char* a, const char* b){
    if (!a || !b || !a[0] || !b[0]) return 0;
    return toupper((unsigned char)a[0]) == toupper((unsigned char)b[0]);
}

// Ensure trailing backslash on non-empty strings
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

// Basename (pointer into input string), e.g., "E:\A\B\C" -> "C"
static const char* BaseNameOf(const char* path){
    const char* s = path ? strrchr(path, '\\') : 0;
    return s ? (s+1) : path;
}

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
void Actions::Execute(Action act) {
    FileBrowserApp& app = FileBrowserApp::Get();
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

    switch (act) {

        // ---- Open / Enter / Launch --------------------------------------------
    case ACT_OPEN:
    {
        if (sel) {
            if (sel->isUpEntry) { app.UpOne(src); }
            else if (sel->isDir) { app.EnterSelection(src); }
            else if (HasXbeExt(sel->name)) {
                char full[512]; JoinPath(full, sizeof(full), src.curPath, sel->name);
                if (!LaunchXbeA(full)) app.SetStatusLastErr("Launch failed");
            }
        }
        break;
    }

    // ---- Copy -----------------------------------------------------------------
    case ACT_COPY:
    {
        if (src.mode != 1) { app.SetStatus("Open a folder"); break; }

        // Resolve destination (other pane preferred)
        char dstDir[512];
        if (!app.ResolveDestDir(dstDir, sizeof(dstDir))) { app.SetStatus("Pick a destination"); break; }
        if ((dstDir[0] == 'D' || dstDir[0] == 'd') && dstDir[1] == ':') { app.SetStatus("Cannot copy to D:\\"); break; }
        NormalizeDirA(dstDir);
        if (!CanWriteHereA(dstDir)) { app.SetStatusLastErr("Dest not writable"); break; }

        // Gather sources
        std::vector<std::string> srcs;
        GatherMarkedOrSelectedFullPaths(src, srcs);
        if (srcs.empty()) { app.SetStatus("Nothing to copy"); break; }

        // Compute total bytes for progress bar
        ULONGLONG total = 0;
        for (size_t i = 0; i < srcs.size(); ++i) total += DirSizeRecursiveA(srcs[i].c_str());

        // --- preflight free-space check on destination (kept from your version) ---
        {
            ULONGLONG freeB = 0, totB = 0;
            GetDriveFreeTotal(dstDir, freeB, totB);
            if (total > freeB) {
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
        CopyProgCtx ctx = { 0, false, false, 0, false };
        SetCopyProgressCallback(CopyProgThunk, &ctx);

        ULONGLONG base = 0;           // cumulative bytes completed
        char lastDstTop[512] = { 0 };   // for cancel cleanup

        // NEW: track results so the final toast reflects reality
        size_t copiedOk = 0, failed = 0, skipped = 0;

        for (size_t i = 0; i < srcs.size(); ++i) {
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
                ULONGLONG freeB = 0, totB = 0;
                GetDriveFreeTotal(lastDstTop, freeB, totB); // any path on dest volume is fine
                if (thisSize > freeB) {
                    char need[64], have[64];
                    FormatSize(thisSize, need, sizeof(need));
                    FormatSize(freeB, have, sizeof(have));
                    app.SetStatus("Not enough space for %s: need %s, have %s", bn, need, have);
                    break; // stop the whole batch (change to 'continue;' to try remaining items)
                }
            }
            // --- end per-item check ---

            if (!CopyRecursiveWithProgressA(sp, dstDir, total)) {
                if (ctx.canceled) {
                    // Remove partial destination of the current item, then stop
                    DeleteRecursiveA(lastDstTop);
                    break;
                }
                // Non-cancel failure
                ++failed;
            }
            else {
                base += thisSize;
                ++copiedOk;
            }
        }

        // End progress and clear callback
        SetCopyProgressCallback(NULL, NULL);
        app.EndProgress();

        if (ctx.canceled) {
            app.SetStatus("Copy canceled (%u done, %u skipped, %u failed)",
                (unsigned)copiedOk, (unsigned)skipped, (unsigned)failed);
            app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);
            break;
        }

        // If we copied into the currently displayed dest folder, refresh it
        Pane& dstp = app.m_pane[1 - app.m_active];
        if (dstp.mode == 1 && _stricmp(dstp.curPath, dstDir) == 0) ListDirectory(dstp.curPath, dstp.items);

        // Clear marks and refresh both panes
        for (size_t i = 0; i < src.items.size(); ++i) src.items[i].marked = false;
        app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);

        // Final toast that reflects what actually happened
        if (failed == 0 && skipped == 0) {
            app.SetStatus("Copied %u item(s)", (unsigned)copiedOk);
        }
        else {
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
        if ((dstDir[0] == 'D' || dstDir[0] == 'd') && dstDir[1] == ':') { app.SetStatus("Cannot move to D:\\"); break; }
        NormalizeDirA(dstDir);
        if (!CanWriteHereA(dstDir)) { app.SetStatusLastErr("Dest not writable"); break; }

        std::vector<std::string> srcs;
        GatherMarkedOrSelectedFullPaths(src, srcs);
        if (srcs.empty()) { app.SetStatus("Nothing to move"); break; }

        // Total bytes for progress
        ULONGLONG total = 0;
        for (size_t i = 0; i < srcs.size(); ++i) total += DirSizeRecursiveA(srcs[i].c_str());

        // --- preflight space only when cross-volume (move will copy+delete) ---
        {
            const bool sameVol = SameDriveLetter(src.curPath, dstDir) != 0;
            if (!sameVol) {
                ULONGLONG freeB = 0, totB = 0;
                GetDriveFreeTotal(dstDir, freeB, totB);
                if (total > freeB) {
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
        CopyProgCtx ctx = { 0, false, false, 0, false };
        SetCopyProgressCallback(CopyProgThunk, &ctx);

        size_t movedOk = 0, failed = 0, skipped = 0;  // NEW: track results
        ULONGLONG base = 0;

        for (size_t i = 0; i < srcs.size(); ++i) {
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
                ULONGLONG freeB = 0, totB = 0;
                GetDriveFreeTotal(dstTop, freeB, totB);
                if (thisSize > freeB) {
                    char need[64], have[64];
                    FormatSize(thisSize, need, sizeof(need));
                    FormatSize(freeB, have, sizeof(have));
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
                if (!CopyRecursiveWithProgressA(sp, dstDir, total)) {
                    if (ctx.canceled) {
                        // Clean partial dest and stop
                        DeleteRecursiveA(dstTop);
                        break;
                    }
                    // Non-cancel failure
                    ++failed;
                }
                else {
                    if (DeleteRecursiveA(sp)) {
                        doneThis = TRUE;
                    }
                    else {
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

        if (ctx.canceled) {
            app.SetStatus("Move canceled (%u done, %u skipped, %u failed)",
                (unsigned)movedOk, (unsigned)skipped, (unsigned)failed);
            // On cancel we keep originals; clear marks and refresh UI
            for (size_t i = 0; i < src.items.size(); ++i) src.items[i].marked = false;
            if (src.mode == 1) ListDirectory(src.curPath, src.items);
            {
                Pane& dstp = app.m_pane[1 - app.m_active];
                if (dstp.mode == 1 && _stricmp(dstp.curPath, dstDir) == 0) ListDirectory(dstp.curPath, dstp.items);
            }
            app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);
            break;
        }

        // Normal completion: refresh both panes, clear marks
        for (size_t i = 0; i < src.items.size(); ++i) src.items[i].marked = false;
        if (src.mode == 1) ListDirectory(src.curPath, src.items);
        {
            Pane& dstp = app.m_pane[1 - app.m_active];
            if (dstp.mode == 1 && _stricmp(dstp.curPath, dstDir) == 0) ListDirectory(dstp.curPath, dstp.items);
        }
        app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);

        // NEW: accurate final toast
        if (failed == 0 && skipped == 0) {
            app.SetStatus("Moved %u item(s)", (unsigned)movedOk);
        }
        else {
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

        int ok = 0;
        for (size_t i = 0; i < srcs.size(); ++i) if (DeleteRecursiveA(srcs[i].c_str())) ++ok;

        // Clear marks, refresh, and toast result
        for (size_t i = 0; i < src.items.size(); ++i) src.items[i].marked = false;
        if (src.mode == 1) ListDirectory(src.curPath, src.items);
        app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);
        app.SetStatus("Deleted %d / %d", ok, (int)srcs.size());
        break;
    }

    // ---- Rename (opens modal OSK) ---------------------------------------------
    case ACT_RENAME:
    {
        if (sel && src.mode == 1 && !sel->isUpEntry) {
            app.BeginRename(src.curPath, sel->name);
        }
        else {
            app.SetStatus("Open a folder and select an item");
        }
        break;
    }

    // ---- Make new folder ------------------------------------------------------
    case ACT_MKDIR:
    {
        char baseDir[512] = { 0 };

        if (src.mode == 1) {
            _snprintf(baseDir, sizeof(baseDir), "%s", src.curPath);
        }
        else if (!src.items.empty()) {
            const Item& di = src.items[src.sel];
            if (di.isDir && !di.isUpEntry) {
                _snprintf(baseDir, sizeof(baseDir), "%s", di.name); // drive root, e.g. "E:\"
            }
        }
        baseDir[sizeof(baseDir) - 1] = 0;
        if (!baseDir[0]) { app.SetStatus("Open a folder or select a drive first"); break; }

        if ((baseDir[0] == 'D' || baseDir[0] == 'd') && baseDir[1] == ':') {
            app.SetStatus("Cannot create on D:\\ (read-only)"); break;
        }

        NormalizeDirA(baseDir);
        if (!CanWriteHereA(baseDir)) { app.SetStatusLastErr("Dest not writable"); break; }

        // Auto-name NewFolder[/N] without clobbering existing names
        char nameBuf[64];
        char target[512];
        int idx = 0;
        for (;;) {
            if (idx == 0) _snprintf(nameBuf, sizeof(nameBuf), "NewFolder");
            else          _snprintf(nameBuf, sizeof(nameBuf), "NewFolder%d", idx);
            nameBuf[sizeof(nameBuf) - 1] = 0;

            JoinPath(target, sizeof(target), baseDir, nameBuf);

            if (!DirExistsA(target)) {
                if (CreateDirectoryA(target, NULL)) {
                    app.SetStatus("Created %s", nameBuf);
                }
                else {
                    app.SetStatusLastErr("Create folder failed");
                }
                break;
            }
            if (++idx > 999) { app.SetStatus("Create folder failed (names exhausted)"); break; }
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
                tmpPath[sizeof(tmpPath) - 1] = 0;
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

    // ---- Go to root (or back to drive list) -----------------------------------
    case ACT_GOROOT:
    {
        if (src.mode == 1) {
            if (!IsDriveRoot(src.curPath)) {
                char root[4] = { src.curPath[0], ':', '\\', 0 };
                strncpy(src.curPath, root, sizeof(src.curPath) - 1);
                src.curPath[3] = 0;
                src.sel = 0; src.scroll = 0;
                ListDirectory(src.curPath, src.items);
            }
            else {
                // Already at root -> switch to drive list
                src.mode = 0; src.curPath[0] = 0; src.sel = 0; src.scroll = 0;
                BuildDriveItems(src.items);
            }
        }
        else {
            // In drive list: refresh it
            BuildDriveItems(src.items);
        }
        break;
    }

    // ---- Marking operations ---------------------------------------------------
    case ACT_MARK_ALL:
    {
        Pane& p = app.m_pane[app.m_active];
        if (p.mode==1 && !p.items.empty()) {
            int n=0;
            for (size_t i=0; i<p.items.size(); ++i) {
                if (!p.items[i].isUpEntry && !p.items[i].marked) {
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
        if (p.mode==1 && !p.items.empty()) {
            int toggled=0;
            for (size_t i=0;i<p.items.size(); ++i) {
                if (!p.items[i].isUpEntry) {
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
        if (p.mode==1 && !p.items.empty()) {
            int cleared=0;
            for (size_t i=0;i<p.items.size(); ++i) {
                if (p.items[i].marked) { 
                    p.items[i].marked=false; 
                    ++cleared; 
                }
            }
            app.SetStatus(cleared ? "Cleared marks (%d)" : "No marks", cleared);
        }
        break;
    }

    // ---- Format cache (X/Y/Z + clear E:\CACHE) --------------------------------
    case ACT_FORMAT_CACHE:
    {
        app.SetStatus("Formatting cache partitions (X/Y/Z)...");
        const bool ok = FormatCacheXYZ(0, true);  // 0 => default 16KiB; also clears E:\CACHE
        if (!ok) { 
            app.SetStatus("Format cache failed"); 
            break; 
        }

        app.RefreshPane(app.m_pane[0]);
        app.RefreshPane(app.m_pane[1]);
        app.SetStatus("Formatted X/Y/Z and E:\\CACHE");
        break;
    }

    // ---- Switch active pane ---------------------------------------------------
    case ACT_SWITCHMEDIA:
    {
        app.m_active = 1 - app.m_active;
        break;
    }
	
    // ---- Apply ips patch ------------------------------------------------------
    case ACT_APPLYIPS:
    {
        if (ext && _stricmp(ext, "ips") == 0 && ext2 && _stricmp(ext2, "xbe") == 0) {

            bool bkcr = false;
            bool bkcrfail = false;
            bool bkcrcanc = false;
            bool ptchfail = false;

            char dst[1024];

            strcpy(dst, dstFull);
            strcat(dst, ".bak");

            while (true) {

                // Resolve destination
                char dstDir[512];
                app.ResolveDestDir(dstDir, sizeof(dstDir));
                NormalizeDirA(dstDir);

                if (!CanWriteHereA(dstDir)) {
                    bkcrfail = true;
                    break;
                }

                ULONGLONG total = DirSizeRecursiveA(dstFull);

                // --- preflight free-space check on destination ---
                ULONGLONG freeB = 0, totB = 0;
                GetDriveFreeTotal(dstDir, freeB, totB);
                if (total > freeB) {
                    bkcrfail = true;
                    break;
                }
                // --- end preflight ---

                ULONGLONG base = 0; // cumulative bytes completed

                char dstName[47];
                strcpy(dstName, sel2->name);
                strcat(dstName, ".bak");

                // Begin progress + set callback
                app.BeginProgress(total, dstName, "Creaking bak...");
                CopyProgCtx ctx = { 0, false, false, 0, false };
                SetCopyProgressCallback(CopyProgThunk, &ctx);

                int cb = CreateBak(dstFull, false, UpdateBakProgress);

                // End progress and clear callback
                SetCopyProgressCallback(NULL, NULL);
                app.EndProgress();

                switch (cb) {
                case E_NO_ERROR:
                    bkcr = true;
                    bkcrfail = false;
                    break;

                case E_CANNOT_OVR:
                    bkcr = false;
                    bkcrfail = false;
                    break;

                default:
                    bkcr = false;
                    bkcrfail = true;
                }

                if (bkcrfail) break;

                if (ctx.canceled) {
                    bkcrcanc = true;
                    break;
                }

                if (ApplyIPS(srcFull, dstFull) != E_NO_ERROR) ptchfail = true;

                break;
            }//while

            //Give result
            if (bkcrcanc) {
                remove(dst);
                app.SetStatus("Bak canceled, patch skipped");
            }
            else if (bkcrfail) {
                app.SetStatus("Bak failed, patch skipped");
            }
            else if (ptchfail) {
                if (bkcr) {
                    if (RestoreBak(dst, true) == E_NO_ERROR) app.SetStatus("Patch failed, bak restored");
                    else app.SetStatus("Patch failed, bak restore failed");
                }
                else app.SetStatus("Failed to apply patch");
            }
            else if (!bkcr) {
                app.SetStatus("Patch applied, bak skipped");
            }
            else {
                app.SetStatus("Patch applied, bak created");
            }

        }
        app.RefreshPane(app.m_pane[0]);
        app.RefreshPane(app.m_pane[1]);
        break;
    }

    // ---- Restore .bak file ----------------------------------------------------
    case ACT_RESTOREBAK:
    {
        if (ext && _stricmp(ext, "bak") == 0) {
            if (RestoreBak(srcFull, true) == E_NO_ERROR) app.SetStatus("Bak restored");
            else app.SetStatus("Restore failed");
        }
        app.RefreshPane(app.m_pane[0]);
        app.RefreshPane(app.m_pane[1]);
        break;
    }

    // ---- Unzip zip file -------------------------------------------------------
    case ACT_UNZIPHERE:
    case ACT_UNZIPTO:
    case ACT_UNZIPTOOTHER:
    {
        if (ext && _stricmp(ext, "zip") == 0) {

            // Resolve destination
            char dstDir[512];
            if (act == ACT_UNZIPHERE) {
                if (!app.ResolveSrcDir(dstDir, sizeof(dstDir))) {
                    app.SetStatus("Pick a destination");
                    break;
                }
            }
            else if (act == ACT_UNZIPTO) {
                if (!app.ResolveSrcDir(dstDir, sizeof(dstDir))) {
                    app.SetStatus("Pick a destination");
                    break;
                }
                char name[64];
                strcpy(name, sel->name);
                name[strlen(name) - 4] = '\0';
                strcat(dstDir, name);
            }
            else if (act == ACT_UNZIPTOOTHER) {
                if (!app.ResolveDestDir(dstDir, sizeof(dstDir))) {
                    app.SetStatus("Pick a destination");
                    break;
                }
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

            UNZIP* zip = new UNZIP;

            if (zip->openZIP(srcFull, ZipFile_Open, ZipFile_Close, ZipFile_Read, ZipFile_Seek) != UNZ_OK) {
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
            CopyProgCtx ctx = { 0, false, false, 0, false };
            SetCopyProgressCallback(CopyProgThunk, &ctx);

            size_t extractedOk = 0, skipped = 0;

            while (rc == UNZ_OK) {

                if (ctx.canceled) break;

                if ((rc = ExtractCurrentFileWithProgress(zip, dstDir, true)) != UNZ_OK) skipped += 1;
                else extractedOk += 1;

                if ((rc = zip->gotoNextFile()) == UNZ_OK) {
                    rc = zip->getFileInfo(&fi, szName, 512, NULL, 0, NULL, 0);
                }

                if (CopyProgress::g_copyProgFn) {
                    if (!CopyProgress::g_copyProgFn(LLONG_MIN, NULL, szName, CopyProgress::g_copyProgUser)) {
                        break; // canceled
                    }
                }

            } //end while

            FreeUnZipBuffer();

            // End progress and clear callback
            SetCopyProgressCallback(NULL, NULL);
            app.EndProgress();

            if (ctx.canceled) {
                while (rc == UNZ_OK) {
                    ++skipped;
                    rc = zip->gotoNextFile();
                }
                app.SetStatus("Extraction canceled (%u extracted, %u skipped)", (unsigned)extractedOk, (unsigned)skipped);
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
    }

    } // switch
}
