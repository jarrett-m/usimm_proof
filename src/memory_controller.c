#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "utlist.h"

#include "utils.h"

#include "VAT.h"
#include "memory_controller.h"
#include "meta_cache.h"
#include "params.h"
#include "processor.h"
#include "scheduler.h"

// ROB Structure, used to release stall on instructions
// when the read request completes
extern struct robstructure *ROB;

// Current Processor Cycle
extern long long int CYCLE_VAL;

#define max(a, b) (((a) > (b)) ? (a) : (b))

#define BIG_ACTIVATION_WINDOW 1000000

// moving window that captures each activate issued in the past
int activation_record[MAX_NUM_CHANNELS][MAX_NUM_RANKS][BIG_ACTIVATION_WINDOW];

// record an activate in the activation record
void record_activate(int channel, int rank, long long int cycle) {
  assert(activation_record[channel][rank][(cycle % BIG_ACTIVATION_WINDOW)] ==
         0); // can't have two commands issued the same cycle - hence no two
             // activations in the same cycle
  activation_record[channel][rank][(cycle % BIG_ACTIVATION_WINDOW)] = 1;

  return;
}

// Have there been 3 or less activates in the last T_FAW period
int is_T_FAW_met(int channel, int rank, int cycle) {
  int start = cycle;

  int number_of_activates = 0;

  if (start >= T_FAW) {
    for (int i = 1; i <= T_FAW; i++) {
      if (activation_record[channel][rank]
                           [(start - i) % BIG_ACTIVATION_WINDOW] == 1)
        number_of_activates++;
    }
  } else {
    for (int i = 1; i <= start; i++) {
      if (activation_record[channel][rank][start - i] % BIG_ACTIVATION_WINDOW ==
          1)
        number_of_activates++;
    }
  }
  if (number_of_activates < 4)
    return 1;
  else
    return 0;
}

// shift the moving window, clear out the past
void flush_activate_record(int channel, int rank, long long int cycle) {
  if (cycle >= T_FAW + PROCESSOR_CLK_MULTIPLIER) {
    for (int i = 1; i <= PROCESSOR_CLK_MULTIPLIER; i++)
      activation_record[channel][rank]
                       [(cycle - T_FAW - i) % BIG_ACTIVATION_WINDOW] =
                           0; // make sure cycle >tFAW
  }
}

// initialize dram variables and statistics
void init_memory_controller_vars() {
  data_read_match_read = 0;
  mac_read_match_read = 0;
  data_read_match_write = 0;
  mac_read_match_write = 0;
  data_write_match_write = 0;
  mac_write_match_write = 0;

  for (int i = 0; i < NUMCORES; i++) {
    num_instructions[i] = 0;
  }

  num_read_merge = 0;
  num_write_merge = 0;
  for (int i = 0; i < NUM_CHANNELS; i++) {

    for (int j = 0; j < NUM_RANKS; j++) {
      for (int w = 0; w < BIG_ACTIVATION_WINDOW; w++)
        activation_record[i][j][w] = 0;

      for (int k = 0; k < NUM_BANKS; k++) {
        dram_state[i][j][k].state = IDLE;
        dram_state[i][j][k].active_row = -1;
        dram_state[i][j][k].next_pre = -1;
        dram_state[i][j][k].next_pre = -1;
        dram_state[i][j][k].next_pre = -1;
        dram_state[i][j][k].next_pre = -1;

        cmd_precharge_issuable[i][j][k] = 0;

        stats_num_activate_read[i][j][k] = 0;
        stats_num_activate_write[i][j][k] = 0;
        stats_num_activate_spec[i][j][k] = 0;
        stats_num_precharge[i][j][k] = 0;
        stats_num_read[i][j][k] = 0;
        stats_num_write[i][j][k] = 0;
        cas_issued_current_cycle[i][j][k] = 0;

        update_proof_queue[i][j][k] = NULL;
        last_proof_update[i][j][k] = 0;
      }

      cmd_all_bank_precharge_issuable[i][j] = 0;
      cmd_powerdown_fast_issuable[i][j] = 0;
      cmd_powerdown_slow_issuable[i][j] = 0;
      cmd_powerup_issuable[i][j] = 0;
      cmd_refresh_issuable[i][j] = 0;

      next_refresh_completion_deadline[i][j] = 8 * T_REFI;
      last_refresh_completion_deadline[i][j] = 0;
      forced_refresh_mode_on[i][j] = 0;
      refresh_issue_deadline[i][j] =
          next_refresh_completion_deadline[i][j] - T_RP - 8 * T_RFC;
      num_issued_refreshes[i][j] = 0;

      stats_time_spent_in_active_power_down[i][j] = 0;
      stats_time_spent_in_precharge_power_down_slow[i][j] = 0;
      stats_time_spent_in_precharge_power_down_fast[i][j] = 0;
      last_activate[i][j] = 0;
      // If average_gap_between_activates is 0 then we know that there have been
      // no activates to [i][j]
      average_gap_between_activates[i][j] = 0;

      stats_num_powerdown_slow[i][j] = 0;
      stats_num_powerdown_fast[i][j] = 0;
      stats_num_powerup[i][j] = 0;

      stats_num_activate[i][j] = 0;

      command_issued_current_cycle[i] = 0;
    }

    read_queue_head[i] = NULL;
    write_queue_head[i] = NULL;

    read_queue_length[i] = 0;
    write_queue_length[i] = 0;

    command_issued_current_cycle[i] = 0;

    // Stats
    stats_reads_merged_per_channel[i] = 0;
    stats_writes_merged_per_channel[i] = 0;

    stats_reads_seen[i] = 0;
    stats_writes_seen[i] = 0;
    stats_reads_completed[i] = 0;
    stats_writes_completed[i] = 0;
    stats_average_read_latency[i] = 0;
    stats_average_read_queue_latency[i] = 0;
    stats_average_write_latency[i] = 0;
    stats_average_write_queue_latency[i] = 0;
    stats_page_hits[i] = 0;
    stats_read_row_hit_rate[i] = 0;
  }

  // secured
  stats_macro_reads_seen = 0;
  macro_read_queue_head = NULL;
  macro_read_queue_length = 0;
  macro_write_queue_head = NULL;
  macro_write_queue_length = 0;
  mt_tab = NULL;
  num_macro_read_merge_read = 0;
  num_macro_read_merge_write = 0;
  size_of_map = 0;
  meta_cache_hit_read_return = 0;
  meta_cache_miss_read_return = 0;

  stats_macro_reads_completed = 0;
  stats_macro_writes_completed = 0;

  stats_average_macro_read_latency = 0;
  stats_average_macro_write_latency = 0;

  macro_write_queue_length = 0;
  num_macro_write_merge = 0;

  // CACHE
  int num_set;
  if (CACH_PAD == 1) {
    // num_set = NUMCORES*CNT_SET;
    // num_set = NUMCORES*CNT_SET/2; //16kB Cache
    num_set = 14 * NUMCORES * CNT_SET /
              8; // 56kB Cache per core, Level3 fits in 8kB/core scratchpad
  } else {
    num_set = 2 * NUMCORES * CNT_SET;
  }

  // cache for MAC & counter
  meta_cache[0] = init_cache(META_CH, num_set, CNT_WAY, CNT_OFFSET);
  // meta_cache[0] =  init_cache(META_CH, 1, 4096, 64);
  // meta_cache[0] =  init_cache(META_CH, 65536, 128, 64);		// Large
  // MetaCache simulation (can hold 8GB MACs)
  for (int ii = 0; ii < NUMCORES; ii++)
    meta_cache[ii] = meta_cache[0];
  int num_ways_sharedVC = VC_WAYS * NUMCORES;
  victim_cache[0] =
      init_cache(VICTIM_CH, VC_SETS, num_ways_sharedVC, VC_OFFSET);
  for (int i = 0; i < NUMCORES; i++)
    victim_cache[i] = victim_cache[0];

  size_of_map = 0;
}

/********************************************************/
/*	Utility Functions				*/
/********************************************************/

unsigned int log_base2(unsigned int new_value) {
  int i;
  for (i = 0; i < 32; i++) {
    new_value >>= 1;
    if (new_value == 0)
      break;
  }
  return i;
}

// Function to decompose the incoming DRAM address into the
// constituent channel, rank, bank, row and column ids.
// Note : To prevent memory leaks, call free() on the pointer returned
// by this function after you have used the return value.
dram_address_t *calc_dram_addr(long long int physical_address) {

  long long int input_a, temp_b, temp_a;

  int channelBitWidth = log_base2(NUM_CHANNELS);
  int rankBitWidth = log_base2(NUM_RANKS);
  int bankBitWidth = log_base2(NUM_BANKS);
  int rowBitWidth = log_base2(NUM_ROWS);
  int colBitWidth = log_base2(NUM_COLUMNS);
  int byteOffsetWidth = log_base2(CACHE_LINE_SIZE);

  dram_address_t *this_a = (dram_address_t *)malloc(sizeof(dram_address_t));

  this_a->actual_address = physical_address;

  input_a = physical_address;

  input_a = input_a >> byteOffsetWidth; // strip out the cache_offset

  if (ADDRESS_MAPPING == 1) {
    temp_b = input_a;
    input_a = input_a >> colBitWidth;
    temp_a = input_a << colBitWidth;
    this_a->column = temp_a ^ temp_b; // strip out the column address

    temp_b = input_a;
    input_a = input_a >> channelBitWidth;
    temp_a = input_a << channelBitWidth;
    this_a->channel = temp_a ^ temp_b; // strip out the channel address

    temp_b = input_a;
    input_a = input_a >> bankBitWidth;
    temp_a = input_a << bankBitWidth;
    this_a->bank = temp_a ^ temp_b; // strip out the bank address

    temp_b = input_a;
    input_a = input_a >> rankBitWidth;
    temp_a = input_a << rankBitWidth;
    this_a->rank = temp_a ^ temp_b; // strip out the rank address

    temp_b = input_a;
    input_a = input_a >> rowBitWidth;
    temp_a = input_a << rowBitWidth;
    this_a->row = temp_a ^ temp_b; // strip out the row number
  } else {
    temp_b = input_a;
    input_a = input_a >> channelBitWidth;
    temp_a = input_a << channelBitWidth;
    this_a->channel = temp_a ^ temp_b; // strip out the channel address

    temp_b = input_a;
    input_a = input_a >> bankBitWidth;
    temp_a = input_a << bankBitWidth;
    this_a->bank = temp_a ^ temp_b; // strip out the bank address

    temp_b = input_a;
    input_a = input_a >> rankBitWidth;
    temp_a = input_a << rankBitWidth;
    this_a->rank = temp_a ^ temp_b; // strip out the rank address

    temp_b = input_a;
    input_a = input_a >> colBitWidth;
    temp_a = input_a << colBitWidth;
    this_a->column = temp_a ^ temp_b; // strip out the column address

    temp_b = input_a;
    input_a = input_a >> rowBitWidth;
    temp_a = input_a << rowBitWidth;
    this_a->row = temp_a ^ temp_b; // strip out the row number
  }
  return (this_a);
}

// Function to create a new request node to be inserted into the read
// or write queue.
void *init_new_node(long long int physical_address, long long int arrival_time,
                    optype_t type, int thread_id, int instruction_id,
                    long long int instruction_pc) {
  request_t *new_node = NULL;

  new_node = (request_t *)malloc(sizeof(request_t));

  if (new_node == NULL) {
    printf("FATAL : Malloc Error\n");

    exit(-1);
  } else {

    new_node->physical_address = physical_address;

    new_node->arrival_time = arrival_time;

    new_node->dispatch_time = -100;

    new_node->completion_time = -100;

    new_node->latency = -100;

    new_node->thread_id = thread_id;

    new_node->next_command = NOP;

    new_node->command_issuable = 0;

    new_node->operation_type = type;

    new_node->request_served = 0;

    new_node->picked = 0;

    new_node->type = DATA;

    new_node->instruction_id = instruction_id;

    new_node->instruction_pc = instruction_pc;

    new_node->next = NULL;

    dram_address_t *this_node_addr = calc_dram_addr(physical_address);

    new_node->dram_addr.actual_address = physical_address;
    new_node->dram_addr.channel = this_node_addr->channel;
    new_node->dram_addr.rank = this_node_addr->rank;
    new_node->dram_addr.bank = this_node_addr->bank;
    new_node->dram_addr.row = this_node_addr->row;
    new_node->dram_addr.column = this_node_addr->column;

    free(this_node_addr);

    new_node->user_ptr = NULL;

    return (new_node);
  }
}

// Function that checks to see if an incoming read can be served by a
// write request pending in the write queue and return
// WQ_LOOKUP_LATENCY if there is a match. Also the function goes over
// the read_queue to see if there is a pending read for the same
// address and avoids duplication. The 2nd read is assumed to be
// serviced when the original request completes.

#define RQ_LOOKUP_LATENCY 1

int read_matches_write_or_read_queue(long long int physical_address,
                                     microoptype_t type, int wr_fwd_rd) {
  // get channel info
  dram_address_t *this_addr = calc_dram_addr(physical_address);
  int channel = this_addr->channel;
  free(this_addr);

  request_t *wr_ptr = NULL;
  request_t *rd_ptr = NULL;

  LL_FOREACH(write_queue_head[channel], wr_ptr) {
    if (wr_ptr->dram_addr.actual_address == physical_address &&
        wr_ptr->type == type) {
      num_read_merge++;
      stats_reads_merged_per_channel[channel]++;
      if (type == DATA)
        data_read_match_write++;
      if (type == PROOF)
        mac_read_match_write++;

      return WQ_LOOKUP_LATENCY;
    }
  }

  LL_FOREACH(read_queue_head[channel], rd_ptr) {
    if (rd_ptr->dram_addr.actual_address == physical_address &&
        rd_ptr->type == type) {
      num_read_merge++;
      stats_reads_merged_per_channel[channel]++;

      // if ((type == DATA) && (wr_fwd_rd == 1))
      //   rd_ptr->to_be_dirty = 1;

      if (type == DATA)
        data_read_match_read++;
      if (type == PROOF)
        mac_read_match_read++;
      return RQ_LOOKUP_LATENCY;
    }
  }
  return 0;
}

