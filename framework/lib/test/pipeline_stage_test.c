/* 
 * $__Copyright__$
 */
#include "unity.h"
#include "gallus_apis.h"
#include "gallus_pipeline_stage.h"

#define LOOP_MAX 100
#define SLEEP usleep(0.1 * 1000 * 1000)

#include "test_lib_util.h"

static uint64_t called_counter[PIPELINE_FUNC_MAX];
static volatile bool do_stop = false;
static gallus_mutex_t lock = NULL;

#define PWAIT_TIME_OUT (1000LL * 1000LL * 1000LL)

static inline void
p_lock(void) {
  if (lock != NULL) {
    gallus_mutex_lock(&lock);
  }
}

static inline void
p_unlock(void) {
  if (lock != NULL) {
    gallus_mutex_unlock(&lock);
  }
}

static void
called_counter_reset(void) {
  p_lock();
  memset(&called_counter, 0, sizeof(called_counter));
  p_unlock();
}

static gallus_pipeline_stage_t
stage_alloc(void) {
  gallus_pipeline_stage_t stage;

  stage = (gallus_pipeline_stage_t)malloc(1);
  if (stage == NULL) {
    TEST_FAIL_MESSAGE("stage is NULL.");
  }
  return stage;
}

static void
called_func_count(enum func_type type) {
  p_lock();
  switch (type) {
    case PIPELINE_FUNC_FETCH:
    case PIPELINE_FUNC_MAIN:
    case PIPELINE_FUNC_THROW:
      if (called_counter[type] != 0) {
        goto done;
      }
      break;
    default:
      break;
  }
  called_counter[type]++;

done:
  p_unlock();
}

static bool
check_called_func_count(uint64_t *counter) {
  bool ret = false;

  p_lock();
  if (memcmp(called_counter, counter,
             sizeof(called_counter)) == 0) {
    ret = true;
  } else {
    ret = false;
  }
  p_unlock();

  return ret;
}

static void
pipeline_pre_pause(const gallus_pipeline_stage_t *sptr) {
  (void)sptr;

  gallus_msg_debug(1, "called.\n");
  called_func_count(PIPELINE_FUNC_PRE_PAUSE);
}

static gallus_result_t
pipeline_setup(const gallus_pipeline_stage_t *sptr) {
  (void)sptr;

  gallus_msg_debug(1, "called.\n");
  called_func_count(PIPELINE_FUNC_SETUP);

  return GALLUS_RESULT_OK;
}

static gallus_result_t
pipeline_fetch(const gallus_pipeline_stage_t *sptr,
               size_t idx, void *buf, size_t max) {
  (void)sptr;
  (void)idx;
  (void)buf;
  (void)max;

  gallus_msg_debug(1, "called.\n");
  called_func_count(PIPELINE_FUNC_FETCH);

  return (do_stop == false) ? 1LL : 0LL;
}

static gallus_result_t
pipeline_main(const gallus_pipeline_stage_t *sptr,
              size_t idx, void *buf, size_t n) {
  (void)sptr;
  (void)buf;

  gallus_msg_debug(1, "called " PFSZ(u) ".\n", idx);

  called_func_count(PIPELINE_FUNC_MAIN);

  return (gallus_result_t)n;
}

static gallus_result_t
pipeline_throw(const gallus_pipeline_stage_t *sptr,
               size_t idx, void *buf, size_t n) {
  (void)sptr;
  (void)idx;
  (void)buf;

  gallus_msg_debug(1, "called.\n");
  called_func_count(PIPELINE_FUNC_THROW);

  return (gallus_result_t)n;
}

static gallus_result_t
pipeline_sched(const gallus_pipeline_stage_t *sptr,
               void *buf, size_t n, void *hint) {
  (void)sptr;
  (void)buf;
  (void)n;
  (void)hint;

  gallus_msg_debug(1, "called.\n");
  called_func_count(PIPELINE_FUNC_SCHED);

  return (gallus_result_t)n;
}

static gallus_result_t
pipeline_shutdown(const gallus_pipeline_stage_t *sptr,
                  shutdown_grace_level_t l) {
  (void)sptr;

  gallus_msg_debug(1, "called with \"%s\".\n",
                   (l == SHUTDOWN_RIGHT_NOW) ? "right now" : "gracefully");
  called_func_count(PIPELINE_FUNC_SHUTDOWN);

  return GALLUS_RESULT_OK;
}

static void
pipeline_finalize(const gallus_pipeline_stage_t *sptr,
                  bool is_canceled) {
  (void)sptr;

  gallus_msg_debug(1, "%s.\n",
                   (is_canceled == false) ? "exit normally" : "canceled");
  called_func_count(PIPELINE_FUNC_FINALIZE);

}

