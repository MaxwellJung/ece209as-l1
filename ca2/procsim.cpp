#include "procsim.hpp"
#include <queue>
#include <vector>
#include <iostream>
#include <fstream>

#define NUM_ARCH_REGISTERS 32
#define MAX_INST_COUNT 100000

std::ofstream debug_file;

class Fetch {
public:
    std::queue<proc_inst_t*> q_;
    unsigned int cycle_count_;
    int inst_count_;
    int fetch_rate_;
    std::vector<proc_inst_t*> inst_to_dispatch_;
    int global_tag_;
    std::vector<int> debug_tags_;
    std::vector<int> output_tags_;

    Fetch(int fetch_rate) 
    : cycle_count_(0), inst_count_(0), fetch_rate_(fetch_rate), global_tag_(0) {
    };

    ~Fetch() {}

    void tick() {
        output_tags_.clear();
        for (int i = 0; i < fetch_rate_; ++i) {
            proc_inst_t* p_inst = new proc_inst_t;
            if (read_instruction(p_inst)) {
                p_inst->tag = global_tag_++;
                q_.push(p_inst);
                output_tags_.push_back(p_inst->tag);
                inst_count_++;
            } else break;
        }

        cycle_count_++;
    }

    void update_output() {
        // reset output
        inst_to_dispatch_.clear();

        // compute output
        for (int i = 0; i < fetch_rate_; ++i) {
            if (q_.empty()) break;
            inst_to_dispatch_.push_back(q_.front());
            q_.pop();
        }
        
        log_tags(inst_to_dispatch_);
    }

    void log_tags(std::vector<proc_inst_t*> &inst_list) {
        for (auto & inst : inst_list) {
            debug_tags_.push_back(inst->tag+1);
        }
    }

    void print_debug() {
        for (auto & t : debug_tags_) {
            debug_file << cycle_count_ << "	FETCHED	" << t << "\n";
        }
        debug_tags_.clear();
    }
};

class Dispatch {
public:
    std::queue<proc_inst_t*> q_;
    unsigned int cycle_count_;
    std::vector<proc_inst_t*> inst_to_schedule_;
    std::vector<int> debug_tags_;
    int total_disp_q_size_;
    int max_disp_q_size_;
    std::vector<int> output_tags_;

    Dispatch(size_t reserv_station_size) : cycle_count_(0), total_disp_q_size_(0), max_disp_q_size_(0) {
    };

    ~Dispatch() {

    }

    void tick(std::vector<proc_inst_t*> &inst_to_dispatch) {
        output_tags_.clear();
        for (int i = 0; i < inst_to_dispatch.size(); ++i) {
            q_.push(inst_to_dispatch[i]);
            output_tags_.push_back(inst_to_dispatch[i]->tag);
        }

        total_disp_q_size_ += q_.size();
        max_disp_q_size_ = (q_.size() > max_disp_q_size_) ? q_.size() : max_disp_q_size_;

        cycle_count_++;
    }

    void update_output(size_t num_free_reserv_station_entries) {
        // reset output
        inst_to_schedule_.clear();

        // compute output
        for (int i = 0; i < num_free_reserv_station_entries; ++i) {
            if (q_.empty()) break;
            inst_to_schedule_.push_back(q_.front());
            q_.pop();
        }

        log_tags(inst_to_schedule_);
    }

    void log_tags(std::vector<proc_inst_t*> &inst_list) {
        for (auto & inst : inst_list) {
            debug_tags_.push_back(inst->tag+1);
        }
    }

    void print_debug() {
        for (auto & t : debug_tags_) {
            debug_file << cycle_count_ << "	DISPATCHED	" << t << "\n";
        }
        debug_tags_.clear();
    }
};

class ReservationStation {
public:
    std::vector<ReservationStationEntry*> table;
    size_t num_entries_;
    int cycle_count_;
    int k0_, k1_, k2_;
    std::vector<ReservationStationEntry*> k0_inst_to_execute_;
    std::vector<ReservationStationEntry*> k1_inst_to_execute_;
    std::vector<ReservationStationEntry*> k2_inst_to_execute_;

