/* Read-only candidate discovery. This component never changes game speed. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#ifndef GST_STORY_TEST
#include "story_profile.h"
#else
#define STORY_OBJECT_INDEX_HINT 140001U
#endif

#define RECORDS 16
#define FIELDS 12
#define CLASS_NAMES 64
#define NATIVE_PREFIX 80
#define SNAPSHOT_PAGES 1024
typedef struct { char name[48]; DWORD offset, raw_u32, type_tag_candidate, declaring_depth; } ProbeField;
typedef struct {
    uint64_t native_address, managed_address;
    DWORD instance_id, field_count, origin_field_offset;
    char class_name[64], name_space[32];
    ProbeField fields[FIELDS];
} ProbeRecord;
typedef struct {
    uint64_t class_address, field_table_address;
    DWORD fields_ready_mask, declared_field_count, inspected_classes, reserved;
} ProbeMetadata;
typedef struct {
    uint64_t game_object_address;
    DWORD behaviour_enabled_candidate, hierarchy_active_candidate, local_active_candidate, valid;
} ProbeActivity;
typedef struct {
    uint64_t native_address, handle_address, graph_address, manager_address;
    DWORD instance_id, handle_generation, graph_flags, manager_gate, director_flag, valid;
} ProbeDirector;
typedef struct {
    DWORD magic, version, pid, status;
    volatile LONG sequence;
    DWORD record_count, slot_visits, mono_observations, director_observations, rounds;
    ULONGLONG heartbeat_tick;
    DWORD managed_layout_observations, mole_namespace_observations;
    ProbeRecord records[RECORDS];
    DWORD class_name_count;
    char class_names[CLASS_NAMES][64];
    ProbeMetadata metadata[RECORDS];
    uint64_t table_counts[2];
    DWORD head_cursors[2], scan_duration_ms, max_scan_duration_ms, read_calls, read_failures;
    ProbeActivity activity[RECORDS];
    DWORD fallback_capacity, fallback_cursor, fallback_visits, fallback_matches;
    DWORD director_count;
    ProbeDirector directors[8];
} Probe;
_Static_assert(sizeof(ProbeField) == 64 && sizeof(ProbeRecord) == 896 && sizeof(ProbeActivity) == 24 && sizeof(ProbeDirector)==56 && sizeof(Probe) == 19904, "probe ABI v7");
static HMODULE module;
static BYTE *image;
static Probe working, *output=&working, *published;
static uintptr_t object_map_slot, mono_vtable, director_vtable, gc_handle_table, class_arena_slot;
static uintptr_t graph_manager_slot;
static uintptr_t tracked_map,tracked_chunks[2];
static uint64_t tracked_counts[2];
static DWORD cursor[2], table_kind, completed_tables, catalog_cursor;
static struct { uintptr_t object; DWORD generation,padding; } slots[256];
static struct { uintptr_t address; BYTE data[4096]; } prefix_pages[SNAPSHOT_PAGES];
static ULONGLONG field_refresh_ticks[RECORDS];
static ULONGLONG identity_refresh_ticks[RECORDS];
static ULONGLONG budget_frequency;
static DWORD distributed_offsets[2][64],distributed_next[2],distributed_turn;
static DWORD discovery_round;
static struct { uintptr_t klass; ULONGLONG tick; char name[64]; BOOL valid; } class_cache[256];
static uintptr_t fallback_base;
static struct { DWORD hash,padding,id,reserved; uintptr_t object; } fallback_entries[64];

static ULONGLONG budget_now(void) {
    LARGE_INTEGER value; QueryPerformanceCounter(&value); return (ULONGLONG)value.QuadPart;
}

#include "story_diagnostics.h"

static void bytes(void *to, const void *from, SIZE_T size) {
    volatile BYTE *a = to; const volatile BYTE *b = from;
    for (SIZE_T i=0; i<size; i++) a[i]=b[i];
}
static void zero(void *to, SIZE_T size) { volatile BYTE *a=to; for(SIZE_T i=0;i<size;i++) a[i]=0; }
static BOOL read_at_raw(void *to, uintptr_t address, SIZE_T size) {
#ifdef GST_STORY_TEST
    DWORD *cost=image?(DWORD*)GetProcAddress((HMODULE)image,"GSTStoryQueryDelayUS"):NULL;
    if(cost && *cost && *cost<=1000) {
        LARGE_INTEGER f,a,b;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&a);
        do{QueryPerformanceCounter(&b);}while(b.QuadPart-a.QuadPart<f.QuadPart*(*cost)/1000000);
    }
#endif
    output->read_calls++;
    SIZE_T copied=0;
    if(address<0x10000 || !size || size>4096 || address+size<address ||
       !ReadProcessMemory(GetCurrentProcess(),(const void *)address,to,size,&copied) || copied!=size) {
        output->read_failures++; return FALSE;
    }
    return TRUE;
}
static BOOL read_at(void *to,uintptr_t address,SIZE_T size) {
#ifdef GST_STORY_DIAGNOSTICS
    ULONGLONG before=budget_now(); DWORD stage=dg_stage;
    BOOL result=read_at_raw(to,address,size);
    dg.reads[stage]++; dg.ticks[stage]+=budget_now()-before; return result;
#else
    return read_at_raw(to,address,size);
#endif
}
static uintptr_t pointer(uintptr_t address) { uintptr_t value=0; return read_at(&value,address,8)?value:0; }
static BOOL text_at(char *to, SIZE_T capacity, uintptr_t address) {
    char buffer[96];
    if(capacity>sizeof(buffer) || !read_at(buffer,address,capacity)) return FALSE;
    for(SIZE_T i=0;i<capacity;i++) {
        BYTE c=(BYTE)buffer[i];
        if(!c) { to[i]=0; return TRUE; }
        if(c<32 || c>126 || i+1==capacity) return FALSE;
        to[i]=(char)c;
    }
    return FALSE;
}
static BOOL equal(const char *a,const char *b) {
    for(SIZE_T i=0;i<96;i++) { if(a[i]!=b[i]) return FALSE; if(!a[i]) return TRUE; }
    return FALSE;
}
static BOOL contains(const char *value,const char *term) {
    for(SIZE_T i=0;value[i];i++) {
        SIZE_T j=0;
        while(term[j] && value[i+j]) {
            char a=value[i+j],b=term[j];
            if(a>='A'&&a<='Z') a+=32;
            if(b>='A'&&b<='Z') b+=32;
            if(a!=b) break;
            j++;
        }
        if(!term[j]) return TRUE;
    }
    return FALSE;
}
static BOOL target_class(const char *name) {
    if(contains(name,"Login") || contains(name,"Server") || contains(name,"Account") ||
       contains(name,"Config") || contains(name,"Gacha")) return FALSE;
    return contains(name,"Sitcom") || contains(name,"Dialog") || contains(name,"Talk") ||
           contains(name,"Cutscene") || contains(name,"MonoInLevel");
}
static DWORD priority(const char *name,DWORD id,const ProbeActivity *state) {
    DWORD kind=equal(name,"MonoTalkDialog")?5:contains(name,"EffectPlugin")?1:
               contains(name,"Dialog") || contains(name,"Sitcom")?3:
               contains(name,"Talk") || contains(name,"Cutscene")?2:1;
    return kind*4+(!!(id&0xc0000001))+(state->valid && state->hierarchy_active_candidate==1?2:0);
}
static void catalog(const char *name) {
    for(DWORD i=0;i<output->class_name_count;i++) if(equal(name,output->class_names[i])) return;
    DWORD index=output->class_name_count<CLASS_NAMES?output->class_name_count++:catalog_cursor++%CLASS_NAMES;
    bytes(output->class_names[index],name,64);
}
static BOOL class_names(uintptr_t object,char *name,char *space) {
    uintptr_t klass=pointer(object);
    /* The current executable's class-name getter is mov rax,[rcx+0x70]; ret.
       Its namespace getter uses a different path. Do not infer that layout. */
    zero(space,32);
    return klass && text_at(name,64,pointer(klass+0x70)) && name[0];
}
static uintptr_t managed_from_prefix(const BYTE *native) {
    DWORD kind=0,handle=0;
    bytes(&kind,native+0x20,4);
    if(kind==2) { uintptr_t managed=0; bytes(&managed,native+0x28,8); return managed; }
    uint64_t packed=0; bytes(&packed,native+0x18,8);
    if(packed==0xffffffffffffffffULL) return 0;
    handle=(DWORD)packed;
    uintptr_t page=gc_handle_table+((uintptr_t)(handle>>26)<<16);
    uintptr_t chunk=pointer(page+((handle>>13)&0x1fff)*8);
    return pointer(chunk+(handle&0x1fff)*8);
}
static BOOL snapshot_read_raw(void *to,uintptr_t object,SIZE_T size) {
    uintptr_t page=object&~(uintptr_t)4095; SIZE_T offset=object-page;
    if(object<0x10000 || size>4096 || offset+size>4096) return read_at(to,object,size);
    DWORD index=(DWORD)((page>>12)&(SNAPSHOT_PAGES-1));
    if(prefix_pages[index].address!=page) {
        /* Cache copied bytes only. Each new page copy rechecks its current protection. */
        prefix_pages[index].address=0;
        if(!read_at(prefix_pages[index].data,page,4096)) return read_at(to,object,size);
        prefix_pages[index].address=page;
    }
    bytes(to,prefix_pages[index].data+offset,size); return TRUE;
}
static BOOL snapshot_read(void *to,uintptr_t object,SIZE_T size) {
    DWORD previous=0; ULONGLONG start=0; (void)previous;(void)start;
    D(previous=dg_stage; if(previous==0) dg_stage=1;start=budget_now());
    BOOL result=snapshot_read_raw(to,object,size);
    D(if(previous==0) dg.work_ticks[1]+=budget_now()-start;dg_stage=previous);
    return result;
}
static BOOL snapshot_class_name(uintptr_t managed,char *name) {
    uintptr_t klass=0,string=0; char buffer[64];
    if(!snapshot_read(&klass,managed,8) || !klass) return FALSE;
    DWORD index=(DWORD)((klass>>4)&255); ULONGLONG now=GetTickCount64();
    /* Discovery hint only; observe() and retained records re-read their actual class. */
    if(class_cache[index].klass==klass && now-class_cache[index].tick<500) {
        bytes(name,class_cache[index].name,64); return class_cache[index].valid;
    }
    class_cache[index].klass=klass; class_cache[index].tick=now; class_cache[index].valid=FALSE;
    zero(class_cache[index].name,64);
    if(!snapshot_read(&string,klass+0x70,8) ||
       !snapshot_read(buffer,string,64)) return FALSE;
    for(DWORD i=0;i<64;i++) {
        BYTE c=(BYTE)buffer[i];
        if(!c) {
            name[i]=0; bytes(class_cache[index].name,name,i+1);
            class_cache[index].valid=i!=0; return i!=0;
        }
        if(c<32 || c>126 || i==63) return FALSE;
        name[i]=(char)c;
    }
    return FALSE;
}
static void read_activity(ProbeActivity *state,DWORD origin_offset,const BYTE *native) {
    zero(state,sizeof(*state));
    if(origin_offset || !native) return; /* A referenced child's owner is not this native component. */
    bytes(&state->game_object_address,native+0x40,8);
    state->behaviour_enabled_candidate=native[0x48];
    BYTE flags[3];
    if(!state->game_object_address || !read_at(flags,state->game_object_address+0x1f6,3)) return;
    state->local_active_candidate=flags[0]; state->hierarchy_active_candidate=flags[2];
    /* 0xff requests a cache recomputation in the game getter. We never call it. */
    state->valid=state->behaviour_enabled_candidate<=1 && state->hierarchy_active_candidate<=1;
}
static void activity(ProbeRecord *record,const BYTE *native) {
    read_activity(&output->activity[record-output->records],record->origin_field_offset,native);
}
static BOOL field_name(char *name,uintptr_t at) {
    zero(name,48);
    if(!text_at(name,48,at) || !name[0]) return FALSE;
    for(SIZE_T i=0;name[i];i++) {
        char c=name[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='<'||c=='>')) return FALSE;
    }
    return TRUE;
}
static void observe(uintptr_t native,DWORD id,uintptr_t managed,DWORD origin_offset,const BYTE *prefix);
static void fields(ProbeRecord *record) {
    uintptr_t klass=pointer((uintptr_t)record->managed_address);
    ProbeMetadata *metadata=&output->metadata[record-output->records];
    zero(metadata,sizeof(*metadata)); metadata->class_address=klass;
    record->field_count=0;
    for(DWORD depth=0;klass && depth<4 && record->field_count<FIELDS;depth++) {
        BYTE header[0xca];
        if(!read_at(header,klass,sizeof(header))) break;
        uintptr_t table=0,definition=0; DWORD relative=0; WORD declared=0;
        bytes(&table,header+0x60,8); bytes(&definition,header+0x58,8); bytes(&relative,header+0xb0,4);
        metadata->inspected_classes++;
        if(!depth) metadata->field_table_address=table;
        if(header[0xc9]&0x80) metadata->fields_ready_mask|=1u<<depth;
        /* Only inspect a table already initialized by the game. Never call its setup path. */
        DWORD count=0;
        if((header[0xc9]&0x80) && table && read_at(&declared,definition+0x38,2)) {
            declared=(WORD)(declared+0x3664); count=declared>64?64:declared;
            metadata->declared_field_count+=declared;
        }
        for(DWORD i=0;i<count && record->field_count<FIELDS;i++) {
            struct { uintptr_t name,parent,type; DWORD offset,token; } entry;
            char name[48]; BYTE type_header[12];
            if(!read_at(&entry,table+i*32,sizeof(entry)) ||
               (entry.parent+0xacd2443ba132b32eULL)!=klass ||
               !field_name(name,entry.name^0x577522fa3015cd48ULL)) break;
            DWORD offset=(entry.offset+0x2da7b1)&0xffffff;
            if(offset<16 || offset>4092) continue;
            if(!read_at(type_header,entry.type+0xdfd7e210ed01ba90ULL,sizeof(type_header))) continue;
            WORD attributes=0; bytes(&attributes,type_header+8,2);
            if(attributes&0x10) continue; /* Static fields do not live in this instance. */
            BYTE tag=type_header[10];
            if(!record->origin_field_offset && tag==18) {
                observe((uintptr_t)record->native_address,record->instance_id,
                        pointer((uintptr_t)record->managed_address+offset),offset,NULL);
            }
            if(!((tag>=2&&tag<=9)||tag==17)) continue;
            ProbeField *field=&record->fields[record->field_count];
            zero(field,sizeof(*field));
            SIZE_T size=(tag==2 || tag==4 || tag==5)?1:(tag==3 || tag==6 || tag==7)?2:4;
            if(!read_at(&field->raw_u32,(uintptr_t)record->managed_address+offset,size)) continue;
            /* Obfuscated names need not say 'state'. Keep only small scalars. */
            if(tag!=2 && field->raw_u32>64 && field->raw_u32!=0xffffffff) continue;
            bytes(field->name,name,48);
            field->offset=offset; field->type_tag_candidate=tag; field->declaring_depth=depth;
            record->field_count++;
        }
        if(!relative) break;
        uintptr_t arena=pointer(pointer(class_arena_slot));
        uintptr_t parent=arena?arena+relative:0;
        if(parent==klass) break;
        klass=parent;
    }
}
static void update_fields_raw(ProbeRecord *record,BOOL force) {
    DWORD index=(DWORD)(record-output->records); ULONGLONG now=GetTickCount64();
    if(!contains(record->class_name,"Sitcom") && !equal(record->class_name,"MonoTalkDialog")) {
        /* Rejected UI scalar candidates are not reparsed on every inactive template. */
        record->field_count=0; zero(&output->metadata[index],sizeof(ProbeMetadata));
        output->metadata[index].reserved=1; return;
    }
    if(force || !field_refresh_ticks[index] || now-field_refresh_ticks[index]>=1000 ||
       pointer((uintptr_t)record->managed_address)!=output->metadata[index].class_address) {
        field_refresh_ticks[index]=now; fields(record); return;
    }
    /* Metadata is reused briefly; each selected scalar value is read again. */
    for(DWORD i=0;i<record->field_count;i++) {
        ProbeField *field=&record->fields[i]; DWORD tag=field->type_tag_candidate, value=0;
        SIZE_T size=(tag==2 || tag==4 || tag==5)?1:(tag==3 || tag==6 || tag==7)?2:4;
        if(!read_at(&value,(uintptr_t)record->managed_address+field->offset,size)) {
            record->field_count=0; field_refresh_ticks[index]=0; return;
        }
        if(tag!=2 && value>64 && value!=0xffffffff) {
            field_refresh_ticks[index]=now; fields(record); return;
        }
        field->raw_u32=value;
    }
}
static void update_fields(ProbeRecord *record,BOOL force) {
    DWORD previous=0; (void)previous; ULONGLONG field_started=0,field_nested=0; (void)field_started;(void)field_nested; D(field_started=budget_now();field_nested=dg.work_ticks[2]);
    D(previous=dg_stage; dg_stage=2);
    update_fields_raw(record,force);
    D(dg.work_ticks[2]+=budget_now()-field_started-(dg.work_ticks[2]-field_nested); dg_stage=previous);
}
static void observe(uintptr_t native,DWORD id,uintptr_t managed,DWORD origin_offset,const BYTE *prefix) {
    char name[64]={0},space[32]={0};
    if(!managed || !class_names(managed,name,space)) return;
    if(origin_offset) { output->managed_layout_observations++; catalog(name); }
    if(!target_class(name)) { D(dg.events[3]++); return; }
    ProbeActivity incoming; read_activity(&incoming,origin_offset,prefix);
    DWORD index;
    for(index=0;index<output->record_count;index++) if(output->records[index].managed_address==managed) break;
    if(index<output->record_count && !output->records[index].origin_field_offset && origin_offset) return;
    if(index>=RECORDS) {
        /* A child cannot replace its caller while fields() is using that record. */
        if(origin_offset) return;
        DWORD lowest=priority(name,id,&incoming);
        for(DWORD i=0;i<RECORDS;i++) if(priority(output->records[i].class_name,output->records[i].instance_id,&output->activity[i])<lowest) {
            lowest=priority(output->records[i].class_name,output->records[i].instance_id,&output->activity[i]); index=i;
        }
        if(index>=RECORDS) return;
    }
    BOOL changed=index==output->record_count || output->records[index].managed_address!=managed ||
                 output->records[index].native_address!=native || output->records[index].instance_id!=id ||
                 (output->records[index].origin_field_offset && !origin_offset);
    if(index==output->record_count) { output->record_count++; D(dg.events[4]++); }
    else if(changed) D(dg.events[5]++);
    ProbeRecord *record=&output->records[index];
    identity_refresh_ticks[index]=GetTickCount64();
    if(changed) zero(&output->activity[index],sizeof(ProbeActivity));
    record->native_address=native; record->managed_address=managed; record->instance_id=id;
    record->origin_field_offset=origin_offset;
    bytes(record->class_name,name,64); bytes(record->name_space,space,32);
    bytes(&output->activity[index],&incoming,sizeof(incoming));
    if(changed) update_fields(record,TRUE); /* Retained records are refreshed once before discovery. */
}
static void read_director(ProbeDirector *state,uintptr_t object,DWORD id) {
    struct { uintptr_t pointer; DWORD generation,padding; } handle,after;
    DWORD generation=0,last_generation=0,last_id=0; BYTE gate=0,flag=0;
    zero(state,sizeof(*state)); state->native_address=object; state->instance_id=id;
    if(!read_at(&handle,object+0x98,sizeof(handle)) || !handle.pointer ||
       !read_at(&generation,handle.pointer+0x20,4) || generation!=handle.generation) return;
    state->handle_address=handle.pointer; state->handle_generation=handle.generation;
    state->graph_address=pointer(handle.pointer+0x28);
    state->manager_address=pointer(graph_manager_slot);
    if(!state->graph_address || !state->manager_address ||
       !read_at(&state->graph_flags,state->graph_address+0x98,4) ||
       !read_at(&gate,state->manager_address+0x3d9,1) || !read_at(&flag,object+0x90,1) ||
       !read_at(&after,object+0x98,sizeof(after)) ||
       !read_at(&last_generation,handle.pointer+0x20,4) || !read_at(&last_id,object+8,4) ||
       after.pointer!=handle.pointer || after.generation!=handle.generation ||
       generation!=last_generation || id!=last_id || pointer(handle.pointer+0x28)!=state->graph_address ||
       pointer(graph_manager_slot)!=state->manager_address) return;
    state->manager_gate=gate; state->director_flag=flag; state->valid=TRUE;
}
static DWORD director_priority(const ProbeDirector *state) {
    return !state->valid?0:(state->graph_flags&1) && !state->manager_gate?2:1;
}
static void observe_director(uintptr_t object,DWORD id) {
    ProbeDirector state; read_director(&state,object,id);
    DWORD index;
    for(index=0;index<output->director_count;index++) if(output->directors[index].native_address==object) break;
    if(index==output->director_count) {
        if(index<8) output->director_count++;
        else {
            DWORD lowest=3;
            for(DWORD i=0;i<8;i++) if(director_priority(&output->directors[i])<lowest) {
                lowest=director_priority(&output->directors[i]); index=i;
            }
            if(director_priority(&state)<=lowest) return;
        }
    }
    bytes(&output->directors[index],&state,sizeof(state));
}
static void inspect_object_raw(uintptr_t object,DWORD id,const BYTE *native) {
    uintptr_t vtable=0; bytes(&vtable,native,8);
    if(vtable==director_vtable) {
        output->director_observations++; observe_director(object,id); return;
    }
    if(vtable!=mono_vtable) { D(dg.events[2]++); return; }
    output->mono_observations++;
    uintptr_t managed=managed_from_prefix(native); char name[64]={0};
    if(!managed || !snapshot_class_name(managed,name)) return;
    output->managed_layout_observations++; catalog(name);
    if(target_class(name)) { D(dg.sources[dg_source].accepted++); observe(object,id,managed,0,native); }
    else D(dg.events[3]++);
}
static void inspect_object(uintptr_t object,DWORD id,const BYTE *native) {
    DWORD previous=0; (void)previous; D(previous=dg_stage; dg_stage=1);
    ULONGLONG start=0,fields_before=0;(void)start;(void)fields_before;D(start=budget_now();fields_before=dg.work_ticks[2]);
    inspect_object_raw(object,id,native);
    D(dg.work_ticks[1]+=budget_now()-start-(dg.work_ticks[2]-fields_before);dg_stage=previous);
}
static DWORD scan_batch(uintptr_t chunks,DWORD start,DWORD count,DWORD kind,ULONGLONG deadline) {
    uintptr_t table=pointer(pointer(chunks+(start>>17)*8));
    if(count>256) return 0;
    if(!table) { D(dg.sources[dg_source].skipped+=count); return count; }
    uintptr_t entry=table+(start&0x1ffff)*16;
    DWORD page_slots=(DWORD)((4096-(entry&4095))/16);
    if(!page_slots) page_slots=1;
    if(count>page_slots) count=page_slots;
    /* A failed copy may skip only this page, not valid slots beyond its boundary. */
    if(!read_at(slots,entry,count*16)) { D(dg.sources[dg_source].skipped+=count); return count; }
    for(DWORD i=0;i<count;i++) {
        if(budget_now()>=deadline) return i;
        output->slot_visits++; D(dg.sources[dg_source].examined++);
        BYTE native[NATIVE_PREFIX]; DWORD id=0;
        if(!slots[i].object) continue;
        if(!snapshot_read(native,slots[i].object,sizeof(native))) { D(dg.sources[dg_source].skipped++); continue; }
        bytes(&id,native+8,4);
        if(((id>>1)&0x1fffff)!=start+i || ((id>>22)&255)!=slots[i].generation ||
           ((id>>31)!=(kind!=0)) || (id&0x40000000)) { D(dg.events[1]++); continue; }
        inspect_object(slots[i].object,id,native);
    }
    return count;
}
static DWORD object_hash(DWORD id) {
    DWORD value=id*0x1001+0x7ed55d16;
    value^=(value>>19)^0xc761c23c;
    value=value*0x21+0x165667b1;
    value=(value+0xd3a2646c)^(value<<9);
    value=value*9+0xfd7046c5;
    return value^(value>>16)^0xb55a4f09;
}
static BOOL fallback_reachable(uintptr_t base,DWORD mask,DWORD id,uintptr_t object,ULONGLONG deadline) {
    DWORD hash=object_hash(id), index=hash&mask, step=8;
    for(DWORD tries=0;tries<64 && budget_now()<deadline;tries++) {
        struct { DWORD hash,padding,id,reserved; uintptr_t object; } entry;
        if(!read_at(&entry,base+(uintptr_t)index*3,sizeof(entry))) return FALSE;
        if(entry.hash==(hash&0xfffffffc) && entry.id==id) return entry.object==object;
        if(entry.hash==0xffffffff) return FALSE;
        index=(index+step)&mask; step+=8;
    }
    return FALSE;
}
static void scan_fallback(uintptr_t map,const BYTE *header,ULONGLONG deadline) {
    uintptr_t base=0; DWORD mask=0;
    bytes(&base,header,8); bytes(&mask,header+8,4);
    DWORD capacity=(mask>>3)+1;
    if(base<0x10000 || (mask&7) || !capacity || capacity>0x40000 || (capacity&(capacity-1)) ||
       base+(uintptr_t)capacity*24<base) {
        output->fallback_capacity=0; output->fallback_cursor=0; fallback_base=0; return;
    }
    if(base!=fallback_base || capacity!=output->fallback_capacity) output->fallback_cursor=0;
    output->fallback_capacity=capacity; fallback_base=base;
    D(dg_source=2; dg_epoch(2,base,capacity));
    for(DWORD work=0;work<16 && budget_now()<deadline;work++) {
        DWORD start=output->fallback_cursor%capacity, count=capacity-start;
        uintptr_t entry=base+(uintptr_t)start*24;
        DWORD page_entries=(DWORD)((4096-(entry&4095))/24);
        if(!page_entries) page_entries=1;
        if(count>64) count=64; if(count>page_entries) count=page_entries;
        if(!read_at(fallback_entries,entry,count*24)) {
            D(dg.sources[2].skipped+=count);
            output->fallback_cursor=(start+count)%capacity; continue;
        }
        BYTE current[12]; uintptr_t current_base=0; DWORD current_mask=0;
        if(!read_at(current,map,sizeof(current))) return;
        bytes(&current_base,current,8); bytes(&current_mask,current+8,4);
        if(current_base!=base || current_mask!=mask) {
            output->fallback_capacity=0; output->fallback_cursor=0; fallback_base=0; return;
        }
        for(DWORD i=0;i<count;i++) {
            if(budget_now()>=deadline) { output->fallback_cursor=(start+i)%capacity; return; }
            output->fallback_visits++; D(dg.sources[2].examined++);
            DWORD id=fallback_entries[i].id, native_id=0; uintptr_t vtable=0; BYTE native[NATIVE_PREFIX];
            if(!fallback_entries[i].object || fallback_entries[i].hash!=(object_hash(id)&0xfffffffc) ||
               !snapshot_read(native,fallback_entries[i].object,sizeof(native))) continue;
            bytes(&native_id,native+8,4);
            if(native_id!=id) continue;
            bytes(&vtable,native,8);
            if((vtable==mono_vtable || vtable==director_vtable) &&
               !fallback_reachable(base,mask,id,fallback_entries[i].object,deadline)) continue;
            output->fallback_matches++;
            inspect_object(fallback_entries[i].object,id,native);
        }
        output->fallback_cursor=(start+count)%capacity;
        D(dg.sources[2].cursor=output->fallback_cursor; if(start+count>=capacity) dg.sources[2].passes++);
    }
}
/* Spread discovery across 64 regions while retaining the sequential cursors.
   Offsets are traversal bookkeeping, never cached pointers or page permissions. */