int write_exists_in_write_queue(long long int physical_address,
                                microoptype_t type) {
  // get channel info
  dram_address_t *this_addr = calc_dram_addr(physical_address);
  int channel = this_addr->channel;
  free(this_addr);

  request_t *wr_ptr = NULL;

  LL_FOREACH(write_queue_head[channel], wr_ptr) {
    if (wr_ptr->dram_addr.actual_address == physical_address &&
        wr_ptr->type == type) {
      num_write_merge++;
      stats_writes_merged_per_channel[channel]++;

      if (type == DATA)
        data_write_match_write++;
      if (type == PROOF)
        mac_write_match_write++;

      return 1;
    }
  }
  return 0;
}

//
request_t *insert_mac_read(long long int physical_address,
                           long long int arrival_time, int thread_id,
                           int instruction_id, long long int instruction_pc,
                           int picked, int dirty) {
  optype_t this_op = READ;

  // get channel info
  dram_address_t *this_addr = calc_dram_addr(physical_address);
  int channel = this_addr->channel;
  free(this_addr);

  stats_reads_seen[channel]++;

  request_t *new_node =
      init_new_node(physical_address, arrival_time, this_op, thread_id,
                    instruction_id, instruction_pc);
  new_node->type = PROOF;
  new_node->picked = picked;
  new_node->dirty = dirty;
  LL_APPEND(read_queue_head[channel], new_node);

  read_queue_length[channel]++;

  // UT_MEM_DEBUG("\nCyc: %lld New READ:%lld Core:%d Chan:%d Rank:%d Bank:%d
  // Row:%lld RD_Q_Length:%lld\n", CYCLE_VAL, new_node->id, new_node->thread_id,
  // new_node->dram_addr.channel,  new_node->dram_addr.rank,
  // new_node->dram_addr.bank,  new_node->dram_addr.row,
  // read_queue_length[channel]);

  return new_node;
}

// Insert a new read to the read queue
request_t *insert_read(long long int physical_address,
                       long long int arrival_time, int thread_id,
                       int instruction_id, long long int instruction_pc) {

  optype_t this_op = READ;

  // get channel info
  dram_address_t *this_addr = calc_dram_addr(physical_address);
  int channel = this_addr->channel;
  free(this_addr);

  stats_reads_seen[channel]++;

  request_t *new_node =
      init_new_node(physical_address, arrival_time, this_op, thread_id,
                    instruction_id, instruction_pc);
  new_node->type = DATA;
  new_node->picked = 0;
  new_node->dirty = 0;

  LL_APPEND(read_queue_head[channel], new_node);

  read_queue_length[channel]++;

  // UT_MEM_DEBUG("\nCyc: %lld New READ:%lld Core:%d Chan:%d Rank:%d Bank:%d
  // Row:%lld RD_Q_Length:%lld\n", CYCLE_VAL, new_node->id, new_node->thread_id,
  // new_node->dram_addr.channel,  new_node->dram_addr.rank,
  // new_node->dram_addr.bank,  new_node->dram_addr.row,
  // read_queue_length[channel]);

  return new_node;
}

request_t *insert_macro_write(long long int physical_address,
                              long long int arrival_time, int thread_id,
                              int instruction_id) {
  // stats later lopl
  request_t *new_node = init_new_node(physical_address, arrival_time, WRITE,
                                      thread_id, instruction_id, 0);

  macro_write_queue_length++;
  LL_APPEND(macro_write_queue_head, new_node);
  return new_node;
}

request_t *insert_macro_read(long long int physical_address,
                             long long int arrival_time, int thread_id,
                             int instruction_id, long long int instruction_pc) {
  stats_macro_reads_seen++;
  macro_read_queue_length++;
  // init a read request
  request_t *new_node =
      init_new_node(physical_address, arrival_time, READ, thread_id,
                    instruction_id, instruction_pc);

  LL_APPEND(macro_read_queue_head, new_node);
  return new_node;
}

int read_matches_write_or_read_macro_queue(long long int addr) {
  request_t *wr_ptr = NULL;
  request_t *rd_ptr = NULL;

  LL_FOREACH(macro_write_queue_head, wr_ptr) {
    if (wr_ptr->dram_addr.actual_address == addr) {
      num_macro_read_merge_write++;
      // stats_reads_merged_per_channel[channel]++;
      return WQ_LOOKUP_LATENCY;
    }
  }

  LL_FOREACH(macro_read_queue_head, rd_ptr) {
    if (rd_ptr->dram_addr.actual_address == addr) {
      num_macro_read_merge_read++;
      // stats_reads_merged_per_channel[channel]++;
      return RQ_LOOKUP_LATENCY;
    }
  }
  return 0;
}

int write_exists_in_write_macro_queue(long long int physical_address) {
  request_t *wr_ptr = NULL;

  LL_FOREACH(macro_write_queue_head, wr_ptr) {
    if (wr_ptr->dram_addr.actual_address == physical_address) {
      num_macro_write_merge++;
      // stats_writes_merged_per_channel[channel]++;
      return 1;
    }
  }
  return 0;
}

// Insert a new write to the write queue
request_t *insert_write(long long int physical_address,
                        long long int arrival_time, int thread_id,
                        int instruction_id) {
  optype_t this_op = WRITE;

  dram_address_t *this_addr = calc_dram_addr(physical_address);
  int channel = this_addr->channel;
  free(this_addr);

  stats_writes_seen[channel]++;

  request_t *new_node = init_new_node(physical_address, arrival_time, this_op,
                                      thread_id, instruction_id, 0);
  new_node->type = DATA;
  new_node->picked = 0;
  new_node->dirty = 0;
  LL_APPEND(write_queue_head[channel], new_node);

  write_queue_length[channel]++;

  // UT_MEM_DEBUG("\nCyc: %lld New WRITE:%lld Core:%d Chan:%d Rank:%d Bank:%d
  // Row:%lld WR_Q_Length:%lld\n", CYCLE_VAL, new_node->id, new_node->thread_id,
  // new_node->dram_addr.channel,  new_node->dram_addr.rank,
  // new_node->dram_addr.bank,  new_node->dram_addr.row,
  // write_queue_length[channel]);

  return new_node;
}

request_t *insert_mac_write(long long int physical_address,
                            long long int arrival_time, int thread_id,
                            int instruction_id) {
  optype_t this_op = WRITE;

  dram_address_t *this_addr = calc_dram_addr(physical_address);
  int channel = this_addr->channel;
  free(this_addr);

  stats_writes_seen[channel]++;

  request_t *new_node = init_new_node(physical_address, arrival_time, this_op,
                                      thread_id, instruction_id, 0);
  new_node->type = PROOF;
  new_node->picked = 0;
  new_node->dirty = 1;

  LL_APPEND(write_queue_head[channel], new_node);

  write_queue_length[channel]++;

  // UT_MEM_DEBUG("\nCyc: %lld New WRITE:%lld Core:%d Chan:%d Rank:%d Bank:%d
  // Row:%lld WR_Q_Length:%lld\n", CYCLE_VAL, new_node->id, new_node->thread_id,
  // new_node->dram_addr.channel,  new_node->dram_addr.rank,
  // new_node->dram_addr.bank,  new_node->dram_addr.row,
  // write_queue_length[channel]);

  return new_node;
}

int size_macro_req_table() {
  mt_table_t *tab_ptr;
  int size = 0;
  LL_FOREACH(mt_tab, tab_ptr) {
    if (!mt_tab->macro_req->request_served) {
      size++;
    }
  }
  return (size);
}

void micro_req_gen() {
  // unsigned long long int cnt_start[NUMCORES];
  unsigned long long int macro_start;
  region(TRACE_CAPACITY, 0x0, &macro_start);

  // while there is space in the table and there are requests in the read queue
  // add a new table member

  while ((size_macro_req_table() < MAX_MACRO_REQ) &&
         ((size_macro_wr_queue() != 0) || (size_macro_rd_queue() != 0))) {
    if ((size_macro_rd_queue() != 0) || (size_macro_wr_queue() != 0)) {

      mt_table_t *table_mem = malloc(sizeof(mt_table_t));
      table_mem->macro_req = malloc(sizeof(request_t));

      int t = pick_macro_request(table_mem->macro_req);
      assert(t == 1);
      table_mem->micro_req = NULL;
      LL_APPEND(mt_tab, table_mem);

      if (table_mem->macro_req->operation_type == READ) {
        unsigned long long int MAC_addr = find_MAC(
            table_mem->macro_req->physical_address, 0x0, TRACE_CAPACITY);
        request_t *data_node =
            init_new_node(table_mem->macro_req->physical_address, CYCLE_VAL,
                          table_mem->macro_req->operation_type,
                          table_mem->macro_req->thread_id,
                          table_mem->macro_req->instruction_id,
                          table_mem->macro_req->instruction_pc);
        data_node->type = DATA;
        LL_APPEND(table_mem->micro_req, data_node);
        request_t *mac_node = init_new_node(
            MAC_addr, CYCLE_VAL, READ, table_mem->macro_req->thread_id,
            table_mem->macro_req->instruction_id,
            table_mem->macro_req->instruction_pc);
        mac_node->type = PROOF;
        LL_APPEND(table_mem->micro_req, mac_node);
      } else {
        unsigned long long int MAC_addr = find_MAC(
            table_mem->macro_req->physical_address, 0x0, TRACE_CAPACITY);
        request_t *data_node_w =
            init_new_node(table_mem->macro_req->physical_address, CYCLE_VAL,
                          table_mem->macro_req->operation_type,
                          table_mem->macro_req->thread_id,
                          table_mem->macro_req->instruction_id,
                          table_mem->macro_req->instruction_pc);
        data_node_w->type = DATA;
        LL_APPEND(table_mem->micro_req, data_node_w);

        request_t *mac_node_wr = init_new_node(
            MAC_addr, CYCLE_VAL, WRITE, table_mem->macro_req->thread_id,
            table_mem->macro_req->instruction_id,
            table_mem->macro_req->instruction_pc);
        mac_node_wr->type = PROOF;
        LL_APPEND(table_mem->micro_req, mac_node_wr);
      }
    }
  }

  request_t *micr_tmp;
  mt_table_t *tab; //, * tab_tmp;
  LL_FOREACH(mt_tab, tab) {
    if (!tab->macro_req->request_served) {
      if (tab->macro_req->operation_type == READ) {
        request_t *tmp = NULL;
        LL_FOREACH_SAFE(tab->micro_req, micr_tmp, tmp) {
          if (micr_tmp->picked)
            continue;
          if (micr_tmp->type == DATA) {
            int lat = read_matches_write_or_read_queue(
                micr_tmp->physical_address, DATA, 0);
            if (lat) {
              micr_tmp->completion_time =
                  CYCLE_VAL + (long long int)lat + PIPELINEDEPTH;
              micr_tmp->request_served = 1;
            } else {
              insert_read(micr_tmp->physical_address, CYCLE_VAL,
                          micr_tmp->thread_id, micr_tmp->instruction_id,
                          micr_tmp->instruction_pc);
            }
            micr_tmp->picked = 1;
          } else if (micr_tmp->type == PROOF) {
            tag_t *mac_fnd = look_up(meta_cache[micr_tmp->thread_id],
                                     micr_tmp->physical_address,
                                     1); ///, micr_tmp->thread_id);
            if (mac_fnd == NULL) {
              int lat = read_matches_write_or_read_queue(
                  micr_tmp->physical_address, PROOF, 0);
              if (lat) {
                micr_tmp->completion_time =
                    CYCLE_VAL + (long long int)lat + PIPELINEDEPTH;
                micr_tmp->request_served = 1;
              } else {
                insert_mac_read(micr_tmp->physical_address, CYCLE_VAL,
                                micr_tmp->thread_id, micr_tmp->instruction_id,
                                micr_tmp->instruction_pc, 1, 0);
              }
              micr_tmp->picked = 1;
            } else {
              micr_tmp->picked = 1;
              micr_tmp->dirty = 0;
              micr_tmp->request_served = 1;
              micr_tmp->completion_time = CYCLE_VAL + PIPELINEDEPTH;
              LL_DELETE(tab->micro_req, micr_tmp);
              free(micr_tmp);
            }
          }
        }
      }
      // write
      else {
        request_t *temp;
        LL_FOREACH_SAFE(tab->micro_req, micr_tmp, temp) {
          if (micr_tmp->picked)
            continue;
          if (micr_tmp->type == DATA) {
            if (!write_exists_in_write_queue(micr_tmp->physical_address,
                                             DATA)) {
              insert_write(micr_tmp->physical_address, CYCLE_VAL,
                           micr_tmp->thread_id, micr_tmp->instruction_id);
            } else {
              micr_tmp->completion_time = CYCLE_VAL + PIPELINEDEPTH;
            }

            micr_tmp->picked = 1;
            micr_tmp->request_served = 1;
          } else if (micr_tmp->type == PROOF) {
            if (proof_queue_full(micr_tmp->dram_addr.channel,
                                 micr_tmp->dram_addr.rank,
                                 micr_tmp->dram_addr.bank)) {
              continue;
            } else {
              request_t *new_node = init_new_node(
                  micr_tmp->physical_address, CYCLE_VAL, WRITE,
                  micr_tmp->thread_id, micr_tmp->instruction_id, 0);
              new_node->type = PROOF;

              LL_APPEND(update_proof_queue[micr_tmp->dram_addr.channel]
                                          [micr_tmp->dram_addr.rank]
                                          [micr_tmp->dram_addr.bank],
                        new_node);
              micr_tmp->picked = 1;
              micr_tmp->request_served = 1;
            }
          }
        }
      }
    }
  }
} // micro_req_gen

