/* 
 * $__Copyright__$
 */
#include "gallus_apis.h"
#include "unity.h"

#include <math.h>





#define N_CALLOUT_WORKERS	0





void
setUp(void) {
}


void
tearDown(void) {
}





typedef struct {
  gallus_mutex_t m_lock;
  gallus_cond_t m_cond;
  gallus_chrono_t m_wait_nsec;
  gallus_chrono_t m_last_exec_abstime;
  volatile size_t m_n_exec;
  size_t m_n_stop;
  volatile bool m_is_freeuped;
} callout_arg_struct;
typedef callout_arg_struct *callout_arg_t;


static inline callout_arg_t
s_alloc_arg(size_t n_stop, gallus_chrono_t wait_nsec) {
  callout_arg_t ret = NULL;
  gallus_result_t r;
  gallus_mutex_t lock = NULL;
  gallus_cond_t cond = NULL;

  if (likely((r = gallus_mutex_create(&lock)) != GALLUS_RESULT_OK)) {
    goto done;
  }
  if (likely((r = gallus_cond_create(&cond)) != GALLUS_RESULT_OK)) {
    goto done;
  }

  ret = (callout_arg_t)malloc(sizeof(*ret));
  if (ret == NULL) {
    goto done;
  }

  (void)memset((void *)ret, 0, sizeof(*ret));
  ret->m_lock = lock;
  ret->m_cond = cond;
  ret->m_wait_nsec = wait_nsec;
  ret->m_last_exec_abstime = 0LL;
  ret->m_n_exec = 0LL;
  ret->m_n_stop = n_stop;
  ret->m_is_freeuped = false;

done:
  return ret;
}


static inline void
s_destroy_arg(callout_arg_t arg) {
  if (likely(arg != NULL &&
             arg->m_is_freeuped == true)) {
    gallus_cond_destroy(&(arg->m_cond));
    gallus_mutex_destroy(&(arg->m_lock));
    free((void *)arg);
  }
}


static inline void
s_freeup_arg(void *arg) {

  gallus_msg_debug(1, "enter.\n");

  if (likely(arg != NULL)) {
    callout_arg_t carg = (callout_arg_t)arg;

    (void)gallus_mutex_lock(&(carg->m_lock));
    {
      carg->m_is_freeuped = true;
      (void)gallus_cond_notify(&(carg->m_cond), true);
    }
    (void)gallus_mutex_unlock(&(carg->m_lock));

  }

  gallus_msg_debug(1, "leave.\n");

}


static inline size_t
s_wait_freeup_arg(callout_arg_t arg) {
  size_t ret = 0;

  if (likely(arg != NULL)) {

    (void)gallus_mutex_lock(&(arg->m_lock));
    {
      while (arg->m_is_freeuped != true) {
        (void)gallus_cond_wait(&(arg->m_cond), &(arg->m_lock), -1LL);
      }
      ret = __sync_fetch_and_add(&(arg->m_n_exec), 0);
    }
    (void)gallus_mutex_unlock(&(arg->m_lock));

  }

  return ret;
}


static gallus_result_t
callout_task(void *arg) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_chrono_t now;

  WHAT_TIME_IS_IT_NOW_IN_NSEC(now);

  gallus_msg_debug(1, "enter.\n");

  if (likely(arg != NULL)) {
    callout_arg_t carg = (callout_arg_t)arg;
    size_t n_exec = __sync_add_and_fetch(&(carg->m_n_exec), 1);

    gallus_msg_debug(1, "exec " PFSZ(u) ".\n", n_exec);

    if (carg->m_last_exec_abstime != 0) {
      gallus_msg_debug(1, "interval: " PF64(d) " nsec.\n",
                       now - carg->m_last_exec_abstime);
    }
    carg->m_last_exec_abstime = now;

    if (carg->m_wait_nsec > 0) {
      gallus_msg_debug(1, "sleep " PFSZ(u) " nsec.\n",
                       carg->m_wait_nsec);
      (void)gallus_chrono_nanosleep(carg->m_wait_nsec, NULL);
    }

    if (n_exec < carg->m_n_stop) {
      ret = GALLUS_RESULT_OK;
    }
  }

  gallus_msg_debug(1, "leave.\n");

  return ret;
}