static void
pipeline_freeup(const gallus_pipeline_stage_t *sptr) {
  (void)sptr;

  gallus_msg_debug(1, "called.\n");
  called_func_count(PIPELINE_FUNC_FREEUP);
}

static gallus_result_t
pipeline_maint(const gallus_pipeline_stage_t *sptr, void *arg) {
  (void)sptr;
  (void)arg;

  gallus_msg_debug(1, "called.\n");
  called_func_count(PIPELINE_FUNC_MAINT);

  return GALLUS_RESULT_OK;
}

static void
pipeline_stage_create(gallus_pipeline_stage_t *sptr,
                      gallus_pipeline_stage_pre_pause_proc_t pre_pause_proc,
                      gallus_pipeline_stage_sched_proc_t sched_proc,
                      gallus_pipeline_stage_setup_proc_t setup_proc,
                      gallus_pipeline_stage_fetch_proc_t fetch_proc,
                      gallus_pipeline_stage_main_proc_t main_proc,
                      gallus_pipeline_stage_throw_proc_t throw_proc,
                      gallus_pipeline_stage_shutdown_proc_t shutdown_proc,
                      gallus_pipeline_stage_finalize_proc_t finalize_proc,
                      gallus_pipeline_stage_freeup_proc_t freeup_proc) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  size_t nthd = 1;

  ret = gallus_pipeline_stage_create(sptr, 0,
                                     "gallus_pipeline_stage_create",
                                     nthd,
                                     sizeof(void *), 1024,
                                     pre_pause_proc,
                                     sched_proc,
                                     setup_proc,
                                     fetch_proc,
                                     main_proc,
                                     throw_proc,
                                     shutdown_proc,
                                     finalize_proc,
                                     freeup_proc);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_create error.");
  TEST_ASSERT_NOT_EQUAL_MESSAGE(NULL, *sptr,
                                "stage is NULL.");
}

static void
pipeline_stage_create_start(gallus_pipeline_stage_t *sptr,
                            gallus_pipeline_stage_pre_pause_proc_t pre_pause_proc,
                            gallus_pipeline_stage_sched_proc_t sched_proc,
                            gallus_pipeline_stage_setup_proc_t setup_proc,
                            gallus_pipeline_stage_fetch_proc_t fetch_proc,
                            gallus_pipeline_stage_main_proc_t main_proc,
                            gallus_pipeline_stage_throw_proc_t throw_proc,
                            gallus_pipeline_stage_shutdown_proc_t shutdown_proc,
                            gallus_pipeline_stage_finalize_proc_t finalize_proc,
                            gallus_pipeline_stage_freeup_proc_t freeup_proc) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  pipeline_stage_create(sptr,
                        pre_pause_proc,
                        sched_proc,
                        setup_proc,
                        fetch_proc,
                        main_proc,
                        throw_proc,
                        shutdown_proc,
                        finalize_proc,
                        freeup_proc);

  ret = gallus_pipeline_stage_start(sptr);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_start error.");
  SLEEP;
  ret = global_state_set(GLOBAL_STATE_STARTED);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "global_state_set error.");
  SLEEP;
}

void
setUp(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  do_stop = false;
  called_counter_reset();

  ret = gallus_mutex_create(&lock);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_mutex_create error.");
}

void
tearDown(void) {
  gallus_mutex_destroy(&lock);
  lock = NULL;
}

void
test_gallus_pipeline_stage_create_destroy_normal(void) {
  uint64_t counter[PIPELINE_FUNC_MAX] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  gallus_pipeline_stage_t stage = NULL;

  pipeline_stage_create(&stage,
                        pipeline_pre_pause,
                        pipeline_sched,
                        pipeline_setup,
                        pipeline_fetch,
                        pipeline_main,
                        pipeline_throw,
                        pipeline_shutdown,
                        pipeline_finalize,
                        pipeline_freeup);

  gallus_pipeline_stage_destroy(&stage);
  TEST_ASSERT_COUNTER(true, counter,
                      "counter error.");
}

