; Adapted from External-Scheduler-ABI/scheduler.asm at 785d989.
; Preserve the donor cdecl ESP/EBP switch ABI. All switches enter with IF=0.
BITS 32
section .text
global switch_context, isr_pit, isr_unexpected
extern scheduler_tick, kernel_exception
switch_context:
    push ebp
    mov ebp,esp
    mov eax,[ebp+8]
    pusha
    pushf
    mov [eax],esp
    mov [eax+4],ebp
    mov eax,[ebp+12]
    mov esp,[eax]
    mov ebp,[eax+4]
    popf                         ; IF remains clear until caller/IRQ return
    popa
    pop ebp
    ret
isr_pit:
    pusha
    cld
    ; Acknowledge before dispatch: a fresh task has no suspended IRQ epilogue.
    mov al,0x20
    out 0x20,al
    call scheduler_tick
    popa
    iret
%assign i 0
%rep 32
exception_%+i:
%if i != 8 && i != 10 && i != 11 && i != 12 && i != 13 && i != 14 && i != 17 && i != 21 && i != 29 && i != 30
    push dword 0
%endif
    push dword i
    jmp exception_common
%assign i i+1
%endrep
isr_unexpected:
    push dword 0
    push dword 255
exception_common:
    cli
    cld
    ; Nonreturning panic: stack starts with vector, error.
    mov eax,[esp]
    mov edx,[esp+4]
    push edx
    push eax
    call kernel_exception
.halt:
    hlt
    jmp .halt
section .rodata
global exception_stubs
exception_stubs:
%assign i 0
%rep 32
    dd exception_%+i
%assign i i+1
%endrep
section .note.GNU-stack noalloc noexec nowrite progbits
