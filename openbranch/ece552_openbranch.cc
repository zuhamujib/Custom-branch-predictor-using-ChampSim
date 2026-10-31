#include "openbranch.h"

// Base TAGE-inspired predictor for ECE552 Lab 2
// T0: PC-indexed base predictor
// T1-T6: tagged tables with increasing global history lengths
// Every counter is a 2-bit saturating counter stored in a uint8_t (0-1: not taken, 2-3: taken)

// how the algorithm works:
// 1. maintains a base predictor table T0 of the size 2048 and each entry is a 2-bit counter
// 2. 6 other tagged tabled T1-T6 which have geometrically increasing GHR lengths of {4, 8, 16, 32, 64, 128, 256, 512} 
// 3. each tag table entry has a tag, a 2-bit counter and a 2-bit usefulness counter
// 4. for each branch, the comparison is done between the calculated tag and tthe tag stored in the table to find a match, if a match is found, the corresponding counter is used to make a prediction
// 5. the predictor first checks the base predictor table T0 if a match is found
// 6. then it checks the tagged tables T1-T6 for a match, if match found, the longest history matching table is used as the provider
// 7. if no match found in the tagged tables, the base predictor is used

// in more easier terms:
// The predictor searches the tables from shortest to longest history. Whenever it finds a matching tag, it replaces the current provider. 
// At the end, the provider is the longest-history table that matched.
// If no tag matches, it uses the base predictor.

// how the pc and history map to each table: that is tag and index calculation
// 1. the index is calculated by mixing the pc and the history using a hash function
// 2. the tag is calculated by taking a portion of the pc and the history

namespace {
  const int T0_SIZE = 2048;
  const int NUM_TAGGED_TABLES = 6;
  const int TAG_TABLE_SIZE = 512;
  const int NUM_TAG_BITS = 10;
  const int MAX_GHR_SIZE = 128;

  // mask to extract the tag from a hash.
  const uint32_t TAG_MASK = (1u << NUM_TAG_BITS) - 1; // keeps the low NUM_TAG_BITS bits of a tag
  
  // gometrically increasing global history lengths.
  const int HISTORY_LENGTHS[NUM_TAGGED_TABLES] = {4, 8, 16, 32, 64, 128};

  // base T0 predictor table
  uint8_t base_table[T0_SIZE];
  
  // struct for the tagged table entries
  struct TagtableEntries{
    uint16_t tag;
    uint8_t counter; // 0-1: not taken, 2-3: taken
    uint8_t usefulness_counter; // 2-bit usefulness counter
  };

  // tagged predictor tables T1-T7
  TagtableEntries tagged_tables[NUM_TAGGED_TABLES][TAG_TABLE_SIZE];

  // a global history register to store the branch outcomes or history where history[0] is the most recent branch outcome
  uint8_t global_history[MAX_GHR_SIZE];

  // helper functions for the predictor
  // Extract one 2-bit counter.
  bool get_counter_prediction(size_t counter) {
    return counter >= 2; // predict taken if the counter is 10 or 11 and not-taken for vice versa 00 and 01
  }

  // Move the counter toward the actual outcome, saturating at 00 and 11.
  void update_counter(uint8_t &counter, bool taken) {
    if (taken)
    {
      if (counter < 3) {
        ++counter;
      }
    } else {
      if (counter > 0) {
        --counter;
      }
    }
  }

  // update global history register: shifting every outcome one place older and insert the newest outcome at index 0
  void shift_history(bool taken) {
    for (int i = MAX_GHR_SIZE - 1; i > 0; i--) {
      global_history[i] = global_history[i - 1];
    }
    global_history[0] = taken;
  }

  // updating the usefulness counter up or down, saturating at 0 and 3 <- identical to the 2-bit counter update funtion
  void update_usefulness(uint8_t &useful, bool increase) {
    if (increase)
    {
      if (useful < 3) {
        ++useful;
      }
    } else {
      if (useful > 0) {
        --useful;
      }
    }
  }

  // Mix PC and history information to produce a hash <- identical to lowbias32, published by Chris Wellons in the GitHub project hash-prospector
  uint32_t mix_hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
  }

  // Fold the most recent 'length' outcomes into a hash
  uint32_t history_hash(int length) {
    uint32_t h = 0x9e3779b9U ^ (uint32_t)length;

    for (int i = 0; i < length; i++) {
      h ^= (uint32_t)global_history[i] + 0x9e3779b9U + (h << 6) + (h >> 2);
      h = mix_hash(h);
    }

    return h;
  }

  // Get the index into the given tagged table from the PC and the history
  int table_index(uint64_t ip, int table) {
    int length = HISTORY_LENGTHS[table];
    uint32_t pc = (uint32_t)(ip >> 2);
    uint32_t hist = history_hash(length);

    return (int)(mix_hash(pc ^ hist ^ (0x9e3779b9U * (table + 1))) % TAG_TABLE_SIZE);
  }

  // Get the tag for the given tagged table from the PC and the history
  uint16_t make_tag(uint64_t ip, int table) {
    int length = HISTORY_LENGTHS[table];
    uint32_t pc = (uint32_t)(ip >> 2);
    uint32_t hist = history_hash(length);

    uint16_t tag = (uint16_t)(mix_hash(pc ^ (hist >> 1) ^ (0x85ebca6bU * (table + 1))) & TAG_MASK);

    // reserving the tag zero to represent an unused entry
    if (tag == 0) {
      tag = 1;
    }

    return tag;
  }
}