static void scan_distributed(const uintptr_t chunks[2],ULONGLONG deadline) {
    D(dg_source=4;dg_epoch(4,chunks[0]^chunks[1],output->table_counts[0]+output->table_counts[1]));
    for(DWORD work=0;work<128 && budget_now()<deadline;work++) {
        DWORD kind=distributed_turn++&1,region=distributed_next[kind]++&63;
        DWORD count=(DWORD)output->table_counts[kind];
        DWORD begin=(DWORD)((uint64_t)count*region/64),end=(DWORD)((uint64_t)count*(region+1)/64);
        if(end<=begin)continue;
        DWORD *offset=&distributed_offsets[kind][region];
        if(*offset>=end-begin)*offset=0;
        DWORD start=begin+*offset,n=end-start;
        if(n>256)n=256;
        DWORD boundary=0x20000-(start&0x1ffff);if(n>boundary)n=boundary;
        DWORD visited=scan_batch(chunks[kind],start,n,kind,deadline);
        *offset+=visited;D(dg.sources[4].cursor=start+visited;if(visited)dg.regions_seen[kind]|=(uint64_t)1<<region);
    }
}
static void refresh_retained(void) {
    ULONGLONG start=0,fields_before=0;(void)start;(void)fields_before;D(start=budget_now();fields_before=dg.work_ticks[2]);
    DWORD previous=0; (void)previous; D(previous=dg_stage; dg_stage=3; dg.events[6]++);
    for(DWORD i=0;i<output->record_count;i++) {
        ProbeRecord *record=&output->records[i]; DWORD id=0;
        ULONGLONG now=GetTickCount64();
        /* Ordinary dialogue is refreshed every slice. Background telemetry must
           not consume every discovery slice merely because it was found first. */
        if(!equal(record->class_name,"MonoTalkDialog") && !contains(record->class_name,"Sitcom") &&
           identity_refresh_ticks[i] &&
           now-identity_refresh_ticks[i]<1000) {
            if(now-identity_refresh_ticks[i]>550) output->activity[i].valid=FALSE;
            continue;
        }
        identity_refresh_ticks[i]=now;
        char name[64]={0},space[32]={0}; BYTE native[NATIVE_PREFIX];
        BOOL alive=read_at(native,(uintptr_t)record->native_address,sizeof(native));
        uintptr_t managed=alive?managed_from_prefix(native):0;
        if(alive) bytes(&id,native+8,4);
        if(record->origin_field_offset) managed=pointer(managed+record->origin_field_offset);
        if(!alive || id!=record->instance_id ||
           managed!=record->managed_address ||
           !class_names((uintptr_t)record->managed_address,name,space) || !equal(name,record->class_name)) {
            output->record_count--;
            bytes(&output->records[i],&output->records[output->record_count],sizeof(ProbeRecord));
            bytes(&output->metadata[i],&output->metadata[output->record_count],sizeof(ProbeMetadata));
            bytes(&output->activity[i],&output->activity[output->record_count],sizeof(ProbeActivity));
            field_refresh_ticks[i]=field_refresh_ticks[output->record_count];
            identity_refresh_ticks[i]=identity_refresh_ticks[output->record_count];
            zero(&output->records[output->record_count],sizeof(ProbeRecord));
            zero(&output->metadata[output->record_count],sizeof(ProbeMetadata));
            zero(&output->activity[output->record_count],sizeof(ProbeActivity));
            field_refresh_ticks[output->record_count]=0;
            identity_refresh_ticks[output->record_count]=0; i--; continue;
        }
        activity(record,native);
        update_fields(record,FALSE);
    }
    for(DWORD i=0;i<output->director_count;i++) {
        ProbeDirector *record=&output->directors[i]; uintptr_t table=pointer(record->native_address); DWORD id=0;
        if(table!=director_vtable || !read_at(&id,record->native_address+8,4) || id!=record->instance_id) {
            output->director_count--;
            bytes(record,&output->directors[output->director_count],sizeof(*record));
            zero(&output->directors[output->director_count],sizeof(*record)); i--; continue;
        }
        read_director(record,record->native_address,record->instance_id);
    }
    D(dg.work_ticks[3]+=budget_now()-start-(dg.work_ticks[2]-fields_before);dg_stage=previous);
}
static void test_slice_delay(const char *name) {
#ifdef GST_STORY_TEST
    DWORD *delay=(DWORD *)GetProcAddress((HMODULE)image,name);
    if(delay && *delay && *delay<=10) {
        LARGE_INTEGER frequency,start,now;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);
        do { QueryPerformanceCounter(&now); } while(now.QuadPart-start.QuadPart<frequency.QuadPart*(*delay)/1000);
    }
