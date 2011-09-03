#include "flow_table.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "hashing.h"

#ifdef TESTING
/* This is for testing only */
static uint32_t (*alternate_hash_function)(const char* data, int len) = NULL;
#endif

static int flow_entry_compare(flow_table_entry_t* first,
                                   flow_table_entry_t* second) {
    return first->occupied == second->occupied
        && first->ip_source == second->ip_source
        && first->ip_destination == second->ip_destination
        && first->transport_protocol == second->transport_protocol
        && first->port_source == second->port_source
        && first->port_destination == second->port_destination;
}

void flow_table_init(flow_table_t* table) {
  memset(table->entries, '\0', sizeof(table->entries));
  table->num_elements = 0;
  table->base_timestamp_seconds = 0;
  table->num_expired_flows = 0;
  table->num_dropped_flows = 0;
}

int flow_table_process_flow(flow_table_t* table,
                            flow_table_entry_t* new_entry,
                            const struct timeval* timestamp) {
  uint32_t hash;
  int probe;
  int first_available = -1;

  new_entry->occupied = ENTRY_OCCUPIED;
  hash = fnv_hash_32((char *)new_entry, sizeof(*new_entry));
#ifdef TESTING
  if (alternate_hash_function) {
    hash = alternate_hash_function((char *)new_entry, sizeof(*new_entry));
  }
#endif

  for (probe = 0; probe < HT_NUM_PROBES; ++probe) {
    uint32_t table_idx
      = (uint32_t)(hash + HT_C1*probe + HT_C2*probe*probe) % FLOW_TABLE_ENTRIES;
    flow_table_entry_t* entry = &table->entries[table_idx];
    if (entry->occupied == ENTRY_OCCUPIED
        && table->base_timestamp_seconds
            + entry->last_update_time_seconds
            + FLOW_TABLE_EXPIRATION_SECONDS < timestamp->tv_sec) {
      entry->occupied = ENTRY_DELETED;
      --table->num_elements;
      ++table->num_expired_flows;
    }
    if (flow_entry_compare(new_entry, entry)) {
      entry->last_update_time_seconds
          = timestamp->tv_sec - table->base_timestamp_seconds;
      return table_idx;
    }
    if (entry->occupied != ENTRY_OCCUPIED) {
      if (first_available < 0) {
        first_available = table_idx;
      }
      if (entry->occupied == ENTRY_EMPTY) {
        break;
      }
    }
  }

  if (first_available >= 0) {
    if (table->num_elements == 0) {
      table->base_timestamp_seconds = timestamp->tv_sec;
    }
    new_entry->last_update_time_seconds
        = timestamp->tv_sec - table->base_timestamp_seconds;
    table->entries[first_available] = *new_entry;
    ++table->num_elements;
    return first_available;
  }

  ++table->num_dropped_flows;
  return -1;
}

int flow_table_write_update(flow_table_t* table, FILE* handle) {
  if (fprintf(handle,
              "%ld %u %d %d",
              table->base_timestamp_seconds,
              table->num_elements,
              table->num_expired_flows,
              table->num_dropped_flows) < 0) {
    perror("Error sending update");
    return -1;
  }

  int idx;
  for (idx = 0; idx < table->num_elements; ++idx) {
    if (table->entries[idx].occupied == ENTRY_OCCUPIED) {
      if (fprintf(handle,
            "%u %u %hhu %hu %hu\n",
            table->entries[idx].ip_source,
            table->entries[idx].ip_destination,
            table->entries[idx].transport_protocol,
            table->entries[idx].port_source,
            table->entries[idx].port_destination) < 0) {
        perror("Error sending update");
        return -1;
      }
    }
  }
  fprintf(handle, "\n");

  return 0;
}

#ifdef TESTING
void testing_set_hash_function(uint32_t (*hasher)(const char* data, int len)) {
  alternate_hash_function = hasher;
}
#endif
