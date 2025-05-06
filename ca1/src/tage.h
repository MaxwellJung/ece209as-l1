// #include "ooo_cpu.h"
#include <stdint.h>
#include <stdlib.h>

#define Tag uint16_t
#define Index uint16_t
#define Path uint64_t
#define History uint64_t
#define TAGE_BIMODAL_TABLE_INDEX_BITS 14
#define TAGE_BIMODAL_TABLE_SIZE (1 << TAGE_BIMODAL_TABLE_INDEX_BITS)
#define TAGE_NUM_COMPONENTS 5 // TODO
#define TAGE_BASE_COUNTER_BITS 2
#define TAGE_BASE_COUNTER_MAX ((1 << TAGE_BASE_COUNTER_BITS) - 1)
#define TAGE_BASE_COUNTER_WEAKLY_TAKEN (1 << (TAGE_BASE_COUNTER_BITS - 1))
#define TAGE_COUNTER_BITS 3
#define TAGE_COUNTER_MAX ((1 << TAGE_COUNTER_BITS) - 1)
#define TAGE_COUNTER_WEAKLY_TAKEN (1 << (TAGE_COUNTER_BITS - 1))
#define TAGE_USEFUL_BITS 2
#define TAGE_USEFUL_MAX ((1 << TAGE_USEFUL_BITS) - 1)
#define TAGE_GLOBAL_HISTORY_BUFFER_LENGTH 1024
#define TAGE_PATH_HISTORY_BUFFER_LENGTH 32
#define TAGE_MIN_HISTORY_LENGTH 5
#define TAGE_HISTORY_ALPHA 2.71828182846
#define TAGE_RESET_USEFUL_INTERVAL 512000
#define LAST_N_BITS(x, n) ((x) & ((1 << (n)) - 1)) // Extract last n bits from x
#define ITH_BIT(x, i) (((x) & (1 << (i))) >> (i)) // Extract i-th bit from x

#define TAGE_MAX_INDEX_BITS 12
const uint8_t TAGE_INDEX_BITS[TAGE_NUM_COMPONENTS] = {12, 12, 12, 12, 12, };
const uint8_t TAGE_TAG_BITS[TAGE_NUM_COMPONENTS] = {9, 9, 9, 9, 9, };

class BitQueue {
private:
    uint32_t* _bit_arr;
    size_t _bit_length;
    size_t _arr_len;

public:
    BitQueue(size_t length) : _bit_length(length) {
        _arr_len = (_bit_length+32-1)/32;
        _bit_arr = new uint32_t[_arr_len];
    }

    void push(bool bit) {
        uint8_t carry_in = bit ? 1 : 0;
        uint8_t carry_out;
        for (int i = 0; i < _arr_len; i++) {
            carry_out = ITH_BIT(_bit_arr[i], 31);
            _bit_arr[i] <<= 1;
            _bit_arr[i] |= carry_in;
            carry_in = carry_out;
        }
    }

    uint64_t to_ulong() {
        if (_arr_len < 2)
            return _bit_arr[0];
        
        return ((uint64_t)_bit_arr[1] << 32) | _bit_arr[0];
    }

    uint32_t slice(size_t start, size_t end) {
        uint32_t length = end - start + 1;
        uint32_t upper = _bit_arr[end/32];
        uint32_t lower = _bit_arr[start/32];
        uint64_t concat = ((uint64_t)upper << 32) | lower;
        return LAST_N_BITS(concat >> (start % 32), length);
    }

    uint32_t get_compressed(size_t in_bits, size_t out_bits) {
        // Find how many out_bits can fit into u32 (aka lanes)
        // such that each loop XORs multiple lanes
        // Converge lanes into single out_bits at the end
        uint32_t lanes = 32/out_bits;
        uint32_t result = 0;
        uint32_t partial = 0;
        int end;
        for (int start = 0; start < in_bits; start += lanes*out_bits) {
            end = start + lanes*out_bits - 1;
            if (end > in_bits-1)
                end = in_bits-1;
            partial = slice(start, end);
            result ^= partial;
        }

        uint32_t compressed = 0;
        for (int i = 0; i < lanes; i++) {
            compressed ^= LAST_N_BITS(result, out_bits);
            result >>= out_bits;
        }

        return compressed;
    }

