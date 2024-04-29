// meta data cache

#include "meta_cache.h"
#include "VAT.h"
#include "memory_controller.h"
#include "params.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

char *microoptype_Strings1[] = {"DATA", "MAC"};

extern long long int CYCLE_VAL;

struct pri_record_t {
  int way_index;
  int pri;
} pri_record;

int cmpfunc(const void *a, const void *b) {
  struct pri_record_t *aa = (struct pri_record_t *)a;
  struct pri_record_t *bb = (struct pri_record_t *)b;
  return (aa->pri - bb->pri);
}

unsigned long long int tag_2_address(cache_t *my_cache,
                                     unsigned long long int in_tag, int set) {
  return (in_tag << (log_base2(my_cache->num_set) +
                     log_base2(my_cache->num_offset))) +
         (set << log_base2(my_cache->num_offset));
}

// to malloc a new cache and initialize it
cache_t *init_cache(cachetype_t cachetype, int set, int way, int offset) {
  cache_t *my_cache = (cache_t *)malloc(sizeof(cache_t));
  my_cache->cache_type = cachetype;
  my_cache->miss_num = 0;
  my_cache->hit_num = 0;
  my_cache->cnt_miss_num = 0;
  my_cache->cnt_hit_num = 0;
  my_cache->mac_miss_num = 0;
  my_cache->mac_hit_num = 0;
  my_cache->num_offset = offset;
  my_cache->num_way = way;
  my_cache->num_set = set;
  tag_t *my_tag = (tag_t *)malloc(sizeof(tag_t) * set * way);
  assert(my_tag != NULL);
  for (long long int i = 0; i < set * way; i++) {
    my_tag[i].address = 0x0;
    my_tag[i].arv_time = 0;
    my_tag[i].RRPV = RRIP_N - 1;
    my_tag[i].priority =
        100000000;       // my_cache->num_way+2; // something out of range
    my_tag[i].dirty = 0; // not dirty
    my_tag[i].valid = 0; // invalid
    my_tag[i].father_not_update = 0; // update!
    my_tag[i].instruction_id = 0;
    my_tag[i].thread_id = 0;
    my_tag[i].arv_time = -1;
    my_tag[i].type = MAC;
    my_tag[i].num_hits = 0;
  }
  my_cache->tag_array = my_tag;
  return (my_cache);
}

void update_arrv_time(cache_t *my_cache, int set) {
  for (int i = set * my_cache->num_way;
       i < (set * my_cache->num_way + my_cache->num_way); i++) {
    if (my_cache->tag_array[i].valid == 1)
      my_cache->tag_array[i].arv_time++; // TODO: Check
  }
}

