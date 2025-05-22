#ifndef PROCSIM_HPP
#define PROCSIM_HPP

#include <cstdint>
#include <cstdio>

#define DEFAULT_K0 1
#define DEFAULT_K1 2
#define DEFAULT_K2 3
#define DEFAULT_R 8
#define DEFAULT_F 4

typedef enum {
    FETCH,
    DISPATCH,
    SCHEDULE,
    EXECUTE,
    WRITEBACK
} inst_stage_t;

typedef struct _proc_inst_t
{
    uint32_t instruction_address;
    int32_t op_code;
    int32_t src_reg[2];
    int32_t dest_reg;
    
    // You may introduce other fields as needed
    uint32_t tag;
    inst_stage_t stage;
} proc_inst_t;

typedef struct {
    uint32_t fetch;
    uint32_t disp;
    uint32_t sched;
    uint32_t exec;
    uint32_t state;
} InstStatus;

typedef struct _proc_stats_t
{
    float avg_inst_retired;
    float avg_inst_fired;
    float avg_disp_size;
    unsigned long max_disp_size;
    unsigned long retired_instruction;
    unsigned long cycle_count;
} proc_stats_t;

class ReservationStationEntry {
public:
    // The operation to perform on source operands S1 and S2.
    int32_t op;

    // The reservation stations that will 
    // produce the corresponding source operand; 
    // a value of zero indicates that the source operand 
    // is already available in Vj or Vk, or is unnecessary
    ReservationStationEntry* q_j;
    ReservationStationEntry* q_k;

    // The value of the source operands. 
    // Note that only one of the V fields
    //  or the Q field is valid for each operand. 
    // For loads, the Vk field is used to hold the offset field. 
    int32_t v_j, v_k;

    // Used to hold information for the memory address calculation for a load or store. 
    // Initially, the immediate field of the instruction is stored here;
    // after the address calculation, the effective address is stored here.
    int32_t a;

    // Indicates that this reservation station 
    // and its accompanying functional unit are occupied.
    bool busy;

    // custom fields
    proc_inst_t* inst;
    bool executed;
};

bool read_instruction(proc_inst_t* p_inst);

void setup_proc(uint64_t r, uint64_t k0, uint64_t k1, uint64_t k2, uint64_t f);
void run_proc(proc_stats_t* p_stats);
void complete_proc(proc_stats_t* p_stats);

#endif /* PROCSIM_HPP */
