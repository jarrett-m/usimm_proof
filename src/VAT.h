#ifndef __SGX_H__
#define __SGX_H__
#include "memory_controller.h"
#include "params.h"
#define TREE_DEGREE 4
#define HASH_VALUE_SIZE                                                        \
  4 // HASH_VALUE_SIZE hash values can be fit into one cache line
#define PAGE_SIZE 64 // number of cache line
#define MAC_SIZE 8   // number of MAC in one cache line
#define HASH_SIZE 8  // number of hash in one cache line
#define CNT_1st_SIZE                                                           \
  64 // number of counter per cache line in first level of ToC
#define CNT_2nd_SIZE                                                           \
  (CNT_1st_SIZE / 2) // number of counter per cache line in 2nd level of ToC
#define CNT_3rd_SIZE                                                           \
  (CNT_2nd_SIZE / 2) // number of counter per cache line in 3nd level of ToC
#define ceiling(x, y) ((x == (x / y) * y) ? x / y : ((x / y) + 1))
#define CAHE_LINE_OFFSET 64
#define WIDTH_CL_OFFSET 6
#define WIDTH_ADDRESS ADDRESS_BITS
//#define CAPACITY TRACE_CAPACITY

#define sizeof_var(var) ((size_t)(&(var) + 1) - (size_t)(&(var)))

// to calculate log4
int log4(int x);
// to calculate logy (x)
int logyx(int x, int y);
// to calculate uper bound of log y (x)
int ceiling_logyx(int x, int y);
// return the number of data in each page
int num_data_cl_per_page(void);
// to find the regions between data+MAC, hash, and counters
void region(unsigned long long int capacity, unsigned long long int data_start,
            unsigned long long int *mac_start);
// find the MAC for particaluar data cache line
unsigned long long int find_MAC(unsigned long long int addr,
                                unsigned long long int data_start,
                                unsigned long long int capacity);
// find the number of node in SToC
unsigned long long int num_node_SSGX(unsigned long long int capacity);
// find the addresses of counter cache lines in ToC
// int find_counters (unsigned long long int addr, unsigned long long int
// capacity, 				   unsigned long long int data_start,unsigned
// long long int *counters
//);

// int find_counters_for_counters (unsigned long long int addr, microoptype_t
// type, unsigned long long int capacity, 				   unsigned long
// long int
// data_start,unsigned long long int *counters);

#endif //__SGX_H__