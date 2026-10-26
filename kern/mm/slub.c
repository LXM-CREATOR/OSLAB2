#include <assert.h>
#include <list.h>
#include <mmu.h>
#include <pmm.h>
#include <slub.h>
#include <stdio.h>

/*
 * A compact teaching SLUB implementation.
 *
 * Small requests use one-page slabs and power-of-two size classes. Free
 * objects store their next pointer inside the object itself. Requests larger
 * than the biggest class fall back to alloc_pages(). This intentionally omits
 * Linux SLUB's per-CPU caches, locking and NUMA support.
 */
#define SLUB_CACHE_COUNT 8
#define SLUB_SMALL_MAGIC 0x534C5542534D414CULL
#define SLUB_LARGE_MAGIC 0x534C55424C415247ULL
#define SLUB_ALIGNMENT   16

struct slub_page {
    uint64_t magic;
    size_t cache_index;
    size_t inuse;
    size_t total;
    size_t pages;
    size_t requested;
    void *free_list;
    list_entry_t link;
};

struct slub_cache {
    size_t object_size;
    list_entry_t slabs;
};

static const size_t slub_sizes[SLUB_CACHE_COUNT] = {
    16, 32, 64, 128, 256, 512, 1024, 2048,
};

static struct slub_cache slub_caches[SLUB_CACHE_COUNT];
static bool slub_ready;

static size_t
slub_header_size(void) {
    return ROUNDUP(sizeof(struct slub_page), SLUB_ALIGNMENT);
}

static void *
slub_page_kva(struct Page *page) {
    return (void *)(page2pa(page) + va_pa_offset);
}

static size_t
slub_find_cache(size_t size) {
    for (size_t i = 0; i < SLUB_CACHE_COUNT; i ++) {
        if (size <= slub_caches[i].object_size) {
            return i;
        }
    }
    return SLUB_CACHE_COUNT;
}

static struct slub_page *
slub_grow(size_t cache_index) {
    struct Page *page = alloc_page();
    if (page == NULL) {
        return NULL;
    }

    struct slub_cache *cache = &slub_caches[cache_index];
    struct slub_page *slab = (struct slub_page *)slub_page_kva(page);
    size_t header = slub_header_size();
    size_t total = (PGSIZE - header) / cache->object_size;
    assert(total > 0);

    slab->magic = SLUB_SMALL_MAGIC;
    slab->cache_index = cache_index;
    slab->inuse = 0;
    slab->total = total;
    slab->pages = 1;
    slab->requested = 0;
    slab->free_list = NULL;

    uintptr_t first = (uintptr_t)slab + header;
    for (size_t i = total; i > 0; i --) {
        void *object = (void *)(first + (i - 1) * cache->object_size);
        *(void **)object = slab->free_list;
        slab->free_list = object;
    }
    list_add(&cache->slabs, &slab->link);
    return slab;
}

static void *
slub_alloc_large(size_t size) {
    size_t header = slub_header_size();
    size_t page_count = ROUNDUP(header + size, PGSIZE) / PGSIZE;
    struct Page *page = alloc_pages(page_count);
    if (page == NULL) {
        return NULL;
    }

    struct slub_page *slab = (struct slub_page *)slub_page_kva(page);
    slab->magic = SLUB_LARGE_MAGIC;
    slab->cache_index = SLUB_CACHE_COUNT;
    slab->inuse = 1;
    slab->total = 1;
    slab->pages = page_count;
    slab->requested = size;
    slab->free_list = NULL;
    list_init(&slab->link);
    return (void *)((uintptr_t)slab + header);
}

void
slub_init(void) {
    if (slub_ready) {
        return;
    }
    for (size_t i = 0; i < SLUB_CACHE_COUNT; i ++) {
        slub_caches[i].object_size = slub_sizes[i];
        list_init(&slub_caches[i].slabs);
    }
    slub_ready = 1;
}

void *
slub_alloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    if (!slub_ready) {
        slub_init();
    }

    size_t cache_index = slub_find_cache(size);
    if (cache_index == SLUB_CACHE_COUNT) {
        return slub_alloc_large(size);
    }

    struct slub_cache *cache = &slub_caches[cache_index];
    struct slub_page *slab = NULL;
    list_entry_t *entry = &cache->slabs;
    while ((entry = list_next(entry)) != &cache->slabs) {
        struct slub_page *candidate =
            to_struct(entry, struct slub_page, link);
        if (candidate->free_list != NULL) {
            slab = candidate;
            break;
        }
    }
    if (slab == NULL) {
        slab = slub_grow(cache_index);
        if (slab == NULL) {
            return NULL;
        }
    }

    void *object = slab->free_list;
    slab->free_list = *(void **)object;
    slab->inuse ++;
    assert(slab->inuse <= slab->total);
    return object;
}

