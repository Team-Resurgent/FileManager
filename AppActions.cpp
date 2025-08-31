#include "AppActions.h"
#include "FileBrowserApp.h"
#include "FsUtil.h"

#include <stdio.h>
#include <string.h>

static bool CopyProgressThunk(ULONGLONG done, ULONGLONG total,
                              const char* current, void* user)
{
    FileBrowserApp* app = (FileBrowserApp*)user;
    if (app) app->UpdateProgress(done, total, current);
    return true; // return false to support cancel later
}

struct CopyProgCtx {
    FileBrowserApp* app;
    ULONGLONG base;     // bytes already completed from previous items
};

static bool CopyProgThunk(ULONGLONG done, ULONGLONG total, const char* label, void* user){
    CopyProgCtx* c = (CopyProgCtx*)user;
    c->app->UpdateProgress(c->base + done, total, label);
    return true; // (add cancel logic later if desired)
}

static int SameDriveLetter(const char* a, const char* b){
    if (!a || !b || !a[0] || !b[0]) return 0;
    return toupper((unsigned char)a[0]) == toupper((unsigned char)b[0]);
}

static void NormalizeSlashEndLocal(char* s, size_t cap){
    size_t n = s ? strlen(s) : 0;
    if (n && s[n-1] != '\\' && n+1 < cap){ s[n] = '\\'; s[n+1] = 0; }
}

static int IsSubPathCaseI(const char* parent, const char* child){
    char p[512], c[512];
    _snprintf(p, sizeof(p), "%s", parent ? parent : ""); p[sizeof(p)-1] = 0;
    _snprintf(c, sizeof(c), "%s", child  ? child  : ""); c[sizeof(c)-1] = 0;
    NormalizeSlashEndLocal(p, sizeof(p));
    NormalizeSlashEndLocal(c, sizeof(c));
    return _strnicmp(p, c, (int)strlen(p)) == 0; // child starts with parent?
}

static const char* BaseNameOf(const char* path){
    const char* s = path ? strrchr(path, '\\') : 0;
    return s ? (s+1) : path;
}

namespace AppActions {

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

void Execute(Action act, FileBrowserApp& app)
{
    // Access the panes (allowed because this function is a friend)
    Pane& src = app.m_pane[app.m_active];
    Pane& dst = app.m_pane[1 - app.m_active];

    const Item* sel = NULL;
    if (!src.items.empty()) sel = &src.items[src.sel];

    char srcFull[512] = "";
    if (sel && src.mode == 1 && !sel->isUpEntry)
        JoinPath(srcFull, sizeof(srcFull), src.curPath, sel->name);

    switch (act)
    {
    case ACT_OPEN:
		if (sel){
			if (sel->isUpEntry) { app.UpOne(src); }
			else if (sel->isDir) { app.EnterSelection(src); }
			else {
				// If it's an XBE, launch it
				if (HasXbeExt(sel->name)){
					char full[512]; JoinPath(full, sizeof(full), src.curPath, sel->name);
					if (!LaunchXbeA(full)){
						app.SetStatusLastErr("Launch failed");
					}
				}
			}
		}
		break;

    case ACT_COPY:
	{
		if (src.mode != 1) { app.SetStatus("Open a folder"); break; }

		char dstDir[512];
		if (!app.ResolveDestDir(dstDir, sizeof(dstDir))) { app.SetStatus("Pick a destination"); break; }
		if ((dstDir[0]=='D'||dstDir[0]=='d') && dstDir[1]==':'){ app.SetStatus("Cannot copy to D:\\"); break; }
		NormalizeDirA(dstDir);
		if (!CanWriteHereA(dstDir)){ app.SetStatusLastErr("Dest not writable"); break; }

		std::vector<std::string> srcs;
		GatherMarkedOrSelectedFullPaths(src, srcs);
		if (srcs.empty()) { app.SetStatus("Nothing to copy"); break; }

		// total bytes across all items
		ULONGLONG total=0;
		for (size_t i=0;i<srcs.size();++i) total += DirSizeRecursiveA(srcs[i].c_str());

		app.BeginProgress(total, srcs[0].c_str(), "Copying...");
		CopyProgCtx ctx = { &app, 0 };
		SetCopyProgressCallback(CopyProgThunk, &ctx);

		ULONGLONG base = 0;
		for (size_t i=0;i<srcs.size();++i){
			ULONGLONG thisSize = DirSizeRecursiveA(srcs[i].c_str());
			ctx.base = base;
			CopyRecursiveWithProgressA(srcs[i].c_str(), dstDir, total);
			base += thisSize;
		}

		SetCopyProgressCallback(NULL, NULL);
		app.EndProgress();

		// refresh dest pane listing if we copied into it
		Pane& dstp = app.m_pane[1 - app.m_active];
		if (dstp.mode==1 && _stricmp(dstp.curPath, dstDir)==0) ListDirectory(dstp.curPath, dstp.items);

		// clear marks on source and refresh both panes
		for (size_t i=0;i<src.items.size();++i) src.items[i].marked=false;
		app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);
		app.SetStatus("Copied %d item(s)", (int)srcs.size());
		break;
	}


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

		// total for progress
		ULONGLONG total=0;
		for (size_t i=0;i<srcs.size();++i) total += DirSizeRecursiveA(srcs[i].c_str());

		app.BeginProgress(total, srcs[0].c_str(), "Moving...");
		CopyProgCtx ctx = { &app, 0 };
		SetCopyProgressCallback(CopyProgThunk, &ctx);

		size_t movedOk = 0, failed = 0;
		ULONGLONG base = 0;

		for (size_t i=0;i<srcs.size();++i){
			const char* sp = srcs[i].c_str();
			ULONGLONG thisSize = DirSizeRecursiveA(sp);
			ctx.base = base;

			// build destination top (dstDir + basename(sp))
			const char* baseName = BaseNameOf(sp);
			char dstTop[512]; JoinPath(dstTop, sizeof(dstTop), dstDir, baseName);

			BOOL doneThis = FALSE;

			// Fast path: same drive + not moving into own subfolder
			if (SameDriveLetter(sp, dstDir) && !IsSubPathCaseI(sp, dstTop)) {
				if (MoveFileA(sp, dstTop)) {
					doneThis = TRUE;  // instant
				}
				// if rename fails (exists, etc), we fall back to copy+delete below
			}

			// Fallback: copy -> delete original (delete only if copy succeeded)
			if (!doneThis) {
				if (CopyRecursiveWithProgressA(sp, dstDir, total)) {
					if (DeleteRecursiveA(sp)) {
						doneThis = TRUE;
					} else {
						// optional: consider removing the just-copied dstTop on failure to delete original
					}
				}
			}

			if (doneThis) ++movedOk; else ++failed;
			base += thisSize;
		}

		SetCopyProgressCallback(NULL, NULL);
		app.EndProgress();

		// refresh panes & clear marks
		for (size_t i=0;i<src.items.size();++i) src.items[i].marked=false;
		if (src.mode==1) ListDirectory(src.curPath, src.items);
		{
			Pane& dstp = app.m_pane[1 - app.m_active];
			if (dstp.mode==1 && _stricmp(dstp.curPath, dstDir)==0) ListDirectory(dstp.curPath, dstp.items);
		}
		app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);