int proof_queue_full(int channel, int rank, int bank) {
  request_t *temp;
  int count = 0;
  LL_FOREACH(update_proof_queue[channel][rank][bank], temp) { count++; }
  if (count >= MAX_PROOF_QUEUE) {
    return 1;
  } else {
    return 0;
  }
}

void update_macro_thread(request_t *request) {
  //assert(request->operation_type == READ);
  // write micro requests do not need to be updated
  // with the data of completion time because this
  // has already happened at the time of inserting
  // into read queue
  mt_table_t *tab;
  request_t *micr_tmp;
  int found = 0;
  LL_FOREACH(mt_tab, tab) {
    LL_FOREACH(tab->micro_req, micr_tmp) {
      if ((micr_tmp->physical_address == request->physical_address) &&
          ((micr_tmp->type == PROOF)||(micr_tmp->operation_type == READ)) &&
          (micr_tmp->request_served == 0) && (micr_tmp->picked == 1)) {
        found = 1;
        micr_tmp->completion_time = request->completion_time;
        micr_tmp->request_served = 1;
        break;
      }
    }
    if (found == 1)
      break;
  }
  assert(found == 1);
}

int issue_proof_flush(int channel, int rank, int b){
    dram_state[channel][rank][b].state = REFRESHING;
    dram_state[channel][rank][b].active_row = -1;
    long long int temp = CYCLE_VAL + 1 * T_REFI; //1 cos we are only refreshing one bank.
    
    dram_state[channel][rank][b].next_act = temp;
    dram_state[channel][rank][b].next_pre = temp;
    dram_state[channel][rank][b].next_refresh = temp;
    dram_state[channel][rank][b].next_powerdown = temp;
    return 1;
}

/*int issue_proof_flush(int channel, int rank, int bank){
  if (!is_refresh_allowed_bank(channel, rank, bank)) {
    // printf("PANIC : SCHED_ERROR: REFRESH command not issuable in cycle:%lld\n",
    //        CYCLE_VAL);
    return 0;
  } else {
  long long int cycle = CYCLE_VAL;

  if (dram_state[channel][rank][bank].state == PRECHARGE_POWER_DOWN_SLOW) {
      dram_state[channel][rank][bank].next_act = max(
          cycle + T_XP_DLL + T_RFC, dram_state[channel][rank][bank].next_act);
      dram_state[channel][rank][bank].next_pre = max(
          cycle + T_XP_DLL + T_RFC, dram_state[channel][rank][bank].next_pre);
      dram_state[channel][rank][bank].next_refresh =
          max(cycle + T_XP_DLL + T_RFC,
              dram_state[channel][rank][bank].next_refresh);
      dram_state[channel][rank][bank].next_powerdown =
          max(cycle + T_XP_DLL + T_RFC,
              dram_state[channel][rank][bank].next_powerdown);
  } else if (dram_state[channel][rank][bank].state ==
              PRECHARGE_POWER_DOWN_FAST) {
      dram_state[channel][rank][bank].next_act =
          max(cycle + T_XP + T_RFC, dram_state[channel][rank][bank].next_act);
      dram_state[channel][rank][bank].next_pre =
          max(cycle + T_XP + T_RFC, dram_state[channel][rank][bank].next_pre);
      dram_state[channel][rank][bank].next_refresh = max(
          cycle + T_XP + T_RFC, dram_state[channel][rank][bank].next_refresh);
      dram_state[channel][rank][bank].next_powerdown = max(
          cycle + T_XP + T_RFC, dram_state[channel][rank][bank].next_powerdown);
  } else if (dram_state[channel][rank][bank].state == ACTIVE_POWER_DOWN) {
      dram_state[channel][rank][bank].next_act = max(
          cycle + T_XP + T_RP + T_RFC, dram_state[channel][rank][bank].next_act);
      dram_state[channel][rank][bank].next_pre = max(
          cycle + T_XP + T_RP + T_RFC, dram_state[channel][rank][bank].next_pre);
      dram_state[channel][rank][bank].next_refresh =
          max(cycle + T_XP + T_RP + T_RFC,
              dram_state[channel][rank][bank].next_refresh);
      dram_state[channel][rank][bank].next_powerdown =
          max(cycle + T_XP + T_RP + T_RFC,
              dram_state[channel][rank][bank].next_powerdown);
  } else // rank powered up
  {
    int flag = 0;
    if (dram_state[channel][rank][bank].state == ROW_ACTIVE) {
      flag = 1;
    }
    if (flag) // at least a single bank is open
    {
        dram_state[channel][rank][bank].next_act =
            max(cycle + T_RP + T_RFC, dram_state[channel][rank][bank].next_act);
        dram_state[channel][rank][bank].next_pre =
            max(cycle + T_RP + T_RFC, dram_state[channel][rank][bank].next_pre);
        dram_state[channel][rank][bank].next_refresh = max(
            cycle + T_RP + T_RFC, dram_state[channel][rank][bank].next_refresh);
        dram_state[channel][rank][bank].next_powerdown =
            max(cycle + T_RP + T_RFC,
                dram_state[channel][rank][bank].next_powerdown);
    } else // everything precharged
    {
        dram_state[channel][rank][bank].next_act =
            max(cycle + T_RFC, dram_state[channel][rank][bank].next_act);
        dram_state[channel][rank][bank].next_pre =
            max(cycle + T_RFC, dram_state[channel][rank][bank].next_pre);
        dram_state[channel][rank][bank].next_refresh =
            max(cycle + T_RFC, dram_state[channel][rank][bank].next_refresh);
        dram_state[channel][rank][bank].next_powerdown =
            max(cycle + T_RFC, dram_state[channel][rank][bank].next_powerdown);
    }
  }
  dram_state[channel][rank][bank].active_row = -1;
  dram_state[channel][rank][bank].state = REFRESHING;
  command_issued_current_cycle[channel] = 1;
  last_proof_update[channel][rank][bank] = CYCLE_VAL;
  }
  return 1;
}*/

void empty_proof_queue(int channel, int rank, int bank){
  //set request served and next command to COL_WRITE_CMD and completion time
  request_t *temp;
  // LL_FOREACH(update_proof_queue[channel][rank][bank], temp){
  //   update_macro_thread(temp);
  // }

  request_t *safe;
  LL_FOREACH_SAFE(update_proof_queue[channel][rank][bank], temp, safe){
    LL_DELETE(update_proof_queue[channel][rank][bank], temp);
    free(temp);
    temp = NULL;
  }

  //g
}

int proof_time_passed(int channel, int rank, int bank){
  if (last_proof_update[channel][rank][bank] < CYCLE_VAL- PROOF_UPDATE_CYCLE){
    return 1;
  }
  return 0;
}

void check_to_issue_proof_flush(){
  for(int i = 0; i < NUM_CHANNELS; i++){
    for(int j = 0; j < NUM_RANKS; j++){
      for(int k = 0; k < NUM_BANKS; k++){       
        if(proof_queue_full(i, j, k) /*|| proof_time_passed(i, j, k)*/){
            issue_proof_flush(i, j, k);
            empty_proof_queue(i, j, k);
        }
      }
    }
  }
}

int pick_macro_request(request_t *reqst) {
  request_t *rd_ptr = NULL;
  request_t *wr_ptr = NULL;
  assert((size_macro_wr_queue() != 0) || (size_macro_rd_queue() != 0));
  int drain_macro_writes = 0;

  // if in write drain mode, keep draining writes until the
  // write queue occupancy drops to LO_WM
  if (drain_macro_writes &&
      (size_macro_wr_queue() /*macro_write_queue_length*/ > MACRO_LO_WM)) {
    drain_macro_writes = 1; // Keep draining.
  } else {
    drain_macro_writes = 0; // No need to drain.
  }

  if (size_macro_wr_queue() /*macro_write_queue_length*/ > MACRO_HI_WM) {
    drain_macro_writes = 1;
  } else {
    if (size_macro_rd_queue() == 0 /*macro_read_queue_length*/)
      drain_macro_writes = 1;
  }

  if (drain_macro_writes) {
    LL_FOREACH(macro_write_queue_head, wr_ptr) {
      if ((!wr_ptr->request_served) && (!wr_ptr->picked)) {
        break;
      }
    }
    if (wr_ptr != NULL) {
      wr_ptr->picked = 1; // picked
      memcpy(reqst, wr_ptr, sizeof(request_t));
      return (1);
    } else {
      return (0);
    }
  } else {
    LL_FOREACH(macro_read_queue_head, rd_ptr) {
      if ((!rd_ptr->request_served) && (!rd_ptr->picked)) {
        break;
      }
    }
    if (rd_ptr != NULL) {
      rd_ptr->picked = 1; // picked
      assert(rd_ptr->request_served == 0);
      memcpy(reqst, rd_ptr, sizeof(request_t));
      return (1);
    } else {
      return (0);
    }
  }
}

void clean_macro_queues() {
  // clean sh**
  // clean reads
  request_t *rd_ptr = NULL;
  request_t *rd_tmp = NULL;
  LL_FOREACH_SAFE(macro_read_queue_head, rd_ptr, rd_tmp) {
    if (rd_ptr->request_served == 1) {
      LL_DELETE(macro_read_queue_head, rd_ptr);
      free(rd_ptr);
      macro_read_queue_length--;
      assert(macro_read_queue_length >= 0);
    }
  }

  request_t *wrt_ptr = NULL;
  request_t *wrt_tmp = NULL;
  LL_FOREACH_SAFE(macro_write_queue_head, wrt_ptr, wrt_tmp) {
    if (wrt_ptr->request_served == 1) {
      LL_DELETE(macro_write_queue_head, wrt_ptr);
      free(wrt_ptr);
      macro_write_queue_length--;
      assert(macro_write_queue_length >= 0);
    }
  }

  // clean macro table
  request_t *mic = NULL;
  request_t *mic_tmp = NULL;
  mt_table_t *table_ptr = NULL;
  mt_table_t *table_tmp = NULL;
  LL_FOREACH_SAFE(mt_tab, table_ptr, table_tmp) {
    if (table_ptr->macro_req->request_served == 1) {
      LL_DELETE(mt_tab, table_ptr);

      LL_FOREACH_SAFE(table_ptr->micro_req, mic, mic_tmp) {
        LL_DELETE(table_ptr->micro_req, mic);
        free(mic);
      }
      free(table_ptr->macro_req);
      free(table_ptr);
    }
  }
}

void update_backward() {
  mt_table_t *tab;
  request_t *micr_req;
  int done = 1;
  long long int max_comp = 0;
  LL_FOREACH(mt_tab, tab) {
    done = 1;
    if (tab->micro_req == NULL) {
      max_comp = tab->macro_req->completion_time;
      if (tab->macro_req->completion_time == 0)
        tab->macro_req->completion_time = CYCLE_VAL;
    }
    LL_FOREACH(tab->micro_req, micr_req) {
      if (!micr_req->request_served) {
        done = 0;
        break;
      } else if (micr_req->operation_type == READ) {
        if (max_comp < micr_req->completion_time)
          max_comp = micr_req->completion_time;
      }
    }
    if (done == 1) {
      request_t *rd_req, *wr_req;
      // means this MACRO instruction is done
      tab->macro_req->completion_time = max_comp;
      tab->macro_req->latency =
          tab->macro_req->completion_time - tab->macro_req->arrival_time;
      if (tab->macro_req->operation_type == READ) {
        stats_macro_reads_completed++;
        stats_average_macro_read_latency =
            ((stats_macro_reads_completed - 1) *
                 stats_average_macro_read_latency +
             tab->macro_req->latency) /
            stats_macro_reads_completed;
        ROB[tab->macro_req->thread_id]
            .comptime[tab->macro_req->instruction_id] =
            tab->macro_req->completion_time + PIPELINEDEPTH;
      } else {
        // write
        stats_macro_writes_completed++;
        stats_average_macro_write_latency =
            ((stats_macro_writes_completed - 1) *
                 stats_average_macro_write_latency +
             tab->macro_req->latency) /
            stats_macro_writes_completed;
      }
      tab->macro_req->request_served = 1;
      int find = 0;
      if (tab->macro_req->operation_type == READ) {
        LL_FOREACH(macro_read_queue_head, rd_req) {
          if (equal_request(rd_req, tab->macro_req)) {
            find = 1;
            rd_req->request_served = 1;
            rd_req->completion_time = tab->macro_req->completion_time;
            break;
          }
        }
        if (find == 1)
          break;
      } else {
        LL_FOREACH(macro_write_queue_head, wr_req) {
          if (equal_request(wr_req, tab->macro_req)) {
            find = 1;
            wr_req->request_served = 1;
            wr_req->completion_time = tab->macro_req->completion_time;
            break;
          }
        }
        if (find == 1)
          break;
        // LL_FOREACH(update_proof_queue[tab->macro_req->dram_addr.channel]
        //                              [tab->macro_req->dram_addr.rank]
        //                              [tab->macro_req->dram_addr.bank],
        //            wr_req) {
        //   if (equal_request(wr_req, tab->macro_req)) {
        //     find = 1;
        //     wr_req->request_served = 1;
        //     wr_req->completion_time = tab->macro_req->completion_time;
        //     break;
        //   }
        // }
        // if (find == 1)
        //   break;
      }
      assert(find == 1); // should be found in either macro_write_queue_head or
                         // macro_read_queue_head
    }
  }
}