// how to promote the priority of the block which gets a hit
void promotion_policy(cache_t *my_cache, unsigned long long int addr) {
  int this_set = find_set(my_cache->num_offset, my_cache->num_set, addr);
  unsigned long long int this_tag =
      find_tag(my_cache->num_offset, my_cache->num_set, addr);
  struct pri_record_t pri_arr[my_cache->num_way]; // new block considered
  int class_hight;

  update_arrv_time(my_cache,
                   this_set); // all blocks except for the one which is promoted

  if ((my_cache->cache_type == VICTIM_CH) &&
      (!VC_MIRROR_RP)) { // VictimCache - using LRU, not mirroring
                         // ReplacementPolicy of Metacache
    // LRU
    int j = 0;
    for (int i = this_set * my_cache->num_way;
         i < (this_set * my_cache->num_way + my_cache->num_way); i++) {
      if (this_tag == my_cache->tag_array[i].address &&
          my_cache->tag_array[i].valid == 1) {
        my_cache->tag_array[i].arv_time = 0;
      }

      pri_arr[j].pri = my_cache->tag_array[i].arv_time;
      pri_arr[j].way_index = i;
      j++;
    }
  } else if (LRU_REP == 1) {
    // LRU
    int j = 0;
    for (int i = this_set * my_cache->num_way;
         i < (this_set * my_cache->num_way + my_cache->num_way); i++) {
      if (this_tag == my_cache->tag_array[i].address &&
          my_cache->tag_array[i].valid == 1) {
        pri_arr[j].pri = 0;                  // my_cache->tag_array[i].arv_time;
        my_cache->tag_array[i].arv_time = 0; // now it is updated
      } else
        pri_arr[j].pri = my_cache->tag_array[i].arv_time;

      pri_arr[j].way_index = i;
      j++;
    }

  } else if (LRU_REP == 0) {
    int j = 0;
    int typ = 0;
    for (int i = this_set * my_cache->num_way;
         i < (this_set * my_cache->num_way + my_cache->num_way); i++) {
      typ = my_cache->tag_array[i].type;
      class_hight = 11 - typ;

      if (this_tag == my_cache->tag_array[i].address &&
          my_cache->tag_array[i].valid == 1) {
        my_cache->tag_array[i].arv_time = 0; // now it is pdated
                                             // pri_arr[j].pri =  class_hight;
      }
      // else

      int applied_THETA = 0;
      int thres1 = ((typ == MAC) ? (THRES_THETA_1 - 2) : (THRES_THETA_1 + 1));
      int thres2 = ((typ == MAC) ? (THRES_THETA_2 - 2) : (THRES_THETA_2 + 1));
      if (my_cache->tag_array[i].num_hits > thres1)
        applied_THETA = THETA * THRES_THETA_1;
      if (my_cache->tag_array[i].num_hits > thres2)
        applied_THETA = THETA * THRES_THETA_2;
      pri_arr[j].pri = ALPHA * my_cache->tag_array[i].arv_time +
                       BETA * class_hight + applied_THETA;
      pri_arr[j].way_index = i;
      j++;
    }

  } else if (LRU_REP == 2) // SRRIP
  {
    int j = 0;
    for (int i = this_set * my_cache->num_way;
         i < (this_set * my_cache->num_way + my_cache->num_way); i++) {
      if (this_tag == my_cache->tag_array[i].address &&
          my_cache->tag_array[i].valid == 1) {
        pri_arr[j].pri = 0; // my_cache->tag_array[i].arv_time;
        if (TYPE_RRIP == 0)
          my_cache->tag_array[i].RRPV = 0; // now it is updated
        else if (TYPE_RRIP == 1 && my_cache->tag_array[i].RRPV != 0)
          my_cache->tag_array[i].RRPV--;
      }
      // else

      pri_arr[j].pri = my_cache->tag_array[i].RRPV;

      pri_arr[j].way_index = i;
      j++;
    }

  } else if (LRU_REP == 3) // SRRIP + level
  {
    int j = 0;
    int typ = 0;
    for (int i = this_set * my_cache->num_way;
         i < (this_set * my_cache->num_way + my_cache->num_way); i++) {
      typ = my_cache->tag_array[i].type;
      class_hight = 11 - typ;

      if (this_tag == my_cache->tag_array[i].address &&
          my_cache->tag_array[i].valid == 1) {
        if (TYPE_RRIP == 0)
          my_cache->tag_array[i].RRPV = 0; // now it is updated
        else if (TYPE_RRIP == 1 && my_cache->tag_array[i].RRPV != 0)
          my_cache->tag_array[i].RRPV--;
      }
      // else

      int applied_THETA = 0;
      int thres1 = ((typ == MAC) ? (THRES_THETA_1 - 2) : (THRES_THETA_1 + 1));
      int thres2 = ((typ == MAC) ? (THRES_THETA_2 - 2) : (THRES_THETA_2 + 1));
      if (my_cache->tag_array[i].num_hits > thres1)
        applied_THETA = THETA * THRES_THETA_1;
      if (my_cache->tag_array[i].num_hits > thres2)
        applied_THETA = THETA * THRES_THETA_2;
      pri_arr[j].pri = ALPHA * my_cache->tag_array[i].RRPV +
                       BETA * class_hight + applied_THETA;
      pri_arr[j].way_index = i;
      j++;
    }
  }

  // sort ascendingly based on pri[] and assign the parity accordingly.
  // sort
  qsort(pri_arr, my_cache->num_way, sizeof(struct pri_record_t), cmpfunc);

  for (int i = 0; i < my_cache->num_way;
       i++) // i = this_set*my_cache->num_way;
            // i<(this_set*my_cache->num_way+my_cache->num_way); i++)
  {
    if (my_cache->tag_array[pri_arr[i].way_index].valid == 1)
      my_cache->tag_array[pri_arr[i].way_index].priority = i;
    else {
      my_cache->tag_array[pri_arr[i].way_index].priority =
          100000000; // very likely to be chosen to evict  	//TODO: Increase
                     // if num_cores increased, as shared VC could have 1024
                     // ways
    }

    // priority is between 0 to way-1, however, pri_arr is something related to
    // the clock the block arrived
  }
}

