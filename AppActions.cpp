#include "AppActions.h"
#include "FileBrowserApp.h"
#include "FsUtil.h"

#include <stdio.h>
#include <string.h>

namespace AppActions {

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
        }
        break;

    case ACT_COPY:
    {
        if (!sel || sel->isUpEntry) { app.SetStatus("Nothing to copy"); break; }

        char dstDir[512];
        if (!app.ResolveDestDir(dstDir, sizeof(dstDir))) {
            app.SetStatus("Pick a destination (open a folder or select a drive)");
            break;
        }
        if ((dstDir[0]=='D' || dstDir[0]=='d') && dstDir[1]==':') {
            app.SetStatus("Cannot copy to D:\\ (read-only)");
            break;
        }

        NormalizeDirA(dstDir);
        if (!CanWriteHereA(dstDir)){ app.SetStatusLastErr("Dest not writable"); break; }

        if (CopyRecursiveA(srcFull, dstDir)) {
            Pane& dstp = app.m_pane[1 - app.m_active];
            if (dstp.mode==1 && _stricmp(dstp.curPath, dstDir)==0)
                ListDirectory(dstp.curPath, dstp.items);
            app.SetStatus("Copied to %s", dstDir);
        } else {
            app.SetStatusLastErr("Copy failed");
        }
        app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);
        break;
    }

    case ACT_MOVE:
    {
        if (!sel || sel->isUpEntry) { app.SetStatus("Nothing to move"); break; }

        char dstDir[512];
        if (!app.ResolveDestDir(dstDir, sizeof(dstDir))) {
            app.SetStatus("Pick a destination (open a folder or select a drive)");
            break;
        }
        if ((dstDir[0]=='D' || dstDir[0]=='d') && dstDir[1]==':') {
            app.SetStatus("Cannot move to D:\\ (read-only)");
            break;
        }

        NormalizeDirA(dstDir);
        if (!CanWriteHereA(dstDir)){ app.SetStatusLastErr("Dest not writable"); break; }

        if (CopyRecursiveA(srcFull, dstDir)) {
            if (!DeleteRecursiveA(srcFull)) {
                app.SetStatusLastErr("Move: delete source failed");
            } else {
                Pane& srcp = app.m_pane[app.m_active];
                Pane& dstp = app.m_pane[1 - app.m_active];
                if (dstp.mode==1 && _stricmp(dstp.curPath, dstDir)==0)
                    ListDirectory(dstp.curPath, dstp.items);
                if (srcp.mode==1) {
                    ListDirectory(srcp.curPath, srcp.items);
                    if (srcp.sel >= (int)srcp.items.size()) srcp.sel = (int)srcp.items.size()-1;
                }
                app.SetStatus("Moved to %s", dstDir);
            }
        } else {
            app.SetStatusLastErr("Move: copy failed");
        }
        app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);
        break;
    }

    case ACT_DELETE:
        if (sel){
            if (DeleteRecursiveA(srcFull)){
                ListDirectory(src.curPath, src.items);
                if (src.sel >= (int)src.items.size()) src.sel = (int)src.items.size()-1;
                app.SetStatus("Deleted");
            } else {
                app.SetStatus("Delete failed");
            }
        }
        app.RefreshPane(app.m_pane[0]); app.RefreshPane(app.m_pane[1]);
        break;

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

    case ACT_SWITCHMEDIA:
        app.m_active = 1 - app.m_active;
        break;
    }
}

} // namespace AppActions
