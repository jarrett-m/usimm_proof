#ifndef __META_CACHE_H__
#define __META_CACHE_H__

#include "memory_controller.h"

typedef enum {MAC_CH, CNT_CH, META_CH, VICTIM_CH} cachetype_t;

typedef struct tag
{
  unsigned long long int address; // TODO: Sarab - Use uints
  int arv_time; // arrival time in the number of clocks before the current clock 
  int RRPV; // for RRIP algorithm 
  int priority; // shows the priority in the pririty stack for replacement policy 
  int dirty;
  // 8 bit the least segnificat digits of dirty and valid shows dirty and 
  // valid bits of 8 64-bit chyncks od MAC address if FG_PARITY_CACHE is activated.  
  int valid;
  int thread_id;
  int instruction_id;
  int father_not_update;
  int num_hits;
  microoptype_t type;
}tag_t;


typedef struct cache
{
  cachetype_t cache_type;  
	int num_set;
	int num_way;
	int num_offset;
	double hit_num;
	double miss_num;
  double cnt_hit_num;
	double cnt_miss_num;
  double mac_hit_num;
	double mac_miss_num;
	tag_t *tag_array;
  // victimcache_t *victim_cache;   // Victim Cache - Old
	
}cache_t;

#define find_tag(numoffset, numset, addr) ((addr) >> (log_base2(numoffset))) 
#define find_set(numoffset, numset, addr) ((addr >> log_base2(numoffset)) & (((long long int)0x1 << log_base2(numset))-1))

// tag in this cache contains the set and tag so we just shift it by 6 bit to 
// throw away the offset bits. 

cache_t *meta_cache[MAX_NUMCORES];
cache_t *victim_cache[MAX_NUMCORES];
// convert tag to address 
unsigned long long int tag_2_address(cache_t * my_cache, unsigned long long int in_tag, int set);

// to initialize a cache 
cache_t * init_cache (cachetype_t cachetype, int set, int way, int offset);

// to look up the data inside the cache 
tag_t* look_up (cache_t * my_cache, unsigned long long int addr, int do_update);

//to replace a cache line and return back the replaced cache line  
int  replacement_cache (cache_t * my_cache, unsigned long long int addr);

//to insert a cache line in cache 
tag_t * insert_cache (cache_t * my_cache, unsigned long long int addr, int father_not_update,
													int thread_id, int instruction_id, int dirty, microoptype_t typ);
													
													
void print_cache (cache_t * my_cache);


#endif //__META_CACHE_H__