void insertion_policy(cache_t *my_cache, unsigned long long int addr) {
  int this_set = find_set(my_cache->num_offset, my_cache->num_set, addr);
  // unsigned long long int this_tag = find_tag(my_cache->num_offset,
  // my_cache->num_set, addr);
  struct pri_record_t pri_arr[my_cache->num_way]; // new block considered
  int class_hight;
  update_arrv_time(my_cache, this_set);
  // printf ("%llx --> \n", addr);
  // updating pri_arr

  if ((my_cache->cache_type == VICTIM_CH) &&
      (!VC_MIRROR_RP)) { // VictimCache - using LRU, not mirroring
                         // ReplacementPolicy of Metacache
    int j = 0;
    for (int i = this_set * my_cache->num_way;
         i < (this_set * my_cache->num_way + my_cache->num_way); i++) {
      pri_arr[j].pri = my_cache->tag_array[i].arv_time;
      pri_arr[j].way_index = i;
      j++;
    }
  } else if (LRU_REP == 1) {
    //
    int j = 0;
    for (int i = this_set * my_cache->num_way;
         i < (this_set * my_cache->num_way + my_cache->num_way); i++) {

      pri_arr[j].pri = my_cache->tag_array[i].arv_time;
      pri_arr[j].way_index = i;
      //	printf ("[%d %d %llx]",pri_arr[j].pri,pri_arr[j].way_index,
      //my_cache->tag_array[pri_arr[j].way_index].address);
      j++;
    }
    // printf ("\n");
  } else if (LRU_REP == 0) {

    int j = 0;
    int typ = 0;
    for (int i = this_set * my_cache->num_way;
         i < (this_set * my_cache->num_way + my_cache->num_way); i++) {
      typ = my_cache->tag_array[i].type;
      class_hight = 11 - typ;

      int applied_THETA = 0;
      int thres1 = ((typ == MAC) ? (THRES_THETA_1 - 2) : (THRES_THETA_1 + 1));
      int thres2 = ((typ == MAC) ? (THRES_THETA_2 - 2) : (THRES_THETA_2 + 1));
      if (my_cache->tag_array[i].num_hits > thres1)
        applied_THETA = THETA * THRES_THETA_1;
      if (my_cache->tag_array[i].num_hits > thres2)
        applied_THETA = THETA * THRES_THETA_2;
      pri_arr[j].pri = (ALPHA * my_cache->tag_array[i].arv_time) +
                       (BETA * class_hight) + applied_THETA;
      pri_arr[j].way_index = i;

      j++;
    }
  } else if (LRU_REP == 2) // RRIP
  {
    int j = 0;
    int find_way_1 = 0;
    // int index_way_1;
    while (find_way_1 == 0) {
      for (int i = this_set * my_cache->num_way;
           i < (this_set * my_cache->num_way + my_cache->num_way); i++) {
        if (my_cache->tag_array[i].RRPV == RRIP_N - 1) {
          find_way_1 = 1;
          break;
        }
      }
      if (find_way_1 == 0) {
        for (int i = this_set * my_cache->num_way;
             i < (this_set * my_cache->num_way + my_cache->num_way); i++) {
          assert((my_cache->tag_array[i].RRPV != RRIP_N - 1) &&
                 (my_cache->tag_array[i].valid == 1));
          my_cache->tag_array[i].RRPV++;
          assert(my_cache->tag_array[i].RRPV <= RRIP_N - 1);
        }
      }
    }

    j = 0;
    for (int i = this_set * my_cache->num_way;
         i < (this_set * my_cache->num_way + my_cache->num_way); i++) {

      pri_arr[j].pri = my_cache->tag_array[i].RRPV;
      pri_arr[j].way_index = i;
      //	printf ("[%d %d %llx]",pri_arr[j].pri,pri_arr[j].way_index,
      //my_cache->tag_array[pri_arr[j].way_index].address);
      j++;
    }

  } else if (LRU_REP == 3) // RRIP+level
  {
    int j = 0;
    int find_way_1 = 0;
    // int index_way_1;
    while (find_way_1 == 0) {
      for (int i = this_set * my_cache->num_way;
           i < (this_set * my_cache->num_way + my_cache->num_way); i++) {
        if (my_cache->tag_array[i].RRPV == RRIP_N - 1) {
          find_way_1 = 1;
          //	index_way_1 = i;
          break;
        }
      }
      if (find_way_1 == 0) {
        for (int i = this_set * my_cache->num_way;
             i < (this_set * my_cache->num_way + my_cache->num_way); i++) {
          assert((my_cache->tag_array[i].RRPV != RRIP_N - 1) &&
                 (my_cache->tag_array[i].valid == 1));
          my_cache->tag_array[i].RRPV++;
        }
      }
    }

    j = 0;
    int typ = 0;
    for (int i = this_set * my_cache->num_way;
         i < (this_set * my_cache->num_way + my_cache->num_way); i++) {
      typ = my_cache->tag_array[i].type;
      class_hight = 11 - typ;

      int applied_THETA = 0;
      int thres1 = ((typ == MAC) ? (THRES_THETA_1 - 2) : (THRES_THETA_1 + 1));
      int thres2 = ((typ == MAC) ? (THRES_THETA_2 - 2) : (THRES_THETA_2 + 1));
      if (my_cache->tag_array[i].num_hits > thres1)
        applied_THETA = THETA * THRES_THETA_1;
      if (my_cache->tag_array[i].num_hits > thres2)
        applied_THETA = THETA * THRES_THETA_2;
      pri_arr[j].pri = (ALPHA * my_cache->tag_array[i].RRPV) +
                       (BETA * class_hight) + applied_THETA;
      pri_arr[j].way_index = i;

      j++;
    }
  }
  // sort ascendingly based on pri[] and assign the parity accordingly.
  // sort
  qsort(pri_arr, my_cache->num_way, sizeof(struct pri_record_t), cmpfunc);

  for (int i = 0; i < my_cache->num_way;
       i++) // i = this_set*my_cache->num_way;
            // i<(this_set*my_cache->num_way+my_cache->num_way); i++)
  {

    // printf ("pri_arr[%d].way_index = %d\n", i, pri_arr[i].way_index);
    if (my_cache->tag_array[pri_arr[i].way_index].valid == 1)

      my_cache->tag_array[pri_arr[i].way_index].priority =
          i; // priority is between 0 to way-1, however, pri_arr is something
             // related to the clock the block arrived
    else {
      my_cache->tag_array[pri_arr[i].way_index].priority = 100000000;
    }
  }
}

