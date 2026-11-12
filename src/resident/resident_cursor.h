#pragma once
#include <windows.h>

namespace istaroth {
// Owned by the UI thread. Balance only our ShowCursor changes, not the game's.
struct MenuCursor {
    RECT clip{};
    POINT position{};
    HCURSOR previous=nullptr;
    unsigned increments=0;
    bool saved=false;

    bool Save() {
        if(saved)return true;
        CURSORINFO info={sizeof(info)};
        if(!GetClipCursor(&clip)||!GetCursorPos(&position)||!GetCursorInfo(&info))return false;
        previous=info.hCursor;saved=true;return true;
    }
    bool Show() {
        if(!saved||!ClipCursor(nullptr))return false;
        // Global visibility before activation does not describe this thread's counter.
        int count=ShowCursor(TRUE);
        if(count>0)ShowCursor(FALSE);else increments++;
        while(count<0&&increments<32){count=ShowCursor(TRUE);increments++;}
        if(count<0)return false;
        SetCursor(LoadCursorW(nullptr,IDC_ARROW));return true;
    }
    void Restore(bool restore_global) {
        if(!saved)return;
        while(increments){ShowCursor(FALSE);increments--;}
        // Do not move or confine the cursor after focus has gone to another app.
        if(restore_global){SetCursor(previous);SetCursorPos(position.x,position.y);ClipCursor(&clip);}
        saved=false;
    }
};
}