#else
    (void)name;
#endif
}
static void scan(void) {
    ULONGLONG started=budget_now(), deadline=started+budget_frequency*8/1000;
    /* Discovery copies live for this bounded scan only; retained signals read fresh. */
    for(DWORD i=0;i<SNAPSHOT_PAGES;i++) prefix_pages[i].address=0;
    test_slice_delay("GSTStoryPrepDelay");
    uintptr_t map=pointer(object_map_slot), chunks[2]={0}; BYTE header[0xb0];
    output->status=1;
    if(!map || !read_at(header,map,sizeof(header))) {
        tracked_map=0;tracked_chunks[0]=tracked_chunks[1]=0;tracked_counts[0]=tracked_counts[1]=0;
        cursor[0]=cursor[1]=completed_tables=0;
        D(dg_epoch(0,0,0);dg_epoch(1,0,0);dg_epoch(2,0,0));
        output->record_count=0;
        output->director_count=0; zero(output->directors,sizeof(output->directors));
        zero(output->records,sizeof(output->records));
        zero(output->metadata,sizeof(output->metadata));
        zero(output->activity,sizeof(output->activity));
        zero(field_refresh_ticks,sizeof(field_refresh_ticks));
        zero(identity_refresh_ticks,sizeof(identity_refresh_ticks));
        zero(output->table_counts,sizeof(output->table_counts));
        output->fallback_capacity=output->fallback_cursor=0; fallback_base=0;
        return;
    }
    for(DWORD kind=0;kind<2;kind++) {
        bytes(&output->table_counts[kind],header+(kind?0x98:0x20),8);
        bytes(&chunks[kind],header+(kind?0xa8:0x30),8);
        if(output->table_counts[kind]>0x200000) {
            output->record_count=0; zero(output->records,sizeof(output->records));
            output->director_count=0; zero(output->directors,sizeof(output->directors));
            zero(output->metadata,sizeof(output->metadata));
            zero(output->activity,sizeof(output->activity));
            zero(field_refresh_ticks,sizeof(field_refresh_ticks));
            zero(identity_refresh_ticks,sizeof(identity_refresh_ticks)); return;
        }
    }
    output->status=2;
    for(DWORD k=0;k<2;k++) {
        if(tracked_map!=map || tracked_chunks[k]!=chunks[k] || tracked_counts[k]!=output->table_counts[k]) {
            cursor[k]=0; completed_tables=0;
            zero(distributed_offsets[k],sizeof(distributed_offsets[k]));distributed_next[k]=0;
            D(dg.regions_seen[k]=0);
            D(dg.sources[4].capacity=~(uint64_t)0);
            D(dg.sources[k].capacity=~(uint64_t)0);
        }
        tracked_chunks[k]=chunks[k]; tracked_counts[k]=output->table_counts[k];
        D(dg_epoch(k,chunks[k],output->table_counts[k]));
    }
    tracked_map=map;
    refresh_retained();
    /* Give distributed discovery the first slice on alternate rounds. A slow
       head or fallback read must not permanently starve every other region. */
    BOOL distributed_first=!(discovery_round++&1);
    ULONGLONG distributed_deadline;
    if(distributed_first) {
        distributed_deadline=budget_now()+budget_frequency*2/1000;
        if(distributed_deadline>deadline) distributed_deadline=deadline;
        scan_distributed(chunks,distributed_deadline);
    }
    /* Main traversal gets the first available slice; hints must not starve it. */
    ULONGLONG head_deadline=budget_now()+budget_frequency*4/1000;
    if(head_deadline>deadline) head_deadline=deadline;
    /* ponytail: 8 ms soft slice and 32768 slots per tick; no cached page permissions. */
    for(DWORD work=0;work<128 && budget_now()<head_deadline;work++) {
        DWORD kind=table_kind; table_kind^=1;
        DWORD count=(DWORD)output->table_counts[kind], *head=&cursor[kind];
        if(*head>=count) *head=0;
        if(count) {
            DWORD batch=count-*head, boundary=0x20000-(*head&0x1ffff);
            if(batch>256) batch=256; if(batch>boundary) batch=boundary;
            D(dg_source=kind);
            DWORD visited=scan_batch(chunks[kind],*head,batch,kind,head_deadline);
            *head+=visited; D(dg.sources[kind].cursor=*head);
            if(!visited) break;
        }
        if(*head>=count) {
            *head=0; completed_tables|=1u<<kind; D(dg.sources[kind].passes++);
            if(completed_tables==3) { output->rounds++; completed_tables=0; }
        }
    }
    ULONGLONG fallback_deadline=budget_now()+budget_frequency/1000;
    if(fallback_deadline>deadline) fallback_deadline=deadline;
    if(budget_now()<fallback_deadline) scan_fallback(map,header,fallback_deadline);
    if(!distributed_first) {
        distributed_deadline=budget_now()+budget_frequency*2/1000;
        if(distributed_deadline>deadline)distributed_deadline=deadline;
        scan_distributed(chunks,distributed_deadline);
    }
    static ULONGLONG last_hint;
    ULONGLONG hint_now=GetTickCount64();
    if(hint_now-last_hint>=1000 && budget_now()<deadline && output->table_counts[0]>STORY_OBJECT_INDEX_HINT) {
        last_hint=hint_now;
        test_slice_delay("GSTStoryHintDelay");
        D(dg_source=3; dg.events[0]++; dg_epoch(3,chunks[0],output->table_counts[0]));
        scan_batch(chunks[0],STORY_OBJECT_INDEX_HINT,1,0,deadline);
    }
    output->head_cursors[0]=cursor[0]; output->head_cursors[1]=cursor[1];
    D(if(budget_now()>deadline) dg.events[7]++);
    output->scan_duration_ms=(DWORD)((budget_now()-started)*1000/budget_frequency);
    if(output->scan_duration_ms>output->max_scan_duration_ms) output->max_scan_duration_ms=output->scan_duration_ms;
}
static BOOL profile(void) {
    image=(BYTE *)GetModuleHandleW(NULL);
#ifdef GST_STORY_TEST
    object_map_slot=(uintptr_t)GetProcAddress((HMODULE)image,"GSTStoryTestMap");
    mono_vtable=(uintptr_t)GetProcAddress((HMODULE)image,"GSTStoryMonoVtable");
    director_vtable=(uintptr_t)GetProcAddress((HMODULE)image,"GSTStoryDirectorVtable");
    gc_handle_table=(uintptr_t)GetProcAddress((HMODULE)image,"GSTStoryGCTable");
    class_arena_slot=(uintptr_t)GetProcAddress((HMODULE)image,"GSTStoryClassArena");
    graph_manager_slot=(uintptr_t)GetProcAddress((HMODULE)image,"GSTStoryGraphManager");
    return object_map_slot && mono_vtable && director_vtable && gc_handle_table && class_arena_slot && graph_manager_slot;
#else
    for(SIZE_T i=0;i<sizeof(story_checks)/sizeof(story_checks[0]);i++) {
        BYTE actual[64];
        if(!read_at(actual,(uintptr_t)image+story_checks[i].rva,story_checks[i].size)) return FALSE;
        for(SIZE_T j=0;j<story_checks[i].size;j++) if(actual[j]!=story_checks[i].expected[j]) return FALSE;
    }
    object_map_slot=(uintptr_t)image+STORY_OBJECT_MAP_RVA;
    mono_vtable=(uintptr_t)image+STORY_MONO_VTABLE_RVA;
    director_vtable=(uintptr_t)image+STORY_DIRECTOR_VTABLE_RVA;
    gc_handle_table=(uintptr_t)image+STORY_GC_HANDLE_TABLE_RVA;
    class_arena_slot=(uintptr_t)image+STORY_CLASS_ARENA_RVA;
    graph_manager_slot=(uintptr_t)image+STORY_GRAPH_MANAGER_RVA;
    return TRUE;
#endif
}
static void mapping_name(wchar_t *name,const wchar_t *prefix) {
    SIZE_T n=0,count=0;
    while(prefix[n]) { name[n]=prefix[n]; n++; }
    wchar_t digits[10]; DWORD pid=GetCurrentProcessId();
    do { digits[count++]=L'0'+pid%10; pid/=10; } while(pid);
    while(count) name[n++]=digits[--count]; name[n]=0;
}
static void publish(void) {
    output->heartbeat_tick=GetTickCount64();
    /* Scanning happens in private storage. Readers only wait for this bounded copy. */
    InterlockedIncrement(&published->sequence);
    bytes(published,output,16);
    bytes((BYTE *)published+20,(BYTE *)output+20,sizeof(Probe)-20);
    InterlockedIncrement(&published->sequence);
#ifdef GST_STORY_DIAGNOSTICS
    if(dg_view) {
        dg.heartbeat=output->heartbeat_tick; dg.status=output->status;
        InterlockedIncrement(&dg_view->sequence);
        bytes(dg_view,&dg,16);bytes((BYTE*)dg_view+20,(BYTE*)&dg+20,sizeof(dg)-20);
        InterlockedIncrement(&dg_view->sequence);
    }
#endif
}
static DWORD WINAPI worker(void *unused) {
    (void)unused;
    wchar_t name[96];
    mapping_name(name,L"Local\\GenshinSkip.Story.");
    SetLastError(ERROR_SUCCESS);
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(Probe),name);
    if(!mapping || GetLastError()==ERROR_ALREADY_EXISTS) { if(mapping) CloseHandle(mapping); FreeLibraryAndExitThread(module,1); }
    published=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Probe));
    if(!published) { CloseHandle(mapping); FreeLibraryAndExitThread(module,2); }
    output->magic=0x47535331; output->version=7; output->pid=GetCurrentProcessId(); output->status=1;
    publish();
    BOOL valid=profile();
    LARGE_INTEGER frequency;
    if(!QueryPerformanceFrequency(&frequency) || frequency.QuadPart<1000) valid=FALSE;
    else budget_frequency=(ULONGLONG)frequency.QuadPart;