// looking for the block with the lowest priority
int victim_selection(cache_t *my_cache, unsigned long long int addr) {
  int this_set = find_set(my_cache->num_offset, my_cache->num_set, addr);
  int index = this_set * my_cache->num_way;
  for (int i = this_set * my_cache->num_way;
       i < (this_set * my_cache->num_way + my_cache->num_way); i++) {
    if (my_cache->tag_array[index].priority <=
        my_cache->tag_array[i].priority) {
      index = i;
    }
  }
  return (index);
}

// to look up inside the cache, if it finds an invalid cache line,
// it returns it back but if it can not find it at all it returns NULL
// do_update is 1 means this access should be counted and if it is 0
// this access is not counted as an official access
// look_up doesn't change dirty bit
tag_t *look_up(cache_t *my_cache, unsigned long long int addr, int do_update)

{

  unsigned long long int this_tag =
      find_tag(my_cache->num_offset, my_cache->num_set, addr);
  unsigned long long int this_set =
      find_set(my_cache->num_offset, my_cache->num_set, addr);

  unsigned long long int mac_start;
  // unsigned long long int counter_start[NUMCORES];
  region(TRACE_CAPACITY, 0x0, &mac_start);

  if (COLLECT_CACHE_TRACE && size_of_map < BIGNUM_TRACE) {
    int found = 0;
    for (int i = 0; i < size_of_map; i++) {
      if (map_cache_activity[i].addr == addr) {
        found = 1;
        map_cache_activity[i].value.num_accessed++;
        map_cache_activity[i].value.cycle_last_accessed = CYCLE_VAL;
        break;
      }
    }
    if (!found) {
      map_cache_activity[size_of_map].addr = addr;
      map_cache_activity[size_of_map].value.num_accessed = 1;
      map_cache_activity[size_of_map].value.cycle_last_accessed = CYCLE_VAL;
      size_of_map++;
    }
  }

  // wharever we are looking up is found in the cache
  int hit = 0;
  // success shows if we can find the tag.
  int success = 0;
  int found; // index of cache line
  // print_cache(cnt_cache);
  for (int i = (this_set * my_cache->num_way);
       i < (this_set * my_cache->num_way + my_cache->num_way); i++) {
    if ((my_cache->tag_array[i].address == this_tag) &&
        (my_cache->tag_array[i].valid == 1)) {
      success = 1;
      found = i;
      hit = 1;
      my_cache->tag_array[i].num_hits++;
      break;
    }
  }

  unsigned long long int mac_st;
  region(TRACE_CAPACITY, 0x0, &mac_st);
  // printf ("cnt_st = %llx mac_st = %llx \n", cnt_st, mac_st);
  // getchar();

  if (do_update) {
    if (hit == 1) {
      my_cache->hit_num++;
      // if (addr >= cnt_st )
      //	my_cache->cnt_hit_num++;
      if (addr >= mac_st)
        my_cache->mac_hit_num++;
      else
        assert(0);
    } else {
      my_cache->miss_num++;
      // if (addr >= cnt_st )
      //	my_cache->cnt_miss_num++;
      if (addr >= mac_st)
        my_cache->mac_miss_num++;
      else
        assert(0);
    }
  }

  // //Perfect Cachce (All-hits) simulation
  // if(success)
  // 	return(&my_cache->tag_array[found]);

  if (success) {
    if (do_update) {
      // update the arrival time
      update_arrv_time(my_cache, this_set);
      my_cache->tag_array[found].arv_time = 0;
      // prmote the new block.
      promotion_policy(my_cache, addr);
    }
    return (&my_cache->tag_array[found]);
    // if it is a fine-granularity cache, it still returns the cache line and we
    // need to check if the 64-bit chunk of data exists or not.
  }

  else {
    return (NULL);
  }
}