int size_macro_rd_queue() {
  request_t *rd_ptr;
  int size = 0;
  LL_FOREACH(macro_read_queue_head, rd_ptr) {
    if (!rd_ptr->picked) {
      size++;
    }
  }
  return (size);
}

int size_macro_wr_queue() {
  request_t *wr_ptr;
  int size = 0;
  LL_FOREACH(macro_write_queue_head, wr_ptr) {
    if (!wr_ptr->picked) {
      size++;
    }
  }
  return (size);
}

int equal_request(request_t *req1, request_t *req2) {
  if ((req1->physical_address == req2->physical_address) &&
      (req2->operation_type == req1->operation_type) &&
      (req1->thread_id == req2->thread_id) &&
      (req1->instruction_id == req2->instruction_id))
    return (1);
  else
    return (0);
}

// Function to update the states of the read queue requests.
// Each DRAM cycle, this function iterates over the read queue and
// updates the next_command and command_issuable fields to mark which
// commands can be issued this cycle
void update_read_queue_commands(int channel) {
  request_t *curr = NULL;

  LL_FOREACH(read_queue_head[channel], curr) {
    // ignore the requests whose completion time has been determined
    // these requests will be removed this very cycle
    if (curr->request_served == 1)
      continue;

    int bank = curr->dram_addr.bank;

    int rank = curr->dram_addr.rank;

    int row = curr->dram_addr.row;

    switch (dram_state[channel][rank][bank].state) {
      // if the DRAM bank has no rows open and the chip is
      // powered up, the next command for the request
      // should be ACT.
    case IDLE:
    case PRECHARGING:
    case REFRESHING:

      curr->next_command = ACT_CMD;

      if (CYCLE_VAL >= dram_state[channel][rank][bank].next_act &&
          is_T_FAW_met(channel, rank, CYCLE_VAL))
        curr->command_issuable = 1;
      else
        curr->command_issuable = 0;

      // check if we are in OR too close to the forced refresh period
      if (forced_refresh_mode_on[channel][rank] ||
          ((CYCLE_VAL + T_RAS) > refresh_issue_deadline[channel][rank]))
        curr->command_issuable = 0;
      break;

    case ROW_ACTIVE:

      // if the bank is active then check if this is a row-hit or not
      // If the request is to the currently
      // opened row, the next command should
      // be a COL_RD, else it should be a
      // PRECHARGE
      if (row == dram_state[channel][rank][bank].active_row) {
        curr->next_command = COL_READ_CMD;

        if (CYCLE_VAL >= dram_state[channel][rank][bank].next_read)
          curr->command_issuable = 1;
        else
          curr->command_issuable = 0;

        if (forced_refresh_mode_on[channel][rank] ||
            ((CYCLE_VAL + T_RTP) > refresh_issue_deadline[channel][rank]))
          curr->command_issuable = 0;
      } else {
        curr->next_command = PRE_CMD;

        if (CYCLE_VAL >= dram_state[channel][rank][bank].next_pre)
          curr->command_issuable = 1;
        else
          curr->command_issuable = 0;

        if (forced_refresh_mode_on[channel][rank] ||
            ((CYCLE_VAL + T_RP) > refresh_issue_deadline[channel][rank]))
          curr->command_issuable = 0;
      }

      break;
      // if the chip was powered, down the
      // next command required is power_up

    case PRECHARGE_POWER_DOWN_SLOW:
    case PRECHARGE_POWER_DOWN_FAST:
    case ACTIVE_POWER_DOWN:

      curr->next_command = PWR_UP_CMD;

      if (CYCLE_VAL >= dram_state[channel][rank][bank].next_powerup)
        curr->command_issuable = 1;
      else
        curr->command_issuable = 0;

      if ((dram_state[channel][rank][bank].state ==
           PRECHARGE_POWER_DOWN_SLOW) &&
          ((CYCLE_VAL + T_XP_DLL) > refresh_issue_deadline[channel][rank]))
        curr->command_issuable = 0;
      else if (((dram_state[channel][rank][bank].state ==
                 PRECHARGE_POWER_DOWN_FAST) ||
                (dram_state[channel][rank][bank].state == ACTIVE_POWER_DOWN)) &&
               ((CYCLE_VAL + T_XP) > refresh_issue_deadline[channel][rank]))
        curr->command_issuable = 0;

      break;

    default:
      break;
    }
  }
}

// Similar to update_read_queue above, but for write queue
void update_write_queue_commands(int channel) {
  request_t *curr = NULL;

  LL_FOREACH(write_queue_head[channel], curr) {

    if (curr->request_served == 1)
      continue;

    int bank = curr->dram_addr.bank;

    int rank = curr->dram_addr.rank;

    int row = curr->dram_addr.row;

    switch (dram_state[channel][rank][bank].state) {
    case IDLE:
    case PRECHARGING:
    case REFRESHING:
      curr->next_command = ACT_CMD;

      if (CYCLE_VAL >= dram_state[channel][rank][bank].next_act &&
          is_T_FAW_met(channel, rank, CYCLE_VAL))
        curr->command_issuable = 1;
      else
        curr->command_issuable = 0;

      // check if we are in or too close to the forced refresh period
      if (forced_refresh_mode_on[channel][rank] ||
          ((CYCLE_VAL + T_RAS) > refresh_issue_deadline[channel][rank]))
        curr->command_issuable = 0;

      break;

    case ROW_ACTIVE:

      if (row == dram_state[channel][rank][bank].active_row) {
        curr->next_command = COL_WRITE_CMD;

        if (CYCLE_VAL >= dram_state[channel][rank][bank].next_write)
          curr->command_issuable = 1;
        else
          curr->command_issuable = 0;

        if (forced_refresh_mode_on[channel][rank] ||
            ((CYCLE_VAL + T_CWD + T_DATA_TRANS + T_WR) >
             refresh_issue_deadline[channel][rank]))
          curr->command_issuable = 0;
      } else {
        curr->next_command = PRE_CMD;

        if (CYCLE_VAL >= dram_state[channel][rank][bank].next_pre)
          curr->command_issuable = 1;
        else
          curr->command_issuable = 0;

        if (forced_refresh_mode_on[channel][rank] ||
            ((CYCLE_VAL + T_RP) > refresh_issue_deadline[channel][rank]))
          curr->command_issuable = 0;
      }

      break;

    case PRECHARGE_POWER_DOWN_SLOW:
    case PRECHARGE_POWER_DOWN_FAST:
    case ACTIVE_POWER_DOWN:

      curr->next_command = PWR_UP_CMD;

      if (CYCLE_VAL >= dram_state[channel][rank][bank].next_powerup)
        curr->command_issuable = 1;
      else
        curr->command_issuable = 0;

      if (forced_refresh_mode_on[channel][rank])
        curr->command_issuable = 0;

      if ((dram_state[channel][rank][bank].state ==
           PRECHARGE_POWER_DOWN_SLOW) &&
          ((CYCLE_VAL + T_XP_DLL) > refresh_issue_deadline[channel][rank]))
        curr->command_issuable = 0;
      else if (((dram_state[channel][rank][bank].state ==
                 PRECHARGE_POWER_DOWN_FAST) ||
                (dram_state[channel][rank][bank].state == ACTIVE_POWER_DOWN)) &&
               ((CYCLE_VAL + T_XP) > refresh_issue_deadline[channel][rank]))
        curr->command_issuable = 0;

      break;

    default:
      break;
    }
  }
}

// Remove finished requests from the queues.
void clean_queues(int channel) {

  request_t *rd_ptr = NULL;
  request_t *rd_tmp = NULL;
  request_t *wrt_ptr = NULL;
  request_t *wrt_tmp = NULL;

  // Delete all READ requests whose completion time has been determined i.e.
  // COL_RD has been issued
  LL_FOREACH_SAFE(read_queue_head[channel], rd_ptr, rd_tmp) {
    if (rd_ptr->request_served == 1) {
      assert(rd_ptr->next_command == COL_READ_CMD);

      assert(rd_ptr->completion_time != -100);

      LL_DELETE(read_queue_head[channel], rd_ptr);

      if (rd_ptr->user_ptr)
        free(rd_ptr->user_ptr);

      free(rd_ptr);

      read_queue_length[channel]--;

      assert(read_queue_length[channel] >= 0);
    }
  }

  // Delete all WRITE requests whose completion time has been determined i.e
  // COL_WRITE has been issued
  LL_FOREACH_SAFE(write_queue_head[channel], wrt_ptr, wrt_tmp) {
    if (wrt_ptr->request_served == 1) {
      if(wrt_ptr->next_command != COL_WRITE_CMD){
        printf("PANIC: SCHED_ERROR: WRITE request served but next command is not COL_WRITE_CMD\n");
      }
      assert(wrt_ptr->next_command == COL_WRITE_CMD);
      LL_DELETE(write_queue_head[channel], wrt_ptr);

      if (wrt_ptr->user_ptr)
        free(wrt_ptr->user_ptr);

      free(wrt_ptr);

      write_queue_length[channel]--;

      assert(write_queue_length[channel] >= 0);
    }
  }
}

