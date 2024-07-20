#include <mem/heap.hpp>
#include <util/logger.h>
#include <libc/string.h>
#include <mem/vmm.hpp>
#include <cstdlib>

using namespace mem;

mem::Heap *mem::kheap = nullptr;

// Heap is basically just a clone of bitmap since nothing special is needed to add to it
// All memory management is already taken care of by VMM
Heap::Heap(VMM& vmm, size_t size, size_t bsize) :
    Bitmap{size, bsize, reinterpret_cast<uint8_t*>(vmm.malloc(size))}
{
    log(Info, "HEAP", "Initialized heap");
}

void *malloc(size_t size) {
    if (kheap)
        return kheap->malloc(size);
    log(Error, "HEAP", "malloc called before heap was initialized");
    abort();
}

void *realloc(void *old, size_t size) {
    if (kheap)
        return kheap->realloc(old, size);
    log(Error, "HEAP", "realloc called before heap was initialized");
    abort();
}

void free(void *ptr) {
    if (kheap) {
        kheap->free(ptr);
        return;
    }
    log(Error, "HEAP", "free called before heap was initialized");
    abort();
}

void *operator new(size_t size) {
    return malloc(size);
}

void operator delete(void *ptr) {
    return free(ptr);
}

void *operator new[](size_t size) {
    return operator new(size);
}

void operator delete[](void *ptr) {
    return operator delete(ptr);
}