    ~BitQueue() {
        delete[] _bit_arr;
    }
};

struct tage_predictor_table_entry
{
    uint8_t ctr; // The counter on which prediction is based Range - 0-7
    Tag tag; // Stores the tag
    uint8_t useful; // Variable to store the usefulness of the entry Range - 0-3
};

class Tage
{
private:
    /* data */
    int num_branches; // Stores the number of branch instructions since the last useful reset
    uint8_t bimodal_table[TAGE_BIMODAL_TABLE_SIZE]; // Array represent the counters of the bimodal table
    struct tage_predictor_table_entry predictor_table[TAGE_NUM_COMPONENTS][(1 << TAGE_MAX_INDEX_BITS)];
    BitQueue global_history; // Stores the global branch history
    BitQueue path_history; // Stores the last bits of the last N branch PCs
    uint8_t use_alt_on_na; // 4 bit counter to decide between alternate and provider component prediction
    int component_history_lengths[TAGE_NUM_COMPONENTS]; // History lengths used to compute hashes for different components
    bool tage_pred, pred, alt_pred; // Final prediction , provider prediction, and alternate prediction
    int pred_comp, alt_comp; // Provider and alternate component of last branch PC
    int STRONG; //Strength of provider prediction counter of last branch PC

public:
    void init();  // initialise the member variables
    bool predict(uint64_t ip);  // return the prediction from tage
    void update(uint64_t ip, bool taken);  // updates the state of tage

    Index get_bimodal_index(uint64_t ip);   // helper hash function to index into the bimodal table
    Index get_predictor_index(uint64_t ip, int component);   // helper hash function to index into the predictor table using histories
    Tag get_tag(uint64_t ip, int component);   // helper hash function to get the tag of particular ip and component
    int get_match_below_n(uint64_t ip, int component);   // helper function to find the hit component strictly before the component argument
    void ctr_update(uint8_t &ctr, int cond, int low, int high);   // counter update helper function (including clipping)
    bool get_prediction(uint64_t ip, int comp);   // helper function for prediction
    Path get_path_history_hash(int component);   // helper hash function to compress the path history
    History get_compressed_global_history(int inSize, int outSize); // Compress global history of last 'inSize' branches into 'outSize' by wrapping the history

    Tage();
    ~Tage();
};

void Tage::init()
{
    /*
    Initializes the member variables
    */
    use_alt_on_na = 8;
    tage_pred = 0;
    for (int i = 0; i < TAGE_BIMODAL_TABLE_SIZE; i++)
    {
        bimodal_table[i] = TAGE_BASE_COUNTER_WEAKLY_TAKEN; // weakly taken
    }
    for (int i = 0; i < TAGE_NUM_COMPONENTS; i++)
    {
        for (int j = 0; j < (1 << TAGE_INDEX_BITS[i]); j++)
        {
            predictor_table[i][j].ctr = TAGE_COUNTER_WEAKLY_TAKEN; // weakly taken
            predictor_table[i][j].useful = 0;                           // not useful
            predictor_table[i][j].tag = 0;
        }
    }

    double power = 1;
    for (int i = 0; i < TAGE_NUM_COMPONENTS; i++)
    {
        component_history_lengths[i] = int(TAGE_MIN_HISTORY_LENGTH * power + 0.5); // set component history lengths
        power *= TAGE_HISTORY_ALPHA;
    }

    num_branches = 0;
}

bool Tage::get_prediction(uint64_t ip, int comp)
{
    /*
    Get the prediction according to a specific component 
    */
    if(comp == 0) // Check if component is the bimodal table
    {
        Index index = get_bimodal_index(ip); // Get bimodal index
        return bimodal_table[index] >= TAGE_BASE_COUNTER_WEAKLY_TAKEN;
    }
    else
    {
        Index index = get_predictor_index(ip, comp); // Get component-specific index
        return predictor_table[comp - 1][index].ctr >= TAGE_COUNTER_WEAKLY_TAKEN;
    }
}

