////////////////////////////////////////////
//                                        //
//       Hawkeye replacement policy       //
//   Maxwell Jung, maxwelljung@ucla.edu   //
//                                        //
////////////////////////////////////////////

#include "../inc/champsim_crc2.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define NUM_CORE 1
#define LLC_SETS NUM_CORE*2048
#define LLC_WAYS 16
#define OCC_VECT_LEN (8*LLC_WAYS)
#define PRED_INDEX_BITS 16
#define PRED_VALUE_BITS 8
#define EPSILON 1.0f

uint32_t predictor_[1<<PRED_INDEX_BITS];

int hashFunc(uint64_t PC) {
    int hashed_pc = 0;
    for (size_t i = 0; i < 8*sizeof(PC); i += PRED_INDEX_BITS) {
        hashed_pc = hashed_pc ^ (PC % (1 << PRED_INDEX_BITS));
        PC >>= PRED_INDEX_BITS;
    }

    return hashed_pc;
}

void incrementPredictor(uint64_t PC) {
    int hashed_pc = hashFunc(PC);

    if (predictor_[hashed_pc] < (1 << PRED_VALUE_BITS) - 1) {
        predictor_[hashed_pc]++;
    }
}

void decrementPredictor(uint64_t PC) {
    int hashed_pc = hashFunc(PC);

    if (predictor_[hashed_pc] > 0) {
        predictor_[hashed_pc]--;
    }
}

typedef struct {
    uint32_t timestamp;
    uint64_t PC;
} lru_entry_t;

typedef struct {
    uint32_t occ_val;
    uint64_t paddr;
    uint64_t PC;
} opt_gen_entry_t;

class OptGen {
private:
    opt_gen_entry_t occupancy_vect_[OCC_VECT_LEN];
public:
    OptGen() {
        memset(occupancy_vect_, 0, sizeof(occupancy_vect_));
    }

    void insert(uint64_t paddr, uint64_t PC) {
        // make room at the end
        for (int i = 1; i < OCC_VECT_LEN; i++) {
            occupancy_vect_[i-1] = occupancy_vect_[i];
        }
        // set most recent entry to 0 (1 if bypassing is not allowed)
        occupancy_vect_[OCC_VECT_LEN-1] = {.occ_val=1, .paddr=paddr, .PC=PC};

        // check history from back to front for first time load
        bool first_time_load = true;
        int first_time_load_index = OCC_VECT_LEN-1;
        uint64_t last_access_pc;
        for (int i = OCC_VECT_LEN-2; i >= 0; i--) {
            if (occupancy_vect_[i].paddr == paddr) {
                first_time_load = false;
                first_time_load_index = i;
                last_access_pc = occupancy_vect_[i].PC;
                break;
            }
        }

        if (!first_time_load) {
            // check if every element corresponding to the usage interval
            // is less than the cache capacity
            bool hit = true;
            for (int i = OCC_VECT_LEN-2; i >= first_time_load_index; i--) {
                if (occupancy_vect_[i].occ_val >= LLC_WAYS) {
                    hit = false;
                    break;
                };
            }

            // printf("paddr=%d, last_access_pc=%d, PC=%d\n", paddr, last_access_pc, PC);
            if (hit) {
                // increment elements in usage interval
                for (int i = OCC_VECT_LEN-2; i >= first_time_load_index; i--) {
                    occupancy_vect_[i].occ_val++;
                }
                // train PC positively
                incrementPredictor(last_access_pc);
                // printOccVect();
            } else {
                // train PC negatively
                decrementPredictor(last_access_pc);
                // printOccVect();
            }
        }
    }

    void printOccVect() {
        for (int i = 0; i < OCC_VECT_LEN; i++) {
            printf("%d ", occupancy_vect_[i].occ_val);
        }
        printf("\n");
    }
};

OptGen opt_gen_[LLC_SETS];
lru_entry_t lru[LLC_SETS][LLC_WAYS];

bool isCacheAverse(uint64_t PC) {
    return predictor_[hashFunc(PC)] < (1 << (PRED_VALUE_BITS-1));
}

// initialize replacement state
void InitReplacementState() {
    cout << "Initialize Hawkeye" << endl;

    // init predictor values
    for (int i = 0; i < 1<<PRED_INDEX_BITS; i++) {
        predictor_[i] = 1 << (PRED_VALUE_BITS-1);
    }

    // init lru values
    for (int i=0; i<LLC_SETS; i++) {
        for (int j=0; j<LLC_WAYS; j++) {
            lru[i][j].timestamp = j;
        }
    }

    /* initialize random seed: */
    srand(time(NULL));
}

// find replacement victim
// return value should be 0 ~ 15 or 16 (bypass)
uint32_t GetVictimInSet (uint32_t cpu, uint32_t set, const BLOCK *current_set, uint64_t PC, uint64_t paddr, uint32_t type) {
    // evict cache-averse line
    for (int i = 0; i < LLC_WAYS; i++) {
        if (lru[set][i].timestamp == LLC_WAYS-1) {
            // printf("Evicted cache averse line\n");
            return i;
        }
    }

    // if no cache-averse line, evict oldest line
    int oldest_victim = 0;
    for (int i = 0; i < LLC_WAYS; i++)
        if (lru[set][i].timestamp > lru[set][oldest_victim].timestamp) {
            oldest_victim = i;
        }

    decrementPredictor(lru[set][oldest_victim].PC);
    // printf("Evicted oldest cache friendly line (PC=%d, age=%d)\n", lru[set][oldest_victim].PC, lru[set][oldest_victim].timestamp);
    return oldest_victim;
}

// called on every cache hit and cache fill
void UpdateReplacementState (uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr, uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit) {
    opt_gen_[set].insert(paddr, PC);
    // printf("last_accessed_PC %d, opt_hit %d\n", last_accessed_PC, opt_hit);

    // update lru replacement state
    if (isCacheAverse(PC)) {
        if (hit) {
            lru[set][way].timestamp = LLC_WAYS-1;
            lru[set][way].PC = PC;
        } else {
            lru[set][way].timestamp = LLC_WAYS-1;
            lru[set][way].PC = PC;
        }
        // printf("PC=%d cache averse update\n", PC);
    } else { // cache friendly
        if (hit) {
            lru[set][way].timestamp = 0;
            lru[set][way].PC = PC;
        } else {
            // age all lines
            for (int i = 0; i < LLC_WAYS; i++) {
                if (lru[set][i].timestamp < LLC_WAYS-2) {
                    lru[set][i].timestamp++; // max value is LLC_WAYS-2

                    assert(lru[set][i].timestamp <= LLC_WAYS-2);
                }
            }
            lru[set][way].timestamp = 0;
            lru[set][way].PC = PC;
        }
        // printf("PC=%d cache friendly update\n", PC);
    }
}

// use this function to print out your own stats on every heartbeat 
void PrintStats_Heartbeat()
{

}

// use this function to print out your own stats at the end of simulation
void PrintStats()
{

}
