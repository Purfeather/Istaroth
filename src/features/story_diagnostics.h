/* Separate research-only telemetry. No changes to the ABI7 story mapping. */
#ifdef GST_STORY_DIAGNOSTICS
typedef struct { uint64_t base,capacity,epoch,passes,cursor,examined,skipped,accepted; } DiscoverySource;
typedef struct {
    DWORD magic,version,pid,size; volatile LONG sequence; DWORD status;
    uint64_t heartbeat,frequency;
    DiscoverySource sources[5]; /* head0, head1, fallback, hint, tail */
    uint64_t events[8]; /* hint attempts, identity rejects, type rejects, class rejects, admissions, replacements, refreshes, overruns */
    uint64_t reads[4],ticks[4],work_ticks[4]; /* table, object, fields, retained refresh (exclusive nested accounting) */
} StoryDiagnostics;
_Static_assert(sizeof(StoryDiagnostics)==520,"research diagnostics v1");
static StoryDiagnostics dg,*dg_view;
static HANDLE dg_mapping;
static DWORD dg_source,dg_stage;
#define D(x) do { x; } while(0)
static void dg_epoch(DWORD source,uintptr_t base,uint64_t capacity) {
    DiscoverySource *s=&dg.sources[source];
    if(s->base!=base || s->capacity!=capacity) {
        uint64_t epoch=s->epoch+1; *s=(DiscoverySource){0};
        s->base=base;s->capacity=capacity;s->epoch=epoch;
    }
}
#else
#define D(x) do {} while(0)
#endif