bool Tage::predict(uint64_t ip)
{
    pred_comp = get_match_below_n(ip, TAGE_NUM_COMPONENTS + 1); // Get the first predictor from the end which matches the PC
    alt_comp = get_match_below_n(ip, pred_comp); // Get the first predictor below the provider which matches the PC 

    //Store predictions for both components for use in the update step
    pred = get_prediction(ip, pred_comp); 
    alt_pred = get_prediction(ip, alt_comp);

    if(pred_comp == 0)
        tage_pred = pred;
    else
    {
        Index index = get_predictor_index(ip, pred_comp);
        STRONG = abs(2 * predictor_table[pred_comp - 1][index].ctr + 1 - (1 << TAGE_COUNTER_BITS)) > 1;
        if (use_alt_on_na < 8 || STRONG) // Use provider component only if USE_ALT_ON_NA < 8 or the provider counter is strong
            tage_pred = pred;
        else
            tage_pred = alt_pred;
    }
    return tage_pred;
}

void Tage::ctr_update(uint8_t &ctr, int cond, int low, int high)
{
    /*
    Function to update bounded counters according to some condition
    */
    if(cond && ctr < high)
        ctr++;
    else if(!cond && ctr > low)
        ctr--;
}

void Tage::update(uint64_t ip, bool taken)
{
    /*
    function to update the state (member variables) of the tage class
    */
    if (pred_comp > 0)  // the predictor component is not the bimodal table
    {
        struct tage_predictor_table_entry *entry = &predictor_table[pred_comp - 1][get_predictor_index(ip, pred_comp)];
        uint8_t useful = entry->useful;

        if(!STRONG)
        {
            if (pred != alt_pred)
                ctr_update(use_alt_on_na, !(pred == taken), 0, 15);
        }

        if(alt_comp > 0)  // alternate component is not the bimodal table
        {
            struct tage_predictor_table_entry *alt_entry = &predictor_table[alt_comp - 1][get_predictor_index(ip, alt_comp)];
            if(useful == 0)
                ctr_update(alt_entry->ctr, taken, 0, TAGE_COUNTER_MAX); // update ctr for alternate predictor if useful for predictor is 0
        }
        else
        {
            Index index = get_bimodal_index(ip);
            if (useful == 0)
                ctr_update(bimodal_table[index], taken, 0, TAGE_BASE_COUNTER_MAX);  // update ctr for alternate predictor if useful for predictor is 0
        }

        // update u
        if (pred != alt_pred)
        {
            if (pred == taken)
            {
                if (entry->useful < TAGE_USEFUL_MAX)
                    entry->useful++;  // if prediction from preditor component was correct
            }
            else
            {
                if(use_alt_on_na < 8)
                {
                    if (entry->useful > 0)
                        entry->useful--;  // if prediction from altpred component was correct
                } 
            }
        }

        ctr_update(entry->ctr, taken, 0, TAGE_COUNTER_MAX);  // update ctr for predictor component
    }
    else
    {
        Index index = get_bimodal_index(ip);
        ctr_update(bimodal_table[index], taken, 0, TAGE_BASE_COUNTER_MAX);  // update ctr for predictor if predictor is bimodal
    }

    // allocate tagged entries on misprediction
    if (tage_pred != taken)
    {
        long rand = LAST_N_BITS(random(), TAGE_NUM_COMPONENTS - pred_comp - 1);
        int start_component = pred_comp + 1;

        //compute the start-component for search
        if (ITH_BIT(rand, 0) == 1)  // 0.5 probability
        {
            start_component++;
            if (ITH_BIT(rand, 1) == 1)  // 0.25 probability
                start_component++;
        }

        //Allocate atleast one entry if no free entry
        int isFree = 0;
        for (int i = pred_comp + 1; i <= TAGE_NUM_COMPONENTS; i++)
        {
            struct tage_predictor_table_entry *entry_new = &predictor_table[i - 1][get_predictor_index(ip, i)];
            if (entry_new->useful == 0)
                isFree = 1;
        }
        if (!isFree && start_component <= TAGE_NUM_COMPONENTS)
            predictor_table[start_component - 1][get_predictor_index(ip, start_component)].useful = 0;
        
        
        // search for entry to steal from the start-component till end
        for (int i = start_component; i <= TAGE_NUM_COMPONENTS; i++)
        {
            struct tage_predictor_table_entry *entry_new = &predictor_table[i - 1][get_predictor_index(ip, i)];
            if (entry_new->useful == 0)
            {
                entry_new->tag = get_tag(ip, i);
                entry_new->ctr = TAGE_COUNTER_WEAKLY_TAKEN;
                break;
            }
        }
    }

    // update global history
    global_history.push(taken);

    // update path history
    path_history.push(ITH_BIT(ip, 0));
    
    // graceful resetting of useful counter
    num_branches++;
    if (num_branches % TAGE_RESET_USEFUL_INTERVAL == 0)
    {
        num_branches = 0;
        for (int i = 0; i < TAGE_NUM_COMPONENTS; i++)
        {
            for (int j = 0; j < (1 << TAGE_INDEX_BITS[i]); j++)
                predictor_table[i][j].useful >>= 1;
        }
    }
}