void
test_prologue(void) {
  gallus_result_t r;
  const char *argv0 =
    ((IS_VALID_STRING(gallus_get_command_name()) == true) ?
     gallus_get_command_name() : "callout_test");
  const char *const argv[] = {
    argv0, NULL
  };

  (void)gallus_mainloop_set_callout_workers_number(N_CALLOUT_WORKERS);
  r = gallus_mainloop_with_callout(1, argv, NULL, NULL,
                                   false, false, true);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
}





/*
 * Cancel before exec.
 */


void
test_urgent_cancel_before(void) {
  gallus_result_t r = GALLUS_RESULT_ANY_FAILURES;
  gallus_callout_task_t t = NULL;
  size_t n_exec;

  callout_arg_t arg = NULL;

  arg = s_alloc_arg(10, 500LL * 1000LL * 1000LL);
  TEST_ASSERT_NOT_EQUAL(arg, NULL);

  r = gallus_callout_create_task(&t, 0, __func__,
                                 callout_task, (void *)arg,
                                 s_freeup_arg);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  r = gallus_callout_submit_task(&t,
                                 0LL,
                                 1000LL * 1000LL * 1000LL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  gallus_callout_cancel_task(&t);

  n_exec = s_wait_freeup_arg(arg);
  if (n_exec == 1) {
    /*
     * urgent tasks could be executed before cancellation.
     */
    n_exec = 0;
  }
  TEST_ASSERT_EQUAL(n_exec, 0);

  s_destroy_arg(arg);
}


void
test_delayed_cancel_before(void) {
  gallus_result_t r = GALLUS_RESULT_ANY_FAILURES;
  gallus_callout_task_t t = NULL;
  size_t n_exec;

  callout_arg_t arg = NULL;

  arg = s_alloc_arg(10, 500LL * 1000LL * 1000LL);
  TEST_ASSERT_NOT_EQUAL(arg, NULL);

  r = gallus_callout_create_task(&t, 0, __func__,
                                 callout_task, (void *)arg,
                                 s_freeup_arg);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  r = gallus_callout_submit_task(&t,
                                 0LL,
                                 1000LL * 1000LL * 1000LL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  gallus_callout_cancel_task(&t);

  n_exec = s_wait_freeup_arg(arg);
  if (n_exec == 1) {
    /*
     * delayed tasks could be executed before cancellation.
     */
    n_exec = 0;
  }
  TEST_ASSERT_EQUAL(n_exec, 0);

  s_destroy_arg(arg);
}


void
test_idle_cancel_before(void) {
  gallus_result_t r = GALLUS_RESULT_ANY_FAILURES;
  gallus_callout_task_t t = NULL;
  size_t n_exec;

  callout_arg_t arg = NULL;

  arg = s_alloc_arg(10, 500LL * 1000LL * 1000LL);
  TEST_ASSERT_NOT_EQUAL(arg, NULL);

  r = gallus_callout_create_task(&t, 0, __func__,
                                 callout_task, (void *)arg,
                                 s_freeup_arg);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  r = gallus_callout_submit_task(&t,
                                 -1LL,
                                 1000LL * 1000LL * 1000LL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  gallus_callout_cancel_task(&t);

  n_exec = s_wait_freeup_arg(arg);
  if (n_exec == 1) {
    /*
     * idle tasks could be executed before cancellation.
     */
    n_exec = 0;
  }
  TEST_ASSERT_EQUAL(n_exec, 0);

  s_destroy_arg(arg);
}





/*
 * Cancel while exec
 */


void
test_urgent_cancel_while(void) {
  gallus_result_t r = GALLUS_RESULT_ANY_FAILURES;
  gallus_callout_task_t t = NULL;
  size_t n_exec;

  callout_arg_t arg = NULL;

  arg = s_alloc_arg(10, 1500LL * 1000LL * 1000LL);
  TEST_ASSERT_NOT_EQUAL(arg, NULL);

  r = gallus_callout_create_task(&t, 0, __func__,
                                 callout_task, (void *)arg,
                                 s_freeup_arg);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  r = gallus_callout_submit_task(&t,
                                 0LL,
                                 3000LL * 1000LL * 1000LL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  (void)gallus_chrono_nanosleep(1000LL * 1000LL * 1000LL, NULL);

  gallus_callout_cancel_task(&t);

  n_exec = s_wait_freeup_arg(arg);
  if (n_exec == 0) {
    /*
     * urgent tasks could be cancelled before execution.
     */
    n_exec = 1;
  }
  TEST_ASSERT_EQUAL(n_exec, 1);

  s_destroy_arg(arg);
}


void
test_delayed_cancel_while(void) {
  gallus_result_t r = GALLUS_RESULT_ANY_FAILURES;
  gallus_callout_task_t t = NULL;
  size_t n_exec;

  callout_arg_t arg = NULL;

  arg = s_alloc_arg(10, 1500LL * 1000LL * 1000LL);
  TEST_ASSERT_NOT_EQUAL(arg, NULL);

  r = gallus_callout_create_task(&t, 0, __func__,
                                 callout_task, (void *)arg,
                                 s_freeup_arg);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  r = gallus_callout_submit_task(&t,
                                 100LL * 1000LL * 1000LL,
                                 3000LL * 1000LL * 1000LL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  (void)gallus_chrono_nanosleep(1000LL * 1000LL * 1000LL, NULL);

  gallus_callout_cancel_task(&t);

  n_exec = s_wait_freeup_arg(arg);
  if (n_exec == 0) {
    /*
     * timed tasks could be cancelled before execution.
     */
    n_exec = 1;
  }
  TEST_ASSERT_EQUAL(n_exec, 1);

  s_destroy_arg(arg);
}


void
test_idle_cancel_while(void) {
  gallus_result_t r = GALLUS_RESULT_ANY_FAILURES;
  gallus_callout_task_t t = NULL;
  size_t n_exec;

  callout_arg_t arg = NULL;

  arg = s_alloc_arg(10, 1500LL * 1000LL * 1000LL);
  TEST_ASSERT_NOT_EQUAL(arg, NULL);

  r = gallus_callout_create_task(&t, 0, __func__,
                                 callout_task, (void *)arg,
                                 s_freeup_arg);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  r = gallus_callout_submit_task(&t,
                                 -1LL,
                                 3000LL * 1000LL * 1000LL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  (void)gallus_chrono_nanosleep(1000LL * 1000LL * 1000LL, NULL);

  gallus_callout_cancel_task(&t);

  n_exec = s_wait_freeup_arg(arg);
  if (n_exec == 0) {
    /*
     * idle tasks could be cancelled before execution.
     */
    n_exec = 1;
  }
  TEST_ASSERT_EQUAL(n_exec, 1);

  s_destroy_arg(arg);
}





/*
 * Force exec
 */


void
test_urgent_force(void) {
  gallus_result_t r = GALLUS_RESULT_ANY_FAILURES;
  gallus_callout_task_t t = NULL;
  size_t n_exec;
  size_t i;
  callout_arg_t arg = NULL;
  gallus_callout_task_state_t st;

  arg = s_alloc_arg(10, 0LL);
  TEST_ASSERT_NOT_EQUAL(arg, NULL);

  r = gallus_callout_create_task(&t, 0, __func__,
                                 callout_task, (void *)arg,
                                 s_freeup_arg);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  r = gallus_callout_submit_task(&t,
                                 0LL,
                                 500LL * 1000LL * 1000LL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  (void)gallus_chrono_nanosleep(100LL * 1000LL * 1000LL, NULL);

  for (i = 0; i < 5; i++) {
    gallus_msg_debug(1, "iter. " PF64(u) "\n", i);

    r = gallus_callout_task_state(&t, &st);
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

    if (st == TASK_STATE_ENQUEUED) {
      r = gallus_callout_exec_task_forcibly(&t);
      if (r == GALLUS_RESULT_INVALID_STATE) {
        continue;
      }
      TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
    } else if (st == TASK_STATE_UNKNOWN ||
               st == TASK_STATE_CANCELLED) {
      break;
    }

    (void)gallus_chrono_nanosleep(100LL * 1000LL * 1000LL, NULL);
  }

  n_exec = s_wait_freeup_arg(arg);
  TEST_ASSERT_EQUAL(n_exec, 10);

  s_destroy_arg(arg);
}


void
test_delayed_force(void) {
  gallus_result_t r = GALLUS_RESULT_ANY_FAILURES;
  gallus_callout_task_t t = NULL;
  size_t n_exec;
  size_t i;
  callout_arg_t arg = NULL;
  gallus_callout_task_state_t st;

  arg = s_alloc_arg(10, 0LL);
  TEST_ASSERT_NOT_EQUAL(arg, NULL);

  r = gallus_callout_create_task(&t, 0, __func__,
                                 callout_task, (void *)arg,
                                 s_freeup_arg);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  r = gallus_callout_submit_task(&t,
                                 100LL * 1000LL * 1000LL,
                                 500LL * 1000LL * 1000LL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  (void)gallus_chrono_nanosleep(100LL * 1000LL * 1000LL, NULL);

  for (i = 0; i < 5; i++) {
    gallus_msg_debug(1, "iter. " PF64(u) "\n", i);

    r = gallus_callout_task_state(&t, &st);
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

    if (st == TASK_STATE_ENQUEUED) {
      r = gallus_callout_exec_task_forcibly(&t);
      if (r == GALLUS_RESULT_INVALID_STATE) {
        continue;
      }
      TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
    } else if (st == TASK_STATE_UNKNOWN ||
               st == TASK_STATE_CANCELLED) {
      break;
    }

    (void)gallus_chrono_nanosleep(100LL * 1000LL * 1000LL, NULL);
  }

  n_exec = s_wait_freeup_arg(arg);
  TEST_ASSERT_EQUAL(n_exec, 10);

  s_destroy_arg(arg);
}


void
test_idle_force(void) {
  gallus_result_t r = GALLUS_RESULT_ANY_FAILURES;
  gallus_callout_task_t t = NULL;
  size_t n_exec;
  size_t i;
  callout_arg_t arg = NULL;
  gallus_callout_task_state_t st;

  arg = s_alloc_arg(10, 0LL);
  TEST_ASSERT_NOT_EQUAL(arg, NULL);

  r = gallus_callout_create_task(&t, 0, __func__,
                                 callout_task, (void *)arg,
                                 s_freeup_arg);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  r = gallus_callout_submit_task(&t,
                                 -1LL,
                                 500LL * 1000LL * 1000LL);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

  (void)gallus_chrono_nanosleep(1000LL * 1000LL * 1000LL, NULL);

  for (i = 0; i < 5; i++) {
    gallus_msg_debug(1, "iter. " PF64(u) "\n", i);

    r = gallus_callout_task_state(&t, &st);
    TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);

    if (st == TASK_STATE_ENQUEUED) {
      r = gallus_callout_exec_task_forcibly(&t);
      if (r == GALLUS_RESULT_INVALID_STATE) {
        continue;
      }
      TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
    } else if (st == TASK_STATE_UNKNOWN ||
               st == TASK_STATE_CANCELLED) {
      break;
    }

    (void)gallus_chrono_nanosleep(100LL * 1000LL * 1000LL, NULL);
  }

  n_exec = s_wait_freeup_arg(arg);
  TEST_ASSERT_EQUAL(n_exec, 10);

  s_destroy_arg(arg);
}





void
test_epilogue(void) {
  gallus_result_t r = global_state_request_shutdown(SHUTDOWN_GRACEFULLY);
  TEST_ASSERT_EQUAL(r, GALLUS_RESULT_OK);
  gallus_mainloop_wait_thread();
}
