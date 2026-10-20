#include "minios/runtime.h"
#include "platform.h"
#include "scheduler.h"
#define WIDTH 256u
static int32_t weights[WIDTH*WIDTH];
static MiniOSProgress progress[MINIOS_PROGRESS_ACTORS];
void minios_runtime_init(void) {
    for(uint32_t r=0;r<WIDTH;r++) for(uint32_t c=0;c<WIDTH;c++)
        weights[r*WIDTH+c]=(int32_t)((17*r+31*c)%101)-50;
    for(uint32_t i=0;i<MINIOS_PROGRESS_ACTORS;i++)
        progress[i]=(MiniOSProgress){MINIOS_RUNTIME_VERSION,i+1,0,0,0};
}
MiniOSRuntimeStatus minios_region_view(uint32_t region,size_t offset,size_t length,const void **out) {
    if(!out) return MINIOS_RUNTIME_BAD_ARGUMENT;
    *out=NULL;
    if(region!=MINIOS_REGION_MODEL) return MINIOS_RUNTIME_BAD_ARGUMENT;
    if(!minios_region_bounds(sizeof(weights),offset,length)) return MINIOS_RUNTIME_BAD_RANGE;
    *out=(const unsigned char *)weights+offset;
    return MINIOS_RUNTIME_OK;
}
MiniOSRuntimeStatus minios_runtime_model(MiniOSModelView *out) {
    if(!out) return MINIOS_RUNTIME_BAD_ARGUMENT;
    *out=(MiniOSModelView){MINIOS_MOCK_MODEL_VERSION,WIDTH,WIDTH,WIDTH*WIDTH,sizeof(weights),weights};
    return MINIOS_RUNTIME_OK;
}
MiniOSRuntimeStatus minios_progress_publish(uint32_t actor,uint32_t done,uint32_t total) {
    if(actor<1 || actor>MINIOS_PROGRESS_ACTORS || done>total) return MINIOS_RUNTIME_BAD_ARGUMENT;
    uint32_t f=irq_save();
    progress[actor-1]=(MiniOSProgress){MINIOS_RUNTIME_VERSION,actor,done,total,scheduler_now_ms()};
    irq_restore(f); return MINIOS_RUNTIME_OK;
}
MiniOSRuntimeStatus minios_progress_snapshot(uint32_t actor,MiniOSProgress *out) {
    if(!out || actor<1 || actor>MINIOS_PROGRESS_ACTORS) return MINIOS_RUNTIME_BAD_ARGUMENT;
    uint32_t f=irq_save(); *out=progress[actor-1]; irq_restore(f); return MINIOS_RUNTIME_OK;
}