Index Tage::get_bimodal_index(uint64_t ip)
{
    /*
    Return index of the PC in the bimodal table using the last K bits
    */
    return LAST_N_BITS(ip, TAGE_BIMODAL_TABLE_INDEX_BITS);
}

Path Tage::get_path_history_hash(int component)
{
    /*
    Use a hash-function to compress the path history
    */
    Path A = 0;
    
    int size = component_history_lengths[component - 1] > 16 ? 16 : component_history_lengths[component-1]; // Size of hash output
    A = path_history.to_ulong();

    A = LAST_N_BITS(A, size);
    Path A1;
    Path A2;
    A1 = LAST_N_BITS(A, TAGE_INDEX_BITS[component - 1]); // Get last M bits of A
    A2 = LAST_N_BITS(A >> TAGE_INDEX_BITS[component - 1], TAGE_INDEX_BITS[component - 1]) ; // Get second last M bits of A

    // Use the hashing from the CBP-4 L-Tage submission
    A2 = LAST_N_BITS(A2 << component, TAGE_INDEX_BITS[component - 1]) + (A2 >> abs(TAGE_INDEX_BITS[component - 1] - component));
    A = A1 ^ A2;
    A = LAST_N_BITS(A << component, TAGE_INDEX_BITS[component - 1]) + (A >> abs(TAGE_INDEX_BITS[component - 1] - component));
    
    return A;
}

History Tage::get_compressed_global_history(int inSize, int outSize)
{
    /*
    Compress global history of last 'inSize' branches into 'outSize' by wrapping the history
    */
    return global_history.get_compressed(inSize, outSize);
}

Index Tage::get_predictor_index(uint64_t ip, int component)
{
    /*
    Get index of PC in a particular predictor component
    */
    Path path_history_hash = get_path_history_hash(component); // Hash of path history

    // Hash of global history
    History global_history_hash = get_compressed_global_history(component_history_lengths[component - 1], TAGE_INDEX_BITS[component - 1]);

    return LAST_N_BITS(ip ^ (ip >> (abs(TAGE_INDEX_BITS[component - 1] - component) + 1)) ^ global_history_hash ^ path_history_hash, TAGE_INDEX_BITS[component-1]);
}

Tag Tage::get_tag(uint64_t ip, int component)
{
    /*
    Get tag of a PC for a particular predictor component
    */
    History global_history_hash = get_compressed_global_history(component_history_lengths[component - 1], TAGE_TAG_BITS[component - 1]);
    
    return LAST_N_BITS(ip ^ global_history_hash, TAGE_TAG_BITS[component - 1]);
}

int Tage::get_match_below_n(uint64_t ip, int component)
{
    /*
    Get component number of first predictor which has an entry for the IP below a specfic component number
    */
    for (int i = component - 1; i >= 1; i--)
    {
        Index index = get_predictor_index(ip, i);
        Tag tag = get_tag(ip, i);

        if (predictor_table[i - 1][index].tag == tag) // Compare tags at a specific index
        {
            return i;
        }
    }

    return 0; // Default to bimodal in case no match found
}

Tage::Tage() : global_history(TAGE_GLOBAL_HISTORY_BUFFER_LENGTH), path_history(TAGE_PATH_HISTORY_BUFFER_LENGTH)
{
}

Tage::~Tage()
{
}
