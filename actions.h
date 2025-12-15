#pragma once

typedef enum {
    ACT_NONE,
    ACT_OPEN,         // Enter dir / up one / launch .xbe (context-sensitive)
    ACT_COPY,         // Copy selected/marked items to other pane/dest
    ACT_MOVE,         // Move selected/marked items (rename within volume or copy+delete)
    ACT_DELETE,       // Delete selected/marked items (recursive)
    ACT_RENAME,       // Start on-screen keyboard to rename current item
    ACT_MKDIR,        // Create a new folder in active/selected location
    ACT_CALCSIZE,     // Calculate total size of selected item (recursive)
    ACT_GOROOT,       // Jump to drive root; if at root, return to drive list

    ACT_CLEAR_MARKS,  // Clear all mark flags in the active pane
    ACT_MARK_ALL,     // Mark all regular entries (skip "..")
    ACT_INVERT_MARKS, // Toggle mark flag on each regular entry

    ACT_SWITCHMEDIA,  // Switch active pane (left <-> right)
    ACT_FORMAT_CACHE, // Format X/Y/Z cache partitions (+ clear E:\CACHE)

    ACT_APPLYIPS,     //xpatchlib
    ACT_RESTOREBAK,   //xpatchlib
    ACT_UNZIPHERE,    //unzipLIB
    ACT_UNZIPTO,      //unzipLIB
    ACT_UNZIPTOOTHER, //unzipLIB

    ACT_CANCEL,

} Action;

class Actions {
public:
    static void Execute(Action act);
};
