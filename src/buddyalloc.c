#include <exec/types.h>
#include <proto/exec.h>

#include "buddyalloc.h"
#include "emu68-vc4.h"

/*
 * Size of the display list which the buddy allocator manages. Currently, due to
 * using of UWORD, maximum size is 63336 items. Not that it matters as display list
 * space is much shorter.
 */
#define ARRAY_SIZE 0x300
// Almost every DL is larger than 8 items, so set minimum block size to 16 bytes
#define MIN_BLOCK_SIZE 16

// Calculate the largest power of 2 block that fits in ARRAY_SIZE
#define MAX_BLOCK_SIZE_CALC(size) \
    ((size) >= 32768 ? 32768 : \
     (size) >= 16384 ? 16384 : \
     (size) >= 8192 ? 8192 : \
     (size) >= 4096 ? 4096 : \
     (size) >= 2048 ? 2048 : \
     (size) >= 1024 ? 1024 : \
     (size) >= 512 ? 512 : \
     (size) >= 256 ? 256 : \
     (size) >= 128 ? 128 : \
     (size) >= 64 ? 64 : \
     (size) >= 32 ? 32 : 16)

#define MAX_BLOCK_SIZE MAX_BLOCK_SIZE_CALC(ARRAY_SIZE)

// Calculate number of orders: log2(MAX_BLOCK_SIZE / MIN_BLOCK_SIZE) + 1
#define NUM_ORDERS_CALC(max, min) \
    ((max) == (min) ? 1 : \
     (max) / (min) >= 2048 ? 12 : \
     (max) / (min) >= 1024 ? 11 : \
     (max) / (min) >= 512 ? 10 : \
     (max) / (min) >= 256 ? 9 : \
     (max) / (min) >= 128 ? 8 : \
     (max) / (min) >= 64 ? 7 : \
     (max) / (min) >= 32 ? 6 : \
     (max) / (min) >= 16 ? 5 : \
     (max) / (min) >= 8 ? 4 : \
     (max) / (min) >= 4 ? 3 : \
     (max) / (min) >= 2 ? 2 : 1)

#define NUM_ORDERS NUM_ORDERS_CALC(MAX_BLOCK_SIZE, MIN_BLOCK_SIZE)

// Calculate number of 32-bit words needed for bitmaps at each level
#define BITMAP_WORDS_FOR_ORDER(order) \
    (((ARRAY_SIZE / (MIN_BLOCK_SIZE << (order))) + 31) / 32)

// Maximum number of 32-bit words needed (for order 0)
#define MAX_BITMAP_WORDS BITMAP_WORDS_FOR_ORDER(0)

typedef struct {
    //UBYTE array[ARRAY_SIZE];
    // Each order gets enough 32-bit words to hold its bitmap
    ULONG bitmap[NUM_ORDERS][MAX_BITMAP_WORDS];
} BuddyAllocator;

static inline int order_for_size(UWORD size) {
    int order = 0;
    UWORD block_size = MIN_BLOCK_SIZE;
    while (block_size < size && order < NUM_ORDERS - 1) {
        block_size *= 2;
        order++;
    }
    return order;
}

static inline UWORD block_size_for_order(int order) {
    return MIN_BLOCK_SIZE << order;
}

static inline int num_blocks_for_order(int order) {
    return ARRAY_SIZE / block_size_for_order(order);
}

static inline BOOL get_bit(const ULONG *bitmap, int index) {
    int word = index / 32;
    int bit = index % 32;
    return (bitmap[word] & (1U << bit)) != 0;
}

static inline void set_bit(ULONG *bitmap, int index, BOOL value) {
    int word = index / 32;
    int bit = index % 32;
    if (value) {
        bitmap[word] |= (1U << bit);
    } else {
        bitmap[word] &= ~(1U << bit);
    }
}

static inline int find_first_set(const ULONG *bitmap, int max_bits) {
    int num_words = (max_bits + 31) / 32;
    for (int w = 0; w < num_words; w++) {
        if (bitmap[w] != 0) {
            // Found a word with set bits
            for (int b = 0; b < 32 && w * 32 + b < max_bits; b++) {
                if (bitmap[w] & (1U << b)) {
                    return w * 32 + b;
                }
            }
        }
    }
    return -1;
}

void BuddyInit(struct VC4Base *base) {
    struct ExecBase *SysBase = base->vc4_SysBase;
    BuddyAllocator *alloc = AllocMem(sizeof(BuddyAllocator), MEMF_CLEAR);

    base->vc4_BuddyAllocator = (APTR)alloc;

    if (alloc) {
        // Fill from largest order down
        int remaining = ARRAY_SIZE;
        int offset = 0;
        
        for (int order = NUM_ORDERS - 1; order >= 0 && remaining > 0; order--) {
            int block_size = block_size_for_order(order);
            
            while (remaining >= block_size) {
                int bit_index = offset / block_size;
                set_bit(alloc->bitmap[order], bit_index, TRUE);
                offset += block_size;
                remaining -= block_size;
            }
        }
    }
}

ULONG BuddyAlloc(struct VC4Base *base, UWORD size) {
    BuddyAllocator *alloc = (BuddyAllocator *)base->vc4_BuddyAllocator;
    
    int order = order_for_size(size);
    if (order >= NUM_ORDERS) {
        return 0xffffffff;
    }
    
    int current_order = order;
    while (current_order < NUM_ORDERS) {
        int bit_index = find_first_set(alloc->bitmap[current_order], 
                                       num_blocks_for_order(current_order));
        if (bit_index >= 0) {
            set_bit(alloc->bitmap[current_order], bit_index, FALSE);
            
            while (current_order > order) {
                current_order--;
                int left_buddy = bit_index * 2;
                int right_buddy = bit_index * 2 + 1;
                
                set_bit(alloc->bitmap[current_order], left_buddy, TRUE);
                set_bit(alloc->bitmap[current_order], right_buddy, TRUE);
                
                bit_index = left_buddy;
                set_bit(alloc->bitmap[current_order], left_buddy, FALSE);
            }
            
            UWORD offset = bit_index * block_size_for_order(order);
            return offset | ((ULONG)size << 16);
        }
        current_order++;
    }
    
    return 0xffffffff;
}

void BuddyFree(struct VC4Base *base, ULONG id) {
    BuddyAllocator *alloc = (BuddyAllocator *)base->vc4_BuddyAllocator;
    UWORD offset = BUDDY_OFFSET(id);
    UWORD size = BUDDY_SIZE(id);

    // Return immediately, if result of failed allocation is given to this function
    if (id == 0xffffffff)
        return;
    
    int order = order_for_size(size);
    if (order >= NUM_ORDERS) {
        return;
    }
    
    int bit_index = offset / block_size_for_order(order);
    set_bit(alloc->bitmap[order], bit_index, TRUE);
    
    while (order < NUM_ORDERS - 1) {
        int buddy_index = bit_index ^ 1;
        
        // Check if buddy exists and is free
        int block_size = block_size_for_order(order);
        UWORD buddy_offset = buddy_index * block_size;
        
        if (buddy_offset >= ARRAY_SIZE) {
            break;
        }
        
        if (!get_bit(alloc->bitmap[order], buddy_index)) {
            break;
        }
        
        set_bit(alloc->bitmap[order], bit_index, FALSE);
        set_bit(alloc->bitmap[order], buddy_index, FALSE);
        
        order++;
        bit_index = bit_index / 2;
        set_bit(alloc->bitmap[order], bit_index, TRUE);
    }
}