#ifdef GST_STORY_TEST
    ULONGLONG started=GetTickCount64();
    DWORD *test_delay=(DWORD *)GetProcAddress((HMODULE)image,"GSTStoryReadDelay");
#endif
#ifndef GST_STORY_TEST
    mapping_name(name,L"Local\\GenshinSkip.Time.");
    HANDLE clock_mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name);
    BYTE *clock_state=clock_mapping?MapViewOfFile(clock_mapping,FILE_MAP_READ,0,0,104):NULL;
#endif
#ifdef GST_STORY_DIAGNOSTICS
    mapping_name(name,L"Local\\GenshinSkip.StoryDiagnostic.");
    dg.magic=0x47534431; dg.version=2; dg.pid=GetCurrentProcessId(); dg.size=sizeof(dg); dg.frequency=budget_frequency;
    SetLastError(0); dg_mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(dg),name);
    if(dg_mapping && GetLastError()!=ERROR_ALREADY_EXISTS) dg_view=MapViewOfFile(dg_mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(dg));
    if(!dg_view) valid=FALSE;
#endif
    while(TRUE) {
#ifdef GST_STORY_TEST
        if(GetTickCount64()-started>30000) break;
        DWORD delay=0;
        if(test_delay && read_at(&delay,(uintptr_t)test_delay,4) && delay<=500) Sleep(delay);
#else
        ULONGLONG heartbeat=0; DWORD status=7;
        if(!clock_state || !read_at(&heartbeat,(uintptr_t)clock_state+48,8) ||
            !read_at(&status,(uintptr_t)clock_state+12,4) || status==7 ||
            !heartbeat || GetTickCount64()-heartbeat>1500) break;
#endif
        /* stage totals exclude nested field work */
#ifdef GST_STORY_DIAGNOSTICS
        ULONGLONG scan_start=budget_now(), other=dg.work_ticks[1]+dg.work_ticks[2]+dg.work_ticks[3];
#endif
        if(valid) scan(); else output->status=3;
#ifdef GST_STORY_DIAGNOSTICS
        dg.work_ticks[0]+=budget_now()-scan_start-(dg.work_ticks[1]+dg.work_ticks[2]+dg.work_ticks[3]-other);
#endif
        publish();
        Sleep(50);
    }
    output->status=7; publish();
