#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "all_tests.h"
#include "command_description.h"
#include "executor.h"
#include "time_utils.h"

/* helper */
static void create_trivial_counter_variable(VARZExecutor_t *executor) {
  struct VARZOperationDescription desc;

  desc.op = VARZOP_MHT_COUNTER_ADD;
  strcpy(desc.variable_name, "foobar");
  desc.op_data.counter_add_op.time = 27;
  desc.op_data.counter_add_op.amt = 1;
  VARZExecutorExecute(executor, &desc);
}

static int test_executor_ignores_invalid_op() {
  VARZExecutor_t executor;
  struct VARZOperationDescription desc;
  VARZExecutorInit(&executor, 2);

  desc.op = VARZOP_INVALID;

  VARZExecutorExecute(&executor, &desc);

  if (executor.mht_counters_ht.total_entries != 0) {
    return 1;
  } else if(executor.mht_samplers_ht.total_entries != 0) {
    return 1;
  }
  VARZExecutorFree(&executor);
  return 0;
}


static int test_executor_with_new_mnt_counter_add_op() {
  VARZExecutor_t executor;
  struct VARZOperationDescription desc;
  VARZExecutorInit(&executor, 2);

  desc.op = VARZOP_MHT_COUNTER_ADD;
  strcpy(desc.variable_name, "foobar");
  desc.op_data.counter_add_op.time = 27;
  desc.op_data.counter_add_op.amt = 1;

  VARZExecutorExecute(&executor, &desc);

  if (executor.mht_counters_ht.total_entries != 1) {
    return 1;
  } else if(executor.mht_samplers_ht.total_entries != 0) {
    return 1;
  }

  VARZExecutorFree(&executor);
  return 0;
}


static int test_executor_with_repeat_mnt_counter_add_op() {
  VARZExecutor_t executor;
  struct VARZOperationDescription desc;
  VARZExecutorInit(&executor, 2);

  desc.op = VARZOP_MHT_COUNTER_ADD;
  strcpy(desc.variable_name, "foobar");
  desc.op_data.counter_add_op.time = 27;
  desc.op_data.counter_add_op.amt = 1;

  // First Execution
  VARZExecutorExecute(&executor, &desc);

  desc.op_data.counter_add_op.amt = 6; 
  desc.op_data.counter_add_op.time = 42;
  VARZExecutorExecute(&executor, &desc);

  if (executor.mht_counters_ht.total_entries != 1) {
    return 1;
  } else if(executor.mht_samplers_ht.total_entries != 0) {
    return 1;
  }

  VARZExecutorFree(&executor);
  return 0;
}


static int test_executor_with_new_mnt_sampler_add_op() {
  VARZExecutor_t executor;
  struct VARZOperationDescription desc;
  VARZExecutorInit(&executor, 2);

  desc.op = VARZOP_MHT_SAMPLE_ADD;
  strcpy(desc.variable_name, "foobaz");
  desc.op_data.sampler_add_op.time = 27;
  desc.op_data.sampler_add_op.value = 1;
  desc.op_data.sampler_add_op.random_vals[0] = 1;
  desc.op_data.sampler_add_op.random_vals[1] = 2;

  VARZExecutorExecute(&executor, &desc);

  if (executor.mht_counters_ht.total_entries != 0) {
    return 1;
  } else if(executor.mht_samplers_ht.total_entries != 1) {
    return 1;
  }

  VARZExecutorFree(&executor);
  return 0;
}


static int test_executor_with_repeat_mnt_sampler_add_op() {
  VARZExecutor_t executor;
  struct VARZOperationDescription desc;
  VARZExecutorInit(&executor, 2);

  desc.op = VARZOP_MHT_SAMPLE_ADD;
  strcpy(desc.variable_name, "foobaz");
  desc.op_data.sampler_add_op.time = 27;
  desc.op_data.sampler_add_op.value = 1;
  desc.op_data.sampler_add_op.random_vals[0] = 1;
  desc.op_data.sampler_add_op.random_vals[1] = 2;

  VARZExecutorExecute(&executor, &desc);
  
  desc.op_data.sampler_add_op.time = 42;
  desc.op_data.sampler_add_op.value = 11;
  VARZExecutorExecute(&executor, &desc);

  if (executor.mht_counters_ht.total_entries != 0) {
    return 1;
  } else if(executor.mht_samplers_ht.total_entries != 1) {
    return 1;
  }

  VARZExecutorFree(&executor);
  return 0;
}

