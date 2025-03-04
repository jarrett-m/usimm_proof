#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "VAT.h"
#include "configfile.h"
#include "memory_controller.h"
#include "meta_cache.h"
#include "params.h"
#include "processor.h"
#include "scheduler.h"
#include "uthash.h"
#include "utils.h"
#include "utlist.h"
#include <time.h>

#define MAXTRACELINESIZE 64
long long int BIGNUM = 1000000;

int expt_done = 0;

long long int CYCLE_VAL = 0;

long long int get_current_cycle() { return CYCLE_VAL; }

struct robstructure *ROB;

FILE **tif; /* The handles to the trace input files. */
FILE *config_file;
FILE *vi_file;

int *prefixtable;
// Moved the following to memory_controller.h so that they are visible
// from the scheduler.
// long long int *committed;
// long long int *fetched;
long long int *time_done;
long long int total_time_done;
float core_power = 0;

int main(int argc, char *argv[]) {

  printf("---------------------------------------------\n");
  printf("-- USIMM: the Utah SImulated Memory Module --\n");
  printf("--              Version: 1.3               --\n");
  printf("---------------------------------------------\n");

  int numc = 0;
  int num_ret = 0;
  int num_fetch = 0;
  int num_done = 0;
  int numch = 0;
  int writeqfull = 0;
  int fnstart;
  int currMTapp;
  long long int maxtd;
  int maxcr;
  // int pow_of_2_cores;
  char newstr[MAXTRACELINESIZE];
  int *nonmemops;
  char *opertype;
  long long int *addr;

  long long int *instrpc;
  int chips_per_rank = -1;

  /* Initialization code. */
  printf("Initializing.\n");

  if (argc < 3) {
    printf("Need at least one input configuration file and one trace file as "
           "argument.  Quitting.\n");
    return -3;
  }

  config_file = fopen(argv[1], "r");
  if (!config_file) {
    printf("Missing system configuration file.  Quitting. \n");
    return -4;
  }

  NUMCORES = argc - 2;

  printf("TRACE_CAPACITY = %lld NUMCORES = %d  \n", TRACE_CAPACITY, NUMCORES);

  ROB = (struct robstructure *)malloc(sizeof(struct robstructure) * NUMCORES);

  tif = (FILE **)malloc(sizeof(FILE *) * NUMCORES);
  committed = (long long int *)malloc(sizeof(long long int) * NUMCORES);
  fetched = (long long int *)malloc(sizeof(long long int) * NUMCORES);
  time_done = (long long int *)malloc(sizeof(long long int) * NUMCORES);
  nonmemops = (int *)malloc(sizeof(int) * NUMCORES);
  opertype = (char *)malloc(sizeof(char) * NUMCORES);
  addr = (long long int *)malloc(sizeof(long long int) * NUMCORES);

  instrpc = (long long int *)malloc(sizeof(long long int) * NUMCORES);
  prefixtable = (int *)malloc(sizeof(int) * NUMCORES);
  currMTapp = -1;
  for (numc = 0; numc < NUMCORES; numc++) {
    tif[numc] = fopen(argv[numc + 2], "r");
    if (!tif[numc]) {
      printf("Missing input trace file %d.  Quitting. \n", numc);
      return -5;
    }

    /* The addresses in each trace are given a prefix that equals
       their core ID.  If the input trace starts with "MT", it is
       assumed to be part of a multi-threaded app.  The addresses
       from this trace file are given a prefix that equals that of
       the last seen input trace file that starts with "MT0".  For
       example, the following is an acceptable set of inputs for
       multi-threaded apps CG (4 threads) and LU (2 threads):
       usimm 1channel.cfg MT0CG MT1CG MT2CG MT3CG MT0LU MT1LU */
    prefixtable[numc] = numc;

    /* Find the start of the filename.  It's after the last "/". */
    for (fnstart = strlen(argv[numc + 2]); fnstart >= 0; fnstart--) {
      if (argv[numc + 2][fnstart] == '/') {
        break;
      }
    }
    fnstart++; /* fnstart is either the letter after the last / or the 0th
                  letter. */

    if ((strlen(argv[numc + 2]) - fnstart) > 2) {
      if ((argv[numc + 2][fnstart + 0] == 'M') &&
          (argv[numc + 2][fnstart + 1] == 'T')) {
        if (argv[numc + 2][fnstart + 2] == '0') {
          currMTapp = numc;
        } else {
          if (currMTapp < 0) {
            printf("Poor set of input parameters.  Input file %s starts with "
                   "\"MT\", but there is no preceding input file starting with "
                   "\"MT0\".  Quitting.\n",
                   argv[numc + 2]);
            return -6;
          } else
            prefixtable[numc] = currMTapp;
        }
      }
    }
    printf("Core %d: Input trace file %s : Addresses will have prefix %d\n",
           numc, argv[numc + 2], prefixtable[numc]);

    committed[numc] = 0;
    fetched[numc] = 0;
    time_done[numc] = 0;
    ROB[numc].head = 0;
    ROB[numc].tail = 0;
    ROB[numc].length = 0;
    ROB[numc].inflight = 0;
    ROB[numc].tracedone = 0;
    ROB[numc].length = 0;
  }

  read_config_file(config_file);

  /* Find the appropriate .vi file to read*/
  /*if (NUM_CHANNELS == 1 && NUMCORES == 1) {
          vi_file = fopen("input/1Gb_x4.vi", "r");
          chips_per_rank= 16;
          printf("Reading vi file: 1Gb_x4.vi\t\n%d Chips per
  Rank\n",chips_per_rank); } else if (NUM_CHANNELS == 1 && NUMCORES == 2) {
          vi_file = fopen("input/2Gb_x4.vi", "r");
          chips_per_rank= 16;
          printf("Reading vi file: 2Gb_x4.vi\t\n%d Chips per
  Rank\n",chips_per_rank); } else if (NUM_CHANNELS == 1 && (NUMCORES > 2) &&
  (NUMCORES <= 4)) { vi_file = fopen("input/4Gb_x4.vi", "r"); chips_per_rank=
  16; printf("Reading vi file: 4Gb_x4.vi\t\n%d Chips per
  Rank\n",chips_per_rank); } else if (NUM_CHANNELS == 4 && NUMCORES == 1) {
          vi_file = fopen("input/1Gb_x16.vi", "r");
          chips_per_rank= 4;
          printf("Reading vi file: 1Gb_x16.vi\t\n%d Chips per
  Rank\n",chips_per_rank); } else if (NUM_CHANNELS == 4 && NUMCORES == 2) {
          vi_file = fopen("input/1Gb_x8.vi", "r");
          chips_per_rank= 8;
          printf("Reading vi file: 1Gb_x8.vi\t\n%d Chips per
  Rank\n",chips_per_rank); } else if (NUM_CHANNELS == 4 && (NUMCORES > 2) &&
  (NUMCORES <= 4)) { vi_file = fopen("input/2Gb_x8.vi", "r"); chips_per_rank= 8;
          printf("Reading vi file: 2Gb_x8.vi\t\n%d Chips per
  Rank\n",chips_per_rank); } else if (NUM_CHANNELS == 4 && (NUMCORES > 4) &&
  (NUMCORES <= 8)) { vi_file = fopen("input/4Gb_x8.vi", "r"); chips_per_rank= 8;
          printf("Reading vi file: 4Gb_x8.vi\t\n%d Chips per
  Rank\n",chips_per_rank); } else if (NUM_CHANNELS == 4 && (NUMCORES > 8) &&
  (NUMCORES <= 16)) { vi_file = fopen("input/4Gb_x4.vi", "r"); chips_per_rank=
  16; printf("Reading vi file: 4Gb_x4.vi\t\n%d Chips per
  Rank\n",chips_per_rank); } else { printf ("PANIC:: Channel - Core
  configuration not supported\n"); assert (-1);
  }

  if (!vi_file) {
    printf("Missing DRAM chip parameter file.  Quitting. \n");
    return -5;
  } */

  vi_file = fopen(
      "/uusoc/scratch/res/arch/students/meysam/input/usimminput/1Gb_x1.vi",
      "r");
  chips_per_rank = 16;
  printf("Reading vi file: 1Gb_x1.vi\t\n%d Chips per Rank\n", chips_per_rank);

  /* int channelBitWidth = log_base2(NUM_CHANNELS);
 int rankBitWidth = log_base2(NUM_RANKS);
 int bankBitWidth = log_base2(NUM_BANKS);
 int rowBitWidth = log_base2(NUM_ROWS);
 int colBitWidth = log_base2(NUM_COLUMNS);*/
  // int byteOffsetWidth = log_base2(CACHE_LINE_SIZE);

  assert((log_base2(NUM_CHANNELS) + log_base2(NUM_RANKS) +
          log_base2(NUM_BANKS) + log_base2(NUM_ROWS) + log_base2(NUM_COLUMNS) +
          log_base2(CACHE_LINE_SIZE)) == ADDRESS_BITS);
  /* Increase the address space and rows per bank depending on the number of
   * input traces. */
  /// USIMM CHANGE for IV UCIV
  ///  there is a memory which is touched by trace (TRACE_CAPACITY) and usually
  ///  it is 4G. this is the amount of data trace is touching but since there
  ///  are MAC, counter, and hashes the total memory size which is being
  ///  addressed by memory controller is twice bigger and it would be 8G for 4G
  ///  data. This data is for all cores. Unlike the traditional USIMM, the total
  ///  memory is not multiplied of size of memory (say 4G) to the number of
  ///  cores. Instead, all cores are accessing the original size of memory (say
  ///  4G instead of 4 * 8 G). As a result, NUM_ROW is not NUM_ROW * NUM_CORE
  ///  any more.
  /// ADDRESS_BITS = ADDRESS_BITS + log_base2(NUMCORES);
  ///  if (NUMCORES == 1) {
  ///    pow_of_2_cores = 1;
  ///  }
  ///  else {
  ///  pow_of_2_cores = 1 << ((int)log_base2(NUMCORES-1) + 1);
  ///  }
  /// NUM_ROWS = NUM_ROWS * pow_of_2_cores;

  //   read_config_file(vi_file);
  print_params();

  for (int i = 0; i < NUMCORES; i++) {
    ROB[i].comptime = (long long int *)malloc(sizeof(long long int) * ROBSIZE);
    ROB[i].mem_address =
        (long long int *)malloc(sizeof(long long int) * ROBSIZE);
    ROB[i].instrpc = (long long int *)malloc(sizeof(long long int) * ROBSIZE);
    ROB[i].optype = (int *)malloc(sizeof(int) * ROBSIZE);
  }

  init_memory_controller_vars();
  init_scheduler_vars();
  /* Done initializing. */
  // rewind the files to seek the begining of the files.
  for (numc = 0; numc < NUMCORES; numc++) {
    fclose(tif[numc]);
    tif[numc] = fopen(argv[numc + 2], "r");
  }
  // while (num_line_trace < NUM_INST)
  //{
  // traces_similarity[num_line_trace].x = 0;
  // num_line_trace++; }

  /* Must start by reading one line of each trace file. */
  for (numc = 0; numc < NUMCORES; numc++) {
    if (fgets(newstr, MAXTRACELINESIZE, tif[numc])) {
      if (sscanf(newstr, "%d %c", &nonmemops[numc], &opertype[numc]) > 0) {
        if (opertype[numc] == 'R') {
          // if (sscanf(newstr,"%d %c %Lx
          // %d",&nonmemops[numc],&opertype[numc],&addr[numc],&dirt[numc]) != 4)
          if (sscanf(newstr, "%d %c %Lx", &nonmemops[numc], &opertype[numc],
                     &addr[numc]) != 3) {
            printf("Panic6.  Poor trace format.\n");
            return -4;
          } else {
            instrpc[numc] = 2640;
            // printf ("dirt[%d] = %d \n", numc, dirt[numc]);
          }

        } else {
          if (opertype[numc] == 'W') {
            if (sscanf(newstr, "%d %c %Lx", &nonmemops[numc], &opertype[numc],
                       &addr[numc]) != 3) {
              printf("Panic7.  Poor trace format.\n");
              return -3;
            }
          } else {
            printf("Panic8.  Poor trace format.\n");
            return -2;
          }
        }
        addr[numc] = ((addr[numc] >> 6) << 6);
        addr[numc] =
            (addr[numc] & (((long long int)0x1 << (ADDRESS_BITS - 1)) - 1));
        // addr[numc] = (addr[numc] & (((long long int)0x1<<
        // (ADDRESS_BITS-1-log_base2(NUMCORES)))-1))+ (long long int)((long long
        //int)prefixtable[numc] << (ADDRESS_BITS-1-log_base2(NUMCORES)));

      } else {
        printf("Panic9.  Poor trace format.\n");
        return -1;
      }
    } else {
      if (ROB[numc].inflight == 0) {
        num_done++;
        if (!time_done[numc])
          time_done[numc] = 1;
      }
      ROB[numc].tracedone = 1;
    }
  }

  printf("Starting simulation.\n");

  while (!expt_done) {
    /* For each core, retire instructions if they have finished. */
    for (numc = 0; numc < NUMCORES; numc++) {
      num_ret = 0;
      while ((num_ret < MAX_RETIRE) && ROB[numc].inflight) {
        /* Keep retiring until retire width is consumed or ROB is empty. */
        if (ROB[numc].comptime[ROB[numc].head] < CYCLE_VAL) {
          /* Keep retiring instructions if they are done. */
          ROB[numc].head = (ROB[numc].head + 1) % ROBSIZE;
          ROB[numc].inflight--;
          if (ROB[numc].optype[ROB[numc].head] == 'R')
            ROB[numc].length--;
          committed[numc]++;
          num_ret++;
        } else /* Instruction not complete.  Stop retirement for this core. */
          break;
      } /* End of while loop that is retiring instruction for one core. */
    }   /* End of for loop that is retiring instructions for all cores. */

    if (CYCLE_VAL % PROCESSOR_CLK_MULTIPLIER == 0) {
      /* Execute function to find ready instructions. */

      update_memory();

      /* Execute user-provided function to select ready instructions for issue.
       */
      /* Based on this selection, update DRAM data structures and set
         instruction completion times. */
      for (int c = 0; c < NUM_CHANNELS; c++) {
        schedule(c);
        gather_stats(c);
      }
    }

    /* For each core, bring in new instructions from the trace file to
       fill up the ROB. */
    num_done = 0;
    writeqfull = 0;
    for (int c = 0; c < NUM_CHANNELS; c++) {
      if (write_queue_length[c] == WQ_CAPACITY) {
        writeqfull = 1;
        break;
      }
    }

    for (numc = 0; numc < NUMCORES; numc++) {
      if ((!ROB[numc].tracedone) &&
          (num_instructions[numc] < (NUM_INST))) { 
        /* Try to fetch if EOF has not been encountered. */
        num_fetch = 0;
        while ((num_fetch < MAX_FETCH) && (ROB[numc].inflight != ROBSIZE) &&
               (!writeqfull)) {
          /* Keep fetching until fetch width or ROB capacity or WriteQ are fully
           * consumed. */
          /* Read the corresponding trace file and populate the tail of the ROB
           * data structure. */
          /* If Memop, then populate read/write queue.  Set up completion time.
           */
          if (nonmemops[numc]) { /* Have some non-memory-ops to consume. */
            ROB[numc].optype[ROB[numc].tail] = 'N';
            ROB[numc].comptime[ROB[numc].tail] = CYCLE_VAL + PIPELINEDEPTH;
            nonmemops[numc]--;
            ROB[numc].tail = (ROB[numc].tail + 1) % ROBSIZE;
            ROB[numc].inflight++;
            fetched[numc]++;
            num_fetch++;
          } else {
            if (opertype[numc] == 'R') {
              num_instructions[numc]++;
              ROB[numc].length++;

              addr[numc] = ((addr[numc] >> 6) << 6);
              addr[numc] = (addr[numc] &
                            (((long long int)0x1 << (ADDRESS_BITS - 1)) - 1));

              ROB[numc].mem_address[ROB[numc].tail] = addr[numc];

              ROB[numc].optype[ROB[numc].tail] = opertype[numc];
              ROB[numc].comptime[ROB[numc].tail] = CYCLE_VAL + BIGNUM;
              ROB[numc].instrpc[ROB[numc].tail] = instrpc[numc];

              // Check to see if the read is for buffered data in write queue -
              // return constant latency if match in WQ
              // add in read queue otherwise
              int lat = read_matches_write_or_read_macro_queue(
                  addr[numc]); // read_matches_write_or_read_queue(addr[numc]);
              if (lat) {
                ROB[numc].comptime[ROB[numc].tail] =
                    CYCLE_VAL + (long long int)lat + PIPELINEDEPTH; //????
              } else {
                // IV
                // insert_read(addr[numc], CYCLE_VAL, numc, ROB[numc].tail,
                // instrpc[numc]);
                // insert in MACRO read queue
                insert_macro_read(addr[numc], CYCLE_VAL, numc, ROB[numc].tail,
                                  instrpc[numc]);
              }
            } else {
              if (opertype[numc] == 'W') {
                num_instructions[numc]++;

                addr[numc] = ((addr[numc] >> 6) << 6);
                addr[numc] = (addr[numc] &
                              (((long long int)0x1 << (ADDRESS_BITS - 1)) - 1));

                ROB[numc].mem_address[ROB[numc].tail] = addr[numc];
                ROB[numc].optype[ROB[numc].tail] = opertype[numc];
                ROB[numc].comptime[ROB[numc].tail] = CYCLE_VAL + PIPELINEDEPTH;

                // sensitive
                if (!write_exists_in_write_macro_queue(addr[numc])) // write_exists_in_write_queue(addr[numc]))
                                                                    // //IV
                {
                  // insert_write(addr[numc], CYCLE_VAL, numc, ROB[numc].tail);
                  // IV
                  insert_macro_write(addr[numc], CYCLE_VAL, numc,
                                     ROB[numc].tail);
                }

                for (int c = 0; c < NUM_CHANNELS; c++) {
                  if (write_queue_length[c] == WQ_CAPACITY) {
                    writeqfull = 1;
                    break;
                  }
                }

              } else {
                printf("Panic1.  Poor trace format. \n");
                return -1;
              }
            }
            ROB[numc].tail = (ROB[numc].tail + 1) % ROBSIZE;
            ROB[numc].inflight++;
            fetched[numc]++;
            num_fetch++;

            /* Done consuming one line of the trace file.  Read in the next. */
            if (fgets(newstr, MAXTRACELINESIZE, tif[numc])) {
              if (sscanf(newstr, "%d %c", &nonmemops[numc], &opertype[numc]) >
                  0) {
                if (opertype[numc] == 'R') {
                  if (sscanf(newstr, "%d %c %Lx", &nonmemops[numc],
                             &opertype[numc], &addr[numc]) != 3) {
                    printf("Panic2.  Poor trace format.\n");
                    return -4;
                  } else {
                    instrpc[numc] = 2640;
                    //	printf ("dirt[%d] = %d \n", numc, dirt[numc]);
                  }

                  ///	printf ("RRR  %d %c
                  ///%Lx\n",nonmemops[numc],opertype[numc],addr[numc]);
                } else {
                  if (opertype[numc] == 'W') {
                    if (sscanf(newstr, "%d %c %Lx", &nonmemops[numc],
                               &opertype[numc], &addr[numc]) != 3) {

                      printf("Panic3.  Poor trace format.\n");
                      return -3;
                    }
                    /// printf ("TTT  %d %c
                    /// %Lx\n",nonmemops[numc],opertype[numc],addr[numc]);
                  } else {
                    printf("Panic4.  Poor trace format.\n");
                    return -2;
                  }
                }
              } else {
                printf("Panic5.  Poor trace format.\n");
                return -1;
              }
            } else {
              if (ROB[numc].inflight == 0) {
                num_done++;
                if (!time_done[numc])
                  time_done[numc] = CYCLE_VAL;
              }
              ROB[numc].tracedone = 1;
              break; /* Break out of the while loop fetching instructions. */
            }
            // printf ("######%c \n",opertype[numc]);
          } /* Done consuming the next rd or wr. */

        }    /* One iteration of the fetch while loop done. */
      }      /* Closing brace for if(trace not done). */
      else { /* Input trace is done.  Check to see if all inflight instrs have
                finished. */
        if ((ROB[numc].inflight == 0) || (num_instructions[numc] > NUM_INST)) {
          // printf ("%lld | numc = %d is done ", CYCLE_VAL, numc);

          num_done++;
          if (!time_done[numc])
            time_done[numc] = CYCLE_VAL;
        }
      }
    } /* End of for loop that goes through all cores. */

    if (num_done == NUMCORES) {
      /* Traces have been consumed and in-flight windows are empty.  Must
       * confirm that write queues have been drained. */
      for (numch = 0; numch < NUM_CHANNELS; numch++) {
        if (write_queue_length[numch])
          break;
      }
      if ((numch == NUM_CHANNELS) && (size_macro_rd_queue() == 0) &&
          (size_macro_wr_queue() == 0) && (mt_tab == NULL)) {
        expt_done = 1; /* All traces have been consumed and the write queues are
                          drained. */
      }
    }

    // IV

    micro_req_gen();
    check_to_issue_proof_flush();
    update_backward();
    clean_macro_queues();

    CYCLE_VAL++; /* Advance the simulation cycle. */
  }

  /* Code to make sure that the write queue drain time is included in
     the execution time of the thread that finishes last. */
  maxtd = time_done[0];
  maxcr = 0;
  for (numc = 1; numc < NUMCORES; numc++) {
    if (time_done[numc] > maxtd) {
      maxtd = time_done[numc];
      maxcr = numc;
    }
  }
  time_done[maxcr] = CYCLE_VAL;

  core_power = 0;
  for (numc = 0; numc < NUMCORES; numc++) {
    /* A core has peak power of 10 W in a 4-channel config.  Peak power is
     * consumed while the thread is running, else the core is perfectly power
     * gated. */
    core_power =
        core_power + (10 * ((float)time_done[numc] / (float)CYCLE_VAL));
  }
  if (NUM_CHANNELS == 1) {
    /* The core is more energy-efficient in our single-channel configuration. */
    core_power = core_power / 2.0;
  }

  printf("Done with loop. Printing stats.\n");
  printf("Cycles %lld\n", CYCLE_VAL);
  /*if (MAC_TABLE_EN)
                printf ("found = %d \n", found_in_mac_table);*/
  total_time_done = 0;
  for (numc = 0; numc < NUMCORES; numc++) {
    printf("Done: Core %d: Fetched %lld : Committed %lld : At time : %lld\n",
           numc, fetched[numc], committed[numc], time_done[numc]);
    total_time_done += time_done[numc];
  }
  printf("Sum of execution times for all programs: %lld\n", total_time_done);
  // printf ("MAC_TABLE_SIZE = %d MAC_TABLE_EN = %d \n", MAC_TABLE_SIZE,
  // MAC_TABLE_EN);
  printf("Num reads merged: %lld\n", num_read_merge);
  printf("Num writes merged: %lld\n", num_write_merge);
  printf("Num macro reads merged read: %lld werite %lld\n",
         num_macro_read_merge_read, num_macro_read_merge_write);
  printf("Num macro writes merged: %lld\n", num_macro_write_merge);
  printf("stats_macro_reads_completed: %lld\n", stats_macro_reads_completed);
  printf("stats_macro_writes_completed: %lld\n", stats_macro_writes_completed);

  for (int h = 0; h < 12; h++) {

    printf("UMERGED read with a read  %lld read with a write %lld  write with "
           "a write %lld \n",
           num_macro_read_merge_read, num_macro_read_merge_write,
           num_macro_write_merge);

    scheduler_stats();
    print_stats();

    /*Print Cycle Stats*/

    for (int c = 0; c < NUM_CHANNELS; c++)
      for (int r = 0; r < NUM_RANKS; r++)
        calculate_power(c, r, 0, chips_per_rank);

    printf("\n#-------------------------------------- Power Stats "
           "----------------------------------------------\n");
    printf("Note:  1. termRoth/termWoth is the power dissipated in the ODT "
           "resistors when Read/Writes terminate \n");
    printf("          in other ranks on the same channel\n");
    printf("#------------------------------------------------------------------"
           "-------------------------------\n\n");

    /*Print Power Stats*/

    float total_system_power = 0;
    for (int c = 0; c < NUM_CHANNELS; c++)
      for (int r = 0; r < NUM_RANKS; r++)
        total_system_power += calculate_power(c, r, 1, chips_per_rank);

    printf("\n#----------------------------------------------------------------"
           "---------------------------------\n");
    if (NUM_CHANNELS == 4) { // Assuming that this is 4channel.cfg
      printf("Total memory system power = %f W\n", total_system_power / 1000);
      printf("Miscellaneous system power = 40 W  # Processor uncore power, "
             "disk, I/O, cooling, etc.\n");
      printf("Processor core power = %f W  # Assuming that each core consumes "
             "10 W when running\n",
             core_power);
      printf("Total system power = %f W # Sum of the previous three lines\n",
             40 + core_power + total_system_power / 1000);
      printf("Energy Delay product (EDP) = %2.9f J.s\n",
             (40 + core_power + total_system_power / 1000) *
                 (float)((double)CYCLE_VAL / (double)3200000000) *
                 (float)((double)CYCLE_VAL / (double)3200000000));
    } else { //  Assuming that this is 1channel.cfg
      printf("Total memory system power = %f W\n", total_system_power / 1000);
      printf("Miscellaneous system power = 10 W  # Processor uncore power, "
             "disk, I/O, cooling, etc.\n"); // The total 40 W misc power will be
                                            // split across 4 channels, only 1
                                            // of which is being considered in
                                            // the 1-channel experiment.
      printf("Processor core power = %f W  # Assuming that each core consumes "
             "5 W\n",
             core_power); // Assuming that the cores are more lightweight.
      printf("Total system power = %f W # Sum of the previous three lines\n",
             10 + core_power + total_system_power / 1000);
      printf("Energy Delay product (EDP) = %2.9f J.s\n",
             (10 + core_power + total_system_power / 1000) *
                 (float)((double)CYCLE_VAL / (double)3200000000) *
                 (float)((double)CYCLE_VAL / (double)3200000000));
    }
  }

  printf("\ntotal cycles %lld\n", CYCLE_VAL);

  for(int c = 0; c < NUM_CHANNELS; c++){
    for(int r = 0; r < NUM_RANKS; r++){
      for(int b = 0; b < NUM_BANKS; b++){
        //stats_banks_flushed
        printf("Channel %d Rank %d Bank %d Flushed: %lldx\n", c, r, b, stats_banks_flushed[c][r][b]);
      }
    }
  }

  return 0;
}