    ReservationStation(uint64_t k0, uint64_t k1, uint64_t k2)
     : num_entries_(2*(k0+k1+k2)), cycle_count_(0), k0_(k0), k1_(k1), k2_(k2){
        for (int i = 0; i < 2*(k0+k1+k2); ++i) {
            table.push_back(new ReservationStationEntry());
        }
    }

    ~ReservationStation() {
        for (int i = 0; i < table.size(); ++i) {
            delete table[i];
        }
    }

    ReservationStationEntry* get_entry(int i) {
        return table[i-1];
    }

    ReservationStationEntry* get_first_available_entry() {
        ReservationStationEntry* available_rs_entry = nullptr;
        for (int i = 1; i <= num_entries_; ++i) {
            if (!get_entry(i)->busy) {
                available_rs_entry = get_entry(i);
                break;
            }
        }

        return available_rs_entry;
    }

    size_t count_free_entries() {
        size_t num_free_entries = 0;
        for (int i = 1; i <= num_entries_; ++i) {
            if (!get_entry(i)->busy) {
                num_free_entries++;
            }
        }

        return num_free_entries;
    }

    bool is_full() {
        for (int i = 1; i <= num_entries_; ++i) {
            if (!get_entry(i)->busy) return false;
        }
        return true;
    }

    void insert(proc_inst_t* p_inst, std::vector<ReservationStationEntry*> &register_statuses) {
        int rs = p_inst->src_reg[0];
        int rt = p_inst->src_reg[1];
        int rd = p_inst->dest_reg;

        ReservationStationEntry* available_rs_entry = get_first_available_entry();

        // Update entry
        if (available_rs_entry) {
            if (rs >= 0 && register_statuses[rs]) {
                available_rs_entry->q_j = register_statuses[rs];
            } else {
                // r->v_j = Regs[rs];
                available_rs_entry->q_j = nullptr;
            }

            if (rt >= 0 && register_statuses[rt]) {
                available_rs_entry->q_k = register_statuses[rt];
            } else {
                // r->v_k = Regs[rt];
                available_rs_entry->q_k = nullptr;
            }

            available_rs_entry->busy = true;
            if (rd >= 0)
                register_statuses[rd] = available_rs_entry;

            available_rs_entry->op = p_inst->op_code;
            available_rs_entry->inst = p_inst;
            available_rs_entry->executed = false;
        }
    }

    void tick(std::vector<proc_inst_t*> &inst_to_schedule, std::vector<ReservationStationEntry*> &register_statuses) {
        for (int i = 0; i < inst_to_schedule.size(); ++i) {
            insert(inst_to_schedule[i], register_statuses);
        }

        cycle_count_++;
    }

    void update_output(int avail_k0, int avail_k1, int avail_k2) {
        // reset output
        k0_inst_to_execute_.clear();
        k1_inst_to_execute_.clear();
        k2_inst_to_execute_.clear();

        std::vector<ReservationStationEntry*> k0_inst_ready_to_execute;
        std::vector<ReservationStationEntry*> k1_inst_ready_to_execute;
        std::vector<ReservationStationEntry*> k2_inst_ready_to_execute;

        ReservationStationEntry* ready_entry;
        for (int i = 1; i <= num_entries_; ++i) {
            ready_entry = get_entry(i);
            // ignore entry if not valid or already executed
            if (!ready_entry->busy || ready_entry->executed) continue;

            // execute if all dependencies are met
            if (ready_entry->q_j == nullptr && ready_entry->q_k == nullptr) {
                switch (ready_entry->op) {
                case 0:
                    /* route to k0 */
                    k0_inst_ready_to_execute.push_back(ready_entry);
                    break;
                case 1:
                    /* route to k1 */
                    k1_inst_ready_to_execute.push_back(ready_entry);
                    break;
                case 2:
                    /* route to k2 */
                    k2_inst_ready_to_execute.push_back(ready_entry);
                    break;
                default:
                    /* route to k1 */
                    k1_inst_ready_to_execute.push_back(ready_entry);
                    break;
                }
            }
        }

        ReservationStationEntry* lowest_tag_rse;
        for (int i = 0; i < avail_k0; ++i) {
            lowest_tag_rse = pop_lowest_tag_rse(k0_inst_ready_to_execute);
            if (lowest_tag_rse) {
                k0_inst_to_execute_.push_back(lowest_tag_rse);
                lowest_tag_rse->executed = true;
            }
        }
        for (int i = 0; i < avail_k1; ++i) {
            lowest_tag_rse = pop_lowest_tag_rse(k1_inst_ready_to_execute);
            if (lowest_tag_rse) {
                k1_inst_to_execute_.push_back(lowest_tag_rse);
                lowest_tag_rse->executed = true;
            }
        }
        for (int i = 0; i < avail_k2; ++i) {
            lowest_tag_rse = pop_lowest_tag_rse(k2_inst_ready_to_execute);
            if (lowest_tag_rse) {
                k2_inst_to_execute_.push_back(lowest_tag_rse);
                lowest_tag_rse->executed = true;
            }
        }
    }

