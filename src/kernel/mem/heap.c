#include <sys.h>
#include <mem/heap.h>
#include <mem/vmm.h>
#include <mem/pmm.h>
#include <kernel/logging.h>
#include <libc/string.h>

heap_t *heap_add(size_t pages, size_t bsize, vmm_t *vi, heap_t *n) {
    heap_t *h = valloc(pages, vi);
    h->bm.bm = (uint8_t*) h + sizeof(heap_t);
    init_bitmap(&h->bm, pages * PAGE_SIZE - sizeof(heap_t), bsize);
    h->next = n;

    return h;
}

// Memory is not guaranteed to be zeroed
void *halloc(size_t size, heap_t *heap) {
    for (heap_t *b = heap; b; b = b->next) {
        void *addr = bm_alloc(size, &b->bm);
        if (addr)
            return addr;
    }

    return NULL;
}

void hfree(void *ptr, heap_t *heap) {
    for (heap_t *b = heap; b; b = b->next)
        if (bm_free(ptr, &heap->bm))
            return;
    log(Warning, "HEAP", "Pointer was not from any known region");
}

void *hrealloc(void *old, size_t size, heap_t heap) {
    // void *new = halloc(size, heap);
    // heap_region_t *b = get_region(old, heap);
    // if (!b) {
    //     log(Warning, "HEAP", "Old pointer was not from any known region");
    //     return NULL;
    // }
    // uint8_t *bm = (uint8_t*) &b[1];
    // size_t sblock = ((uintptr_t) old - (uintptr_t) bm) / b->bsize;
    // uint8_t id = bm[sblock];
    // size_t blocks = 0;
    // for (; bm[sblock + blocks] == id && (sblock + blocks) < (b->size / b->bsize); blocks++);
    // size_t cpysize = blocks * b->bsize;
    // if (size < cpysize)
    //     cpysize = size;
    // memcpy(new, old, size);
    // hfree(old, heap);
    // return new;
    return NULL;
}

void free_heap(vmm_t *vmm_info, heap_t *heap) {
    for (heap_t *b = heap; b; b = b->next)
        vfree(b, vmm_info);
}