// This affects state change
// Issue a valid command for a request in either the read or write
// queue.
// Upon issuing the request, the dram_state is changed and the
// next_"cmd" variables are updated to indicate when the next "cmd"
// can be issued to each bank
int issue_request_command(request_t *request) {
  long long int cycle = CYCLE_VAL;
  if (request->command_issuable != 1 ||
      command_issued_current_cycle[request->dram_addr.channel]) {
    printf("PANIC: SCHED_ERROR : Command for request selected can not be "
           "issued in  cycle:%lld.\n",
           CYCLE_VAL);
    return 0;
  }

  int channel = request->dram_addr.channel;
  int rank = request->dram_addr.rank;
  int bank = request->dram_addr.bank;
  long long int row = request->dram_addr.row;
  command_t cmd = request->next_command;

  switch (cmd) {
  case ACT_CMD:

    assert(dram_state[channel][rank][bank].state == PRECHARGING ||
           dram_state[channel][rank][bank].state == IDLE ||
           dram_state[channel][rank][bank].state == REFRESHING);
    // open row
    dram_state[channel][rank][bank].state = ROW_ACTIVE;

    dram_state[channel][rank][bank].active_row = row;

    dram_state[channel][rank][bank].next_pre =
        max((cycle + T_RAS), dram_state[channel][rank][bank].next_pre);

    dram_state[channel][rank][bank].next_refresh =
        max((cycle + T_RAS), dram_state[channel][rank][bank].next_refresh);

    dram_state[channel][rank][bank].next_read =
        max(cycle + T_RCD, dram_state[channel][rank][bank].next_read);

    dram_state[channel][rank][bank].next_write =
        max(cycle + T_RCD, dram_state[channel][rank][bank].next_write);

    dram_state[channel][rank][bank].next_act =
        max(cycle + T_RC, dram_state[channel][rank][bank].next_act);

    dram_state[channel][rank][bank].next_powerdown =
        max(cycle + T_RCD, dram_state[channel][rank][bank].next_powerdown);

    for (int i = 0; i < NUM_BANKS; i++)
      if (i != bank)
        dram_state[channel][rank][i].next_act =
            max(cycle + T_RRD, dram_state[channel][rank][i].next_act);

    record_activate(channel, rank, cycle);

    if (request->operation_type == READ)
      stats_num_activate_read[channel][rank][bank]++;
    else
      stats_num_activate_write[channel][rank][bank]++;

    stats_num_activate[channel][rank]++;

    average_gap_between_activates[channel][rank] =
        ((average_gap_between_activates[channel][rank] *
          (stats_num_activate[channel][rank] - 1)) +
         (CYCLE_VAL - last_activate[channel][rank])) /
        stats_num_activate[channel][rank];

    last_activate[channel][rank] = CYCLE_VAL;

    command_issued_current_cycle[channel] = 1;
    break;

  case COL_READ_CMD:

    assert(dram_state[channel][rank][bank].state == ROW_ACTIVE);

    dram_state[channel][rank][bank].next_pre =
        max(cycle + T_RTP, dram_state[channel][rank][bank].next_pre);

    dram_state[channel][rank][bank].next_refresh =
        max(cycle + T_RTP, dram_state[channel][rank][bank].next_refresh);

    dram_state[channel][rank][bank].next_powerdown =
        max(cycle + T_RTP, dram_state[channel][rank][bank].next_powerdown);

    for (int i = 0; i < NUM_RANKS; i++) {
      for (int j = 0; j < NUM_BANKS; j++) {
        if (i != rank)
          dram_state[channel][i][j].next_read =
              max(cycle + T_DATA_TRANS + T_RTRS,
                  dram_state[channel][i][j].next_read);

        else
          dram_state[channel][i][j].next_read =
              max(cycle + max(T_CCD, T_DATA_TRANS),
                  dram_state[channel][i][j].next_read);

        dram_state[channel][i][j].next_write =
            max(cycle + T_CAS + T_DATA_TRANS + T_RTRS - T_CWD,
                dram_state[channel][i][j].next_write);
      }
    }

    // set the completion time of this read request
    // in the ROB and the controller queue.
    request->completion_time = CYCLE_VAL + T_CAS + T_DATA_TRANS;
    request->latency = request->completion_time - request->arrival_time;
    request->dispatch_time = CYCLE_VAL;
    request->request_served = 1;

    if (request->type == PROOF) {
      // insert into cache
      tag_t *mac_fnd =
          look_up(meta_cache[request->thread_id], request->physical_address,
                  0); //, request->thread_id);
      if (mac_fnd == NULL) {
        meta_cache_miss_read_return++;
        tag_t *evicted;
        evicted = insert_cache(meta_cache[request->thread_id],
                               request->physical_address, 0, request->thread_id,
                               request->instruction_id, request->dirty,
                               request->type);
        if (evicted != NULL) {
          // write back if dirty
          if (evicted->dirty &&
              !write_exists_in_write_queue(evicted->address, evicted->type)) {
            insert_write(evicted->address, CYCLE_VAL, evicted->thread_id,
                         evicted->instruction_id);
          }
          free(evicted);
        }
      } else {
        meta_cache_hit_read_return++;
      }
    }

    if (SECURED == 1) {
      update_macro_thread(request); // fuck me
    }

    // update the ROB with the completion time
    ROB[request->thread_id].comptime[request->instruction_id] =
        request->completion_time + PIPELINEDEPTH;

    stats_reads_completed[channel]++;
    stats_average_read_latency[channel] =
        ((stats_reads_completed[channel] - 1) *
             stats_average_read_latency[channel] +
         request->latency) /
        stats_reads_completed[channel];
    stats_average_read_queue_latency[channel] =
        ((stats_reads_completed[channel] - 1) *
             stats_average_read_queue_latency[channel] +
         (request->dispatch_time - request->arrival_time)) /
        stats_reads_completed[channel];
    // UT_MEM_DEBUG("Req:%lld finishes at Cycle: %lld\n", request->id,
    // request->completion_time);

    // printf("Cycle: %10lld, Reads  Completed = %5lld, this_latency= %5lld,
    // latency = %f\n", CYCLE_VAL, stats_reads_completed[channel],
    // request->latency, stats_average_read_latency[channel]);

    stats_num_read[channel][rank][bank]++;

    for (int i = 0; i < NUM_RANKS; i++) {
      if (i != rank)
        stats_time_spent_terminating_reads_from_other_ranks[channel][i] +=
            T_DATA_TRANS;
    }

    command_issued_current_cycle[channel] = 1;
    cas_issued_current_cycle[channel][rank][bank] = 1;
    break;

  case COL_WRITE_CMD:

    assert(dram_state[channel][rank][bank].state == ROW_ACTIVE);

    // UT_MEM_DEBUG("\nCycle: %lld Cmd: COL_WRITE Req:%lld Chan:%d Rank:%d
    // Bank:%d \n", CYCLE_VAL, request->id, channel, rank, bank);

    dram_state[channel][rank][bank].next_pre =
        max(cycle + T_CWD + T_DATA_TRANS + T_WR,
            dram_state[channel][rank][bank].next_pre);

    dram_state[channel][rank][bank].next_refresh =
        max(cycle + T_CWD + T_DATA_TRANS + T_WR,
            dram_state[channel][rank][bank].next_refresh);

    dram_state[channel][rank][bank].next_powerdown =
        max(cycle + T_CWD + T_DATA_TRANS + T_WR,
            dram_state[channel][rank][bank].next_powerdown);

    for (int i = 0; i < NUM_RANKS; i++) {
      for (int j = 0; j < NUM_BANKS; j++) {
        if (i != rank) {
          dram_state[channel][i][j].next_write =
              max(cycle + T_DATA_TRANS + T_RTRS,
                  dram_state[channel][i][j].next_write);

          dram_state[channel][i][j].next_read =
              max(cycle + T_CWD + T_DATA_TRANS + T_RTRS - T_CAS,
                  dram_state[channel][i][j].next_read);
        } else {
          dram_state[channel][i][j].next_write =
              max(cycle + max(T_CCD, T_DATA_TRANS),
                  dram_state[channel][i][j].next_write);

          dram_state[channel][i][j].next_read =
              max(cycle + T_CWD + T_DATA_TRANS + T_WTR,
                  dram_state[channel][i][j].next_read);
        }
      }
    }

    // set the completion time of this write request
    request->completion_time = CYCLE_VAL + T_DATA_TRANS + T_WR;
    request->latency = request->completion_time - request->arrival_time;
    request->dispatch_time = CYCLE_VAL;
    request->request_served = 1;

    stats_writes_completed[channel]++;

    stats_num_write[channel][rank][bank]++;

    stats_average_write_latency[channel] =
        ((stats_writes_completed[channel] - 1) *
             stats_average_write_latency[channel] +
         request->latency) /
        stats_writes_completed[channel];
    stats_average_write_queue_latency[channel] =
        ((stats_writes_completed[channel] - 1) *
             stats_average_write_queue_latency[channel] +
         (request->dispatch_time - request->arrival_time)) /
        stats_writes_completed[channel];
    // UT_MEM_DEBUG("Req:%lld finishes at Cycle: %lld\n", request->id,
    // request->completion_time);

    // printf("Cycle: %10lld, Writes Completed = %5lld, this_latency= %5lld,
    // latency = %f\n", CYCLE_VAL, stats_writes_completed[channel],
    // request->latency, stats_average_write_latency[channel]);

    for (int i = 0; i < NUM_RANKS; i++) {
      if (i != rank)
        stats_time_spent_terminating_writes_to_other_ranks[channel][i] +=
            T_DATA_TRANS;
    }

    command_issued_current_cycle[channel] = 1;
    cas_issued_current_cycle[channel][rank][bank] = 2;
    break;

  case PRE_CMD:

    assert(dram_state[channel][rank][bank].state == ROW_ACTIVE ||
           dram_state[channel][rank][bank].state == PRECHARGING ||
           dram_state[channel][rank][bank].state == IDLE ||
           dram_state[channel][rank][bank].state == REFRESHING);

    // UT_MEM_DEBUG("\nCycle: %lld Cmd:PRE Req:%lld Chan:%d Rank:%d Bank:%d \n",
    // CYCLE_VAL, request->id, channel, rank, bank);

    dram_state[channel][rank][bank].state = PRECHARGING;

    dram_state[channel][rank][bank].active_row = -1;

    dram_state[channel][rank][bank].next_act =
        max(cycle + T_RP, dram_state[channel][rank][bank].next_act);

    dram_state[channel][rank][bank].next_powerdown =
        max(cycle + T_RP, dram_state[channel][rank][bank].next_powerdown);

    dram_state[channel][rank][bank].next_pre =
        max(cycle + T_RP, dram_state[channel][rank][bank].next_pre);

    dram_state[channel][rank][bank].next_refresh =
        max(cycle + T_RP, dram_state[channel][rank][bank].next_refresh);

    stats_num_precharge[channel][rank][bank]++;

    command_issued_current_cycle[channel] = 1;

    break;

  case PWR_UP_CMD:

    assert(dram_state[channel][rank][bank].state == PRECHARGE_POWER_DOWN_SLOW ||
           dram_state[channel][rank][bank].state == PRECHARGE_POWER_DOWN_FAST ||
           dram_state[channel][rank][bank].state == ACTIVE_POWER_DOWN);

    // UT_MEM_DEBUG("\nCycle: %lld Cmd: PWR_UP_CMD Chan:%d Rank:%d \n",
    // CYCLE_VAL, channel, rank);

    for (int i = 0; i < NUM_BANKS; i++) {

      if (dram_state[channel][rank][i].state == PRECHARGE_POWER_DOWN_SLOW ||
          dram_state[channel][rank][i].state == PRECHARGE_POWER_DOWN_FAST) {
        dram_state[channel][rank][i].state = IDLE;
        dram_state[channel][rank][i].active_row = -1;
      } else {
        dram_state[channel][rank][i].state = ROW_ACTIVE;
      }

      if (dram_state[channel][rank][i].state == PRECHARGE_POWER_DOWN_SLOW) {
        dram_state[channel][rank][i].next_powerdown =
            max(cycle + T_XP_DLL, dram_state[channel][rank][i].next_powerdown);

        dram_state[channel][rank][i].next_pre =
            max(cycle + T_XP_DLL, dram_state[channel][rank][i].next_pre);

        dram_state[channel][rank][i].next_read =
            max(cycle + T_XP_DLL, dram_state[channel][rank][i].next_read);

        dram_state[channel][rank][i].next_write =
            max(cycle + T_XP_DLL, dram_state[channel][rank][i].next_write);

        dram_state[channel][rank][i].next_act =
            max(cycle + T_XP_DLL, dram_state[channel][rank][i].next_act);

        dram_state[channel][rank][i].next_refresh =
            max(cycle + T_XP_DLL, dram_state[channel][rank][i].next_refresh);
      } else {

        dram_state[channel][rank][i].next_powerdown =
            max(cycle + T_XP, dram_state[channel][rank][i].next_powerdown);

        dram_state[channel][rank][i].next_pre =
            max(cycle + T_XP, dram_state[channel][rank][i].next_pre);

        dram_state[channel][rank][i].next_read =
            max(cycle + T_XP, dram_state[channel][rank][i].next_read);

        dram_state[channel][rank][i].next_write =
            max(cycle + T_XP, dram_state[channel][rank][i].next_write);

        dram_state[channel][rank][i].next_act =
            max(cycle + T_XP, dram_state[channel][rank][i].next_act);

        dram_state[channel][rank][i].next_refresh =
            max(cycle + T_XP, dram_state[channel][rank][i].next_refresh);
      }
    }

    stats_num_powerup[channel][rank]++;
    command_issued_current_cycle[channel] = 1;

    break;
  case NOP:

    // UT_MEM_DEBUG("\nCycle: %lld Cmd: NOP Chan:%d\n", CYCLE_VAL, channel);
    break;

  default:
    break;
  }
  return 1;
}

// Function called to see if the rank can be transitioned into a fast low
// power state - ACT_PDN or PRE_PDN_FAST.
int is_powerdown_fast_allowed(int channel, int rank) {
  int flag = 0;

  // if already a command has been issued this cycle, or if
  // forced refreshes are underway, or if issuing this command
  // will cause us to miss the refresh deadline, do not allow it
  if (command_issued_current_cycle[channel] ||
      forced_refresh_mode_on[channel][rank] ||
      (CYCLE_VAL + T_PD_MIN + T_XP > refresh_issue_deadline[channel][rank]))
    return 0;

  // command can be allowed if the next_powerdown is met for all banks in the
  // rank
  for (int i = 0; i < NUM_BANKS; i++) {
    if ((dram_state[channel][rank][i].state == PRECHARGING ||
         dram_state[channel][rank][i].state == ROW_ACTIVE ||
         dram_state[channel][rank][i].state == IDLE ||
         dram_state[channel][rank][i].state == REFRESHING) &&
        CYCLE_VAL >= dram_state[channel][rank][i].next_powerdown)
      flag = 1;
    else
      return 0;
  }

  return flag;
}

// Function to see if the rank can be transitioned into a slow low
// power state - i.e. PRE_PDN_SLOW
int is_powerdown_slow_allowed(int channel, int rank) {
  int flag = 0;

  if (command_issued_current_cycle[channel] ||
      forced_refresh_mode_on[channel][rank] ||
      (CYCLE_VAL + T_PD_MIN + T_XP_DLL > refresh_issue_deadline[channel][rank]))
    return 0;

  // Sleep command can be allowed if the next_powerdown is met for all banks in
  // the rank and if all the banks are precharged
  for (int i = 0; i < NUM_BANKS; i++) {
    if (dram_state[channel][rank][i].state == ROW_ACTIVE)
      return 0;
    else {
      if ((dram_state[channel][rank][i].state == PRECHARGING ||
           dram_state[channel][rank][i].state == IDLE ||
           dram_state[channel][rank][i].state == REFRESHING) &&
          CYCLE_VAL >= dram_state[channel][rank][i].next_powerdown)
        flag = 1;
      else
        return 0;
    }
  }
  return flag;
}

// Function to see if the rank can be powered up
int is_powerup_allowed(int channel, int rank) {
  if (command_issued_current_cycle[channel] ||
      forced_refresh_mode_on[channel][rank])
    return 0;

  if (((dram_state[channel][rank][0].state == PRECHARGE_POWER_DOWN_SLOW) ||
       (dram_state[channel][rank][0].state == PRECHARGE_POWER_DOWN_FAST) ||
       (dram_state[channel][rank][0].state == ACTIVE_POWER_DOWN)) &&
      (CYCLE_VAL >= dram_state[channel][rank][0].next_powerup)) {
    // check if issuing it will cause us to miss the refresh
    // deadline. If it does, don't allow it. The forced
    // refreshes will issue an implicit power up anyway
    if ((dram_state[channel][rank][0].state == PRECHARGE_POWER_DOWN_SLOW) &&
        ((CYCLE_VAL + T_XP_DLL) > refresh_issue_deadline[channel][0]))
      return 0;
    if (((dram_state[channel][rank][0].state == PRECHARGE_POWER_DOWN_FAST) ||
         (dram_state[channel][rank][0].state == ACTIVE_POWER_DOWN)) &&
        ((CYCLE_VAL + T_XP) > refresh_issue_deadline[channel][0]))
      return 0;
    return 1;
  } else
    return 0;
}

