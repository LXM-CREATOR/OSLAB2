#include <buddy_pmm.h>
#include <list.h>
#include <pmm.h>
#include <stdio.h>

/* Supports blocks from 1 page to 2^20 pages. */
#define BUDDY_MAX_ORDER 20

static list_entry_t buddy_free_lists[BUDDY_MAX_ORDER + 1];
static size_t buddy_nr_free;

static size_t
buddy_order_size(unsigned int order) {
    return (size_t)1 << order;
}

static unsigned int
buddy_order_for_pages(size_t n) {
    unsigned int order = 0;
    size_t size = 1;
    while (size < n && order < BUDDY_MAX_ORDER) {
        size <<= 1;
        order ++;
    }
    assert(size >= n);
    return order;
}

static void
buddy_add_block(struct Page *base, unsigned int order) {
    base->property = buddy_order_size(order);
    SetPageProperty(base);
    list_add(&buddy_free_lists[order], &base->page_link);
}

static void
buddy_init(void) {
    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order ++) {
        list_init(&buddy_free_lists[order]);
    }
    buddy_nr_free = 0;
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);

    for (struct Page *p = base; p != base + n; p ++) {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }

    size_t remaining = n;
    while (remaining > 0) {
        ppn_t ppn = page2ppn(base);
        unsigned int order = 0;

        while (order < BUDDY_MAX_ORDER) {
            size_t next_size = buddy_order_size(order + 1);
            if (next_size > remaining || (ppn & (next_size - 1)) != 0) {
                break;
            }
            order ++;
        }

        size_t block_size = buddy_order_size(order);
        buddy_add_block(base, order);
        base += block_size;
        remaining -= block_size;
    }
    buddy_nr_free += n;
}

static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);

    unsigned int wanted = buddy_order_for_pages(n);
    size_t allocated_size = buddy_order_size(wanted);
    if (allocated_size > buddy_nr_free) {
        return NULL;
    }

    unsigned int found = wanted;
    while (found <= BUDDY_MAX_ORDER &&
           list_empty(&buddy_free_lists[found])) {
        found ++;
    }
    if (found > BUDDY_MAX_ORDER) {
        return NULL;
    }

    list_entry_t *entry = list_next(&buddy_free_lists[found]);
    struct Page *base = le2page(entry, page_link);
    list_del(entry);
    ClearPageProperty(base);

    while (found > wanted) {
        found --;
        size_t half_size = buddy_order_size(found);
        struct Page *right = base + half_size;
        buddy_add_block(right, found);
    }

    base->property = allocated_size;
    ClearPageProperty(base);
    buddy_nr_free -= allocated_size;
    return base;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);

    unsigned int order = buddy_order_for_pages(n);
    size_t block_size = buddy_order_size(order);
    assert(base->property == block_size);

    for (struct Page *p = base; p != base + block_size; p ++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }
    buddy_nr_free += block_size;

    while (order < BUDDY_MAX_ORDER) {
        ppn_t ppn = page2ppn(base);
        ppn_t buddy_ppn = ppn ^ block_size;

        if (buddy_ppn < nbase || buddy_ppn + block_size > npage) {
            break;
        }

        struct Page *buddy = pages + (buddy_ppn - nbase);
        if (!PageProperty(buddy) || buddy->property != block_size) {
            break;
        }

        list_del(&buddy->page_link);
        ClearPageProperty(buddy);
        buddy->property = 0;
        if (buddy < base) {
            base = buddy;
        }
        order ++;
        block_size <<= 1;
    }

    buddy_add_block(base, order);
}

static size_t
buddy_nr_free_pages(void) {
    return buddy_nr_free;
}

static void
buddy_check(void) {
    size_t before = buddy_nr_free_pages();
    assert(before >= 6);

    struct Page *p0 = alloc_pages(1);
    struct Page *p1 = alloc_pages(1);
    struct Page *p2 = alloc_pages(3);
    assert(p0 != NULL && p1 != NULL && p2 != NULL);
    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(p0->property == 1 && p1->property == 1);
    assert(p2->property == 4);
    assert(buddy_nr_free_pages() == before - 6);

    free_pages(p2, 3);
    free_pages(p1, 1);
    free_pages(p0, 1);
    assert(buddy_nr_free_pages() == before);

    cprintf("buddy_check() succeeded!\n");
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