    ReservationStationEntry* pop_lowest_tag_rse(std::vector<ReservationStationEntry*> &rse_list) {
        if (rse_list.size() <= 0) return nullptr;
        // pick instruction with the lowest tag
        ReservationStationEntry* lowest_tag_rse = rse_list[0];
        int lowest_tag_rse_index = 0;
        for (int i = 0; i < rse_list.size(); ++i) {
            if (rse_list[i]->inst->tag < lowest_tag_rse->inst->tag) {
                lowest_tag_rse = rse_list[i];
                lowest_tag_rse_index = i;
            }
        }

        rse_list.erase(rse_list.begin() + lowest_tag_rse_index);

        return lowest_tag_rse;
    }
};

class Schedule {
public:
    int cycle_count_;
    int inst_count_;
    ReservationStation* reserv_station_;
    std::vector<ReservationStationEntry*> register_statuses_;
    std::vector<int> debug_tags_;
    std::vector<int> output_tags_;

    Schedule(uint64_t k0, uint64_t k1, uint64_t k2)
    : cycle_count_(0), inst_count_(0) {
        reserv_station_ = new ReservationStation(k0, k1, k2);
        for (int i = 0; i < NUM_ARCH_REGISTERS; ++i) {
            register_statuses_.push_back(nullptr);
        }
    }

    ~Schedule() {
        delete reserv_station_;
    }

    void tick(std::vector<proc_inst_t*> &inst_to_schedule) {
        output_tags_.clear();
        reserv_station_->tick(inst_to_schedule, register_statuses_);
        for (auto & inst : inst_to_schedule) {
            output_tags_.push_back(inst->tag);
        }

        cycle_count_++;
    }

    void update_output(int avail_k0, int avail_k1, int avail_k2) {
        reserv_station_->update_output(avail_k0, avail_k1, avail_k2);

        log_tags(reserv_station_->k0_inst_to_execute_);
        log_tags(reserv_station_->k1_inst_to_execute_);
        log_tags(reserv_station_->k2_inst_to_execute_);

        inst_count_ += reserv_station_->k0_inst_to_execute_.size() +
                       reserv_station_->k1_inst_to_execute_.size() +
                       reserv_station_->k2_inst_to_execute_.size();
    }

    void log_tags(std::vector<ReservationStationEntry*> &inst_list) {
        for (auto & rse : inst_list) {
            debug_tags_.push_back(rse->inst->tag+1);
        }
    }

    void print_debug() {
        for (auto & t : debug_tags_) {
            debug_file << cycle_count_ << "	SCHEDULED	" << t << "\n";
        }
        debug_tags_.clear();
    }
};

class FunctionalUnit {
public:
    int cycle_count_;
    int latency_;
    bool output_valid_;
    int exec_start_cycle_;
    int exec_end_cycle_;
    ReservationStationEntry* current_inst_;