		if (failed == 0) app.SetStatus("Moved %u item(s)", (unsigned)movedOk);
		else             app.SetStatus("Moved %u, %u failed", (unsigned)movedOk, (unsigned)failed);
		break;
	}




    case ACT_DELETE:
	{
		if (src.mode != 1) { app.SetStatus("Open a folder"); break; }

		std::vector<std::string> srcs;
		GatherMarkedOrSelectedFullPaths(src, srcs);
		if (srcs.empty()) { app.SetStatus("Nothing to delete"); break; }

		int ok=0;
		for (size_t i=0;i<srcs.size();++i) if (DeleteRecursiveA(srcs[i].c_str())) ++ok;

		for (size_t i=0;i<src.items.size();++i) src.items[i].marked=false;
		if (src.mode==1) ListDirectory(src.curPath, src.items);
		app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);
		app.SetStatus("Deleted %d / %d", ok, (int)srcs.size());
		break;
	}


    case ACT_RENAME:
        if (sel && src.mode==1 && !sel->isUpEntry){
            app.BeginRename(src.curPath, sel->name);
        } else {
            app.SetStatus("Open a folder and select an item");
        }
        break;

    case ACT_MKDIR:
    {
        char baseDir[512] = {0};

        if (src.mode == 1) {
            _snprintf(baseDir, sizeof(baseDir), "%s", src.curPath);
        } else if (!src.items.empty()) {
            const Item& di = src.items[src.sel];
            if (di.isDir && !di.isUpEntry) {
                _snprintf(baseDir, sizeof(baseDir), "%s", di.name); // e.g. "E:\\"
            }
        }
        baseDir[sizeof(baseDir)-1] = 0;
        if (!baseDir[0]) { app.SetStatus("Open a folder or select a drive first"); break; }

        if ((baseDir[0]=='D' || baseDir[0]=='d') && baseDir[1]==':') {
            app.SetStatus("Cannot create on D:\\ (read-only)"); break;
        }

        NormalizeDirA(baseDir);
        if (!CanWriteHereA(baseDir)) { app.SetStatusLastErr("Dest not writable"); break; }

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

        if (src.mode == 1 && _stricmp(src.curPath, baseDir) == 0) {
            app.SelectItemInPane(src, nameBuf);
        }
        break;
    }

    case ACT_CALCSIZE:
        if (sel){
            ULONGLONG bytes = DirSizeRecursiveA(srcFull);
            char tmp[64]; FormatSize(bytes, tmp, sizeof(tmp));
            app.SetStatus("%s", tmp);
        }
        break;

    case ACT_GOROOT:
        if (src.mode == 1){
            if (!IsDriveRoot(src.curPath)){
                char root[4] = { src.curPath[0], ':', '\\', 0 };
                strncpy(src.curPath, root, sizeof(src.curPath)-1);
                src.curPath[3] = 0;
                src.sel = 0; src.scroll = 0;
                ListDirectory(src.curPath, src.items);
            } else {
                src.mode = 0; src.curPath[0] = 0; src.sel = 0; src.scroll = 0;
                BuildDriveItems(src.items);
            }
        } else {
            BuildDriveItems(src.items);
        }
        break;

	case ACT_MARK_ALL:
	{
		Pane& p = app.m_pane[app.m_active];
		if (p.mode==1 && !p.items.empty()){
			int n=0;
			for (size_t i=0;i<p.items.size(); ++i){
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

    case ACT_SWITCHMEDIA:
        app.m_active = 1 - app.m_active;
        break;
    }
}

} // namespace AppActions
