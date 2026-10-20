#ifndef TEACHING_PLATFORM_H
#define TEACHING_PLATFORM_H
#include <stdint.h>
static inline uint8_t inb(uint16_t port) {
    uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(port));
}
static inline uint32_t irq_save(void) {
    uint32_t f; __asm__ volatile("pushfl; popl %0; cli" : "=r"(f) :: "memory"); return f;
}
static inline void irq_restore(uint32_t f) {
    __asm__ volatile("pushl %0; popfl" :: "r"(f) : "memory", "cc");
}
#endif
