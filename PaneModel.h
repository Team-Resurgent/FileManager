#ifndef PANEMODEL_H
#define PANEMODEL_H

#include <vector>
#include "FsUtil.h"

// Public, light-weight model used by renderer/actions
struct Pane {
    std::vector<Item> items;
    char  curPath[512];
    int   mode;   // 0=drives, 1=dir
    int   sel;
    int   scroll;
    Pane(){ curPath[0]=0; mode=0; sel=0; scroll=0; }
};

#endif // PANEMODEL_H
