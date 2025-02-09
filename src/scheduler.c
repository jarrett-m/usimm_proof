#include "utils.h"
#include "utlist.h"
#include <stdio.h>

#include "memory_controller.h"

extern long long int CYCLE_VAL;

void init_scheduler_vars() {
  // initialize all scheduler variables here

  return;
}

// write queue high water mark; begin draining writes if write queue exceeds
// this value
#define HI_WM 80
// 40

// end write queue drain once write queue has this many writes in it
#define LO_WM 10
// 2

//#define LOW_NUM 20

// 1 means we are in write-drain mode for that channel
int drain_writes[MAX_NUM_CHANNELS];

/* Each cycle it is possible to issue a valid command from the read or write
   queues OR a valid precharge command to any bank (issue_precharge_command())
   OR
   a valid precharge_all bank command to a rank
   (issue_all_bank_precharge_command()) OR a power_down command
   (issue_powerdown_command()), programmed either for fast or slow exit mode OR
   a refresh command (issue_refresh_command())
   OR
   a power_up command (issue_powerup_command())
   OR
   an activate to a specific row (issue_activate_command()).

   If a COL-RD or COL-WR is picked for issue, the scheduler also has the
   option to issue an auto-precharge in this cycle (issue_autoprecharge()).

   Before issuing a command it is important to check if it is issuable. For the
   RD/WR queue resident commands, checking the "command_issuable" flag is
   necessary. To check if the other commands (mentioned above) can be issued, it
   is important to check one of the following functions: is_precharge_allowed,
   is_all_bank_precharge_allowed, is_powerdown_fast_allowed,
   is_powerdown_slow_allowed, is_powerup_allowed, is_refresh_allowed,
   is_autoprecharge_allowed, is_activate_allowed.
   */

void schedule(int channel) {
  request_t *rd_ptr = NULL;
  request_t *wr_ptr = NULL;
  request_t *ptr = NULL;

  // if in write drain mode, keep draining writes until the
  // write queue occupancy drops to LO_WM

  if (drain_writes[channel] && (write_queue_length[channel] > LO_WM)) {
    drain_writes[channel] = 1; // Keep draining.
  } else {
    drain_writes[channel] = 0; // No need to drain.
                               // num_ACT_CMD[channel]  = 0;
  }

  // initiate write drain if either the write queue occupancy
  // has reached the HI_WM , OR, if there are no pending read
  // requests

  if (write_queue_length[channel] > HI_WM) {
    drain_writes[channel] = 1;
  } else {
    if (!read_queue_length[channel]) {
      // if (CYCLE_VAL%2 == 0)
      drain_writes[channel] = 1;
    }
  }

  /*if (CYCLE_VAL > 2540000000)
  {
          printf ("drain_writes[0] = %d \n",  drain_writes[channel]);
  }*/
  // If in write drain mode, look through all the write queue
  // elements (already arranged in the order of arrival), and
  // issue the command for the first request that is ready
  if (drain_writes[channel]) {
    /*if (CYCLE_VAL > 2540000000)
    {
            printf ("___________________ write queue in scheduler
    ____________\n"); LL_FOREACH(write_queue_head[channel], wr_ptr)
            {
                    printf (" addr = %llx issuable = %d served = %d \n",
    wr_ptr->physical_address, wr_ptr->command_issuable, wr_ptr->request_served);
            }

    }*/

    LL_FOREACH(write_queue_head[channel], wr_ptr) {
      if (wr_ptr->command_issuable) {
        /*if (CYCLE_VAL > 2540000000)
        {
                printf ("read issuabale with address = %llx \n",
        wr_ptr->physical_address);
        }*/
        if (wr_ptr->next_command == PRE_CMD && FR_FCFS) {
          int keep_open = 0;
          LL_FOREACH(write_queue_head[channel], ptr) {
            if (ptr->dram_addr.channel == wr_ptr->dram_addr.channel &&
                ptr->dram_addr.rank == wr_ptr->dram_addr.rank &&
                ptr->dram_addr.bank == wr_ptr->dram_addr.bank &&
                ptr->dram_addr.row ==
                    dram_state[wr_ptr->dram_addr.channel]
                              [wr_ptr->dram_addr.rank][wr_ptr->dram_addr.bank]
                                  .active_row) {
              keep_open = 1;
              break;
            }
          }
          if (keep_open)
            continue;
          else {

            /* 	LL_FOREACH(read_queue_head[channel], ptr)
                    {
                            if (ptr->dram_addr.channel == wr_ptr->dram_addr.
               channel && ptr->dram_addr.rank == wr_ptr->dram_addr.rank &&
                                    ptr->dram_addr.bank ==
               wr_ptr->dram_addr.bank && ptr->dram_addr.row ==
               dram_state[wr_ptr->dram_addr.
               channel][wr_ptr->dram_addr.rank][wr_ptr->dram_addr.bank].active_row)
                                    {
                                            keep_open = 1;
                                            break;
                                    }
                    }
                    if (keep_open) continue;*/
            issue_request_command(wr_ptr);
            break;
          }

        } else {
          /* if (wr_ptr->next_command == ACT_CMD && num_ACT_CMD[channel] >=
             LOW_NUM)
                  {
                          printf ("%llx not issued because num_ACT_CMD = %lld
             \n", wr_ptr->physical_address, num_ACT_CMD[channel] );
                          //getchar();
                          continue;
                  }*/
          issue_request_command(wr_ptr);
          break;
        }
      }
    }
    return;
  }

  // Draining Reads
  // look through the queue and find the first request whose
  // command can be issued in this cycle and issue it
  // Simple FCFS
  if (!drain_writes[channel]) {
    /*if (CYCLE_VAL > 2540000000)
    {
            printf ("___________________ write queue in scheduler
    ____________\n"); LL_FOREACH(read_queue_head[channel], rd_ptr)
            {
                    printf (" addr = %llx issuable = %d served = %d \n",
    rd_ptr->physical_address, rd_ptr->command_issuable, rd_ptr->request_served);
            }

    }*/
    LL_FOREACH(read_queue_head[channel], rd_ptr) {
      if (rd_ptr->command_issuable) {
        /*	if (CYCLE_VAL > 2540000000)
                {
                        printf ("read issuabale with address = %llx \n",
           rd_ptr->physical_address);
                }*/
        if (rd_ptr->next_command == PRE_CMD && FR_FCFS) {
          int keep_open = 0;
          LL_FOREACH(read_queue_head[channel], ptr) {
            if (ptr->dram_addr.channel == rd_ptr->dram_addr.channel &&
                ptr->dram_addr.rank == rd_ptr->dram_addr.rank &&
                ptr->dram_addr.bank == rd_ptr->dram_addr.bank &&
                ptr->dram_addr.row ==
                    dram_state[rd_ptr->dram_addr.channel]
                              [rd_ptr->dram_addr.rank][rd_ptr->dram_addr.bank]
                                  .active_row) {
              keep_open = 1;
              break;
            }
          }
          if (keep_open)
            continue;
          else {
            issue_request_command(rd_ptr);
            break;
          }

        } else {
          issue_request_command(rd_ptr);
          break;
        }
      }
    }
    return;
  }
}

void scheduler_stats() { /* Nothing to print for now. */ }
