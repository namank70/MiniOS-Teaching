#include "console.h"
#include "scheduler.h"
#include "platform.h"
#include "string.h"
#include "minios/runtime.h"
#include "minios/shell.h"
#include "minios/suggestion_policy.h"

/* Only the shell task writes console output. Other tasks publish bounded events. */
static MiniOSSuggestionEvent events[8];
static unsigned event_head,event_count;
static int post_event(const MiniOSSuggestionEvent *event) {
    uint32_t f=irq_save();
    if(event_count==8) { irq_restore(f); return 0; }
    events[(event_head+event_count)%8]=*event; event_count++;
    irq_restore(f); return 1;
}
static int take_event(MiniOSSuggestionEvent *event) {
    uint32_t f=irq_save();
    if(!event_count) { irq_restore(f); return 0; }
    *event=events[event_head]; event_head=(event_head+1)%8; event_count--;
    irq_restore(f); return 1;
}
static int shell_write(void *unused,const char *text) { (void)unused; console_write(text); return 0; }
static void print_tasks(void) {
    TaskSnapshot snapshot[NUM_TASKS]; scheduler_snapshot(snapshot);
    console_write("ID NAME STATE PRIORITY SWITCHES\n");
    static const char *states[]={"READY","RUNNING","SLEEPING"};
    for(unsigned i=0;i<NUM_TASKS;i++) {
        console_uint(snapshot[i].id); console_write(" "); console_write(snapshot[i].name);
        console_write(" "); console_write(states[snapshot[i].state]); console_write(" ");
        console_uint(snapshot[i].priority); console_write(" "); console_uint(snapshot[i].switches); console_write("\n");
    }
}
static void print_progress(void) {
    console_write("ACTOR COMPLETED TOTAL HEARTBEAT_MS\n");
    for(unsigned i=1;i<=MINIOS_PROGRESS_ACTORS;i++) {
        MiniOSProgress p; minios_progress_snapshot(i,&p);
        console_uint(i); console_write(" "); console_uint(p.completed); console_write(" ");
        console_uint(p.total); console_write(" "); console_uint((uint32_t)p.heartbeat_ms); console_write("\n");
    }
}
static void kernel_check(void) {
    const void *view=NULL;
    int ok=minios_region_view(MINIOS_REGION_MODEL,0,4,&view)==MINIOS_RUNTIME_OK && *(const int32_t *)view==-50;
    ok=ok && minios_region_view(MINIOS_REGION_MODEL,256*256*4,1,&view)==MINIOS_RUNTIME_BAD_RANGE;
    MiniOSModelView model; MiniOSMockResult result;
    minios_runtime_model(&model);
    MiniOSContext c={MINIOS_CONTEXT_VERSION,0,750,MINIOS_ZONE_STATION,10,30,1};
    ok=ok && minios_mock_predict(&c,&model,&result)==MINIOS_MOCK_OK && result.suggestion==MINIOS_SUGGESTION_REFUEL;
    MiniOSSuggestionPolicy p; MiniOSSuggestionEvent e;
    minios_policy_init(&p,750);
    ok=ok && minios_policy_evaluate(&p,&c,&model,&e)==MINIOS_POLICY_EVENT_READY;
    ok=ok && minios_policy_commit(&p,c.input_sequence)==MINIOS_POLICY_EVENT_READY;
    ok=ok && minios_policy_evaluate(&p,&c,&model,&e)==MINIOS_POLICY_NO_TRIGGER;
    console_write(ok ? "PASS: kernel regions and policy\n" : "FAIL: kernel regions and policy\n");
    /* With IRQs masked, yield must not synthesize clock ticks. */
    uint32_t flags = irq_save();
    uint32_t before = scheduler_tick_now();
    for (unsigned i = 0; i < 100; ++i) task_yield();
    int clock_ok = scheduler_tick_now() == before;
    irq_restore(flags);
    before = scheduler_tick_now();
    task_sleep(20);
    clock_ok = clock_ok && (uint32_t)(scheduler_tick_now() - before) >= 20;
    console_write(clock_ok ? "PASS: kernel timer and sleep\n" : "FAIL: kernel timer and sleep\n");
}
static void execute(char *line) {
    /* Trim for extension matching; Naman's dispatcher retains ownership of its commands. */
    char *start=line; while(*start==' ' || *start=='\t') start++;
    size_t n=0; while(start[n]) n++;
    while(n && (start[n-1]==' ' || start[n-1]=='\t')) start[--n]=0;
    if(!strcmp(start,"tasks")) { print_tasks(); return; }
    if(!strcmp(start,"progress")) { print_progress(); return; }
    MiniOSShellStatus rc=minios_shell_execute(start,shell_write,NULL);
    if(rc==MINIOS_SHELL_LINE_TOO_LONG) console_write("Error: line too long (64 max)\n");
    if(!strcmp(start,"help")) console_write("  tasks    Show kernel task snapshots\n  progress Show shared progress snapshots\n");
    if(!strcmp(start,"status")) {
        console_write("Kernel: i686; PIT ticks="); console_uint(scheduler_tick_now());
        console_write("; tasks=4; model_bytes=262144\nConsole: COM1 + PS/2; provider: deterministic mock\n");
    }
    if(!strcmp(start,"test")) kernel_check();
}
static void shell_task(void) {
    char line[MINIOS_SHELL_MAX_LINE+1]; size_t used=0;
    int overflow=0,last_cr=0,escape=0; uint32_t loops=0;
    console_write("MiniOS Teaching kernel shell\nType help. Serial: Ctrl-a x exits QEMU.\nminios> ");
    for(;;) {
        MiniOSSuggestionEvent event;
        while(take_event(&event)) {
            console_write("\n[proactive] ");
            const char *names[]={"NONE","REFUEL","TAKE_BREAK","PREPARE_ARRIVAL"};
            console_write(names[event.suggestion]); console_write(" (scheduled mock context)\nminios> ");
            for(size_t i=0;i<used;i++) console_putc(line[i]);
        }
        /* Bound each input slice even when a sender streams continuously. */
        for(unsigned reads=0;reads<128;reads++) {
            int ch=console_read(); if(ch<0) break;
            if(ch==27) { escape=1; continue; }
            if(escape) { if(escape==1 && ch=='[') escape=2; else if(ch>=0x40 && ch<=0x7e) escape=0; continue; }
            if(ch=='\n' && last_cr) { last_cr=0; continue; }
            last_cr=(ch=='\r');
            if(ch=='\r' || ch=='\n') {
                console_write("\n"); line[used]=0;
                if(overflow) console_write("Error: line too long (64 max); discarded\n");
                else execute(line);
                used=0; overflow=0; console_write("minios> ");
            } else if(ch==8 || ch==127) {
                if(used && !overflow) { used--; console_write("\b \b"); }
            } else if(ch>=32 && ch<127) {
                if(used<MINIOS_SHELL_MAX_LINE && !overflow) { line[used++]=(char)ch; console_putc((char)ch); }
                else overflow=1;
            } else if(ch==3) {
                used=0; overflow=0; console_write("^C\nminios> ");
            }
        }
        minios_progress_publish(1,++loops,UINT32_MAX);
        task_sleep(5);
    }
}
static void recommendation_task(void) {
    MiniOSSuggestionPolicy policy; MiniOSModelView model; MiniOSSuggestionEvent event;
    minios_runtime_model(&model); minios_policy_init(&policy,750);
    uint32_t loops=0;
    task_sleep(2000); /* visible unsolicited demo after shell is ready */
    for(;;) {
        uint64_t now=scheduler_now_ms();
        MiniOSContext context={MINIOS_CONTEXT_VERSION,now,(uint32_t)((750+now/60000)%1440),MINIOS_ZONE_STATION,10,30,1};
        if(minios_policy_evaluate(&policy,&context,&model,&event)==MINIOS_POLICY_EVENT_READY && post_event(&event))
            minios_policy_commit(&policy,event.input_sequence);
        minios_progress_publish(3,++loops,UINT32_MAX);
        task_sleep(100);
    }
}
static void runtime_task(void) {
    uint32_t loops=0;
    for(;;) { minios_progress_publish(2,++loops,UINT32_MAX); task_sleep(250); }
}
void kernel_main(uint32_t magic,uint32_t info) {
    (void)info; console_init();
    if(magic!=0x2badb002) { console_write("PANIC: invalid multiboot magic\n"); return; }
    console_write("BOOT: MiniOS teaching i686\n");
    minios_runtime_init();
    const TaskEntry entries[NUM_TASKS]={shell_task,recommendation_task,runtime_task,task_idle};
    scheduler_init(entries); scheduler_start();
}
