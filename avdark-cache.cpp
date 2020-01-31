/**
 * Cache simulation using a functional system simulator.
 *
 * Course: Advanced Computer Architecture, Uppsala University
 * Course Part: Lab assignment 1
 *
 * Original authors: UART 1.0(?)
 * Modified by: Andreas Sandberg <andreas.sandberg@it.uu.se>
 * Revision (2015, 2016, 2017, 2018): German Ceballos, Johan Janzen, Chris Sakalis
 *
 */

#include "avdark-cache.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>

#ifdef SIMICS
/* Simics stuff  */
#include <simics/api.h>
#include <simics/alloc.h>
#include <simics/utils.h>

#define AVDC_MALLOC(nelems, type) (type *) MM_MALLOC(nelems, type)
#define AVDC_FREE(p) MM_FREE(p)
#warning "SIMICS DETECTED"
break;

#else

#define AVDC_MALLOC(nelems, type) (type *) malloc(nelems * sizeof(type))
#define AVDC_FREE(p) free(p)

#endif

/**
 * Cache block information.
 *
 * Fill in the data structure
 * HINT: You will probably need to change this structure
 */
struct avdc_cache_line {
        avdc_tag_t tag;
        int valid;
	long counter; // perhaps this counter is what we need to change
};

/**
 * Extract the cache line tag from a physical address.
 *
 * You probably don't want to change this function, instead you may
 * want to change how the tag_shift field is calculated in
 * avdc_resize().
 */
static inline avdc_pa_t tag_from_pa(avdark_cache_t *self, avdc_pa_t pa) {
        return pa >> self->tag_shift;
}

/**
 * Calculate the cache line index from a physical address.
 *
 * Feel free to experiment and change this function
 */
static inline int index_from_pa(avdark_cache_t *self, avdc_pa_t pa) {
        return (pa >> self->block_size_log2) & (self->number_of_sets - 1);
}

/**
 * Computes the log2 of a 32 bit integer value. Used in dc_init
 *
 * Do NOT modify!
 */
static int log2_int32(uint32_t value) {
        int i;
        for (i = 0; i < 32; i++) {
                value >>= 1;
                if (value == 0) {
                        break;
		}
        }
        return i;
}

/**
 * Check if a number is a power of 2. Used for cache parameter sanity
 * checks.
 *
 * Do NOT modify!
 */
static int is_power_of_two(uint64_t val) {
        return ((((val)&(val-1)) == 0) && (val > 0));
}

void avdc_dbg_log(avdark_cache_t *self, const char *msg, ...) {
        va_list ap;

        if (self->dbg) {
                const char *name = self->dbg_name ? self->dbg_name : "AVDC";
                fprintf(stderr, "[%s] dbg: ", name);
                va_start(ap, msg);
                vfprintf(stderr, msg, ap);
                va_end(ap);
        }
}


void avdc_access(avdark_cache_t *self, avdc_pa_t pa, avdc_access_type_t type) {
        /* TODO: Update this function */
        avdc_tag_t tag = tag_from_pa(self, pa);
        int setIndex = index_from_pa(self, pa); // this is essentially the set it corresponds to
        int hit = 0;
	avdc_cache_line_t *lru_line, *empty_line, *hit_line;
	empty_line = NULL;
	hit_line = NULL;
	self->lruCounter++; // every access will end with either a hit or a block replacement, must update the counter each time
	lru_line = &self->cache[setIndex][0];

	// TODO OPTIMIZE //
	
	for(unsigned int i = 0; i < self->assoc; i++) { // loop through all the lines in the set
		if (self->cache[setIndex][i].valid) { // if the current block valid bit is 1, there must be A block there
			if(self->cache[setIndex][i].tag == tag) { // if the tag matches
				hit_line = &self->cache[setIndex][i]; // update the hit block
				break;//stonks up
			}

			// update lru line if the value is the lowest
			if(self->cache[setIndex][i].counter < lru_line->counter){
				lru_line = &self->cache[setIndex][i];
			}

		} else { // cold miss if its not valid
			empty_line = &self->cache[setIndex][i];
			// keep track of the line that is not valid, we can add a new block there if no hit without eviction
		}
	}

	if(hit_line != NULL) { // HIT OR MISS; i guess they always miss huh
		hit_line->counter = self->lruCounter;
		hit = 1;
	} else if (empty_line != NULL) { // COLD MISS
		empty_line->valid = 1;
		empty_line->tag = tag;
		empty_line->counter = self->lruCounter;
	} else { // CONFLICT MISS; must do an eviction
		lru_line->tag = tag;
		lru_line->counter = self->lruCounter;
	}


        //hit = self->cache[index].valid && self->cache[index].tag == tag;
        //if (!hit) {
        //        self->cache[index].valid = 1;
        //        self->cache[index].tag = tag;
        //}

        switch (type) {
        case AVDC_READ: /* Read accesses */
                avdc_dbg_log(self, "read: pa: 0x%.16lx, tag: 0x%.16lx, index: %d, hit: %d\n",
                             (unsigned long)pa, (unsigned long)tag, setIndex, hit);
                self->stat_data_read += 1;
                if (!hit)
                        self->stat_data_read_miss += 1;
                break;

        case AVDC_WRITE: /* Write accesses */
                avdc_dbg_log(self, "write: pa: 0x%.16lx, tag: 0x%.16lx, index: %d, hit: %d\n",
                             (unsigned long)pa, (unsigned long)tag, setIndex, hit);
                self->stat_data_write += 1;
                if (!hit)
                        self->stat_data_write_miss += 1;
                break;
        }
}

