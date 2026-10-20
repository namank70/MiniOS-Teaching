BITS 32
section .multiboot
align 4
    dd 0x1badb002, 3, -(0x1badb002+3)
section .bss
align 16
stack_bottom: resb 16384
stack_top:
section .rodata
align 8
gdt:
    dq 0
    dq 0x00cf9a000000ffff
    dq 0x00cf92000000ffff
gdt_end:
gdt_ptr: dw gdt_end-gdt-1
         dd gdt
section .text
global _start
extern kernel_main
_start:
    cli
    cld
    lgdt [gdt_ptr]
    jmp 0x08:.flat
.flat:
    mov cx,0x10
    mov ds,cx
    mov es,cx
    mov fs,cx
    mov gs,cx
    mov ss,cx
    mov esp,stack_top
    xor ebp,ebp
    push ebx
    push eax
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang
section .note.GNU-stack noalloc noexec nowrite progbits
