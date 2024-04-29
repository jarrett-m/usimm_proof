
// Bonsai Merkel tree
// Finding the region of different zones, finding the hashes which are needed to
// authenticate date
#include "VAT.h"
#include "memory_controller.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// parametrs

// floor log 4
int log4(int x) {
  int counter = -1;

  while (x != 0) {
    x /= 4;
    counter++;
  }
  return counter;
}
// = logy (x)
int logyx(int x, int y) {
  assert(y != 0);
  int counter = -1;

  while (x != 0) {
    x /= y;
    counter++;
  }
  return counter;
}

int long_logyx(long long int x, long long int y) {
  assert(y != 0);
  int counter = -1;

  while (x != 0) {
    x /= y;
    counter++;
  }
  return counter;
}

int ceiling_long_logyx(long long int x, long long int y) {
  assert(y != 0);
  assert(y != 1);
  int counter = 0;
  long long int xx = x;
  while ((x != 0) && (x != 1)) {
    x /= y;
    counter++;
  }
  unsigned long long int multiply = 1;
  for (int i = 0; i < counter; i++) {
    multiply *= y;
  }
  if (multiply < xx)
    counter++;
  return counter;
}

// uper limit of logy (x)
/*double*/ int ceiling_logyx(int x, int y) {
  assert(y != 0);
  /*double*/ int counter = 0;

  while ((x != 1) && (x != 0)) {
    x /= y;
    counter++;
  }
  return counter;
}
// to calculate x^y
unsigned long long int ipow(int x, int y) {
  unsigned long long int multiply = 1;

  for (int i = 0; i < y; i++) {
    multiply *= x;
  }
  return (multiply);
}

// calculate the number of counter node (not cache line ) in Short ToC (SToC)
// which is second level of ToC up to root data_cl is the number of cache line
// which trace is touching
unsigned long long int num_node_SSGX(unsigned long long int capacity) {
  unsigned long long int data_cl = ceiling(capacity, 64);
  unsigned long long int SGXs_leaves_mul =
      (ipow(8, ceiling_long_logyx((data_cl / (8)), 8)));
  unsigned long long int num_node = 0;
  SGXs_leaves_mul /= 8;
  while (SGXs_leaves_mul != 0) {
    num_node += SGXs_leaves_mul;
    SGXs_leaves_mul /= 8;
  }
  // num_node++; // root
  return (num_node);
}

// define the region for different zones like data, MAC, hash valuse, and
// counters capacity is in Byte and it is the size of data the input trace is
// accessing and according to that this function is about to define the size of
// counter, and MAC for this amount of data the addresses are in BYTE (byte
// wise) to change them to address of cache line, it is shifted by 6
void region(unsigned long long int capacity, unsigned long long int data_start,
            unsigned long long int *mac_start) {
  // ___________________
  //|     		     |
  //|		data 		 |
  //|__________________|
  //|                  |
  //|		MAC			 |
  //|__________________|

  // the number of cache line of data where trace is touching
  unsigned long long int cap_cl = ceiling(capacity, CAHE_LINE_OFFSET);
  // start address of MAC zone in byte
  unsigned long long int start_mac = data_start + (cap_cl << WIDTH_CL_OFFSET);
  // the number of cache line in mac zone
  // unsigned long long int num_mac_cl = ceiling(cap_cl,MAC_SIZE);
  (*mac_start) = start_mac;
}

// find the address of MAC for particular address in byte
// addr is a BYTE wise address
// capacity is the size of pure data (excluding mac and counter and hash)
unsigned long long int find_MAC(unsigned long long int addr,
                                unsigned long long int data_start,
                                unsigned long long int capacity) {
  // find the index of cache line in data zone
  unsigned long long int index_data_cl = (addr - data_start) >> WIDTH_CL_OFFSET;
  unsigned long long int index_mac_cl = index_data_cl / MAC_SIZE;
  unsigned long long int mac_start;
  region(capacity, data_start, &mac_start);
  return (mac_start + (index_mac_cl << WIDTH_CL_OFFSET));
}