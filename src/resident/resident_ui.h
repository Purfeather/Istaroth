#pragma once
#include <stdint.h>

namespace istaroth {
enum class UiCommand { Enabled, Multiplier, Advance, ChooseFirst, SkipCooldown, Emergency };
enum class UiPhase { Starting, Paused, Waiting, Confirming, Active, Restoring, Fault };
struct UiState {
    UiPhase phase=UiPhase::Starting;
    bool initialized=false,enabled=false,foreground=false,dialogue=false;
    bool actual_known=false,speed_active=false,effects_active=false;
    bool advance=true,choose_first=true,skip_cooldown=true;
    bool advance_active=false,choose_active=false,skip_active=false;
    unsigned target=4;
    float actual=1,baseline=1;
    char reason[128]={};
    // A blurred current-frame DX11 SRV; zero means transparent-color fallback.
    uint64_t backdrop=0;
    // Optional static RGBA icon on the renderer's private DX11 device.
    uint64_t icon_texture=0;
    float display_width=0,display_height=0;
};
using UiEmit=void(*)(UiCommand,int);
void DrawResidentUi(const UiState&,bool& menu_open,float scale,UiEmit);
}