    FunctionalUnit(int latency)
    : cycle_count_(0), latency_(latency), output_valid_(false), 
    exec_start_cycle_(0), exec_end_cycle_(0), current_inst_(nullptr) {

    }

    void tick(ReservationStationEntry* inst_to_execute) {
        if (inst_to_execute) {
            current_inst_ = inst_to_execute;
            output_valid_ = false;
            exec_start_cycle_ = cycle_count_;
            exec_end_cycle_ = exec_start_cycle_ + latency_;
        }

        cycle_count_++;

        if (current_inst_ && cycle_count_ >= exec_end_cycle_) {
            output_valid_ = true;
        }
    }

    bool is_available() {
        return current_inst_ == nullptr;
    }

    void reset() {
        current_inst_ = nullptr;
        output_valid_ = false;
    }
};

class FunctionalGroup {
public:
    int cycle_count_;
    std::vector<FunctionalUnit*> func_units_;
    int num_units_;

    FunctionalGroup(int num_units, int latency)
    : cycle_count_(0), num_units_(num_units) {
        for (int i = 0; i < num_units; ++i) {
            func_units_.push_back(new FunctionalUnit(latency));
        }
    }

    ~FunctionalGroup() {
        for (int i = 0; i < num_units_; ++i) {
            delete func_units_[i];
        }
    }

    void tick(std::vector<ReservationStationEntry*> &res_station_entry_to_execute){
        int exec_count = 0;
        for (int i = 0; i < num_units_; ++i) {
            FunctionalUnit* fu = func_units_[i];
            if (fu->is_available() && exec_count < res_station_entry_to_execute.size()) {
                fu->tick(res_station_entry_to_execute[exec_count++]);
            } else {
                fu->tick(nullptr);
            }
        }
        cycle_count_++;
    }

    // Get number of functional units available to do computation
    int count_free_func_units() {
        int free_func_units = 0;
        for (int i = 0; i < num_units_; ++i) {
            if (func_units_[i]->is_available()) free_func_units++;
        }

        return free_func_units;
    }

    std::vector<FunctionalUnit*> get_completed_func_units() {
        std::vector<FunctionalUnit*> completed_fu;
        for (int i = 0; i < num_units_; ++i) {
            if (func_units_[i]->output_valid_) {
                completed_fu.push_back(func_units_[i]);
            }
        }

        return completed_fu;
    }
};

class Execute {
public:
    int cycle_count_;
    FunctionalGroup* func_group_0_;
    FunctionalGroup* func_group_1_;
    FunctionalGroup* func_group_2_;
    int max_writeback_count_;
    std::vector<ReservationStationEntry*> inst_to_writeback_;
    std::vector<int> debug_tags_;
    std::vector<int> output_tags_;

    Execute(uint64_t k0, uint64_t k1, uint64_t k2, int max_writeback_count)
    : cycle_count_(0), max_writeback_count_(max_writeback_count) {
        func_group_0_ = new FunctionalGroup(k0, 1);
        func_group_1_ = new FunctionalGroup(k1, 1);
        func_group_2_ = new FunctionalGroup(k2, 1);
    }

    ~Execute() {
        delete func_group_0_;
        delete func_group_1_;
        delete func_group_2_;
    }

    void tick(
        std::vector<ReservationStationEntry*> &k0_inst_to_execute, 
        std::vector<ReservationStationEntry*> &k1_inst_to_execute, 
        std::vector<ReservationStationEntry*> &k2_inst_to_execute
    ) {
        output_tags_.clear();
        func_group_0_->tick(k0_inst_to_execute);
        func_group_1_->tick(k1_inst_to_execute);
        func_group_2_->tick(k2_inst_to_execute);

        for (auto & rse : k0_inst_to_execute) {
            output_tags_.push_back(rse->inst->tag);
        }
        for (auto & rse : k1_inst_to_execute) {
            output_tags_.push_back(rse->inst->tag);
        }
        for (auto & rse : k2_inst_to_execute) {
            output_tags_.push_back(rse->inst->tag);
        }

        cycle_count_++;
    }

