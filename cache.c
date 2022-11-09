#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "cache.h"
#include "jbod.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int num_queries = 0;
static int num_hits = 0;

bool cache_exists = false;
int num_elements = 0;

// Function to find minimum value in a array and returns a index
int find_minimum_index(int array[], int array_size)
{
  int minimum_index = 0;
  int minimum_value = array[0];
  for (int i = 1; i < array_size; i++)
  {
    if (array[i] < minimum_value)
    {
      minimum_value = array[i];
      minimum_index = i;
    }
  }
  return minimum_index;
}

int cache_create(int num_entries)
{
  // num_entries is 2 minimum 4096 maximum
  if (num_entries < 2 || num_entries > 4096)
  {
    return -1;
  }
  // If cache already exists can't create more
  if (cache_exists == 1)
  {
    return -1;
  }
  // Dynamically allocate memory to cache with number of caches provided by num_entries
  cache = calloc(num_entries, sizeof(cache_entry_t));
  cache_size = num_entries;
  cache_exists = true;
  return 1;
}

int cache_destroy(void)
{
  // If cache doesn't exists, you can't destory one
  if (cache_exists == false)
  {
    return -1;
  }
  // Free dynamically allocated memory and set it to NULL for potential error
  free(cache);
  cache = NULL;
  
  cache_size = 0;
  cache_exists = false;
  num_elements = 0;
  return 1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf)
{
  // printf("\n*************Cache Lookup Start*********************\n");
  //   buf can't be null
  if (buf == NULL)
  {
    // printf("\nError : buf is null\n");
    return -1;
  }
  // cache can't be null
  if (cache_exists == false)
  {
    // printf("\nError : Cache DNE\n");
    return -1;
  }
  // cache can't be empty
  if (num_elements == 0)
  {
    return -1;
  }

  /*
  for (int i = 0; i < cache_size; i++)
  {
    printf("\nCache number : (%d) | disk (%d) block (%d)\n", i, cache[i].disk_num, cache[i].block_num);
  }
  */

  // result of lookup in a for loop
  for (int i = 0; i < cache_size; i++)
  {

    if (cache[i].disk_num == disk_num && cache[i].block_num == block_num)
    {
      // printf("\nFound!\n");
      memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
      num_queries += 1;
      num_hits += 1;
      cache[i].num_accesses += 1;
      return 1;
    }
  }
  // printf("\n*************Cache Lookup End*********************\n");
  return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf)
{
  for (int i = 0; i < cache_size; i++)
  {
    // Entry exists
    if (cache[i].disk_num == disk_num || cache[i].block_num == block_num)
    {
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      cache[i].num_accesses = 1;
    }
  }
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf)
{
  // INVALID CASES
  // buf can't be null
  if (buf == NULL)
  {
    return -1;
  }
  // cache can't be null
  if (cache_exists == false)
  {

    // printf("\nCache exists = (%d)\n",cache_exists);
    return -1;
  }
  // Invalid disk_num
  if (disk_num < 0 || disk_num > 15)
  {
    return -1;
  }
  // Invalid block_num
  if (block_num < 0 || block_num > 255)
  {
    return -1;
  }
  // Inserting into exisitng entry
  for (int i = 0; i < cache_size; i++)
  {
    if (cache[i].disk_num == disk_num && cache[i].block_num == block_num && cache[i].valid == true)
    {
      return -1;
    }
  }

  // printf("\nelements : (%d)\n",num_elements);
  //  Figure out if it's full or not
  //  Cache is full
  if (num_elements == cache_size)
  {
    int num_access_list[cache_size];
    // Insert by Least frequently used policy
    for (int i = 0; i < cache_size; i++)
    {
      num_access_list[i] = cache[i].num_accesses;
    }
    // Find the least frequently used index
    int lfu_index = find_minimum_index(num_access_list, cache_size);
    memcpy(cache[lfu_index].block, buf, JBOD_BLOCK_SIZE);
    cache[lfu_index].block_num = block_num;
    cache[lfu_index].disk_num = disk_num;
    cache[lfu_index].num_accesses = 1;
    cache[lfu_index].valid = true;
    num_elements += 1;
    return 1;
  }
  // Cache is not full
  else
  {
    for (int i = 0; i < cache_size; i++)
    {

      // If cache entry is empty insert it into here
      if (cache[i].valid == 0)
      {
        memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
        cache[i].block_num = block_num;
        cache[i].disk_num = disk_num;
        cache[i].num_accesses = 1;
        cache[i].valid = true;
        num_elements += 1;
        return 1;
      }
    }
  }

  return 1;
}

bool cache_enabled(void)
{
  return cache != NULL && cache_size > 0;
}

void cache_print_hit_rate(void)
{
  fprintf(stderr, "num_hits: %d, num_queries: %d\n", num_hits, num_queries);
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float)num_hits / num_queries);
}