void avdc_flush_cache(avdark_cache_t *self) {
        /* Update this function */
        for (int i = 0; i < self->number_of_sets; i++) {
		for (unsigned int j = 0; j < self->assoc; j++) {
                	self->cache[i][j].valid = 0;
               		self->cache[i][j].tag = 0;
			self->cache[i][j].counter = 0; // Should loop through the entire cache and set every field //
		}
	}
}

int avdc_resize(avdark_cache_t *self, avdc_size_t size, 
		avdc_block_size_t block_size, avdc_assoc_t assoc) {

	/* Update this function */
        /* HINT: This function precomputes some common values and
         * allocates the self->lines array. You will need to update
         * this to reflect any changes to how this array is supposed
         * to be allocated.
         */

        /* Verify that the parameters are sane */
        if (!is_power_of_two(size) ||
            !is_power_of_two(block_size) ||
            !is_power_of_two(assoc)) {
                fprintf(stderr, "size, block-size and assoc all have to be powers of two and > zero\n");
                return 0;
        }

        /* Update the stored parameters */
        self->size = size;
        self->block_size = block_size;
        self->assoc = assoc;

        /* Cache some common values */
        self->number_of_sets = (self->size / self->block_size) / self->assoc;
        self->block_size_log2 = log2_int32(self->block_size);
        self->tag_shift = self->block_size_log2 + log2_int32(self->number_of_sets);

        /* (Re-)Allocate space for the tags array */
        if (self->cache) {
		for(int i = 0; i < self->number_of_sets; i++){
			AVDC_FREE(self->cache[i]);
		}
                AVDC_FREE(self->cache);
	}
        /* HINT: If you change this, you may have to update
         * avdc_delete() to reflect changes to how thie self->lines
         * array is allocated. */

	
	self->cache = (avdc_cache_line **)malloc(sizeof(avdc_cache_line_t *) * self->number_of_sets);
	// TODO Should check return value of malloc//

	for (int i = 0; i < self->number_of_sets; i++) {
		self->cache[i] =(avdc_cache_line *) malloc(sizeof(avdc_cache_line_t) * self->assoc);
		// TODO Should check return value of malloc//
	}

        /* Flush the cache, this initializes the tag array to a known state */
        avdc_flush_cache(self);

        return 1;
}

void avdc_print_info(avdark_cache_t *self) {
        fprintf(stderr, "Cache Info\n");
        fprintf(stderr, "size: %d, assoc: %d, line-size: %d\n", self->size, self->assoc, self->block_size);
}

void avdc_print_internals(avdark_cache_t *self) {
        fprintf(stderr, "Cache Internals\n");
        fprintf(stderr, "size: %d, assoc: %d, line-size: %d\n", self->size, self->assoc, self->block_size);
        for (int i = 0; i < self->number_of_sets; i++) {
		for(unsigned int j = 0; j < self->assoc; j++) {
               		fprintf(stderr, "tag: <0x%.16lx> valid: %d\n", (long unsigned int)self->cache[i][j].tag, self->cache[i][j].valid);
		}
	}
}

void avdc_reset_statistics(avdark_cache_t *self) {
        self->stat_data_read = 0;
        self->stat_data_read_miss = 0;
        self->stat_data_write = 0;
        self->stat_data_write_miss = 0;
}

avdark_cache_t *avdc_new(avdc_size_t size, avdc_block_size_t block_size, avdc_assoc_t assoc) {
        avdark_cache_t *self;

        self = AVDC_MALLOC(1, avdark_cache_t);

        memset(self, 0, sizeof(*self));
        self->dbg = 0;
	self->lruCounter = 0;
        if (!avdc_resize(self, size, block_size, assoc)) {
                AVDC_FREE(self);
                return NULL;
        }

        return self;
}

void avdc_delete(avdark_cache_t *self) {
        if (self->cache) // if the cache pointer isnt null
		for(int i = 0; i < self->number_of_sets; i++) { // loop through the entire array of sets
			AVDC_FREE(self->cache[i]); // clear each set pointer
		}
                AVDC_FREE(self->cache); // free up the pointer to the sets
        AVDC_FREE(self); // free up the cache pointer
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * c-file-style: "linux"
 * compile-command: "make -k -C ../../"
 * End:
 */
