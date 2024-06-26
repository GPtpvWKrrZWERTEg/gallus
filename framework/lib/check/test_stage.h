/* 
 * $__Copyright__$
 */
#ifndef __TEST_STAGE_H__
#define __TEST_STAGE_H__





#include "gallus_apis.h"
#include "gallus_pipeline_stage_internal.h"





typedef enum {
  test_stage_type_unknown = 0,
  test_stage_type_ingress,
  test_stage_type_intermediate,
  test_stage_type_egress
} test_stage_type_t;


typedef struct {
  size_t m_offset;
  size_t m_length;
} enqueue_info_t;


typedef enum {
  test_stage_state_unknown = 0,
  test_stage_state_initialized,
  test_stage_state_running,
  test_stage_state_done
} test_stage_state_t;


typedef struct test_stage_record {
  base_stage_record m_base_stg;

  test_stage_type_t m_type;

  gallus_mutex_t m_lock;
  gallus_cond_t m_cond;
  volatile bool m_do_exit;

  uint64_t *m_data;
  size_t m_n_data;
  enqueue_info_t *m_enq_infos;	/* The length is # of workers */

  test_stage_state_t *m_states;	/* The length is # of workers */

  size_t m_weight;

  volatile uint64_t m_sum;
  volatile size_t m_n_events;
  volatile uint64_t m_start_clock;
  volatile uint64_t m_end_clock;
  gallus_chrono_t m_start_time;
  gallus_chrono_t m_end_time;
} test_stage_record;
typedef test_stage_record	*test_stage_t;


typedef struct test_stage_spec_t {
  size_t m_n_workers;
  size_t m_n_qs;
  size_t m_q_len;
  size_t m_n_events;
  size_t m_batch_size;
  base_stage_sched_t m_sched_type;
  base_stage_fetch_t m_fetch_type;
  gallus_chrono_t m_to;
} test_stage_spec_t;





#endif /* ! __TEST_STAGE_H__ */