// Function to see if the bank can be activated or not
int is_activate_allowed(int channel, int rank, int bank) {
  if (command_issued_current_cycle[channel] ||
      forced_refresh_mode_on[channel][rank] ||
      (CYCLE_VAL + T_RAS > refresh_issue_deadline[channel][rank]))
    return 0;
  if ((dram_state[channel][rank][bank].state == IDLE ||
       dram_state[channel][rank][bank].state == PRECHARGING ||
       dram_state[channel][rank][bank].state == REFRESHING) &&
      (CYCLE_VAL >= dram_state[channel][rank][bank].next_act) &&
      (is_T_FAW_met(channel, rank, CYCLE_VAL)))
    return 1;
  else
    return 0;
}

// Function to see if the rank can be precharged or not
int is_autoprecharge_allowed(int channel, int rank, int bank) {
  long long int start_precharge = 0;
  if (cas_issued_current_cycle[channel][rank][bank] == 1)
    start_precharge =
        max(CYCLE_VAL + T_RTP, dram_state[channel][rank][bank].next_pre);
  else
    start_precharge = max(CYCLE_VAL + T_CWD + T_DATA_TRANS + T_WR,
                          dram_state[channel][rank][bank].next_pre);

  if (((cas_issued_current_cycle[channel][rank][bank] == 1) &&
       ((start_precharge + T_RP) <= refresh_issue_deadline[channel][rank])) ||
      ((cas_issued_current_cycle[channel][rank][bank] == 2) &&
       ((start_precharge + T_RP) <= refresh_issue_deadline[channel][rank])))
    return 1;
  else
    return 0;
}

// Function to see if the rank can be precharged or not
int is_precharge_allowed(int channel, int rank, int bank) {
  if (command_issued_current_cycle[channel] ||
      forced_refresh_mode_on[channel][rank] ||
      (CYCLE_VAL + T_RP > refresh_issue_deadline[channel][rank]))
    return 0;

  if ((dram_state[channel][rank][bank].state == ROW_ACTIVE ||
       dram_state[channel][rank][bank].state == IDLE ||
       dram_state[channel][rank][bank].state == PRECHARGING ||
       dram_state[channel][rank][bank].state == REFRESHING) &&
      (CYCLE_VAL >= dram_state[channel][rank][bank].next_pre))
    return 1;
  else
    return 0;
}

// function to see if all banks can be precharged this cycle
int is_all_bank_precharge_allowed(int channel, int rank) {
  int flag = 0;
  if (command_issued_current_cycle[channel] ||
      forced_refresh_mode_on[channel][rank] ||
      (CYCLE_VAL + T_RP > refresh_issue_deadline[channel][rank]))
    return 0;

  for (int i = 0; i < NUM_BANKS; i++) {
    if ((dram_state[channel][rank][i].state == ROW_ACTIVE ||
         dram_state[channel][rank][i].state == IDLE ||
         dram_state[channel][rank][i].state == PRECHARGING ||
         dram_state[channel][rank][i].state == REFRESHING) &&
        (CYCLE_VAL >= dram_state[channel][rank][i].next_pre))
      flag = 1;
    else
      return 0;
  }
  return flag;
}
int is_refresh_allowed_bank(int channel, int rank, int bank) {
  if (CYCLE_VAL < dram_state[channel][rank][bank].next_refresh)
    return 0;
  return 1;
}
// function to see if refresh can be allowed this cycle
int is_refresh_allowed(int channel, int rank) {
  if (command_issued_current_cycle[channel] ||
      forced_refresh_mode_on[channel][rank])
    return 0;

  for (int b = 0; b < NUM_BANKS; b++) {
    if (CYCLE_VAL < dram_state[channel][rank][b].next_refresh)
      return 0;
  }
  return 1;
}

// Function to put a rank into the low power mode
int issue_powerdown_command(int channel, int rank, command_t cmd) {
  if (command_issued_current_cycle[channel]) {
    printf("PANIC : SCHED_ERROR: Got beat. POWER_DOWN command not issuable in "
           "cycle:%lld\n",
           CYCLE_VAL);
    return 0;
  }

  // if right CMD has been used
  if ((cmd != PWR_DN_FAST_CMD) && (cmd != PWR_DN_SLOW_CMD)) {
    printf("PANIC: SCHED_ERROR : Only PWR_DN_SLOW_CMD or PWR_DN_FAST_CMD can "
           "be used to put DRAM rank to sleep\n");
    return 0;
  }
  // if the powerdown command can indeed be issued
  if (((cmd == PWR_DN_FAST_CMD) && !is_powerdown_fast_allowed(channel, rank)) ||
      ((cmd == PWR_DN_SLOW_CMD) && !is_powerdown_slow_allowed(channel, rank))) {
    printf(
        "PANIC : SCHED_ERROR: POWER_DOWN command not issuable in cycle:%lld\n",
        CYCLE_VAL);
    return 0;
  }

  for (int i = 0; i < NUM_BANKS; i++) {
    // next_powerup and refresh times
    dram_state[channel][rank][i].next_powerup =
        max(CYCLE_VAL + T_PD_MIN, dram_state[channel][rank][i].next_powerdown);
    dram_state[channel][rank][i].next_refresh =
        max(CYCLE_VAL + T_PD_MIN, dram_state[channel][rank][i].next_refresh);

    // state change
    if (dram_state[channel][rank][i].state == IDLE ||
        dram_state[channel][rank][i].state == PRECHARGING ||
        dram_state[channel][rank][i].state == REFRESHING) {
      if (cmd == PWR_DN_SLOW_CMD) {
        dram_state[channel][rank][i].state = PRECHARGE_POWER_DOWN_SLOW;
        stats_num_powerdown_slow[channel][rank]++;
      } else if (cmd == PWR_DN_FAST_CMD) {
        dram_state[channel][rank][i].state = PRECHARGE_POWER_DOWN_FAST;
        stats_num_powerdown_fast[channel][rank]++;
      }

      dram_state[channel][rank][i].active_row = -1;
    } else if (dram_state[channel][rank][i].state == ROW_ACTIVE) {
      dram_state[channel][rank][i].state = ACTIVE_POWER_DOWN;
    }
  }
  command_issued_current_cycle[channel] = 1;
  return 1;
}

// Function to power a rank up
int issue_powerup_command(int channel, int rank) {
  if (!is_powerup_allowed(channel, rank)) {
    printf("PANIC : SCHED_ERROR: POWER_UP command not issuable in cycle:%lld\n",
           CYCLE_VAL);
    return 0;
  } else {
    long long int cycle = CYCLE_VAL;
    for (int i = 0; i < NUM_BANKS; i++) {

      if (dram_state[channel][rank][i].state == PRECHARGE_POWER_DOWN_SLOW ||
          dram_state[channel][rank][i].state == PRECHARGE_POWER_DOWN_FAST) {
        dram_state[channel][rank][i].state = IDLE;
        dram_state[channel][rank][i].active_row = -1;
      } else {
        dram_state[channel][rank][i].state = ROW_ACTIVE;
      }

      if (dram_state[channel][rank][i].state == PRECHARGE_POWER_DOWN_SLOW) {
        dram_state[channel][rank][i].next_powerdown =
            max(cycle + T_XP_DLL, dram_state[channel][rank][i].next_powerdown);

        dram_state[channel][rank][i].next_pre =
            max(cycle + T_XP_DLL, dram_state[channel][rank][i].next_pre);

        dram_state[channel][rank][i].next_read =
            max(cycle + T_XP_DLL, dram_state[channel][rank][i].next_read);

        dram_state[channel][rank][i].next_write =
            max(cycle + T_XP_DLL, dram_state[channel][rank][i].next_write);

        dram_state[channel][rank][i].next_act =
            max(cycle + T_XP_DLL, dram_state[channel][rank][i].next_act);

        dram_state[channel][rank][i].next_refresh =
            max(cycle + T_XP_DLL, dram_state[channel][rank][i].next_refresh);
      } else {

        dram_state[channel][rank][i].next_powerdown =
            max(cycle + T_XP, dram_state[channel][rank][i].next_powerdown);

        dram_state[channel][rank][i].next_pre =
            max(cycle + T_XP, dram_state[channel][rank][i].next_pre);

        dram_state[channel][rank][i].next_read =
            max(cycle + T_XP, dram_state[channel][rank][i].next_read);

        dram_state[channel][rank][i].next_write =
            max(cycle + T_XP, dram_state[channel][rank][i].next_write);

        dram_state[channel][rank][i].next_act =
            max(cycle + T_XP, dram_state[channel][rank][i].next_act);

        dram_state[channel][rank][i].next_refresh =
            max(cycle + T_XP, dram_state[channel][rank][i].next_refresh);
      }
    }

    command_issued_current_cycle[channel] = 1;
    return 1;
  }
}

// Function to issue a precharge command to a specific bank
int issue_autoprecharge(int channel, int rank, int bank) {
  if (!is_autoprecharge_allowed(channel, rank, bank))
    return 0;
  else {
    long long int start_precharge = 0;

    dram_state[channel][rank][bank].active_row = -1;

    dram_state[channel][rank][bank].state = PRECHARGING;

    if (cas_issued_current_cycle[channel][rank][bank] == 1)
      start_precharge =
          max(CYCLE_VAL + T_RTP, dram_state[channel][rank][bank].next_pre);
    else
      start_precharge = max(CYCLE_VAL + T_CWD + T_DATA_TRANS + T_WR,
                            dram_state[channel][rank][bank].next_pre);

    dram_state[channel][rank][bank].next_act =
        max(start_precharge + T_RP, dram_state[channel][rank][bank].next_act);

    dram_state[channel][rank][bank].next_powerdown = max(
        start_precharge + T_RP, dram_state[channel][rank][bank].next_powerdown);

    dram_state[channel][rank][bank].next_pre =
        max(start_precharge + T_RP, dram_state[channel][rank][bank].next_pre);

    dram_state[channel][rank][bank].next_refresh = max(
        start_precharge + T_RP, dram_state[channel][rank][bank].next_refresh);

    stats_num_precharge[channel][rank][bank]++;

    // reset the cas_issued_current_cycle
    for (int r = 0; r < NUM_RANKS; r++)
      for (int b = 0; b < NUM_BANKS; b++)
        cas_issued_current_cycle[channel][r][b] = 0;

    return 1;
  }
}

// Function to issue an activate command to a specific row
int issue_activate_command(int channel, int rank, int bank, long long int row) {
  if (!is_activate_allowed(channel, rank, bank)) {
    printf("PANIC : SCHED_ERROR: ACTIVATE command not issuable in cycle:%lld\n",
           CYCLE_VAL);
    return 0;
  } else {
    long long int cycle = CYCLE_VAL;

    dram_state[channel][rank][bank].state = ROW_ACTIVE;

    dram_state[channel][rank][bank].active_row = row;

    dram_state[channel][rank][bank].next_pre =
        max((cycle + T_RAS), dram_state[channel][rank][bank].next_pre);

    dram_state[channel][rank][bank].next_refresh =
        max((cycle + T_RAS), dram_state[channel][rank][bank].next_refresh);

    dram_state[channel][rank][bank].next_read =
        max(cycle + T_RCD, dram_state[channel][rank][bank].next_read);

    dram_state[channel][rank][bank].next_write =
        max(cycle + T_RCD, dram_state[channel][rank][bank].next_write);

    dram_state[channel][rank][bank].next_act =
        max(cycle + T_RC, dram_state[channel][rank][bank].next_act);

    dram_state[channel][rank][bank].next_powerdown =
        max(cycle + T_RCD, dram_state[channel][rank][bank].next_powerdown);

    for (int i = 0; i < NUM_BANKS; i++)
      if (i != bank)
        dram_state[channel][rank][i].next_act =
            max(cycle + T_RRD, dram_state[channel][rank][i].next_act);

    record_activate(channel, rank, cycle);

    stats_num_activate[channel][rank]++;
    stats_num_activate_spec[channel][rank][bank]++;

    average_gap_between_activates[channel][rank] =
        ((average_gap_between_activates[channel][rank] *
          (stats_num_activate[channel][rank] - 1)) +
         (CYCLE_VAL - last_activate[channel][rank])) /
        stats_num_activate[channel][rank];

    last_activate[channel][rank] = CYCLE_VAL;

    command_issued_current_cycle[channel] = 1;

    return 1;
  }
}

// Function to issue a precharge command to a specific bank
int issue_precharge_command(int channel, int rank, int bank) {
  if (!is_precharge_allowed(channel, rank, bank)) {
    printf(
        "PANIC : SCHED_ERROR: PRECHARGE command not issuable in cycle:%lld\n",
        CYCLE_VAL);
    return 0;
  } else {
    dram_state[channel][rank][bank].state = PRECHARGING;

    dram_state[channel][rank][bank].active_row = -1;

    dram_state[channel][rank][bank].next_act =
        max(CYCLE_VAL + T_RP, dram_state[channel][rank][bank].next_act);

    dram_state[channel][rank][bank].next_powerdown =
        max(CYCLE_VAL + T_RP, dram_state[channel][rank][bank].next_powerdown);

    dram_state[channel][rank][bank].next_pre =
        max(CYCLE_VAL + T_RP, dram_state[channel][rank][bank].next_pre);

    dram_state[channel][rank][bank].next_refresh =
        max(CYCLE_VAL + T_RP, dram_state[channel][rank][bank].next_refresh);

    stats_num_precharge[channel][rank][bank]++;

    command_issued_current_cycle[channel] = 1;

    return 1;
  }
}

// Function to precharge a rank
int issue_all_bank_precharge_command(int channel, int rank) {
  if (!is_all_bank_precharge_allowed(channel, rank)) {
    printf("PANIC : SCHED_ERROR: ALL_BANK_PRECHARGE command not issuable in "
           "cycle:%lld\n",
           CYCLE_VAL);
    return 0;
  } else {
    for (int i = 0; i < NUM_BANKS; i++) {
      issue_precharge_command(channel, rank, i);
      command_issued_current_cycle[channel] =
          0; /* Since issue_precharge_command would have set this, we need to
                reset it. */
    }
    command_issued_current_cycle[channel] = 1;
    return 1;
  }
}

