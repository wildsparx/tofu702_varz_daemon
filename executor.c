#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "sds/sds.h"

#include "executor.h"
#include "json_helpers.h"
#include "hash_funcs.h"
#include "hash_table.h"
#include "time_utils.h"


#define DEFAULT_START_TIME 0
#define DEFAULT_SAMPLER_SIZE 1000

/***** HELPER DATA TYPES *****/
enum EntryType {
  ET_UNKNOWN = 0,
  ETMHTCounter = 1,
  ETMHTSampler = 2
};

struct VisitorData {
  sds *dest;
  int counter;
  enum EntryType type;
};


/***** STATIC HELPER PROTOTYPES *****/
static void handleMHTCounterAdd(VARZExecutor_t *executor, struct VARZOperationDescription *op);
static char *handleMHTCounterGet(VARZExecutor_t *executor, struct VARZOperationDescription *op);
static void handleMHTSamplerAdd(VARZExecutor_t *executor, struct VARZOperationDescription *op);
static char *handleALLDumpJSON(VARZExecutor_t *executor, struct VARZOperationDescription *op);
static char *handleALLListJSON(VARZExecutor_t *executor, struct VARZOperationDescription *op);
static void handleALLFlush(VARZExecutor_t *executor, struct VARZOperationDescription *op);
static void JSONDictKeysList(VARZHashTable_t *ht, sds *dest);
static void JSONKeyListVisitor(struct VARZHashTableEntry *entry, void *data);
static void JSONReprVisitor(struct VARZHashTableEntry *entry, void *data);
static void MHTCountersJSONRepr(VARZExecutor_t *executor, sds *dest);
static void MHTSamplersJSONRepr(VARZExecutor_t *executor, sds *dest);
static void metadataJSONRepr(VARZExecutor_t *executor, sds *dest);
static void MHTCounterFreeVisitor(struct VARZHashTableEntry *entry, void *data);
static void MHTSamplersFreeVisitor(struct VARZHashTableEntry *entry, void *data);


/***** INTERFACE IMPLEMENTATION *****/

void VARZExecutorInit(VARZExecutor_t *executor, unsigned long hash_table_size) {
  memset(executor, 0, sizeof(VARZExecutor_t));
  VARZHashTableInit(&(executor->mht_counters_ht), hash_table_size);
  VARZHashTableInit(&(executor->mht_samplers_ht), hash_table_size);
  executor->metadata.executor_start_time = VARZCurrentTime();
}

void VARZExecutorFree(VARZExecutor_t *executor) {
  VARZHashTableVisit(&(executor->mht_counters_ht), MHTCounterFreeVisitor, NULL);
  VARZHashTableFree(&(executor->mht_counters_ht));
  VARZHashTableVisit(&(executor->mht_samplers_ht), MHTSamplersFreeVisitor, NULL);
  VARZHashTableFree(&(executor->mht_samplers_ht));
}


void *VARZExecutorExecute(VARZExecutor_t *executor, struct VARZOperationDescription *op) {
  // TODO Come up with a less stupid way of doing this
  switch (op->op) {
    case VARZOP_INVALID:
      return NULL;
    case VARZOP_MHT_COUNTER_ADD:
      handleMHTCounterAdd(executor, op);
      return NULL;
    case VARZOP_MHT_SAMPLE_ADD:
      handleMHTSamplerAdd(executor, op);
      return NULL;
    case VARZOP_ALL_DUMP_JSON:
      return handleALLDumpJSON(executor, op);
    case VARZOP_ALL_LIST_JSON:
      return handleALLListJSON(executor, op);
    case VARZOP_ALL_FLUSH:
      handleALLFlush(executor, op);
      return NULL;
    case VARZOP_MHT_COUNTER_GET:
      return handleMHTCounterGet(executor, op);
  }

  // We got an invalid op...
  assert(0);
}


/***** STATIC HELPERS *****/
static void handleMHTCounterAdd(VARZExecutor_t *executor, struct VARZOperationDescription *op) {
  VARZMHTIntCounter_t *counter;
  struct VARZMHTCounterAddOp *counter_add_op;
  uint64_t name_hash = VARZHashString(op->variable_name);
  counter = VARZHashTableGet(&(executor->mht_counters_ht), op->variable_name, name_hash);

  // Check if it exists
  if (!counter) {
    counter = calloc(1, sizeof(VARZMHTIntCounter_t));
    VARZMHTIntCounterInit(counter, DEFAULT_START_TIME);
    VARZHashTableAdd(&(executor->mht_counters_ht), op->variable_name, name_hash, counter);
  }

  counter_add_op = &(op->op_data.counter_add_op);
  VARZMHTIntCounterIncrement(counter, counter_add_op->time, counter_add_op->amt);
}

static char *handleMHTCounterGet(VARZExecutor_t *executor, struct VARZOperationDescription *op) {
  char *returnme;
  sds return_sds = sdsempty();
  sds *dest = &return_sds;
  VARZMHTIntCounter_t *counter;
  uint64_t name_hash = VARZHashString(op->variable_name);
  counter = VARZHashTableGet(&(executor->mht_counters_ht), op->variable_name, name_hash);
  if(counter) { // found
    VARZMHTIntCounterJSONRepr(counter, dest);
  }
  returnme = strdup(return_sds);
  sdsfree(return_sds);
  return returnme;
}

