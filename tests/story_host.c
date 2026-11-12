#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>
typedef struct { uintptr_t data; WORD attributes; BYTE type, modifiers; DWORD padding; } TestType;
typedef struct TestClass TestClass;
typedef struct { uintptr_t name,parent,type; DWORD offset,token; } TestField;
struct TestClass { BYTE prefix[0x58]; BYTE *definition; TestField *fields; BYTE padding[8]; const char *name; BYTE tail[0x38]; DWORD parent_relative; BYTE tail2[0x15],ready; };
typedef struct Managed Managed;
struct Managed { TestClass *klass; uintptr_t monitor; DWORD state; BYTE isPlaying; BYTE padding[3]; Managed *context; };
typedef struct { uintptr_t vtable; DWORD id,flags; uintptr_t unused,handle; DWORD kind,padding; Managed *managed; BYTE pad[16]; BYTE *game_object; BYTE enabled,tail[7]; } Object;
typedef struct { Object *object; DWORD generation,padding; } Slot;
__declspec(dllexport) BYTE GSTStoryMonoVtable[16];
__declspec(dllexport) BYTE GSTStoryDirectorVtable[16];
__declspec(dllexport) uintptr_t GSTStoryGCTable[8192];
__declspec(dllexport) DWORD GSTStoryReadDelay=300;
__declspec(dllexport) DWORD GSTStoryQueryDelayUS=0;
static BYTE dense_pages[32768][4096];
__declspec(dllexport) DWORD GSTStoryPrepDelay=0,GSTStoryHintDelay=0;
__declspec(align(8)) static BYTE class_arena[0x400];
static uintptr_t arena_base=(uintptr_t)class_arena;
__declspec(dllexport) uintptr_t GSTStoryClassArena=(uintptr_t)&arena_base;
static uintptr_t gc_entries[8192];
static BYTE map[0x160];
__declspec(dllexport) uintptr_t GSTStoryTestMap=(uintptr_t)map;
static TestType integer_type={0,0,8,0,0}, boolean_type={0,0,2,0,0}, class_type={0,0,18,0,0};
static TestField fields[2], parent_fields[2], child_fields[2];
static BYTE root_definition[128],parent_definition[128],child_definition[128];
static Managed child,script,negative_script;
static BYTE game_object[0x200];
__declspec(align(8)) static BYTE director[0xb0],graph_handle[0x30],graph[0xa0],graph_manager[0x3e0];
__declspec(dllexport) uintptr_t GSTStoryGraphManager=(uintptr_t)graph_manager;
static Object object={.id=280002,.kind=2,.managed=&script};
static Object negative_object={.id=280002|0x80000000u,.kind=2,.managed=&negative_script};
static Slot slots[140002];
static Slot negative_slots[140002];
static uintptr_t chunk[2]={(uintptr_t)slots,(uintptr_t)(slots+131072)};
static uintptr_t chunks[2]={(uintptr_t)&chunk[0],(uintptr_t)&chunk[1]};
static uintptr_t negative_chunk[2]={(uintptr_t)negative_slots,(uintptr_t)(negative_slots+131072)};
static uintptr_t negative_chunks[2]={(uintptr_t)&negative_chunk[0],(uintptr_t)&negative_chunk[1]};
static Slot large_slots[2][0x200000];
static uintptr_t large_chunk[2][16], large_chunks[2][16], relocated_chunks[16];
static Object large_objects[2][4];
static Managed large_managed[2][4];
static const DWORD large_indices[4]={12345,555555,1000000,1976543};
typedef struct { DWORD hash,pad0,id,pad1; uintptr_t object; } FallbackEntry;
static FallbackEntry fallback[65536];
static Object live_objects[3];
static Managed live_managed[3];
static BYTE live_go[3][0x200];
static Object crowded_objects[20], fallback_object;
static Managed crowded_managed[20], fallback_managed;
static BYTE crowded_go[0x200], fallback_go[0x200];
static TestClass background_class;
_Static_assert(sizeof(FallbackEntry)==24,"fallback entry layout");
static DWORD fallback_hash(DWORD id) {
    DWORD x=id*0x1001u+0x7ed55d16u;
    x=(x^(x>>19))^0xc761c23cu;
    x=x*0x21u+0x165667b1u;
    x=(x-0x2c5d9b94u)^(x<<9);
    x=x*9u+0xfd7046c5u;
    return (x^(x>>16))^0xb55a4f09u;
}
_Static_assert(offsetof(TestClass,name)==0x70 && offsetof(TestClass,fields)==0x60 && offsetof(TestClass,definition)==0x58 && offsetof(TestClass,parent_relative)==0xb0 && offsetof(TestClass,ready)==0xc9 && sizeof(TestField)==32 && offsetof(TestField,name)==0 && offsetof(TestField,parent)==8 && offsetof(TestField,type)==16 && offsetof(Object,managed)==0x28 && offsetof(Object,game_object)==0x40 && offsetof(Object,enabled)==0x48 && sizeof(Object)==80,"test layout");
static void field(TestField *to,TestClass *parent,TestType *type,const char *name,DWORD offset) {
    to->parent=(uintptr_t)parent-0xacd2443ba132b32eULL;
    to->type=(uintptr_t)type-0xdfd7e210ed01ba90ULL;
    to->name=(uintptr_t)name^0x577522fa3015cd48ULL;
    to->offset=(offset-0x2da7b1)&0xffffff;
}
int wmain(int argc,wchar_t **argv) {
    BOOL live=argc==3 && !wcsncmp(argv[2],L"--live-",7);
    if(argc!=2 && !(argc==3 && (!wcscmp(argv[2],L"--large") || !wcscmp(argv[2],L"--fallback-crowded") || live))) return 1;
    BOOL large=argc==3 && !wcscmp(argv[2],L"--large");
    BOOL crowded=argc==3 && !wcscmp(argv[2],L"--fallback-crowded");
    SYSTEM_INFO system_info; MEMORY_BASIC_INFORMATION guard_info; DWORD old_protect;
    GetSystemInfo(&system_info);
    SIZE_T page_size=system_info.dwPageSize;
    BYTE *pages=(BYTE *)VirtualAlloc(NULL,page_size*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!pages || page_size<sizeof(Object)) return 3;
    Object *cross=(Object *)(pages+page_size-sizeof(Object)/2);
    Object *guard=(Object *)(pages+page_size);
    cross->vtable=(uintptr_t)GSTStoryMonoVtable; cross->id=6;
    guard->vtable=(uintptr_t)GSTStoryMonoVtable; guard->id=0x80000008u;
    if(!VirtualProtect(pages+page_size,page_size,PAGE_READWRITE|PAGE_GUARD,&old_protect)) return 3;
    TestClass *klass=(TestClass *)(class_arena+0x100), *parent=(TestClass *)(class_arena+0x200),
              *child_class=(TestClass *)(class_arena+0x300);
    TestClass *talk_class=(TestClass *)class_arena;
    klass->ready=0x80; parent->ready=0x80; child_class->ready=0x80;
    klass->name="MonoSitcomTimelineManager"; parent->name="SitcomParent"; child_class->name="SitcomContext";
    klass->fields=fields; parent->fields=parent_fields; child_class->fields=child_fields;
    klass->definition=root_definition; parent->definition=parent_definition;
    child_class->definition=child_definition; klass->parent_relative=0x200;
    script.klass=klass; script.context=&child; child.klass=child_class;
    negative_script.klass=klass; negative_script.context=&child;
    *(WORD *)(root_definition+0x38)=(WORD)(2-0x3664);
    *(WORD *)(parent_definition+0x38)=(WORD)(1-0x3664);
    *(WORD *)(child_definition+0x38)=(WORD)(2-0x3664);
    field(fields,klass,&boolean_type,"AHGFKJLN",20); field(fields+1,klass,&class_type,"_context",24);
    field(parent_fields,parent,&integer_type,"_state",16);
    field(parent_fields+1,parent,&integer_type,"_isActiveCanary",16); /* Outside declared field count. */
    field(child_fields,child_class,&integer_type,"_state",16); field(child_fields+1,child_class,&boolean_type,"_isPlaying",20);
    object.vtable=(uintptr_t)GSTStoryMonoVtable; slots[140001].object=&object;
    *(uintptr_t *)director=(uintptr_t)GSTStoryDirectorVtable; *(DWORD *)(director+8)=10;
    *(uintptr_t *)(director+0x98)=(uintptr_t)graph_handle; *(DWORD *)(director+0xa0)=17;
    *(DWORD *)(graph_handle+0x20)=17; *(uintptr_t *)(graph_handle+0x28)=(uintptr_t)graph;
    slots[5].object=(Object *)director;
    negative_object.vtable=(uintptr_t)GSTStoryMonoVtable; negative_slots[140001].object=&negative_object;
    object.game_object=negative_object.game_object=game_object; object.enabled=negative_object.enabled=1;
    GSTStoryGCTable[0]=(uintptr_t)gc_entries; gc_entries[1]=(uintptr_t)&script; gc_entries[2]=(uintptr_t)&negative_script;
    *(DWORD *)(map+0x20)=140002; *(uintptr_t *)(map+0x30)=(uintptr_t)chunks;
    *(uint64_t *)(map+0x98)=140002; *(uintptr_t *)(map+0xa8)=(uintptr_t)negative_chunks;
    if(large) {
        GSTStoryReadDelay=0;
        for(DWORD side=0;side<2;side++) {
            for(DWORD i=0;i<16;i++) {
                large_chunk[side][i]=(uintptr_t)&large_slots[side][i*0x20000];
                large_chunks[side][i]=(uintptr_t)&large_chunk[side][i];
            }
            for(DWORD i=0;i<4;i++) {
                DWORD index=large_indices[i];
                Object *item=&large_objects[side][i];
                item->vtable=(uintptr_t)GSTStoryMonoVtable;
                item->kind=2;
                item->game_object=game_object; item->enabled=1;
                item->id=index*2 | (side?0x80000000u:0);
                large_managed[side][i].klass=klass; large_managed[side][i].context=&child;
                item->managed=&large_managed[side][i];
                large_slots[side][index].object=item;
            }
        }
        *(uint64_t *)(map+0x20)=0x200000; *(uintptr_t *)(map+0x30)=(uintptr_t)large_chunks[0];
        *(uint64_t *)(map+0x98)=0x200000; *(uintptr_t *)(map+0xa8)=(uintptr_t)large_chunks[1];
        large_slots[0][3].object=cross; large_slots[1][4].object=guard;
    } else {
        slots[3].object=cross; negative_slots[4].object=guard;
    }
    if(crowded) {
        GSTStoryReadDelay=0; talk_class->name="MonoTalkDialog";
        talk_class->ready=0x80;
        for(DWORD i=0;i<512;i++) fallback[i].hash=0xffffffffu;
        *(uintptr_t *)(map+0)=(uintptr_t)fallback; *(DWORD *)(map+8)=(512-1)*8;
        *(uint64_t *)(map+0x20)=52; *(uint64_t *)(map+0x98)=5;
        for(DWORD i=0;i<20;i++) {
            DWORD index=32+i;
            crowded_managed[i].klass=talk_class;
            crowded_objects[i].kind=2;
            crowded_objects[i].vtable=(uintptr_t)GSTStoryMonoVtable;
            crowded_objects[i].id=index*2; crowded_objects[i].managed=&crowded_managed[i];
            crowded_objects[i].game_object=crowded_go; crowded_objects[i].enabled=1;
            slots[index].object=&crowded_objects[i];
        }
        fallback_managed.klass=talk_class;
        fallback_object.vtable=(uintptr_t)GSTStoryMonoVtable; fallback_object.id=0x40000002u;
        fallback_object.kind=2;
        fallback_object.managed=&fallback_managed; fallback_object.game_object=fallback_go; fallback_object.enabled=1;
        fallback_go[0x1f6]=fallback_go[0x1f8]=1;
    }
    if(live) {
        if(!wcscmp(argv[2],L"--live-contention")){GSTStoryPrepDelay=2;GSTStoryHintDelay=4;}
        GSTStoryReadDelay=0;talk_class->name="MonoTalkDialog";talk_class->ready=0x80;
        for(DWORD side=0;side<2;side++) for(DWORD i=0;i<16;i++) {
            large_chunk[side][i]=(uintptr_t)&large_slots[side][i*0x20000];
            large_chunks[side][i]=(uintptr_t)&large_chunk[side][i];
        }
        *(uint64_t *)(map+0x20)=524288;*(uintptr_t *)(map+0x30)=(uintptr_t)large_chunks[0];
        *(uint64_t *)(map+0x98)=262144;*(uintptr_t *)(map+0xa8)=(uintptr_t)large_chunks[1];
        for(DWORD i=0;i<65536;i++) fallback[i].hash=0xffffffff;
        *(uintptr_t *)map=(uintptr_t)fallback;*(DWORD *)(map+8)=(65536-1)*8;
        for(DWORD i=0;i<3;i++) {
            DWORD index=i==0?17:i==1?400000:200000;
            live_managed[i].klass=talk_class;
            live_objects[i].kind=2;live_objects[i].vtable=(uintptr_t)GSTStoryMonoVtable;
            live_objects[i].id=index*2;live_objects[i].managed=&live_managed[i];
            live_objects[i].game_object=live_go[i];live_objects[i].enabled=1;
            live_go[i][0x1f6]=live_go[i][0x1f8]=(i!=0);
        }
        large_slots[0][17].object=&live_objects[0];
        if(!wcscmp(argv[2],L"--live-unreadable")) {
            if(!VirtualProtect((BYTE*)large_slots[0]+4096*70,4096,PAGE_NOACCESS,&old_protect))return 4;
        }
    }
    if(live && !wcscmp(argv[2],L"--live-dense")) {
        GSTStoryQueryDelayUS=100;
        for(DWORD i=0;i<32768;i++) {
            Object *item=(Object*)dense_pages[i];item->id=i*2;
            item->vtable=(uintptr_t)GSTStoryMonoVtable+8;
            large_slots[0][i].object=item;
        }
    }
    if(live && !wcscmp(argv[2],L"--live-refresh")) {
        GSTStoryQueryDelayUS=500;
        live_objects[1].id=4096*2;large_slots[0][4096].object=&live_objects[1];
        background_class.name="MonoInLevelMainPage";
        for(DWORD i=0;i<8;i++) {
            DWORD index=32+i;
            crowded_managed[i].klass=&background_class;
            crowded_objects[i].vtable=(uintptr_t)GSTStoryMonoVtable;
            crowded_objects[i].id=index*2;crowded_objects[i].kind=2;
            crowded_objects[i].managed=&crowded_managed[i];
            crowded_objects[i].game_object=crowded_go;crowded_objects[i].enabled=1;
            large_slots[0][index].object=&crowded_objects[i];
        }
    }
    if(!LoadLibraryW(argv[1])) return 2;
    ULONGLONG start=GetTickCount64();
    BOOL inserted=FALSE;
    while(GetTickCount64()-start<(live?29000:large?20000:crowded?15000:14000)) {
        if(!VirtualQuery(pages+page_size,&guard_info,sizeof(guard_info)) || !(guard_info.Protect&PAGE_GUARD))
            ExitProcess(31);
        ULONGLONG elapsed=GetTickCount64()-start;
        if(crowded && !inserted && elapsed>=2500) {
            DWORD hash=fallback_hash(fallback_object.id), mask=(512-1)*8, index=hash&mask, step=8;
            while(fallback[index/8].hash!=0xffffffffu) { index=(index+step)&mask; step+=8; }
            fallback[index/8].id=fallback_object.id; fallback[index/8].object=(uintptr_t)&fallback_object;
            fallback[index/8].hash=hash&~3u; inserted=TRUE;
        }
        if(live && elapsed>=2000) {
            if(!wcscmp(argv[2],L"--live-fallback")) {
                DWORD id=0x40000002;live_objects[1].id=id;
                DWORD hash=fallback_hash(id),index=(hash&((65536-1)*8))/8;
                fallback[index].hash=hash&~3u;fallback[index].id=id;fallback[index].object=(uintptr_t)&live_objects[1];
            } else large_slots[0][400000].object=&live_objects[1];
            if(!wcscmp(argv[2],L"--live-refresh")) {
                /* Isolate refresh starvation from full-table throughput. */
                large_slots[0][400000].object=NULL;
                live_objects[1].id=4096*2;large_slots[0][4096].object=&live_objects[1];
            }
            if(!wcscmp(argv[2],L"--live-multiple"))large_slots[0][200000].object=&live_objects[2];
            if(!wcscmp(argv[2],L"--live-replace") && elapsed>=8000) {
                live_objects[1].id=400000*2+(1u<<22);large_slots[0][400000].generation=1;
            }
            if(!wcscmp(argv[2],L"--live-resize") && elapsed>=5000) {
                *(uint64_t *)(map+0x20)=262144;
                live_objects[1].id=200000*2;large_slots[0][200000].object=&live_objects[1];
                large_slots[0][400000].object=NULL;
                if(elapsed>=8000) {
                    for(DWORD c=0;c<16;c++) relocated_chunks[c]=large_chunks[0][c];
                    *(uint64_t *)(map+0x20)=524288;
                    *(uintptr_t *)(map+0x30)=(uintptr_t)relocated_chunks;
                    live_objects[1].id=400000*2;
                    large_slots[0][200000].object=NULL;large_slots[0][400000].object=&live_objects[1];
                }
            }
            if(elapsed>=12000 && !wcscmp(argv[2],L"--live-cycle")) live_go[1][0x1f8]=0;
            if(elapsed>=14000 && !wcscmp(argv[2],L"--live-cycle")) live_objects[1].managed=NULL;
        }
        if(live && !wcscmp(argv[2],L"--live-dense") && elapsed>=2000) {
            large_slots[0][400000].object=NULL;
            live_objects[1].id=393728*2;large_slots[0][393728].object=&live_objects[1];
        }
        script.state=elapsed>=2000 && elapsed<4000?2:elapsed>=5000 && elapsed<6000?3:elapsed>=8000 && elapsed<9000?4:0;
        *(DWORD *)(graph+0x98)=elapsed>=2000 && elapsed<6000?1:0;
        director[0x90]=elapsed>=2000 && elapsed<6000;
        graph_manager[0x3d9]=elapsed>=4000 && elapsed<5000;
        *(DWORD *)(graph_handle+0x20)=elapsed>=6000 && elapsed<7000?18:17;
        script.isPlaying=script.state==2;
        game_object[0x1f6]=large?0:script.isPlaying;
        game_object[0x1f8]=large?0:(elapsed>=8500 && elapsed<9000?0xff:script.isPlaying);
        negative_script.state=script.state; negative_script.isPlaying=script.isPlaying;
        child.state=script.state; child.isPlaying=script.isPlaying;
        object.kind=script.isPlaying?1:script.state==3?0:script.state==4?3:2;
        object.handle=1;
        object.managed=object.kind==2?&script:NULL;
        if(elapsed>=6000 && elapsed<8000) object.managed=NULL;
        negative_object.kind=object.kind; negative_object.handle=2;
        negative_object.managed=object.managed?&negative_script:NULL;
        if(!large && !crowded && !live && elapsed>=10000) GSTStoryTestMap=0;
        Sleep(20);
    }
    return 0;
}