// Function to issue a refresh
int issue_refresh_command(int channel, int rank) {

  if (!is_refresh_allowed(channel, rank)) {
    printf("PANIC : SCHED_ERROR: REFRESH command not issuable in cycle:%lld\n",
           CYCLE_VAL);
    return 0;
  } else {
    num_issued_refreshes[channel][rank]++;
    long long int cycle = CYCLE_VAL;

    if (dram_state[channel][rank][0].state == PRECHARGE_POWER_DOWN_SLOW) {
      for (int b = 0; b < NUM_BANKS; b++) {
        dram_state[channel][rank][b].next_act = max(
            cycle + T_XP_DLL + T_RFC, dram_state[channel][rank][b].next_act);
        dram_state[channel][rank][b].next_pre = max(
            cycle + T_XP_DLL + T_RFC, dram_state[channel][rank][b].next_pre);
        dram_state[channel][rank][b].next_refresh =
            max(cycle + T_XP_DLL + T_RFC,
                dram_state[channel][rank][b].next_refresh);
        dram_state[channel][rank][b].next_powerdown =
            max(cycle + T_XP_DLL + T_RFC,
                dram_state[channel][rank][b].next_powerdown);
      }
    } else if (dram_state[channel][rank][0].state ==
               PRECHARGE_POWER_DOWN_FAST) {
      for (int b = 0; b < NUM_BANKS; b++) {
        dram_state[channel][rank][b].next_act =
            max(cycle + T_XP + T_RFC, dram_state[channel][rank][b].next_act);
        dram_state[channel][rank][b].next_pre =
            max(cycle + T_XP + T_RFC, dram_state[channel][rank][b].next_pre);
        dram_state[channel][rank][b].next_refresh = max(
            cycle + T_XP + T_RFC, dram_state[channel][rank][b].next_refresh);
        dram_state[channel][rank][b].next_powerdown = max(
            cycle + T_XP + T_RFC, dram_state[channel][rank][b].next_powerdown);
      }
    } else if (dram_state[channel][rank][0].state == ACTIVE_POWER_DOWN) {
      for (int b = 0; b < NUM_BANKS; b++) {
        dram_state[channel][rank][b].next_act = max(
            cycle + T_XP + T_RP + T_RFC, dram_state[channel][rank][b].next_act);
        dram_state[channel][rank][b].next_pre = max(
            cycle + T_XP + T_RP + T_RFC, dram_state[channel][rank][b].next_pre);
        dram_state[channel][rank][b].next_refresh =
            max(cycle + T_XP + T_RP + T_RFC,
                dram_state[channel][rank][b].next_refresh);
        dram_state[channel][rank][b].next_powerdown =
            max(cycle + T_XP + T_RP + T_RFC,
                dram_state[channel][rank][b].next_powerdown);
      }
    } else // rank powered up
    {
      int flag = 0;
      for (int b = 0; b < NUM_BANKS; b++) {
        if (dram_state[channel][rank][b].state == ROW_ACTIVE) {
          flag = 1;
          break;
        }
      }
      if (flag) // at least a single bank is open
      {
        for (int b = 0; b < NUM_BANKS; b++) {
          dram_state[channel][rank][b].next_act =
              max(cycle + T_RP + T_RFC, dram_state[channel][rank][b].next_act);
          dram_state[channel][rank][b].next_pre =
              max(cycle + T_RP + T_RFC, dram_state[channel][rank][b].next_pre);
          dram_state[channel][rank][b].next_refresh = max(
              cycle + T_RP + T_RFC, dram_state[channel][rank][b].next_refresh);
          dram_state[channel][rank][b].next_powerdown =
              max(cycle + T_RP + T_RFC,
                  dram_state[channel][rank][b].next_powerdown);
        }
      } else // everything precharged
      {
        for (int b = 0; b < NUM_BANKS; b++) {
          dram_state[channel][rank][b].next_act =
              max(cycle + T_RFC, dram_state[channel][rank][b].next_act);
          dram_state[channel][rank][b].next_pre =
              max(cycle + T_RFC, dram_state[channel][rank][b].next_pre);
          dram_state[channel][rank][b].next_refresh =
              max(cycle + T_RFC, dram_state[channel][rank][b].next_refresh);
          dram_state[channel][rank][b].next_powerdown =
              max(cycle + T_RFC, dram_state[channel][rank][b].next_powerdown);
        }
      }
    }
    for (int b = 0; b < NUM_BANKS; b++) {
      dram_state[channel][rank][b].active_row = -1;
      dram_state[channel][rank][b].state = REFRESHING;
    }
    command_issued_current_cycle[channel] = 1;
    return 1;
  }
}

void issue_forced_refresh_commands(int channel, int rank) {
  for (int b = 0; b < NUM_BANKS; b++) {

    dram_state[channel][rank][b].state = REFRESHING;
    dram_state[channel][rank][b].active_row = -1;

    dram_state[channel][rank][b].next_act =
        next_refresh_completion_deadline[channel][rank];
    dram_state[channel][rank][b].next_pre =
        next_refresh_completion_deadline[channel][rank];
    dram_state[channel][rank][b].next_refresh =
        next_refresh_completion_deadline[channel][rank];
    dram_state[channel][rank][b].next_powerdown =
        next_refresh_completion_deadline[channel][rank];
  }
}

void gather_stats(int channel) {
  for (int i = 0; i < NUM_RANKS; i++) {

    if (dram_state[channel][i][0].state == PRECHARGE_POWER_DOWN_SLOW)
      stats_time_spent_in_precharge_power_down_slow[channel][i] +=
          PROCESSOR_CLK_MULTIPLIER;
    else if (dram_state[channel][i][0].state == PRECHARGE_POWER_DOWN_FAST)
      stats_time_spent_in_precharge_power_down_fast[channel][i] +=
          PROCESSOR_CLK_MULTIPLIER;
    else if (dram_state[channel][i][0].state == ACTIVE_POWER_DOWN)
      stats_time_spent_in_active_power_down[channel][i] +=
          PROCESSOR_CLK_MULTIPLIER;
    else {
      for (int b = 0; b < NUM_BANKS; b++) {
        if (dram_state[channel][i][b].state == ROW_ACTIVE) {
          stats_time_spent_in_active_standby[channel][i] +=
              PROCESSOR_CLK_MULTIPLIER;
          break;
        }
      }
      stats_time_spent_in_power_up[channel][i] += PROCESSOR_CLK_MULTIPLIER;
    }
  }
}

void print_stats(int channel) {
  long long int activates_for_reads = 0;
  long long int activates_for_spec = 0;
  long long int activates_for_writes = 0;
  long long int read_cmds = 0;
  long long int write_cmds = 0;

  for (int c = 0; c < NUM_CHANNELS; c++) {
    activates_for_writes = 0;
    activates_for_reads = 0;
    activates_for_spec = 0;
    read_cmds = 0;
    write_cmds = 0;
    for (int r = 0; r < NUM_RANKS; r++) {
      for (int b = 0; b < NUM_BANKS; b++) {
        activates_for_writes += stats_num_activate_write[c][r][b];
        activates_for_reads += stats_num_activate_read[c][r][b];
        activates_for_spec += stats_num_activate_spec[c][r][b];
        read_cmds += stats_num_read[c][r][b];
        write_cmds += stats_num_write[c][r][b];
      }
    }

    printf("-------- Channel %d Stats-----------\n", c);
    printf("Total Reads Serviced :          %-7lld\n",
           stats_reads_completed[c]);
    printf("Total Writes Serviced :         %-7lld\n",
           stats_writes_completed[c]);
    printf("Average Read Latency :          %7.5f\n",
           (double)stats_average_read_latency[c]);
    printf("Average Read Queue Latency :    %7.5f\n",
           (double)stats_average_read_queue_latency[c]);
    printf("Average Write Latency :         %7.5f\n",
           (double)stats_average_write_latency[c]);
    printf("Average Write Queue Latency :   %7.5f\n",
           (double)stats_average_write_queue_latency[c]);
    printf("Read Page Hit Rate :            %7.5f\n",
           ((double)(read_cmds - activates_for_reads - activates_for_spec) /
            read_cmds));
    printf("Write Page Hit Rate :           %7.5f\n",
           ((double)(write_cmds - activates_for_writes) / write_cmds));
    printf("------------------------------------\n");
  }
}

void update_issuable_commands(int channel) {
  for (int rank = 0; rank < NUM_RANKS; rank++) {
    for (int bank = 0; bank < NUM_BANKS; bank++)
      cmd_precharge_issuable[channel][rank][bank] =
          is_precharge_allowed(channel, rank, bank);

    cmd_all_bank_precharge_issuable[channel][rank] =
        is_all_bank_precharge_allowed(channel, rank);

    cmd_powerdown_fast_issuable[channel][rank] =
        is_powerdown_fast_allowed(channel, rank);

    cmd_powerdown_slow_issuable[channel][rank] =
        is_powerdown_slow_allowed(channel, rank);

    cmd_refresh_issuable[channel][rank] = is_refresh_allowed(channel, rank);

    cmd_powerup_issuable[channel][rank] = is_powerup_allowed(channel, rank);
  }
}

// function that updates the dram state and schedules auto-refresh if
// necessary. This is called every DRAM cycle
void update_memory() {
  for (int channel = 0; channel < NUM_CHANNELS; channel++) {
    // make every channel ready to receive a new command
    command_issued_current_cycle[channel] = 0;
    for (int rank = 0; rank < NUM_RANKS; rank++) {
      // reset variable
      for (int bank = 0; bank < NUM_BANKS; bank++)
        cas_issued_current_cycle[channel][rank][bank] = 0;

      // clean out the activate record for
      // CYCLE_VAL - T_FAW
      flush_activate_record(channel, rank, CYCLE_VAL);

      // if we are at the refresh completion
      // deadline
      if (CYCLE_VAL == next_refresh_completion_deadline[channel][rank]) {
        // calculate the next
        // refresh_issue_deadline
        num_issued_refreshes[channel][rank] = 0;
        last_refresh_completion_deadline[channel][rank] = CYCLE_VAL;
        next_refresh_completion_deadline[channel][rank] = CYCLE_VAL + 8 * T_REFI;
        refresh_issue_deadline[channel][rank] = next_refresh_completion_deadline[channel][rank] - T_RP - 8 * T_RFC;
        forced_refresh_mode_on[channel][rank] = 0;
        issued_forced_refresh_commands[channel][rank] = 0;
      } else if ((CYCLE_VAL == refresh_issue_deadline[channel][rank]) &&
                 (num_issued_refreshes[channel][rank] < 8)) {
        // refresh_issue_deadline has been
        // reached. Do the auto-refreshes
        forced_refresh_mode_on[channel][rank] = 1;
        issue_forced_refresh_commands(channel, rank);
      } else if (CYCLE_VAL < refresh_issue_deadline[channel][rank]) {
        // update the refresh_issue deadline
        refresh_issue_deadline[channel][rank] =
            next_refresh_completion_deadline[channel][rank] - T_RP -
            (8 - num_issued_refreshes[channel][rank]) * T_RFC;
      }
    }

    // update the variables corresponding to the non-queue
    // variables
    update_issuable_commands(channel);

    // update the request cmds in the queues
    update_read_queue_commands(channel);

    update_write_queue_commands(channel);

    // remove finished requests
    clean_queues(channel);
  }
}

// void_update_memory(){}

//------------------------------------------------------------
// Calculate Power: It calculates and returns average power used by every Rank
// on Every Channel during the course of the simulation Units : Time- ns;
// Current mA; Voltage V; Power mW;
//------------------------------------------------------------