// to insert a cache line into a cache, access shows it is a write request
// or a read one. it returns the cache line which is replaced
// father_not_update shows if its father has updated or not
tag_t *insert_cache(cache_t *my_cache, unsigned long long int addr,
                    int father_not_update, int thread_id, int instruction_id,
                    int dirty, microoptype_t typ) {
  tag_t *ltag = look_up(my_cache, addr, 1);
  if (ltag != NULL) {
    ltag->dirty = dirty;
    return (NULL);

  }

  else {
    /* code */
    // int this_set = find_set(my_cache->num_offset, my_cache->num_set,
    // addr);//, my_cache->cache_type);
    int index;
    insertion_policy(my_cache, addr);
    index = victim_selection(my_cache, addr);
    tag_t *replaced = (tag_t *)malloc(sizeof(tag_t));
    replaced->address = my_cache->tag_array[index].address;
    replaced->arv_time = my_cache->tag_array[index].arv_time;
    replaced->priority = my_cache->tag_array[index].priority;

    replaced->valid = my_cache->tag_array[index].valid;
    replaced->type = my_cache->tag_array[index].type;
    replaced->dirty = my_cache->tag_array[index].dirty;
    replaced->RRPV = my_cache->tag_array[index].RRPV;
    replaced->father_not_update = my_cache->tag_array[index].father_not_update;
    replaced->thread_id = my_cache->tag_array[index].thread_id;
    replaced->instruction_id = my_cache->tag_array[index].instruction_id;
    my_cache->tag_array[index].address =
        find_tag(my_cache->num_offset, my_cache->num_set, addr);

    my_cache->tag_array[index].father_not_update = father_not_update;

    /* code */
    my_cache->tag_array[index].valid = 1;
    my_cache->tag_array[index].dirty = dirty;
    my_cache->tag_array[index].num_hits = 0;
    my_cache->tag_array[index].arv_time = 0;
    if (BRRIP == 1) {
      assert(LRU_REP == 2 || LRU_REP == 3);
      int toss = rand() % 10 + 1;
      // printf ("toss = %d \n", toss);
      if (toss > PROB1)
        my_cache->tag_array[index].RRPV = RRIP_N - 2;
      else
        my_cache->tag_array[index].RRPV = RRIP_N - 1;

    } else if (LRRIP == 1) {
      assert(LRU_REP == 2 || LRU_REP == 3);
      if (my_cache->tag_array[index].type < 5)
        my_cache->tag_array[index].RRPV = RRIP_N - 1;
      else
        my_cache->tag_array[index].RRPV = RRIP_N - 2;
    } else {
      my_cache->tag_array[index].RRPV = RRIP_N - 2;
      assert(BRRIP == 0 && LRRIP == 0);
    }

    my_cache->tag_array[index].instruction_id = instruction_id;
    my_cache->tag_array[index].thread_id = thread_id;
    my_cache->tag_array[index].type = typ;

    return (replaced);
  }

  return NULL;
}

void print_cache(cache_t *my_cache) {
  printf("================================= cache content ===============\n");
  for (int i = 0; i < (my_cache->num_way * my_cache->num_set); i++) {
    int prnt;
    if (i % my_cache->num_way == 0) {
      prnt = 0;
      for (int t = i; t < i + my_cache->num_way; t++) {
        if (my_cache->tag_array[t].valid) {
          prnt = 1;
          break;
        }
      }
      if (prnt == 1)
        printf("\nset %d: ", i / my_cache->num_way);
    }
    if (my_cache->tag_array[i].valid) {
      printf("[%d %llx]", i % my_cache->num_way,
             my_cache->tag_array[i].address);
    }
  }
  printf("\n===============================================================\n");
}