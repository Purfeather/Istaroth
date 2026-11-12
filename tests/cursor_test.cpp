// Deterministic ownership/error checks against the production cursor helper.
// Win32 calls are simulated here; this is not a live game or desktop test.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static RECT clip={10,20,30,40};
static POINT position={15,25};
static HCURSOR cursor=(HCURSOR)(ULONG_PTR)123;
static int display_count=0;
static bool snapshot_ok=true,clip_ok=true;
static unsigned global_writes=0,checks=0;
static BOOL FakeGetClipCursor(RECT* value){if(!snapshot_ok)return FALSE;*value=clip;return TRUE;}
static BOOL FakeGetCursorPos(POINT* value){*value=position;return snapshot_ok;}
static BOOL FakeGetCursorInfo(CURSORINFO* info){info->hCursor=cursor;return snapshot_ok;}
static BOOL FakeClipCursor(const RECT* value){global_writes++;if(!clip_ok)return FALSE;clip=value?*value:RECT{0,0,1920,1080};return TRUE;}
static BOOL FakeSetCursorPos(int x,int y){global_writes++;position={x,y};return TRUE;}
static HCURSOR FakeSetCursor(HCURSOR value){global_writes++;HCURSOR old=cursor;cursor=value;return old;}
static HCURSOR FakeLoadCursorW(HINSTANCE,LPCWSTR){return (HCURSOR)(ULONG_PTR)456;}
static int FakeShowCursor(BOOL show){display_count+=show?1:-1;return display_count;}
#define GetClipCursor FakeGetClipCursor
#define GetCursorPos FakeGetCursorPos
#define GetCursorInfo FakeGetCursorInfo
#define ClipCursor FakeClipCursor
#define SetCursorPos FakeSetCursorPos
#define SetCursor FakeSetCursor
#define LoadCursorW FakeLoadCursorW
#define ShowCursor FakeShowCursor
#include "resident_cursor.h"
static void Check(bool pass){checks++;if(!pass){fprintf(stderr,"cursor check %u failed\n",checks);exit(1);}}
int main() {
    using istaroth::MenuCursor;
    for(int initial:{-5,0,3}) {
        clip={10,20,30,40};position={15,25};cursor=(HCURSOR)(ULONG_PTR)123;display_count=initial;
        MenuCursor state;Check(state.Save());Check(state.Save());
        for(unsigned i=0;i<40;i++)Check(state.Show());
        Check(display_count>=0);Check(clip.left==0&&clip.right==1920);
        position={100,200};state.Restore(true);
        Check(display_count==initial);Check(clip.left==10&&clip.right==30);
        Check(position.x==15&&position.y==25);Check(cursor==(HCURSOR)(ULONG_PTR)123);
        unsigned writes=global_writes;state.Restore(true);Check(global_writes==writes);
    }
    MenuCursor state;display_count=-2;Check(state.Save());Check(state.Show());
    // A game-side decrement is not ours to undo or erase.
    display_count--;Check(state.Show());state.Restore(true);Check(display_count==-3);
    display_count=-2;Check(state.Save());Check(state.Show());
    clip={100,100,200,200};position={150,150};cursor=(HCURSOR)(ULONG_PTR)789;
    unsigned writes=global_writes;state.Restore(false);
    Check(display_count==-2);Check(global_writes==writes);Check(clip.left==100&&position.x==150&&cursor==(HCURSOR)(ULONG_PTR)789);
    snapshot_ok=false;Check(!state.Save());writes=global_writes;Check(!state.Show());state.Restore(true);Check(global_writes==writes);snapshot_ok=true;
    display_count=-40;Check(state.Save());Check(!state.Show());state.Restore(true);Check(display_count==-40);
    display_count=0;Check(state.Save());clip_ok=false;Check(!state.Show());Check(display_count==0);clip_ok=true;state.Restore(true);
    printf("{\"passed\":true,\"checks\":%u,\"scope\":\"simulated Win32 cursor state ownership, not a desktop or game test\"}\n",checks);
}