float calculate_power(int channel, int rank, int print_stats_type,
                      int chips_per_rank) {
  /*
  Power is calculated using the equations from Technical Note "TN-41-01:
  Calculating Memory System Power for DDR" The current values IDD* are taken
  from the data sheets. These are average current values that the chip will draw
  when certain actions occur as frequently as possible. i.e., the worst case
  power consumption Eg: when ACTs happen every tRC pds_<component> is the power
  calculated by directly using the current values from the data sheet. 'pds'
  stands for PowerDataSheet. This will the power drawn by the chip when
  operating under the activity that is assumed in the data sheet. This mostly
  represents the worst case power These pds_<*> components need to be derated in
  accordance with the activity that is observed. Eg: If ACTs occur slower than
  every tRC, then pds_act will be derated to give "psch_act" (SCHeduled Power
  consumed by Activate)
  */

  /*------------------------------------------------------------
  // total_power is the sum of of 13 components listed below
  // Note: CKE is the ClocK Enable to every chip.
  // Note: Even though the reads and write are to a different rank on the same
  channel, the Pull-Up and the Pull-Down resistors continue
  // 		to burn some power. psch_termWoth and psch_termWoth stand for
  the power dissipated in the rank in question when the reads and
  // 		writes are to other ranks on the channel

          psch_act 						-> Power
  dissipated by activating a row psch_act_pdn 				-> Power
  dissipated when CKE is low (disabled) and all banks are precharged
    psch_act_stby 			-> Power dissipated when CKE is high
  (enabled) and at least one back is active (row is open) psch_pre_pdn_fast ->
  Power dissipated when CKE is low (disabled) and all banks are precharged and
  chip is in fast power down
    psch_pre_pdn_slow  	-> Power dissipated when CKE is low (disabled) and all
  banks are precharged and chip is in fast slow  down
    psch_pre_stby 			-> Power dissipated when CKE is high
  (enabled) and at least one back is active (row is open) psch_termWoth
  -> Power dissipated when a Write termiantes at the other set of chips.
    psch_termRoth 			-> Power dissipated when a Read
  termiantes
  at the other set of chips psch_termW 					-> Power
  dissipated when a Write termiantes at the set of chips in question psch_dq
  -> Power dissipated when a Read  termiantes at the set of chips in question
  (Data Pins on the chip are called DQ) psch_ref
  -> Power dissipated during Refresh
    psch_rd 						-> Power dissipated during
  a Read (does ot include power to open a row) psch_wr
  -> Power dissipated during a Write (does ot include power to open a row)

  ------------------------------------------------------------*/

  float pds_act;
  float pds_act_pdn;
  float pds_act_stby;
  float pds_pre_pdn_fast;
  float pds_pre_pdn_slow;
  float pds_pre_stby;
  float pds_wr;
  float pds_rd;
  float pds_ref;
  float pds_dq;
  float pds_termW;
  float pds_termRoth;
  float pds_termWoth;

  float psch_act;
  float psch_pre_pdn_slow;
  float psch_pre_pdn_fast;
  float psch_act_pdn;
  float psch_act_stby;
  float psch_pre_stby;
  float psch_rd;
  float psch_wr;
  float psch_ref;
  float psch_dq;
  float psch_termW;
  float psch_termRoth;
  float psch_termWoth;

  float total_chip_power;
  float total_rank_power;

  long long int writes = 0, reads = 0;

  static int print_total_cycles = 0;

  /*----------------------------------------------------
//Calculating DataSheet Power
  ----------------------------------------------------*/

  pds_act = (IDD0 - (IDD3N * T_RAS + IDD2N * (T_RC - T_RAS)) / T_RC) * VDD;

  pds_pre_pdn_slow = IDD2P0 * VDD;

  pds_pre_pdn_fast = IDD2P1 * VDD;

  pds_act_pdn = IDD3P * VDD;

  pds_pre_stby = IDD2N * VDD;
  pds_act_stby = IDD3N * VDD;

  pds_wr = (IDD4W - IDD3N) * VDD;

  pds_rd = (IDD4R - IDD3N) * VDD;

  pds_ref = (IDD5 - IDD3N) * VDD;

  /*----------------------------------------------------
  //On Die Termination (ODT) Power:
  //Values obtained from Micron Technical Note
  //This is dependent on the termination configuration of the simulated
  configuration
  //our simulator uses the same config as that used in the Tech Note
  ----------------------------------------------------*/
  pds_dq = 3.2 * 10;

  pds_termW = 0;

  pds_termRoth = 24.9 * 10;

  pds_termWoth = 20.8 * 11;

  /*----------------------------------------------------
//Derating worst case power to represent system activity
  ----------------------------------------------------*/

  // average_gap_between_activates was initialised to 0. So if it is still
  // 0, then no ACTs have happened to this rank.
  // Hence activate-power is also 0
  if (average_gap_between_activates[channel][rank] == 0) {
    psch_act = 0;
  } else {
    psch_act = pds_act * T_RC / (average_gap_between_activates[channel][rank]);
  }

  psch_act_pdn = pds_act_pdn *
                 ((double)stats_time_spent_in_active_power_down[channel][rank] /
                  CYCLE_VAL);
  psch_pre_pdn_slow =
      pds_pre_pdn_slow *
      ((double)stats_time_spent_in_precharge_power_down_slow[channel][rank] /
       CYCLE_VAL);
  psch_pre_pdn_fast =
      pds_pre_pdn_fast *
      ((double)stats_time_spent_in_precharge_power_down_fast[channel][rank] /
       CYCLE_VAL);

  psch_act_stby =
      pds_act_stby *
      ((double)stats_time_spent_in_active_standby[channel][rank] / CYCLE_VAL);

  /*----------------------------------------------------
//pds_pre_stby assumes that the system is powered up and every
  //row has been precharged during every cycle
  // In reality, the chip could have been in a power-down mode
  //or a row could have been active. The time spent in these modes
  //should be deducted from total time
  ----------------------------------------------------*/
  psch_pre_stby =
      pds_pre_stby *
      ((double)(CYCLE_VAL - stats_time_spent_in_active_standby[channel][rank] -
                stats_time_spent_in_precharge_power_down_slow[channel][rank] -
                stats_time_spent_in_precharge_power_down_fast[channel][rank] -
                stats_time_spent_in_active_power_down[channel][rank])) /
      CYCLE_VAL;

  /*----------------------------------------------------
//Calculate Total Reads ans Writes performed in the system
  ----------------------------------------------------*/

  for (int i = 0; i < NUM_BANKS; i++) {
    writes += stats_num_write[channel][rank][i];
    reads += stats_num_read[channel][rank][i];
  }

  /*----------------------------------------------------
// pds<rd/wr> assumes that there is rd/wr happening every cycle
  // T_DATA_TRANS is the number of cycles it takes for one rd/wr
  ----------------------------------------------------*/
  psch_wr = pds_wr * (writes * T_DATA_TRANS) / CYCLE_VAL;

  psch_rd = pds_rd * (reads * T_DATA_TRANS) / CYCLE_VAL;

  /*----------------------------------------------------
//pds_ref assumes that there is always a refresh happening.
  //in reality, refresh consumes only T_RFC out of every t_REFI
  ----------------------------------------------------*/
  psch_ref = pds_ref * T_RFC / T_REFI;

  psch_dq = pds_dq * (reads * T_DATA_TRANS) / CYCLE_VAL;

  psch_termW = pds_termW * (writes * T_DATA_TRANS) / CYCLE_VAL;

  psch_termRoth =
      pds_termRoth *
      ((double)
           stats_time_spent_terminating_reads_from_other_ranks[channel][rank] /
       CYCLE_VAL);
  psch_termWoth =
      pds_termWoth *
      ((double)
           stats_time_spent_terminating_writes_to_other_ranks[channel][rank] /
       CYCLE_VAL);

  total_chip_power = psch_act + psch_termWoth + psch_termRoth + psch_termW +
                     psch_dq + psch_ref + psch_rd + psch_wr + psch_pre_stby +
                     psch_act_stby + psch_pre_pdn_fast + psch_pre_pdn_slow +
                     psch_act_pdn;
  total_rank_power = total_chip_power * chips_per_rank;

  double time_in_pre_stby =
      (((double)(CYCLE_VAL - stats_time_spent_in_active_standby[channel][rank] -
                 stats_time_spent_in_precharge_power_down_slow[channel][rank] -
                 stats_time_spent_in_precharge_power_down_fast[channel][rank] -
                 stats_time_spent_in_active_power_down[channel][rank])) /
       CYCLE_VAL);

  if (print_total_cycles == 0) {

    printf("\n#-----------------------------Simulated Cycles "
           "Break-Up-------------------------------------------\n");
    printf("Note:  1.(Read Cycles + Write Cycles + Read Other + Write Other) "
           "should add up to %% cycles during which\n");
    printf("          the channel is busy. This should be the same for all "
           "Ranks on a Channel\n");
    printf("       2.(PRE_PDN_FAST + PRE_PDN_SLOW + ACT_PDN + ACT_STBY + "
           "PRE_STBY) should add up to 100%%\n");
    printf("       3.Power Down means Clock Enable, CKE = 0. In Standby mode, "
           "CKE = 1\n");
    printf("#------------------------------------------------------------------"
           "-------------------------------\n");
    printf("Total Simulation Cycles                      %11lld\n", CYCLE_VAL);
    printf(
        "---------------------------------------------------------------\n\n");

    print_total_cycles = 1;
  }

  if (print_stats_type == 0) {
    /*
    printf ("%3d %6d %13.2f %13.2f %13.2f %13.2f %15.2f %15.2f %15.2f %13.2f
    %11.2f \n",\
                    channel,\
                    rank,\
                    (double)reads/CYCLE_VAL,\
                    (double)writes/CYCLE_VAL,\
                    psch_termRoth,\
                    psch_termWoth,\
                    ((double)stats_time_spent_in_precharge_power_down_fast[channel][rank]/CYCLE_VAL),\
                    ((double)stats_time_spent_in_precharge_power_down_slow[channel][rank]/CYCLE_VAL),\
                    ((double)stats_time_spent_in_active_power_down[channel][rank]/CYCLE_VAL),\
                    ((double)stats_time_spent_in_active_standby[channel][rank]/CYCLE_VAL),\
                    (((double)(CYCLE_VAL -
    stats_time_spent_in_active_standby[channel][rank]-
    stats_time_spent_in_precharge_power_down_slow[channel][rank] -
    stats_time_spent_in_precharge_power_down_fast[channel][rank] -
    stats_time_spent_in_active_power_down[channel][rank]))/CYCLE_VAL)
                    );
*/
    printf("Channel %d Rank %d Read Cycles(%%)           %9.2f # %% cycles the "
           "Rank performed a Read\n",
           channel, rank, (double)reads * T_DATA_TRANS / CYCLE_VAL);
    printf("Channel %d Rank %d Write Cycles(%%)          %9.2f # %% cycles the "
           "Rank performed a Write\n",
           channel, rank, (double)writes * T_DATA_TRANS / CYCLE_VAL);
    printf("Channel %d Rank %d Read Other(%%)            %9.2f # %% cycles "
           "other Ranks on the channel performed a Read\n",
           channel, rank,
           ((double)stats_time_spent_terminating_reads_from_other_ranks[channel]
                                                                       [rank] /
            CYCLE_VAL));
    printf(
        "Channel %d Rank %d Write Other(%%)           %9.2f # %% cycles other "
        "Ranks on the channel performed a Write\n",
        channel, rank,
        ((double)
             stats_time_spent_terminating_writes_to_other_ranks[channel][rank] /
         CYCLE_VAL));
    printf(
        "Channel %d Rank %d PRE_PDN_FAST(%%)          %9.2f # %% cycles the "
        "Rank was in Fast Power Down and all Banks were Precharged\n",
        channel, rank,
        ((double)stats_time_spent_in_precharge_power_down_fast[channel][rank] /
         CYCLE_VAL));
    printf(
        "Channel %d Rank %d PRE_PDN_SLOW(%%)          %9.2f # %% cycles the "
        "Rank was in Slow Power Down and all Banks were Precharged\n",
        channel, rank,
        ((double)stats_time_spent_in_precharge_power_down_slow[channel][rank] /
         CYCLE_VAL));
    printf("Channel %d Rank %d ACT_PDN(%%)               %9.2f # %% cycles the "
           "Rank was in Active Power Down and atleast one Bank was Active\n",
           channel, rank,
           ((double)stats_time_spent_in_active_power_down[channel][rank] /
            CYCLE_VAL));
    printf("Channel %d Rank %d ACT_STBY(%%)              %9.2f # %% cycles the "
           "Rank was in Standby and atleast one bank was Active\n",
           channel, rank,
           ((double)stats_time_spent_in_active_standby[channel][rank] /
            CYCLE_VAL));
    printf("Channel %d Rank %d PRE_STBY(%%)              %9.2f # %% cycles the "
           "Rank was in Standby and all Banks were Precharged\n",
           channel, rank, time_in_pre_stby);
    printf(
        "---------------------------------------------------------------\n\n");

  } else if (print_stats_type == 1) {
    /*----------------------------------------------------
    // Total Power is the sum total of all the components calculated above
    ----------------------------------------------------*/

    printf("Channel %d Rank %d Background(mw)          %9.2f # depends only on "
           "Power Down time and time all banks were precharged\n",
           channel, rank,
           psch_act_pdn + psch_act_stby + psch_pre_pdn_slow +
               psch_pre_pdn_fast + psch_pre_stby);
    printf("Channel %d Rank %d Act(mW)                 %9.2f # power spend "
           "bringing data to the row buffer\n",
           channel, rank, psch_act);
    printf("Channel %d Rank %d Read(mW)                %9.2f # power spent "
           "doing a Read  after the Row Buffer is open\n",
           channel, rank, psch_rd);
    printf("Channel %d Rank %d Write(mW)               %9.2f # power spent "
           "doing a Write after the Row Buffer is open\n",
           channel, rank, psch_wr);
    printf("Channel %d Rank %d Read Terminate(mW)      %9.2f # power "
           "dissipated in ODT resistors during Read\n",
           channel, rank, psch_dq);
    printf("Channel %d Rank %d Write Terminate(mW)     %9.2f # power "
           "dissipated in ODT resistors during Write\n",
           channel, rank, psch_termW);
    printf("Channel %d Rank %d termRoth(mW)            %9.2f # power "
           "dissipated in ODT resistors during Reads  in other ranks\n",
           channel, rank, psch_termRoth);
    printf("Channel %d Rank %d termWoth(mW)            %9.2f # power "
           "dissipated in ODT resistors during Writes in other ranks\n",
           channel, rank, psch_termWoth);
    printf("Channel %d Rank %d Refresh(mW)             %9.2f # depends on "
           "frequency of Refresh (tREFI)\n",
           channel, rank, psch_ref);
    printf("---------------------------------------------------------------\n");
    printf("Channel %d Rank %d Total Rank Power(mW)    %9.2f # (Sum of above "
           "components)*(num chips in each Rank)\n",
           channel, rank, total_rank_power);
    printf(
        "---------------------------------------------------------------\n\n");
  } else {
    printf("PANIC: FN_CALL_ERROR: In calculate_power(), print_stats_type can "
           "only be 1 or 0\n");
    assert(-1);
  }

  return total_rank_power;
}