static void handleMHTSamplerAdd(VARZExecutor_t *executor, struct VARZOperationDescription *op) {
  VARZMHTIntSampler_t *sampler;
  struct VARZMHTSamplerAddOp *sampler_add_op;
  uint64_t name_hash = VARZHashString(op->variable_name);
  sampler = VARZHashTableGet(&(executor->mht_samplers_ht), op->variable_name, name_hash);

  if (!sampler) {
    sampler = calloc(1, sizeof(VARZMHTIntSampler_t));
    VARZMHTIntSamplerInit(sampler, DEFAULT_START_TIME, DEFAULT_SAMPLER_SIZE);
    VARZHashTableAdd(&(executor->mht_samplers_ht), op->variable_name, name_hash, sampler);
  }
  sampler_add_op = &(op->op_data.sampler_add_op);
  VARZMHTIntSamplerAddSample(sampler, sampler_add_op->time, sampler_add_op->value,
                             sampler_add_op->random_vals);
}

static char *handleALLDumpJSON(VARZExecutor_t *executor, struct VARZOperationDescription *op) {
  char *returnme;
  sds return_sds = sdsempty();
  sds *dest = &return_sds;
  VARZJSONDictStart(dest);
  VARZJSONDictKey(dest, "mht_counters");
  MHTCountersJSONRepr(executor, dest);

  VARZJSONDictNextKey(dest);
  VARZJSONDictKey(dest, "mht_samplers");
  MHTSamplersJSONRepr(executor, dest);

  VARZJSONDictNextKey(dest);
  VARZJSONDictKey(dest, "metadata");
  metadataJSONRepr(executor, dest);

  VARZJSONDictEnd(dest);

  returnme = strdup(return_sds);
  sdsfree(return_sds);
  return returnme;
}


static char *handleALLListJSON(VARZExecutor_t *executor, struct VARZOperationDescription *op) {
  char *returnme;
  sds return_sds = sdsempty();

  sds *dest = &return_sds;

  VARZJSONDictStart(dest);

  VARZJSONDictKey(dest, "mht_counters");
  JSONDictKeysList(&(executor->mht_counters_ht), dest);

  VARZJSONDictNextKey(dest);
  VARZJSONDictKey(dest, "mht_samplers");
  JSONDictKeysList(&(executor->mht_samplers_ht), dest);

  VARZJSONDictEnd(dest);

  returnme = strdup(return_sds);
  sdsfree(return_sds);
  return returnme;
}


static void handleALLFlush(VARZExecutor_t *executor, struct VARZOperationDescription *op) {
  unsigned long hash_table_size = executor->mht_counters_ht.num_slots;
  VARZExecutorFree(executor);
  VARZExecutorInit(executor, hash_table_size);
}


static void JSONDictKeysList(VARZHashTable_t *ht, sds *dest) {
  struct VisitorData vd;
  vd.dest = dest;
  vd.counter = 0;
  vd.type = ET_UNKNOWN;
  VARZJSONArrayStart(dest);
  VARZHashTableVisit(ht, JSONKeyListVisitor, &vd);
  VARZJSONArrayEnd(dest);
}

static void JSONKeyListVisitor(struct VARZHashTableEntry *entry, void *data) {
  struct VisitorData *vd = (struct VisitorData*) data;
  sds *dest = vd->dest;
  if(vd->counter != 0) {
    VARZJSONArrayNextItem(dest);
  }
  VARZJSONStringRepr(dest, entry->name);
  vd->counter ++;
}

static void JSONReprVisitor(struct VARZHashTableEntry *entry, void *data) {
  struct VisitorData *vd = (struct VisitorData*) data;
  sds *dest = vd->dest;
  if (vd->counter != 0) {
    VARZJSONArrayNextItem(dest);
  }

  VARZJSONDictStart(dest);

  VARZJSONDictKey(dest, "name");
  VARZJSONStringRepr(dest, entry->name);

  VARZJSONDictNextKey(dest);
  VARZJSONDictKey(dest, "value");
  switch (vd->type) {
    case ETMHTCounter:
      VARZMHTIntCounterJSONRepr(entry->value, dest);
      break;
    case ETMHTSampler:
      VARZMHTIntSamplerJSONRepr(entry->value, dest);
      break;
    default:
      assert(1); // Should never pass
  }

  VARZJSONDictNextKey(dest);
  VARZJSONDictKey(dest, "name_hash");
  VARZJSONUnsignedLongRepr(dest, entry->name_hash);

  VARZJSONDictEnd(dest);
  vd->counter ++;
}


static void MHTCountersJSONRepr(VARZExecutor_t *executor, sds *dest) {
  struct VisitorData vd;
  vd.dest = dest;
  vd.counter = 0;
  vd.type = ETMHTCounter;

  VARZJSONArrayStart(dest);
  VARZHashTableVisit(&(executor->mht_counters_ht), &JSONReprVisitor, &vd);
  VARZJSONArrayEnd(dest);
}


static void MHTSamplersJSONRepr(VARZExecutor_t *executor, sds *dest) {
  struct VisitorData vd;
  vd.dest = dest;
  vd.counter = 0;
  vd.type = ETMHTSampler;

  VARZJSONArrayStart(dest);
  VARZHashTableVisit(&(executor->mht_samplers_ht), &JSONReprVisitor, &vd);
  VARZJSONArrayEnd(dest);
}


static void metadataJSONRepr(VARZExecutor_t *executor, sds *dest) {
  VARZJSONDictStart(dest);

  // We won't call it executor_start_time since that's not part of the abstraction
  VARZJSONDictKey(dest, "start_time");
  VARZJSONUnsignedLongRepr(dest, executor->metadata.executor_start_time);

  VARZJSONDictEnd(dest);
}


static void MHTCounterFreeVisitor(struct VARZHashTableEntry *entry, void *data) {
  free(entry->value);
}

static void MHTSamplersFreeVisitor(struct VARZHashTableEntry *entry, void *data) {
  VARZMHTIntSamplerFree((VARZMHTIntSampler_t*)entry->value);
  free(entry->value);
}