#ifndef GST_STORY_TEST
    if(clock_state) UnmapViewOfFile(clock_state);
    if(clock_mapping) CloseHandle(clock_mapping);
#endif
#ifdef GST_STORY_DIAGNOSTICS
    if(dg_view) UnmapViewOfFile(dg_view); if(dg_mapping) CloseHandle(dg_mapping);
#endif
    UnmapViewOfFile(published); CloseHandle(mapping); FreeLibraryAndExitThread(module,0); return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,void *reserved) {
    (void)reserved;
    if(reason==DLL_PROCESS_ATTACH) {
        wchar_t path[MAX_PATH]; DWORD length=GetModuleFileNameW(NULL,path,MAX_PATH);
        if(!length || length>=MAX_PATH) return FALSE;
        const wchar_t *name=path;
        for(DWORD i=0;i<length;i++) if(path[i]==L'\\') name=path+i+1;
#ifdef GST_STORY_TEST
        if(lstrcmpiW(name,L"gst-story-test.exe")) return FALSE;
#else
        if(lstrcmpiW(name,L"YuanShen.exe")) return FALSE;
#endif
        module=instance; DisableThreadLibraryCalls(instance);
        HANDLE thread=CreateThread(NULL,0,worker,NULL,0,NULL);
        if(!thread) return FALSE; CloseHandle(thread);
    }
    return TRUE;
}