static void
slub_validate_small_object(struct slub_page *slab, void *ptr) {
    assert(slab->cache_index < SLUB_CACHE_COUNT);
    size_t object_size = slub_caches[slab->cache_index].object_size;
    uintptr_t first = (uintptr_t)slab + slub_header_size();
    uintptr_t address = (uintptr_t)ptr;
    assert(address >= first);
    assert(address < first + slab->total * object_size);
    assert((address - first) % object_size == 0);

    for (void *free_object = slab->free_list;
         free_object != NULL;
         free_object = *(void **)free_object) {
        assert(free_object != ptr);
    }
}

void
slub_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    struct slub_page *slab =
        (struct slub_page *)ROUNDDOWN((uintptr_t)ptr, PGSIZE);
    assert(slab->magic == SLUB_SMALL_MAGIC ||
           slab->magic == SLUB_LARGE_MAGIC);

    if (slab->magic == SLUB_LARGE_MAGIC) {
        assert((uintptr_t)ptr == (uintptr_t)slab + slub_header_size());
        size_t pages_to_free = slab->pages;
        slab->magic = 0;
        free_pages(pa2page(PADDR(slab)), pages_to_free);
        return;
    }

    slub_validate_small_object(slab, ptr);
    assert(slab->inuse > 0);
    *(void **)ptr = slab->free_list;
    slab->free_list = ptr;
    slab->inuse --;

    if (slab->inuse == 0) {
        list_del(&slab->link);
        slab->magic = 0;
        free_page(pa2page(PADDR(slab)));
    }
}

static void
slub_fill(void *ptr, size_t size, uint8_t value) {
    uint8_t *bytes = (uint8_t *)ptr;
    for (size_t i = 0; i < size; i ++) {
        bytes[i] = value;
    }
}

static void
slub_expect(void *ptr, size_t size, uint8_t value) {
    uint8_t *bytes = (uint8_t *)ptr;
    for (size_t i = 0; i < size; i ++) {
        assert(bytes[i] == value);
    }
}

void
slub_check(void) {
    static const size_t requests[] = {
        1, 16, 17, 32, 63, 64, 129, 511, 1024, 1500, 2048, 2049, 5000,
    };
    void *objects[sizeof(requests) / sizeof(requests[0])];
    size_t before = nr_free_pages();

    assert(slub_alloc(0) == NULL);
    for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i ++) {
        objects[i] = slub_alloc(requests[i]);
        assert(objects[i] != NULL);
        slub_fill(objects[i], requests[i], (uint8_t)(0x20 + i));
        for (size_t j = 0; j < i; j ++) {
            assert(objects[i] != objects[j]);
        }
    }
    for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i ++) {
        slub_expect(objects[i], requests[i], (uint8_t)(0x20 + i));
    }
    cprintf("[SLUB Test] different sizes and no overlap: PASS\n");

    for (size_t i = sizeof(requests) / sizeof(requests[0]); i > 0; i --) {
        slub_free(objects[i - 1]);
    }
    assert(nr_free_pages() == before);

    void *first = slub_alloc(64);
    void *second = slub_alloc(64);
    assert(first != NULL && second != NULL && first != second);
    slub_free(first);
    void *reused = slub_alloc(64);
    assert(reused == first);
    slub_free(reused);
    slub_free(second);
    assert(nr_free_pages() == before);
    cprintf("[SLUB Test] object reuse: PASS\n");

    void *many[70];
    for (size_t i = 0; i < 70; i ++) {
        many[i] = slub_alloc(64);
        assert(many[i] != NULL);
    }
    assert(nr_free_pages() <= before - 2);
    for (size_t i = 0; i < 70; i ++) {
        slub_free(many[i]);
    }
    assert(nr_free_pages() == before);
    cprintf("[SLUB Test] slab growth and page reclaim: PASS\n");
    cprintf("[SLUB Test] ALL PASS\n");
}