    void update_output(int num_available_result_bus) {
        // Get all FU waiting to writeback
        std::vector<FunctionalUnit*> all_completed_fu;
        std::vector<FunctionalUnit*> f0_completed_fu = func_group_0_->get_completed_func_units();
        all_completed_fu.insert(all_completed_fu.end(), f0_completed_fu.begin(), f0_completed_fu.end());
        std::vector<FunctionalUnit*> f1_completed_fu = func_group_1_->get_completed_func_units();
        all_completed_fu.insert(all_completed_fu.end(), f1_completed_fu.begin(), f1_completed_fu.end());
        std::vector<FunctionalUnit*> f2_completed_fu = func_group_2_->get_completed_func_units();
        all_completed_fu.insert(all_completed_fu.end(), f2_completed_fu.begin(), f2_completed_fu.end());

        log_tags(all_completed_fu);

        // reset output
        inst_to_writeback_.clear();

        // writeback the num_available_result_bus oldest instructions
        for (int i = 0; i < num_available_result_bus; ++i) {
            if (all_completed_fu.size() <= 0) break;
            FunctionalUnit* oldest_fu = pop_oldest_fu(all_completed_fu);
            if (oldest_fu) {
                inst_to_writeback_.push_back(oldest_fu->current_inst_);
                oldest_fu->reset();
            }
        }
    }

    FunctionalUnit* pop_oldest_fu(std::vector<FunctionalUnit*> &fu_list) {
        if (fu_list.size() <= 0)
            return nullptr;
        
        FunctionalUnit* oldest_fu = fu_list[0];
        int oldest_index = 0;
        for (int i = 0; i < fu_list.size(); ++i) {
            if (fu_list[i]->exec_end_cycle_ < oldest_fu->exec_end_cycle_) {
                oldest_fu = fu_list[i];
                oldest_index = i;
            } else if (fu_list[i]->exec_end_cycle_ == oldest_fu->exec_end_cycle_) {
                if (fu_list[i]->current_inst_->inst->tag < oldest_fu->current_inst_->inst->tag) {
                    oldest_fu = fu_list[i];
                    oldest_index = i;
                }
            }
        }
        fu_list.erase(fu_list.begin() + oldest_index);

        return oldest_fu;
    }

    void log_tags(std::vector<FunctionalUnit*> &fu_list) {
        for (auto & fu : fu_list) {
            if (fu->exec_end_cycle_ == cycle_count_)
                debug_tags_.push_back(fu->current_inst_->inst->tag+1);
        }
    }

    void print_debug() {
        for (auto & t : debug_tags_) {
            debug_file << cycle_count_ << "	EXECUTED	" << t << "\n";
        }
        debug_tags_.clear();
    }
};

class CommonDataBus {
public:
    int cycle_count_;
    int inst_count_;
    int num_result_bus_;
    std::vector<ReservationStationEntry*> result_buses_;
    std::vector<ReservationStationEntry*> inst_to_retire_;
    std::vector<int> debug_tags_;
    std::vector<int> output_tags_;

    CommonDataBus(int num_result_bus)
    : cycle_count_(0), inst_count_(0), num_result_bus_(num_result_bus) {
        for (int i = 0; i < num_result_bus; ++i) {
            result_buses_.push_back(nullptr);
        }
    }

    ~CommonDataBus() {
    }

    void tick(std::vector<ReservationStationEntry*> &inst_to_writeback) {
        output_tags_.clear();
        for (int i = 0; i < inst_to_writeback.size(); ++i) {
            push_to_bus(inst_to_writeback[i]);
            output_tags_.push_back(inst_to_writeback[i]->inst->tag);
        }

        cycle_count_++;
    }