void
test_gallus_pipeline_stage_create_invalid_args(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;
  size_t nthd = 1;

  ret = gallus_pipeline_stage_create(NULL, 0,
                                     "test_error",
                                     nthd,
                                     sizeof(void *), 1024,
                                     pipeline_pre_pause,
                                     pipeline_sched,
                                     pipeline_setup,
                                     pipeline_fetch,
                                     pipeline_main,
                                     pipeline_throw,
                                     pipeline_shutdown,
                                     pipeline_finalize,
                                     pipeline_freeup);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "not invalid args.");

  ret = gallus_pipeline_stage_create(&stage, 0,
                                     "",
                                     nthd,
                                     sizeof(void *), 1024,
                                     pipeline_pre_pause,
                                     pipeline_sched,
                                     pipeline_setup,
                                     pipeline_fetch,
                                     pipeline_main,
                                     pipeline_throw,
                                     pipeline_shutdown,
                                     pipeline_finalize,
                                     pipeline_freeup);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "not invalid args.");

  ret = gallus_pipeline_stage_create(&stage, 0,
                                     "test_error",
                                     nthd,
                                     0, 1024,
                                     pipeline_pre_pause,
                                     pipeline_sched,
                                     pipeline_setup,
                                     pipeline_fetch,
                                     pipeline_main,
                                     pipeline_throw,
                                     pipeline_shutdown,
                                     pipeline_finalize,
                                     pipeline_freeup);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "not invalid args.");

  ret = gallus_pipeline_stage_create(&stage, 0,
                                     "test_error",
                                     nthd,
                                     sizeof(void *), 0,
                                     pipeline_pre_pause,
                                     pipeline_sched,
                                     pipeline_setup,
                                     pipeline_fetch,
                                     pipeline_main,
                                     pipeline_throw,
                                     pipeline_shutdown,
                                     pipeline_finalize,
                                     pipeline_freeup);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "not invalid args.");

#if 0
  ret = gallus_pipeline_stage_create(&stage, 0,
                                     "test_error",
                                     nthd,
                                     sizeof(void *), 1024,
                                     pipeline_pre_pause,
                                     NULL,
                                     pipeline_setup,
                                     pipeline_fetch,
                                     pipeline_main,
                                     pipeline_throw,
                                     pipeline_shutdown,
                                     pipeline_finalize,
                                     pipeline_freeup);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "not invalid args.");
#else
  /*
   * NULL check of the sched_proc is omitted from
   * commit:e0495bc27542156c5ad85dd53e579611bb241c94, in order to
   * support "ingress" stages.
   */
#endif

  ret = gallus_pipeline_stage_create(&stage, 0,
                                     "test_error",
                                     nthd,
                                     sizeof(void *), 1024,
                                     pipeline_pre_pause,
                                     pipeline_sched,
                                     pipeline_setup,
                                     pipeline_fetch,
                                     NULL,
                                     pipeline_throw,
                                     pipeline_shutdown,
                                     pipeline_finalize,
                                     pipeline_freeup);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "not invalid args.");

  ret = gallus_pipeline_stage_create(&stage, 0,
                                     "test_error",
                                     nthd,
                                     sizeof(void *), 1024,
                                     pipeline_pre_pause,
                                     pipeline_sched,
                                     pipeline_setup,
                                     NULL,
                                     NULL,
                                     pipeline_throw,
                                     pipeline_shutdown,
                                     pipeline_finalize,
                                     pipeline_freeup);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "not invalid args.");

  ret = gallus_pipeline_stage_create(&stage, 0,
                                     "test_error",
                                     nthd,
                                     sizeof(void *), 1024,
                                     pipeline_pre_pause,
                                     pipeline_sched,
                                     pipeline_setup,
                                     pipeline_fetch,
                                     NULL,
                                     NULL,
                                     pipeline_shutdown,
                                     pipeline_finalize,
                                     pipeline_freeup);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "not invalid args.");

  ret = gallus_pipeline_stage_create(&stage, 0,
                                     "test_error",
                                     nthd,
                                     sizeof(void *), 1024,
                                     pipeline_pre_pause,
                                     pipeline_sched,
                                     pipeline_setup,
                                     NULL,
                                     NULL,
                                     NULL,
                                     pipeline_shutdown,
                                     pipeline_finalize,
                                     pipeline_freeup);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "not invalid args.");

  /* after. */
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_create_already_exists(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage1 = NULL;
  gallus_pipeline_stage_t stage2 = NULL;
  size_t nthd = 1;

  /* set data */
  ret = gallus_pipeline_stage_create(&stage1, 0,
                                     "gallus_pipeline_stage_create",
                                     nthd,
                                     sizeof(void *), 1024,
                                     pipeline_pre_pause,
                                     pipeline_sched,
                                     pipeline_setup,
                                     pipeline_fetch,
                                     pipeline_main,
                                     pipeline_throw,
                                     pipeline_shutdown,
                                     pipeline_finalize,
                                     pipeline_freeup);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_create error.");
  TEST_ASSERT_NOT_EQUAL_MESSAGE(NULL, stage1,
                                "stage is NULL.");

  /* call func. */
  ret = gallus_pipeline_stage_create(&stage2, 0,
                                     "gallus_pipeline_stage_create",
                                     nthd,
                                     sizeof(void *), 1024,
                                     pipeline_pre_pause,
                                     pipeline_sched,
                                     pipeline_setup,
                                     pipeline_fetch,
                                     pipeline_main,
                                     pipeline_throw,
                                     pipeline_shutdown,
                                     pipeline_finalize,
                                     pipeline_freeup);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_ALREADY_EXISTS, ret,
                            "not already exists.");
  TEST_ASSERT_EQUAL_MESSAGE(NULL, stage2,
                            "stage is NULL.");

  /* after. */
  gallus_pipeline_stage_destroy(&stage1);
  gallus_pipeline_stage_destroy(&stage2);
}

