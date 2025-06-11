#include <stdio.h>
#include "utlist.h"
#include "utils.h"

#include "memory_controller.h"
#include "params.h"

/* A basic FRFCFS policy augmented with a not-so-clever close-page policy.
   If the memory bank had a row hit, close the row by precharging. */


extern long long int CYCLE_VAL;

/* A data structure to see if a bank is a candidate for precharge. */
long long int row_hit_count[MAX_NUM_CHANNELS][MAX_NUM_RANKS][MAX_NUM_BANKS];

void init_scheduler_vars()
{
	for (int channel=0; channel<MAX_NUM_CHANNELS; channel++) {
		for (int rank=0; rank<MAX_NUM_RANKS; rank++) {
			for (int bank=0; bank<MAX_NUM_BANKS; bank++) {
				row_hit_count[channel][rank][bank] = 0;
			}
		}
	}

	return;
}

// write queue high water mark; begin draining writes if write queue exceeds this value
#define HI_WM 40

// end write queue drain once write queue has this many writes in it
#define LO_WM 20

// 1 means we are in write-drain mode for that channel
int drain_writes[MAX_NUM_CHANNELS];

/* Each cycle it is possible to issue a valid command from the read or write queues
   OR
   a valid precharge command to any bank (issue_precharge_command())
   OR
   a valid precharge_all bank command to a rank (issue_all_bank_precharge_command())
   OR
   a power_down command (issue_powerdown_command()), programmed either for fast or slow exit mode
   OR
   a refresh command (issue_refresh_command())
   OR
   a power_up command (issue_powerup_command())
   OR
   an activate to a specific row (issue_activate_command()).

   If a COL-RD or COL-WR is picked for issue, the scheduler also has the
   option to issue an auto-precharge in this cycle (issue_autoprecharge()).

   Before issuing a command it is important to check if it is issuable. For the RD/WR queue resident commands, checking the "command_issuable" flag is necessary. To check if the other commands (mentioned above) can be issued, it is important to check one of the following functions: is_precharge_allowed, is_all_bank_precharge_allowed, is_powerdown_fast_allowed, is_powerdown_slow_allowed, is_powerup_allowed, is_refresh_allowed, is_autoprecharge_allowed, is_activate_allowed.
   */


void schedule(int channel)
{
	// if in write drain mode, keep draining writes until the
	// write queue occupancy drops to LO_WM
	if (drain_writes[channel] && (write_queue_length[channel] > LO_WM)) {
	  drain_writes[channel] = 1; // Keep draining.
	}
	else {
	  drain_writes[channel] = 0; // No need to drain.
	}

	// initiate write drain if either the write queue occupancy
	// has reached the HI_WM , OR, if there are no pending read
	// requests
	if(write_queue_length[channel] > HI_WM)
	{
		drain_writes[channel] = 1;
	}
	else {
	  if (!read_queue_length[channel])
	    drain_writes[channel] = 1;
	}

	request_t * first_request_ptr = NULL;
	request_t * first_row_hit_request_ptr = NULL;
	request_t * first_row_miss_request_ptr = NULL;
	request_t * chosen_request_ptr = NULL;

	// If in write drain mode, look through all the write queue
	// elements (already arranged in the order of arrival), and
	// issue the command for the first request to open row
	// if there's no request with open row, pick first request
	if(drain_writes[channel]) {
		request_t * wr_ptr = NULL;
		LL_FOREACH(write_queue_head[channel], wr_ptr) {
			if (wr_ptr->command_issuable) {
				// FCFS
				if (!first_request_ptr) first_request_ptr = wr_ptr;
				// row is open (ready)
				if (wr_ptr->next_command == COL_WRITE_CMD) {
					if (!first_row_hit_request_ptr) first_row_hit_request_ptr = wr_ptr;
					break;
				} else {
					if (!first_row_miss_request_ptr) first_row_miss_request_ptr = wr_ptr;
				}
			}
		}
	}
	// Draining Reads
	// same policy as for writes
	else {
		request_t * rd_ptr = NULL;
		LL_FOREACH(read_queue_head[channel], rd_ptr) {
			if (rd_ptr->command_issuable) {
				// FCFS
				if (!first_request_ptr) first_request_ptr = rd_ptr;
				// row is open (ready)
				if (rd_ptr->next_command == COL_READ_CMD) {
					if (!first_row_hit_request_ptr) first_row_hit_request_ptr = rd_ptr;
					break;
				} else {
					if (!first_row_miss_request_ptr) first_row_miss_request_ptr = rd_ptr;
				}
			}
		}
	}

	// Prioritize open row requests
	if (first_row_hit_request_ptr) {
		chosen_request_ptr = first_row_hit_request_ptr;
		row_hit_count[channel][chosen_request_ptr->dram_addr.rank][chosen_request_ptr->dram_addr.bank]++;
	} else if (first_request_ptr) {
		chosen_request_ptr = first_request_ptr;
		row_hit_count[channel][chosen_request_ptr->dram_addr.rank][chosen_request_ptr->dram_addr.bank] = 0;
	}

	if (chosen_request_ptr)
		issue_request_command(chosen_request_ptr);

	// Precharge banks on idle cycle
	if (!command_issued_current_cycle[channel]) {
		for (int rank=0; rank<NUM_RANKS; rank++) {
			for (int bank=0; bank<NUM_BANKS; bank++) {  /* For all banks on the channel.. */
				if (row_hit_count[channel][rank][bank] > 0) {  /* See if the bank has any row hits */
					if (is_precharge_allowed(channel, rank, bank)) {  /* See if precharge is doable. */
						if (issue_precharge_command(channel, rank, bank)) {
							row_hit_count[channel][rank][bank] = 0;
						}
					}
				}
			}
		}
	}
}

void scheduler_stats()
{
  /* Nothing to print for now. */
}
