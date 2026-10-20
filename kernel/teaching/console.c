#include "console.h"
#include "platform.h"
#include <stddef.h>
static volatile uint16_t *const video = (volatile uint16_t *)0xb8000;
static size_t row, col;
static int shift_left, shift_right, caps, extended;
static const char plain[128] = {
    [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',[10]='9',[11]='0',
    [12]='-',[13]='=',[14]='\b',[15]='\t',[16]='q',[17]='w',[18]='e',[19]='r',[20]='t',
    [21]='y',[22]='u',[23]='i',[24]='o',[25]='p',[26]='[',[27]=']',[28]='\n',
    [30]='a',[31]='s',[32]='d',[33]='f',[34]='g',[35]='h',[36]='j',[37]='k',[38]='l',
    [39]=';',[40]='\'',[41]='`',[43]='\\',[44]='z',[45]='x',[46]='c',[47]='v',
    [48]='b',[49]='n',[50]='m',[51]=',',[52]='.',[53]='/',[57]=' '
};
static const char shifted[128] = {
    [2]='!',[3]='@',[4]='#',[5]='$',[6]='%',[7]='^',[8]='&',[9]='*',[10]='(',[11]=')',
    [12]='_',[13]='+',[26]='{',[27]='}',[39]=':',[40]='"',[41]='~',[43]='|',
    [51]='<',[52]='>',[53]='?'
};
void console_init(void) {
    outb(0x3f9,0); outb(0x3fb,0x80); outb(0x3f8,1); outb(0x3f9,0);
    outb(0x3fb,3); outb(0x3fa,0xc7); outb(0x3fc,0x0b);
    for (size_t i=0;i<80*25;i++) video[i]=0x0720;
}
static void serial_put(char c) {
    for (unsigned i=0;i<1000000;i++) if (inb(0x3fd)&0x20) { outb(0x3f8,(uint8_t)c); break; }
}
void console_putc(char c) {
    if (c=='\n') serial_put('\r');
    serial_put(c);
    if (c=='\r') col=0;
    else if (c=='\n') { col=0; row++; }
    else if (c=='\b') { if (col) col--; else if (row) { row--; col=79; } }
    else { video[row*80+col]=(uint16_t)(0x0700|(uint8_t)c); if (++col==80) { col=0; row++; } }
    if (row>=25) {
        for(size_t i=0;i<80*24;i++) video[i]=video[i+80];
        for(size_t i=80*24;i<80*25;i++) video[i]=0x0720;
        row=24;
    }
    uint16_t cursor=(uint16_t)(row*80+col);
    outb(0x3d4,14); outb(0x3d5,(uint8_t)(cursor>>8));
    outb(0x3d4,15); outb(0x3d5,(uint8_t)cursor);
}
void console_write(const char *text) { while(*text) console_putc(*text++); }
void console_uint(uint32_t v) {
    char digits[10]; unsigned n=0;
    do { digits[n++]=(char)('0'+v%10); v/=10; } while(v);
    while(n) console_putc(digits[--n]);
}
int console_read(void) {
    if (inb(0x3fd)&1) return inb(0x3f8);
    uint8_t status=inb(0x64);
    if (!(status&1)) return -1;
    uint8_t scan=inb(0x60);
    if (status&0x20) return -1; /* mouse data */
    if (scan==0xe0 || scan==0xe1) { extended=1; return -1; }
    if (extended) { extended=0; return -1; }
    if ((scan&0x7f)==42) { shift_left=!(scan&0x80); return -1; }
    if ((scan&0x7f)==54) { shift_right=!(scan&0x80); return -1; }
    if(scan&0x80) return -1;
    if(scan==58) { caps=!caps; return -1; }
    char c=plain[scan]; int shift=shift_left||shift_right;
    if(c>='a'&&c<='z') { if(shift!=caps) c=(char)(c-'a'+'A'); }
    else if(shift && shifted[scan]) c=shifted[scan];
    return c ? c : -1;
}