// // set up for data structures
// // initialization -> called only once at the start
 void openbranch::initialize_branch_predictor() {
  // initializing all counters to weakly not taken (01)
  for (int i = 0; i < T0_SIZE; i++) {
      base_table[i] = 1;
  }

  for (int t = 0; t < NUM_TAGGED_TABLES; t++) {
      for (int i = 0; i < TAG_TABLE_SIZE; i++) {
          tagged_tables[t][i].tag = 0;
          tagged_tables[t][i].counter = 1;
          tagged_tables[t][i].usefulness_counter = 0;
      }
  }

  // initialize the global history
  for (int i = 0; i < MAX_GHR_SIZE; i++) {
      global_history[i] = 0; // outcome: not taken or false
  }
}

// predicts the branch is taken or not-taken by returning a boolean true for taken and false for not-taken
// called every branch before the branch direction and target address are resolved
bool openbranch::predict(uint64_t ip) {
  int base_index = (int)((ip >> 2) % T0_SIZE);

  bool base_prediction = get_counter_prediction(base_table[base_index]);

  int provider_table = -1;

  // search from short to long tables, each hit from a longer table (most recent found) replaces the provider,
  // so the final provider is the longest-history matching table.
  for (int t = 0; t < NUM_TAGGED_TABLES; t++) {
    int index = table_index(ip, t);
    uint16_t tag = make_tag(ip, t);

    if (tagged_tables[t][index].tag == tag) {
      provider_table = t;
    }
  }

  // if none of the tagged tables matched, use the base predictor
  if (provider_table == -1) {
    return base_prediction;
  }

  // Simple baseline TAGE policy:
  // always use the longest-history matching provider
  int provider_index = table_index(ip, provider_table);
  TagtableEntries& provider = tagged_tables[provider_table][provider_index];

  return get_counter_prediction(provider.counter);
}

// updates data structures for the branch outcomes
// Called on each conditional branch instruction, after the branch prediction and target address are resolved
// Update the counter using the actual branch outcome
void openbranch::update(uint64_t ip, bool resolveDir) {
  int base_index = (int)((ip >> 2) % T0_SIZE);

  int provider_table = -1;
  int alternate_table = -1;
  bool base_prediction = get_counter_prediction(base_table[base_index]);
  bool alternate_prediction = base_prediction;

  for (int t = 0; t < NUM_TAGGED_TABLES; t++) {
    int index = table_index(ip, t);
    uint16_t tag = make_tag(ip, t);

    if (tagged_tables[t][index].tag == tag) {
      if (provider_table != -1) {
        alternate_table = provider_table;
      }

      provider_table = t;
    }
  }

  if (alternate_table != -1) {
    int alt_index = table_index(ip, alternate_table);
    alternate_prediction = get_counter_prediction(tagged_tables[alternate_table][alt_index].counter);
  }

  // if a provider is found, use its prediction; otherwise, use the base prediction
  bool provider_prediction = base_prediction;

  if (provider_table != -1) {
    int provider_index = table_index(ip, provider_table);
    provider_prediction = get_counter_prediction(tagged_tables[provider_table][provider_index].counter);
  }

  // Training the base predictor on every branch
  update_counter(base_table[base_index], resolveDir);

  // Train the matching tagged provider
  if (provider_table != -1) {
    int provider_index = table_index(ip, provider_table);
    TagtableEntries& provider = tagged_tables[provider_table][provider_index];

    update_counter(provider.counter, resolveDir);

    // Usefulness measures whether the provider beats the alternate
    if (provider_prediction != alternate_prediction) {
      update_usefulness(provider.usefulness_counter, provider_prediction == resolveDir);
    }
  }

  /*
   * --------------------------------------------------------------------------
   * Handling a misprediction: Allocate one longer-history entry
   *    if no provider  -> any tagged table is eligible
   *    else if has provider -> only tables longer than the provider are eligible to allocate
   * --------------------------------------------------------------------------
  */
  if (provider_prediction != resolveDir) {
    int first_candidate = provider_table + 1;

    for (int t = first_candidate; t < NUM_TAGGED_TABLES; t++) {
      int index = table_index(ip, t);
      TagtableEntries& entry = tagged_tables[t][index];

      if (entry.usefulness_counter == 0) {
        entry.tag = make_tag(ip, t);
        if (resolveDir) {
          entry.counter = 2; // weakly taken
        } else {
          entry.counter = 1; // weakly not taken
        }
        entry.usefulness_counter = 0;
        break; // At most one allocation per misprediction
      } else {
        // reduce the usefulness count of the entry
        update_usefulness(entry.usefulness_counter, false);
      }
    }
  }

  // updating the history at the very last, so the lookup and training above see the same history that was used for the prediction
  shift_history(resolveDir);
}