    void update_output(ReservationStation* reserv_station, std::vector<ReservationStationEntry*> &register_statuses) {
        // reset output
        inst_to_retire_.clear();

        // compute output
        for (int i = 0; i < result_buses_.size(); ++i) {
            if (result_buses_[i]) {
                inst_to_retire_.push_back(result_buses_[i]);
                result_buses_[i] = nullptr;
            }
        }

        log_tags(inst_to_retire_);
        inst_count_ += inst_to_retire_.size();

        for (int i = 0; i < inst_to_retire_.size(); ++i) {
            ReservationStationEntry* r = inst_to_retire_[i];
            if (!r) continue;
            for (int x = 1; x <= reserv_station->num_entries_; ++x) {
                ReservationStationEntry* r_x = reserv_station->get_entry(x);
                if (r_x->q_j == r) {
                    r_x->q_j = nullptr;
                }
                if (r_x->q_k == r) {
                    r_x->q_k = nullptr;
                }
            }

            for (int x = 0; x < NUM_ARCH_REGISTERS; ++x) {
                if (register_statuses[x] == r) {
                    register_statuses[x] = nullptr;
                }
            }

            r->busy = false;
            delete r->inst;
        }
    }

    void push_to_bus(ReservationStationEntry* res_stat_entry) {
        for (int i = 0; i < num_result_bus_; ++i) {
            if (result_buses_[i] == nullptr) {
                result_buses_[i] = res_stat_entry;
                break;
            }
        }
    }

    int count_available_result_buses() {
        // int available_result_bus = 0;
        // for (int i = 0; i < num_result_bus_; ++i) {
        //     if (result_buses_[i] == nullptr) {
        //         available_result_bus++;
        //     }
        // }

        // return available_result_bus;

        return num_result_bus_;
    }

    void log_tags(std::vector<ReservationStationEntry*> &rse_list) {
        for (auto & rse : rse_list) {
            debug_tags_.push_back(rse->inst->tag+1);
        }
    }

    void print_debug() {
        for (auto & t : debug_tags_) {
            debug_file << cycle_count_ << "	STATE UPDATE	" << t << "\n";
        }
        debug_tags_.clear();
    }
};

class Tomasulo {
public:
    int cycle_count_;
    // Fetch stage
    Fetch* fetch_;
    // Dispatch stage
    Dispatch* dispatch_;
    // Schedule stage
    Schedule* schedule_;
    // Execute stage
    Execute* execute_;
    // State update/Writeback stage
    CommonDataBus* common_data_bus_;

    InstStatus inst_status[MAX_INST_COUNT];

    Tomasulo(uint64_t r, uint64_t k0, uint64_t k1, uint64_t k2, uint64_t f)
     : cycle_count_(0) {
        common_data_bus_ =new CommonDataBus(r);
        execute_ =  new Execute(k0, k1, k2, common_data_bus_->num_result_bus_);
        schedule_ = new Schedule(k0, k1, k2);
        dispatch_ = new Dispatch(schedule_->reserv_station_->num_entries_);
        fetch_ = new Fetch(f);
     };

    ~Tomasulo() {
        delete common_data_bus_;
        delete execute_;
        delete schedule_;
        delete dispatch_;
        delete fetch_;
    }

    // set starting input
    void reset() {
        update_output();
    }

    void tick() {
        // clock edge for latching
        fetch_->tick();
        dispatch_->tick(fetch_->inst_to_dispatch_);
        schedule_->tick(dispatch_->inst_to_schedule_);
        execute_->tick(
            schedule_->reserv_station_->k0_inst_to_execute_,
            schedule_->reserv_station_->k1_inst_to_execute_,
            schedule_->reserv_station_->k2_inst_to_execute_
        );
        common_data_bus_->tick(execute_->inst_to_writeback_);

        cycle_count_++;
    }

    void update_output() {
        // combinational logic
        fetch_->update_output();
        dispatch_->update_output(schedule_->reserv_station_->count_free_entries());
        execute_->update_output(common_data_bus_->count_available_result_buses());

        common_data_bus_->update_output(schedule_->reserv_station_, schedule_->register_statuses_);
        schedule_->update_output(
            execute_->func_group_0_->count_free_func_units(), 
            execute_->func_group_1_->count_free_func_units(), 
            execute_->func_group_2_->count_free_func_units()
        );
    }