void
test_gallus_pipeline_stage_setup_normal(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  uint64_t counter[PIPELINE_FUNC_MAX] = {0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create(&stage,
                        pipeline_pre_pause,
                        pipeline_sched,
                        pipeline_setup,
                        pipeline_fetch,
                        pipeline_main,
                        pipeline_throw,
                        pipeline_shutdown,
                        pipeline_finalize,
                        pipeline_freeup);

  /* call func. */
  ret = gallus_pipeline_stage_setup(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_setup(nomal) error.");
  TEST_ASSERT_COUNTER(true, counter,
                      "counter error.");

  /* after. */
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_setup_invalid_args(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;

  /* call func (stage is NUlL). */
  ret = gallus_pipeline_stage_setup(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "not invalid args.");

  /* call func (arg is NUlL). */
  ret = gallus_pipeline_stage_setup(NULL);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "not invalid args.");
}

void
test_gallus_pipeline_stage_setup_func_null(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create(&stage,
                        pipeline_pre_pause,
                        pipeline_sched,
                        NULL,
                        pipeline_fetch,
                        pipeline_main,
                        pipeline_throw,
                        pipeline_shutdown,
                        pipeline_finalize,
                        pipeline_freeup);

  /* FIXME */
  ret = gallus_pipeline_stage_setup(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "func_null error.");

  /* after. */
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_setup_double_call(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create(&stage,
                        pipeline_pre_pause,
                        pipeline_sched,
                        pipeline_setup,
                        pipeline_fetch,
                        pipeline_main,
                        pipeline_throw,
                        pipeline_shutdown,
                        pipeline_finalize,
                        pipeline_freeup);

  ret = gallus_pipeline_stage_setup(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_setup error.");

  /* call func. (double call) */
  ret = gallus_pipeline_stage_setup(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "double call error.");

  /* after. */
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_setup_invalid_state(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create(&stage,
                        pipeline_pre_pause,
                        pipeline_sched,
                        pipeline_setup,
                        pipeline_fetch,
                        pipeline_main,
                        pipeline_throw,
                        pipeline_shutdown,
                        pipeline_finalize,
                        pipeline_freeup);

  ret = gallus_pipeline_stage_start(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_start error.");

  /* call func. (state is STAGE_STATE_STARTED) */
  ret = gallus_pipeline_stage_setup(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_STATE_TRANSITION, ret,
                            "not invalid state.");

  /* after. */
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_setup_invalid_object(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = stage_alloc();

  /* call func. */
  ret = gallus_pipeline_stage_setup(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_OBJECT, ret,
                            "not invalid object.");

  /* afetr. */
  free(stage);
}

void
test_gallus_pipeline_stage_start_normal_fmt(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  uint64_t counter[PIPELINE_FUNC_MAX] = {0, 0, 0, 1, 1, 1, 0, 0, 0, 0};
  gallus_pipeline_stage_t stage = NULL;

  pipeline_stage_create_start(&stage,
                              pipeline_pre_pause,
                              pipeline_sched,
                              pipeline_setup,
                              pipeline_fetch,
                              pipeline_main,
                              pipeline_throw,
                              pipeline_shutdown,
                              pipeline_finalize,
                              pipeline_freeup);

  TEST_ASSERT_COUNTER(true, counter,
                      "counter error.");
  ret = gallus_pipeline_stage_shutdown(&stage, SHUTDOWN_GRACEFULLY);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_shutdown error.");

  /* after. */
  do_stop = true;
  gallus_pipeline_stage_destroy(&stage);
}


void
test_gallus_pipeline_stage_start_normal_fm(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  uint64_t counter[PIPELINE_FUNC_MAX] = {0, 0, 0, 1, 1, 0, 0, 0, 0};
  gallus_pipeline_stage_t stage = NULL;

  pipeline_stage_create_start(&stage,
                              pipeline_pre_pause,
                              pipeline_sched,
                              pipeline_setup,
                              pipeline_fetch,
                              pipeline_main,
                              NULL,
                              pipeline_shutdown,
                              pipeline_finalize,
                              pipeline_freeup);

  TEST_ASSERT_COUNTER(true, counter,
                      "counter error.");
  ret = gallus_pipeline_stage_shutdown(&stage, SHUTDOWN_GRACEFULLY);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_shutdown error.");

  /* after. */
  do_stop = true;
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_start_normal_mt(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  uint64_t counter[PIPELINE_FUNC_MAX] = {0, 0, 0, 0, 1, 1, 0, 0, 0, 0};
  gallus_pipeline_stage_t stage = NULL;

  pipeline_stage_create_start(&stage,
                              pipeline_pre_pause,
                              pipeline_sched,
                              pipeline_setup,
                              NULL,
                              pipeline_main,
                              pipeline_throw,
                              pipeline_shutdown,
                              pipeline_finalize,
                              pipeline_freeup);

  TEST_ASSERT_COUNTER(true, counter,
                      "counter error.");
  ret = gallus_pipeline_stage_shutdown(&stage, SHUTDOWN_GRACEFULLY);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_shutdown error.");

  /* after. */
  do_stop = true;
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_start_normal_m(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  uint64_t counter[PIPELINE_FUNC_MAX] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
  gallus_pipeline_stage_t stage = NULL;

  pipeline_stage_create_start(&stage,
                              pipeline_pre_pause,
                              pipeline_sched,
                              pipeline_setup,
                              NULL,
                              pipeline_main,
                              NULL,
                              pipeline_shutdown,
                              pipeline_finalize,
                              pipeline_freeup);

  TEST_ASSERT_COUNTER(true, counter,
                      "counter error.");
  ret = gallus_pipeline_stage_shutdown(&stage, SHUTDOWN_GRACEFULLY);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_shutdown error.");

  /* after. */
  do_stop = true;
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_start_null(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;

  /* call func. */
  ret = gallus_pipeline_stage_start(NULL);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_start(null) error.");
  ret = gallus_pipeline_stage_start(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_start(null) error.");
}

void
test_gallus_pipeline_stage_start_double_call(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create_start(&stage,
                              pipeline_pre_pause,
                              pipeline_sched,
                              pipeline_setup,
                              pipeline_fetch,
                              pipeline_main,
                              pipeline_throw,
                              pipeline_shutdown,
                              pipeline_finalize,
                              pipeline_freeup);

  /* call func (double call) */
  ret = gallus_pipeline_stage_start(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_STATE_TRANSITION, ret,
                            "gallus_pipeline_stage_start(double call) error.");

  ret = gallus_pipeline_stage_shutdown(&stage, SHUTDOWN_GRACEFULLY);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_shutdown error.");
  /* after. */
  do_stop = true;
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_start_invalid_object(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = stage_alloc();

  /* call func (invalid object) */
  ret = gallus_pipeline_stage_start(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_OBJECT, ret,
                            "gallus_pipeline_stage_start(invalid object) error.");

  /* after. */
  free(stage);
}

void
test_gallus_pipeline_stage_shutdown_wait_normal_gracefully(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  uint64_t counter[PIPELINE_FUNC_MAX] = {0, 0, 0, 1, 1, 1, 1, 1, 0, 0};
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create_start(&stage,
                              pipeline_pre_pause,
                              pipeline_sched,
                              pipeline_setup,
                              pipeline_fetch,
                              pipeline_main,
                              pipeline_throw,
                              pipeline_shutdown,
                              pipeline_finalize,
                              pipeline_freeup);

  /* call func. */
  ret = gallus_pipeline_stage_shutdown(&stage, SHUTDOWN_GRACEFULLY);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_shutdown(normal) error.");

  do_stop = true;
  ret = gallus_pipeline_stage_wait(&stage, PWAIT_TIME_OUT);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_wait error.");

  TEST_ASSERT_COUNTER(true, counter,
                      "counter error.");
  /* after. */
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_shutdown_wait_normal_right_now(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  uint64_t counter[PIPELINE_FUNC_MAX] = {0, 0, 0, 1, 1, 1, 1, 1, 0, 0};
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create_start(&stage,
                              pipeline_pre_pause,
                              pipeline_sched,
                              pipeline_setup,
                              pipeline_fetch,
                              pipeline_main,
                              pipeline_throw,
                              pipeline_shutdown,
                              pipeline_finalize,
                              pipeline_freeup);

  /* call func. */
  ret = gallus_pipeline_stage_shutdown(&stage, SHUTDOWN_RIGHT_NOW);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_shutdown(normal) error.");
  do_stop = true;
  ret = gallus_pipeline_stage_wait(&stage, PWAIT_TIME_OUT);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_wait error.");

  TEST_ASSERT_COUNTER(true, counter,
                      "counter error.");
  /* after. */
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_shutdown_wait_normal_func_null(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  uint64_t counter[PIPELINE_FUNC_MAX] = {0, 0, 0, 1, 1, 1, 0, 0, 0, 0};
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create_start(&stage,
                              pipeline_pre_pause,
                              pipeline_sched,
                              pipeline_setup,
                              pipeline_fetch,
                              pipeline_main,
                              pipeline_throw,
                              NULL,
                              NULL,
                              pipeline_freeup);

  /* call func. */
  ret = gallus_pipeline_stage_shutdown(&stage, SHUTDOWN_GRACEFULLY);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_shutdown(normal) error.");

  do_stop = true;
  ret = gallus_pipeline_stage_wait(&stage, PWAIT_TIME_OUT);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_wait error.");

  TEST_ASSERT_COUNTER(true, counter,
                      "counter error.");
  /* after. */
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_shutdown_null(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;

  ret = gallus_pipeline_stage_shutdown(NULL, SHUTDOWN_GRACEFULLY);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_shutdown(null) error.");
  ret = gallus_pipeline_stage_shutdown(&stage, SHUTDOWN_GRACEFULLY);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_shutdown(null) error.");

  /* after. */
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_shutdown_double_call(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create_start(&stage,
                              pipeline_pre_pause,
                              pipeline_sched,
                              pipeline_setup,
                              pipeline_fetch,
                              pipeline_main,
                              pipeline_throw,
                              pipeline_shutdown,
                              pipeline_finalize,
                              pipeline_freeup);

  ret = gallus_pipeline_stage_shutdown(&stage, SHUTDOWN_GRACEFULLY);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_shutdown(normal) error.");

  do_stop = true;
  ret = gallus_pipeline_stage_wait(&stage, PWAIT_TIME_OUT);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_wait error.");
  /* call func (double call). */
  ret = gallus_pipeline_stage_shutdown(&stage, SHUTDOWN_GRACEFULLY);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_STATE_TRANSITION, ret,
                            "gallus_pipeline_stage_shutdown(double call) error.");

  /* after. */
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_shutdown_invalid_object(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = stage_alloc();

  /* call func (invalid_object). */
  ret = gallus_pipeline_stage_shutdown(&stage, SHUTDOWN_GRACEFULLY);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_OBJECT, ret,
                            "gallus_pipeline_stage_shutdown(invalid object) error.");
  /* after. */
  free(stage);
}

void
test_gallus_pipeline_stage_cancel_normal(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  uint64_t counter[PIPELINE_FUNC_MAX] = {0, 0, 0, 1, 1, 1, 0, 0, 0, 0};
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create_start(&stage,
                              pipeline_pre_pause,
                              pipeline_sched,
                              pipeline_setup,
                              pipeline_fetch,
                              pipeline_main,
                              pipeline_throw,
                              pipeline_shutdown,
                              pipeline_finalize,
                              pipeline_freeup);

  /* call func. */
  ret = gallus_pipeline_stage_cancel(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_cancel(normal) error.");

  TEST_ASSERT_COUNTER(true, counter,
                      "counter error.");
  /* after. */
  do_stop = true;
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_cancel_double_call(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create_start(&stage,
                              pipeline_pre_pause,
                              pipeline_sched,
                              pipeline_setup,
                              pipeline_fetch,
                              pipeline_main,
                              pipeline_throw,
                              pipeline_shutdown,
                              pipeline_finalize,
                              pipeline_freeup);

  ret = gallus_pipeline_stage_cancel(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_cancel(normal) error.");
  do_stop = true;
  ret = gallus_pipeline_stage_wait(&stage, PWAIT_TIME_OUT);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_wait error.");

  /* call func(double call). */
  ret = gallus_pipeline_stage_cancel(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_STATE_TRANSITION, ret,
                            "gallus_pipeline_stage_cancel(double call) error.");

  /* after. */
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_cancel_null(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;

  /* call func(null). */
  ret = gallus_pipeline_stage_cancel(NULL);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_cancel(null) error.");
  ret = gallus_pipeline_stage_cancel(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_cancel(null) error.");
}

void
test_gallus_pipeline_stage_cancel_invalid_object(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = stage_alloc();

  /* call func(invalid object). */
  ret = gallus_pipeline_stage_cancel(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_OBJECT, ret,
                            "gallus_pipeline_stage_cancel(invalid object) error.");

  /* after. */
  free(stage);
}

void
test_gallus_pipeline_stage_wait_null(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;

  /* call func(null). */
  ret = gallus_pipeline_stage_wait(NULL, PWAIT_TIME_OUT);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_wait(null) error.");
  ret = gallus_pipeline_stage_wait(&stage, PWAIT_TIME_OUT);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_wait(null) error.");
}

void
test_gallus_pipeline_stage_wait_invalid_object(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = stage_alloc();

  /* call func(invalid object). */
  ret = gallus_pipeline_stage_wait(&stage, PWAIT_TIME_OUT);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_OBJECT, ret,
                            "gallus_pipeline_stage_wait(invalid object) error.");

  /* after. */
  free(stage);
}

void
test_gallus_pipeline_stage_destroy_normal(void) {
  uint64_t counter[PIPELINE_FUNC_MAX] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create(&stage,
                        pipeline_pre_pause,
                        pipeline_sched,
                        pipeline_setup,
                        pipeline_fetch,
                        pipeline_main,
                        pipeline_throw,
                        pipeline_shutdown,
                        pipeline_finalize,
                        pipeline_freeup);

  /* call func. */
  gallus_pipeline_stage_destroy(&stage);

  TEST_ASSERT_COUNTER(true, counter,
                      "counter error.");
}

void
test_gallus_pipeline_stage_destroy_double_call(void) {
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create(&stage,
                        pipeline_pre_pause,
                        pipeline_sched,
                        pipeline_setup,
                        pipeline_fetch,
                        pipeline_main,
                        pipeline_throw,
                        pipeline_shutdown,
                        pipeline_finalize,
                        pipeline_freeup);

  gallus_pipeline_stage_destroy(&stage);

  /* call func (double call). */
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_destroy_null(void) {
  gallus_pipeline_stage_t stage = NULL;

  /* call func. */
  gallus_pipeline_stage_destroy(NULL);
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_cancel_janitor_normal(void) {
  uint64_t counter[PIPELINE_FUNC_MAX] = {0, 0, 0, 1, 1, 1, 0, 0, 0, 0};
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create_start(&stage,
                              pipeline_pre_pause,
                              pipeline_sched,
                              pipeline_setup,
                              pipeline_fetch,
                              pipeline_main,
                              pipeline_throw,
                              pipeline_shutdown,
                              pipeline_finalize,
                              pipeline_freeup);

  /* call func (normal). */
  gallus_pipeline_stage_cancel_janitor(&stage);

  TEST_ASSERT_COUNTER(true, counter,
                      "counter error.");

  /* after. */
  do_stop = true;
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_submit_normal(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  uint64_t counter[PIPELINE_FUNC_MAX] = {0, 1, 0, 1, 1, 1, 0, 0, 0, 0};
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create_start(&stage,
                              pipeline_pre_pause,
                              pipeline_sched,
                              pipeline_setup,
                              pipeline_fetch,
                              pipeline_main,
                              pipeline_throw,
                              pipeline_shutdown,
                              pipeline_finalize,
                              pipeline_freeup);

  /* call func (normal). */
  ret = gallus_pipeline_stage_submit(&stage, NULL, 0LL, NULL);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_submit(normal) error.");
  TEST_ASSERT_COUNTER(true, counter,
                      "counter error.");

  /* after. */
  do_stop = true;
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_submit_null(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;
  void *evbuf = NULL;

  /* call func (null). */
  ret = gallus_pipeline_stage_submit(NULL, evbuf, sizeof(evbuf), NULL);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_submit(null) error.");
  ret = gallus_pipeline_stage_submit(&stage, evbuf, sizeof(evbuf), NULL);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_submit(null) error.");
}

/* See pipeline_stage2_test.c                                  */
/* [normal unit test for gallus_pipeline_stage_pause/resume()]. */
void
test_gallus_pipeline_stage_pause_resume_null(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;

  /* call func (null). */
  ret = gallus_pipeline_stage_pause(NULL, -1LL);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_pause(null) error.");
  ret = gallus_pipeline_stage_pause(&stage, -1LL);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_pause(null) error.");

  ret = gallus_pipeline_stage_resume(NULL);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_resume(null) error.");
  ret = gallus_pipeline_stage_resume(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_resume(null) error.");
}

void
test_gallus_pipeline_stage_pause_resume_invalid_object(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = stage_alloc();

  /* call func (invalid object). */
  ret = gallus_pipeline_stage_pause(&stage, -1LL);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_OBJECT, ret,
                            "gallus_pipeline_stage_pause(invalid object) error.");

  ret = gallus_pipeline_stage_resume(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_OBJECT, ret,
                            "gallus_pipeline_stage_resume(invalid object) error.");

  /* after. */
  free(stage);
}

void
test_gallus_pipeline_stage_pause_resume_invalid_state_transition(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create(&stage,
                        pipeline_pre_pause,
                        pipeline_sched,
                        pipeline_setup,
                        pipeline_fetch,
                        pipeline_main,
                        pipeline_throw,
                        pipeline_shutdown,
                        pipeline_finalize,
                        pipeline_freeup);

  /* call func (invalid state transition).  */
  ret = gallus_pipeline_stage_pause(&stage, -1LL);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_STATE_TRANSITION, ret,
                            "gallus_pipeline_stage_pause"
                            "(invalid state transition) error.");

  ret = gallus_pipeline_stage_resume(&stage);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_STATE_TRANSITION, ret,
                            "gallus_pipeline_stage_resume"
                            "(invalid state transition) error.");

  /* after. */
  do_stop = true;
  gallus_pipeline_stage_destroy(&stage);
}

/* See pipeline_stage2_test.c                                           */
/* [normal unit test for gallus_pipeline_stage_schedule_maintenance()]. */
void
test_gallus_pipeline_stage_schedule_maintenance_null(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;

  /* call func (null). */
  ret = gallus_pipeline_stage_schedule_maintenance(NULL, pipeline_maint, NULL);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_schedule_maintenanc(null)"
                            " error.");
  ret = gallus_pipeline_stage_schedule_maintenance(&stage, pipeline_maint,
        NULL);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_schedule_maintenanc(null)"
                            " error.");
}

void
test_gallus_pipeline_stage_schedule_maintenance_invalid_state_transition(
  void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;

  /* create data. */
  pipeline_stage_create(&stage,
                        pipeline_pre_pause,
                        pipeline_sched,
                        pipeline_setup,
                        pipeline_fetch,
                        pipeline_main,
                        pipeline_throw,
                        pipeline_shutdown,
                        pipeline_finalize,
                        pipeline_freeup);

  /* call func (invalid state transition).  */
  ret = gallus_pipeline_stage_schedule_maintenance(&stage, pipeline_maint,
        NULL);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_STATE_TRANSITION, ret,
                            "gallus_pipeline_stage_schedule_maintenance"
                            "(invalid state transition) error.");

  /* after. */
  do_stop = true;
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_find_normal(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_pipeline_stage_t stage = NULL;
  gallus_pipeline_stage_t ret_stage1 = NULL;
  gallus_pipeline_stage_t ret_stage2 = NULL;

  /* create data. */
  pipeline_stage_create(&stage,
                        pipeline_pre_pause,
                        pipeline_sched,
                        pipeline_setup,
                        pipeline_fetch,
                        pipeline_main,
                        pipeline_throw,
                        pipeline_shutdown,
                        pipeline_finalize,
                        pipeline_freeup);

  /* call func (normal - found). */
  ret = gallus_pipeline_stage_find("gallus_pipeline_stage_create",
                                   &ret_stage1);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_OK, ret,
                            "gallus_pipeline_stage_find(normal - found) error.");
  TEST_ASSERT_NOT_EQUAL_MESSAGE(NULL, ret_stage1,
                                "ret_stage is NULL.");

  /* call func (normal - not found). */
  ret = gallus_pipeline_stage_find("test_not_found",
                                   &ret_stage2);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_NOT_FOUND, ret,
                            "gallus_pipeline_stage_find(normal- not found) "
                            "error.");
  TEST_ASSERT_EQUAL_MESSAGE(NULL, ret_stage2,
                            "ret_stage is not NULL.");

  /* after. */
  do_stop = true;
  gallus_pipeline_stage_destroy(&stage);
}

void
test_gallus_pipeline_stage_find_null(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  /* call func (null). */
  ret = gallus_pipeline_stage_find("gallus_pipeline_stage_create",
                                   NULL);
  TEST_ASSERT_EQUAL_MESSAGE(GALLUS_RESULT_INVALID_ARGS, ret,
                            "gallus_pipeline_stage_find(null) error.");
}
