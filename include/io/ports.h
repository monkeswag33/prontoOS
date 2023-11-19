#pragma once
#include <stdint.h>

void outb(int port, uint8_t data);
uint8_t inb(int port);
void outl(int port, uint32_t data);
uint32_t inl(int port);