    void update_stats(proc_stats_t* p_stats) {
        p_stats->retired_instruction = common_data_bus_->inst_count_;
        p_stats->avg_disp_size = dispatch_->total_disp_q_size_/(float)dispatch_->cycle_count_;
        p_stats->max_disp_size = dispatch_->max_disp_q_size_;
        p_stats->avg_inst_fired = schedule_->inst_count_/(float)schedule_->cycle_count_;
        p_stats->avg_inst_retired = common_data_bus_->inst_count_/(float)common_data_bus_->cycle_count_;
        p_stats->cycle_count = cycle_count_;
    }

    void update_debug_log() {
        common_data_bus_->print_debug();
        execute_->print_debug();
        schedule_->print_debug();
        dispatch_->print_debug();
        fetch_->print_debug();
    }

    void update_inst_status() {
        for (auto i : fetch_->output_tags_) {
            inst_status[i].fetch = cycle_count_;
        }
        for (auto i : dispatch_->output_tags_) {
            inst_status[i].disp = cycle_count_;
        }
        for (auto i : schedule_->output_tags_) {
            inst_status[i].sched = cycle_count_;
        }
        for (auto i : execute_->output_tags_) {
            inst_status[i].exec = cycle_count_;
        }
        for (auto i : common_data_bus_->output_tags_) {
            inst_status[i].state = cycle_count_;
        }
    }

    void print_inst_status() {
        printf("INST	FETCH	DISP	SCHED	EXEC	STATE\n");

        for (int i = 0; i < MAX_INST_COUNT; ++i) {
            printf("%d	%d	%d	%d	%d	%d\n", 
                i+1, 
                inst_status[i].fetch,
                inst_status[i].disp,
                inst_status[i].sched,
                inst_status[i].exec,
                inst_status[i].state
            );
        }

        printf("\n");
    }

    bool is_finished() {
        return (fetch_->inst_to_dispatch_.size() <= 0) && 
               (dispatch_->inst_to_schedule_.size() <= 0) &&
               (schedule_->reserv_station_->k0_inst_to_execute_.size() <= 0) &&
               (schedule_->reserv_station_->k1_inst_to_execute_.size() <= 0) &&
               (schedule_->reserv_station_->k2_inst_to_execute_.size() <= 0) &&
               (execute_->inst_to_writeback_.size() <= 0);
    }
};

Tomasulo* tomasulo;

/**
 * Subroutine for initializing the processor. You many add and initialize any global or heap
 * variables as needed.
 * XXX: You're responsible for completing this routine
 *
 * @r ROB size
 * @k0 Number of k0 FUs
 * @k1 Number of k1 FUs
 * @k2 Number of k2 FUs
 * @f Number of instructions to fetch
 */
void setup_proc(uint64_t r, uint64_t k0, uint64_t k1, uint64_t k2, uint64_t f) 
{
    tomasulo = new Tomasulo(r, k0, k1, k2, f);
    debug_file.open("debug.log");
    debug_file << "CYCLE	OPERATION	INSTRUCTION\n";
    tomasulo->reset();
}

/**
 * Subroutine that simulates the processor.
 *   The processor should fetch instructions as appropriate, until all instructions have executed
 * XXX: You're responsible for completing this routine
 *
 * @p_stats Pointer to the statistics structure
 */
void run_proc(proc_stats_t* p_stats)
{
    do {
        tomasulo->tick();
        tomasulo->update_output();
        tomasulo->update_stats(p_stats);
        tomasulo->update_debug_log();
        tomasulo->update_inst_status();
    } while (!tomasulo->is_finished());
}

/**
 * Subroutine for cleaning up any outstanding instructions and calculating overall statistics
 * such as average IPC, average fire rate etc.
 * XXX: You're responsible for completing this routine
 *
 * @p_stats Pointer to the statistics structure
 */
void complete_proc(proc_stats_t *p_stats) 
{
    tomasulo->print_inst_status();
    delete tomasulo;
    debug_file.close();
}
