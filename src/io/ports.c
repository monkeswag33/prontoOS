#include <io/ports.h>

inline void outb(int port, uint8_t data) {
    asm volatile ("outb %b0,%w1": :"a" (data), "Nd" (port));
}

inline uint8_t inb(int port) {
    uint8_t data;
    asm volatile ("inb %w1,%0":"=a" (data):"Nd" (port));
    return data;
}

inline void outl(int port, uint32_t data) {
    asm volatile ("outl %0,%w1": :"a" (data), "Nd" (port));
}

inline uint32_t inl(int port) {
    uint32_t data;
    asm volatile ("inl %w1,%0" : "=a" (data) : "Nd" (port));
    return data;
}