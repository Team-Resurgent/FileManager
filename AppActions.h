#pragma once
#include "PaneModel.h"
#include "FsUtil.h"

// Forward declare to avoid circular include
class FileBrowserApp;

// Central list of actions used across UI components
enum Action {
    ACT_OPEN,
    ACT_COPY,
    ACT_MOVE,
    ACT_DELETE,
    ACT_RENAME,
    ACT_MKDIR,
    ACT_CALCSIZE,
    ACT_GOROOT,
    ACT_SWITCHMEDIA
};

namespace AppActions {
    // Runs the requested action against the running app.
    // This is declared a 'friend' inside FileBrowserApp so it can access
    // private helpers (SelectItemInPane, RefreshPane, etc.)
    void Execute(Action act, FileBrowserApp& app);
}