//TODO: Test the visitor
static int test_executor_all_dump_json_trivial() {
  char *expected_result;
  void *result;
  VARZExecutor_t executor;
  struct VARZOperationDescription desc;
  VARZCurrentTimeStubValue(1);
  VARZExecutorInit(&executor, 2);

  memset(&desc, 0, sizeof(desc));
  desc.op = VARZOP_ALL_DUMP_JSON;

  result = VARZExecutorExecute(&executor, &desc);

  expected_result = "{\"mht_counters\":[],\"mht_samplers\":[],\"metadata\":{\"start_time\":1}}";
  if(strcmp(expected_result, result)) {
    printf("Error test_executor_all_list_json_trivial, expected '%s', got '%s'\n", expected_result,
           (char*)result);
    return 1;
  }
  free(result);
  VARZExecutorFree(&executor);
  return 0;
}

// TODO: Test the visitor
static int test_executor_all_list_json_trivial() {
  char *expected_result;
  void *result;
  VARZExecutor_t executor;
  struct VARZOperationDescription desc;
  VARZExecutorInit(&executor, 2);

  memset(&desc, 0, sizeof(desc));
  desc.op = VARZOP_ALL_LIST_JSON;

  result = VARZExecutorExecute(&executor, &desc);

  expected_result = "{\"mht_counters\":[],\"mht_samplers\":[]}";
  if(strcmp(expected_result, result)) {
    return 1;
  }
  free(result);
  VARZExecutorFree(&executor);
  return 0;
}

static int test_executor_all_flush() {
  VARZExecutor_t executor;
  struct VARZOperationDescription desc;
  VARZExecutorInit(&executor, 2);

  create_trivial_counter_variable(&executor);

  // Now flush it to remove it
  desc.op = VARZOP_ALL_FLUSH;
  VARZExecutorExecute(&executor, &desc);

  // Now it should be empty
  if (executor.mht_counters_ht.total_entries != 0) {
    return 1;
  } else if(executor.mht_samplers_ht.total_entries != 0) {
    return 1;
  }

  VARZExecutorFree(&executor);
  return 0;
}

static int test_executor_counter_get_op() {
  char *result;
  VARZExecutor_t executor;
  struct VARZOperationDescription desc;
  VARZExecutorInit(&executor, 2);

  create_trivial_counter_variable(&executor);

  desc.op = VARZOP_MHT_COUNTER_GET;
  strcpy(desc.variable_name, "foobar");

  result = VARZExecutorExecute(&executor, &desc);

  /* Just make sure we get a non-empty string result */

  if(result == NULL) {
    return 1;
  }
  if(!strcmp(result, "")) {
    return 1;
  }

  VARZExecutorFree(&executor);
  return 0;
}

static int test_executor_counter_get_op_nonexistent_key() {
  char *result;
  char *expected_result = "";
  VARZExecutor_t executor;
  struct VARZOperationDescription desc;
  VARZExecutorInit(&executor, 2);

  desc.op = VARZOP_MHT_COUNTER_GET;
  strcpy(desc.variable_name, "foobar");

  result = VARZExecutorExecute(&executor, &desc);
  if(strcmp(expected_result, result)) {
    return 1;
  }
  VARZExecutorFree(&executor);
  return 0;
}


int executor_tests() {
  int failure_count = 0;
  failure_count += test_executor_ignores_invalid_op();
  failure_count += test_executor_with_new_mnt_counter_add_op();
  failure_count += test_executor_with_repeat_mnt_counter_add_op();
  failure_count += test_executor_with_new_mnt_sampler_add_op();
  failure_count += test_executor_with_repeat_mnt_sampler_add_op();
  failure_count += test_executor_all_dump_json_trivial();
  failure_count += test_executor_all_list_json_trivial();
  failure_count += test_executor_all_flush();
  failure_count += test_executor_counter_get_op();
  failure_count += test_executor_counter_get_op_nonexistent_key();
  return failure_count;
}
