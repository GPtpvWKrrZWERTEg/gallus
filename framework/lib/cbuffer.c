/* 
 * $__Copyright__$
 */
#include "gallus_apis.h"
#include "qmuxer_internal.h"





#define N_EMPTY_ROOM	1LL





typedef struct gallus_cbuffer_record {
  gallus_mutex_t m_lock;
  gallus_cond_t m_cond_put;
  gallus_cond_t m_cond_get;
  gallus_cond_t m_cond_awakened;

  volatile int64_t m_r_idx;
  volatile int64_t m_w_idx;
  volatile int64_t m_n_elements;
  volatile size_t m_n_waiters;

  volatile bool m_is_operational;
  volatile bool m_is_awakened;

  gallus_cbuffer_value_freeup_proc_t m_del_proc;

  size_t m_element_size;

  int64_t m_n_max_elements;
  int64_t m_n_max_allocd_elements;

  gallus_qmuxer_t m_qmuxer;
  gallus_qmuxer_poll_event_t m_type;

  char m_data[0];
} gallus_cbuffer_record;





static inline void
s_adjust_indices(gallus_cbuffer_t cb) {
  if (cb != NULL) {
    if (cb->m_w_idx >= 0) {
      /*
       * This might improve the branch prediction performance.
       */
      return;
    } else {
      /*
       * Note that this could happen like once in a handred years
       * even with a 3.0 GHz CPU executing value insertion operations
       * EACH AND EVERY clock, but, prepare for THE CASE.
       */
      cb->m_w_idx = cb->m_n_max_allocd_elements;
      cb->m_r_idx = cb->m_r_idx % cb->m_n_max_allocd_elements;
    }
  }
}


static inline void
s_lock(gallus_cbuffer_t cb) {
  if (cb != NULL) {
    (void)gallus_mutex_lock(&(cb->m_lock));
  }
}


static inline void
s_unlock(gallus_cbuffer_t cb) {
  if (cb != NULL) {
    (void)gallus_mutex_unlock(&(cb->m_lock));
  }
}


static inline char *
s_data_addr(gallus_cbuffer_t cb, int64_t idx) {
  if (cb != NULL && idx >= 0) {
    return
      cb->m_data +
      (idx % cb->m_n_max_allocd_elements) * (int64_t)cb->m_element_size;
  } else {
    return NULL;
  }
}


static inline void
s_freeup_all_values(gallus_cbuffer_t cb) {
  if (cb != NULL) {
    if (cb->m_del_proc != NULL) {
      int64_t i;
      char *addr;

      s_adjust_indices(cb);
      for (i = cb->m_r_idx;
           i < cb->m_w_idx;
           i++) {
        addr = s_data_addr(cb, i);
        if (addr != NULL) {
          cb->m_del_proc((void **)addr);
        }
      }
    }
  }
}


static inline void
s_clean(gallus_cbuffer_t cb, bool free_values) {
  if (cb != NULL) {
    if (free_values == true) {
      s_freeup_all_values(cb);
    }
    cb->m_r_idx = 0;
    cb->m_w_idx = 0;
    (void)memset((void *)(cb->m_data), 0,
                 cb->m_element_size * (size_t)cb->m_n_max_allocd_elements);
    cb->m_n_elements = 0;
  }
}


static inline void
s_shutdown(gallus_cbuffer_t cb, bool free_values) {
  if (cb != NULL) {
    if (cb->m_is_operational == true) {
      cb->m_is_operational = false;
      s_clean(cb, free_values);
      if (cb->m_qmuxer != NULL) {
        qmuxer_notify(cb->m_qmuxer);
      }
      (void)gallus_cond_notify(&(cb->m_cond_get), true);
      (void)gallus_cond_notify(&(cb->m_cond_put), true);
      (void)gallus_cond_notify(&(cb->m_cond_awakened), true);
    }
  }
}


static inline int64_t
s_copyin(gallus_cbuffer_t cb, void *buf, size_t n) {
  int64_t max_rooms = cb->m_n_max_elements - cb->m_n_elements;
  int64_t max_n = (max_rooms < (int64_t)n) ? max_rooms : (int64_t)n;

  s_adjust_indices(cb);

  if (max_n > 0) {
    int64_t idx = cb->m_w_idx % cb->m_n_max_allocd_elements;
    char *dst = cb->m_data + (size_t)idx * cb->m_element_size;

    if ((idx + max_n) <= cb->m_n_max_allocd_elements) {
      (void)memcpy((void *)dst, buf,
                   (size_t)max_n * cb->m_element_size);
    } else {
      int64_t max_n_0 = cb->m_n_max_allocd_elements - idx;
      size_t max_n_0_sz = (size_t)max_n_0 * cb->m_element_size;
      char *src1 = (char *)buf + max_n_0_sz;
      int64_t max_n_1 = max_n - max_n_0;

      (void)memcpy((void *)dst, buf, max_n_0_sz);
      (void)memcpy((void *)(cb->m_data), (void *)src1,
                   (size_t)max_n_1 * cb->m_element_size);
    }

    cb->m_n_elements += max_n;
    cb->m_w_idx += max_n;

    /*
     * And wake the poller (if existed)
     */
    if (cb->m_qmuxer != NULL && NEED_WAIT_READABLE(cb->m_type) == true) {
      qmuxer_notify(cb->m_qmuxer);
    }
    /*
     * And wake all the getters.
     */
    (void)gallus_cond_notify(&(cb->m_cond_get), true);
  }

  return max_n;
}


static inline int64_t
s_copyout(gallus_cbuffer_t cb, void *buf, size_t n, bool do_incr) {
  int64_t max_n = ((int64_t)n < cb->m_n_elements) ?
                  (int64_t)n : cb->m_n_elements;

  s_adjust_indices(cb);

  if (max_n > 0) {
    int64_t idx = cb->m_r_idx % cb->m_n_max_allocd_elements;
    char *src = cb->m_data + (size_t)idx * cb->m_element_size;

    if ((idx + max_n) <= cb->m_n_max_allocd_elements) {
      (void)memcpy(buf, (void *)src, (size_t)max_n * cb->m_element_size);
    } else {
      int64_t max_n_0 = cb->m_n_max_allocd_elements - idx;
      size_t max_n_0_sz = (size_t)max_n_0 * cb->m_element_size;
      char *dst1 = (char *)buf + max_n_0_sz;
      int64_t max_n_1 = max_n - max_n_0;

      (void)memcpy(buf, (void *)src, max_n_0_sz);
      (void)memcpy((void *)dst1, (void *)(cb->m_data),
                   (size_t)max_n_1 * cb->m_element_size);
    }

    if (do_incr == true) {
      cb->m_n_elements -= max_n;
      cb->m_r_idx += max_n;

      /*
       * And wake the poller (if existed)
       */
      if (cb->m_qmuxer != NULL && NEED_WAIT_WRITABLE(cb->m_type) == true) {
        qmuxer_notify(cb->m_qmuxer);
      }
      /*
       * And wake all the putters.
       */
      (void)gallus_cond_notify(&(cb->m_cond_put), true);
    }
  }

  return max_n;
}


static inline gallus_result_t
s_wait_io_ready(gallus_cbuffer_t cb,
                gallus_cond_t *cptr,
                gallus_chrono_t nsec) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  size_t n_waiters;

  if (cb != NULL && cptr != NULL) {
    if (nsec != 0LL) {

      gallus_msg_debug(500, "wait " PF64(d) " nsec.\n",
                       nsec);

      (void)__sync_fetch_and_add(&(cb->m_n_waiters), 1);
      ret = gallus_cond_wait(cptr, &(cb->m_lock), nsec);
      n_waiters = __sync_sub_and_fetch(&(cb->m_n_waiters), 1);

      if (cb->m_is_awakened == true) {
        gallus_msg_debug(5, "awakened while sleeping " PF64(d) " nsec.\n",
                         nsec);
      } else {
        gallus_msg_debug(500, "wait " PF64(d) " nsec done.\n",
                         nsec);
      }

      if (ret == GALLUS_RESULT_OK &&
          cb->m_is_awakened == true) {
        /*
         * A waker wakes all the waiting threads up. Note that the
         * waker is waiting for a notification that the all the
         * waiters are awakened and leave this function.
         */
        if (cb->m_is_operational == true) {
          if (n_waiters == 0) {

            /*
             * All the waiters are gone. Wake the waker up.
             */

            gallus_msg_debug(5, "sync wakeup who woke me up.\n");

            cb->m_is_awakened = false;
            (void)gallus_cond_notify(&(cb->m_cond_awakened), true);
          }
          ret = GALLUS_RESULT_WAKEUP_REQUESTED;
        } else {
          cb->m_is_awakened = false;
          (void)gallus_cond_notify(&(cb->m_cond_awakened), true);
          ret = GALLUS_RESULT_NOT_OPERATIONAL;
        }
      }

    } else {
      ret = GALLUS_RESULT_OK;
    }
  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


static inline gallus_result_t
s_wait_puttable(gallus_cbuffer_t cb, gallus_chrono_t nsec) {
  return s_wait_io_ready(cb, &(cb->m_cond_put), nsec);
}


static inline gallus_result_t
s_wait_gettable(gallus_cbuffer_t cb, gallus_chrono_t nsec) {
  return s_wait_io_ready(cb, &(cb->m_cond_get), nsec);
}


static inline gallus_result_t
s_put_n(gallus_cbuffer_t *cbptr,
        void *valptr,
        size_t n_vals,
        size_t valsz,
        gallus_chrono_t nsec,
        size_t *n_actual_put) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_cbuffer_t cb = NULL;

  if (cbptr != NULL && (cb = *cbptr) != NULL &&
      valptr != NULL &&
      valsz == cb->m_element_size) {

    if (n_vals > 0) {

      int64_t n_copyin = 0LL;

      if (nsec == 0LL) {

        s_lock(cb);
        {

          /*
           * Just put and return.
           */
          if (cb->m_is_operational == true) {
            n_copyin = ret = s_copyin(cb, valptr, n_vals);
          } else {
            ret = GALLUS_RESULT_NOT_OPERATIONAL;
          }

        }
        s_unlock(cb);

      } else if (nsec < 0LL) {

        s_lock(cb);
        {

          /*
           * Repeat putting until all the data are put.
           */
        check_inf:
          mbar();
          if (cb->m_is_operational == true) {
            n_copyin += s_copyin(cb,
                                 (void *)((char *)valptr +
                                          ((size_t)n_copyin * valsz)),
                                 n_vals - (size_t)n_copyin);
            if ((size_t)n_copyin < n_vals) {
              /*
               * Need to repeat.
               */
              if (cb->m_n_elements >= cb->m_n_max_elements) {
                /*
                 * No vacancy. Need to wait for someone get data from
                 * the buffer.
                 */
                if ((ret = s_wait_puttable(cb, -1LL)) ==
                    GALLUS_RESULT_OK) {
                  goto check_inf;
                } else {
                  /*
                   * Any errors occur while waiting.
                   */
                  if (ret == GALLUS_RESULT_TIMEDOUT) {
                    /*
                     * Must not happen.
                     */
                    gallus_msg_fatal("Timed out must not happen here.\n");
                  }
                }
              } else {
                /*
                 * The buffer still has rooms but it couldn't put all
                 * the data??  Must not happen??
                 */
                gallus_msg_fatal("Couldn't put all the data even rooms "
                                 "available. Must not happen.\n");
              }
            } else {
              /*
               * Succeeded.
               */
              ret = n_copyin;
            }
          } else {
            ret = GALLUS_RESULT_NOT_OPERATIONAL;
          }

        }
        s_unlock(cb);

      } else {

        s_lock(cb);
        {

          /*
           * Repeat putting until all the data are put or the spcified
           * time limit is expired.
           */
          gallus_chrono_t copy_start;
          gallus_chrono_t wait_end;
          gallus_chrono_t to = nsec;

        check_to:
          mbar();
          if (cb->m_is_operational == true) {
            WHAT_TIME_IS_IT_NOW_IN_NSEC(copy_start);
            n_copyin += s_copyin(cb,
                                 (void *)((char *)valptr +
                                          ((size_t)n_copyin * valsz)),
                                 n_vals - (size_t)n_copyin);
            if ((size_t)n_copyin < n_vals) {
              /*
               * Need to repeat.
               */
              if (cb->m_n_elements >= cb->m_n_max_elements) {
                /*
                 * No vacancy. Need to wait for someone get data from
                 * the buffer.
                 */
                if ((ret = s_wait_puttable(cb, to)) ==
                    GALLUS_RESULT_OK) {
                  WHAT_TIME_IS_IT_NOW_IN_NSEC(wait_end);
                  to -= (wait_end - copy_start);
                  if (to > 0LL) {
                    goto check_to;
                  }
                  ret = GALLUS_RESULT_TIMEDOUT;
                }
              } else {
                /*
                 * The buffer still has rooms but it couldn't put all
                 * the data??  Must not happen??
                 */
                gallus_msg_fatal("Couldn't put all the data even rooms "
                                 "available. Must not happen.\n");
              }
            } else {
              /*
               * Succeeded.
               */
              ret = n_copyin;
            }
          } else {
            ret = GALLUS_RESULT_NOT_OPERATIONAL;
          }

        }
        s_unlock(cb);

      }

      if (n_actual_put != NULL) {
        *n_actual_put = (size_t)n_copyin;
      }

    } else {
      if (n_actual_put != NULL) {
        *n_actual_put = 0LL;
      }
      ret = GALLUS_RESULT_OK;
    }

  } else {
    if (n_actual_put != NULL) {
      *n_actual_put = 0LL;
    }
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


static inline gallus_result_t
s_get_n(gallus_cbuffer_t *cbptr,
        void *valptr,
        size_t n_vals_max,
        size_t n_at_least,
        size_t valsz,
        gallus_chrono_t nsec,
        size_t *n_actual_get,
        bool do_incr) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  gallus_cbuffer_t cb = NULL;

  if (cbptr != NULL && (cb = *cbptr) != NULL &&
      valptr != NULL &&
      valsz == cb->m_element_size) {

    if (n_vals_max > 0) {

      int64_t n_copyout = 0LL;

      if (nsec == 0LL) {

        s_lock(cb);
        {

          /*
           * Just get and return.
           */
          if (cb->m_is_operational == true) {
            n_copyout = ret = s_copyout(cb, valptr, n_vals_max, do_incr);
          } else {
            ret = GALLUS_RESULT_NOT_OPERATIONAL;
          }

        }
        s_unlock(cb);

      } else if (nsec < 0LL) {

        s_lock(cb);
        {

          /*
           * Repeat getting until all the required # of the data are got.
           */
        check_inf:
          mbar();
          if (cb->m_is_operational == true) {
            n_copyout += s_copyout(cb,
                                   (void *)((char *)valptr +
                                            ((size_t)n_copyout * valsz)),
                                   n_vals_max - (size_t)n_copyout,
                                   do_incr);
            if ((size_t)n_copyout < n_vals_max) {
              /*
               * Need to repeat.
               */
              if (cb->m_n_elements < 1LL) {
                /*
                 * No data. Need to wait for someone put data to the
                 * buffer.
                 */
                if ((ret = s_wait_gettable(cb, -1LL)) ==
                    GALLUS_RESULT_OK) {
                  goto check_inf;
                } else {
                  /*
                   * Any errors occur while waiting.
                   */
                  if (ret == GALLUS_RESULT_TIMEDOUT) {
                    /*
                     * Must not happen.
                     */
                    gallus_msg_fatal("Timed out must not happen here.\n");
                  }
                }
              } else {
                /*
                 * The buffer still has data but it couldn't get all
                 * the data??  Must not happen??
                 */
                gallus_msg_fatal("Couldn't get all the data even the data "
                                 "available. Must not happen.\n");
              }
            } else {
              /*
               * Succeeded.
               */
              ret = n_copyout;
            }
          } else {
            ret = GALLUS_RESULT_NOT_OPERATIONAL;
          }

        }
        s_unlock(cb);

      } else {

        s_lock(cb);
        {

          /*
           * Repeat getting until all the required # of the data are
           * got or the spcified time limit is expired.
           */
          gallus_chrono_t copy_start;
          gallus_chrono_t wait_end;
          gallus_chrono_t to = nsec;

        check_to:
          mbar();
          if (cb->m_is_operational == true) {
            WHAT_TIME_IS_IT_NOW_IN_NSEC(copy_start);
            n_copyout += s_copyout(cb,
                                   (void *)((char *)valptr +
                                            ((size_t)n_copyout * valsz)),
                                   n_vals_max - (size_t)n_copyout,
                                   do_incr);
            if ((size_t)n_copyout < n_at_least) {
              /*
               * Need to repeat.
               */
              if (cb->m_n_elements < 1LL) {
                /*
                 * No data. Need to wait for someone put data to the
                 * buffer.
                 */
                if ((ret = s_wait_gettable(cb, to)) ==
                    GALLUS_RESULT_OK) {
                  WHAT_TIME_IS_IT_NOW_IN_NSEC(wait_end);
                  to -= (wait_end - copy_start);
                  if (to > 0LL) {
                    goto check_to;
                  }
                  ret = GALLUS_RESULT_TIMEDOUT;
                }
              } else {
                /*
                 * The buffer still has data but it couldn't get all
                 * the data??  Must not happen??
                 */
                gallus_msg_fatal("Couldn't get all the data even the data "
                                 "available. Must not happen.\n");
              }
            } else {
              /*
               * Succeeded.
               */
              ret = n_copyout;
            }
          } else {
            ret = GALLUS_RESULT_NOT_OPERATIONAL;
          }

        }
        s_unlock(cb);

      }

      if (n_actual_get != NULL) {
        *n_actual_get = (size_t)n_copyout;
      }

    } else {
      if (n_actual_get != NULL) {
        *n_actual_get = 0LL;
      }
      ret = GALLUS_RESULT_OK;
    }

  } else {
    if (n_actual_get != NULL) {
      *n_actual_get = 0LL;
    }
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}





gallus_result_t
gallus_cbuffer_create_with_size(gallus_cbuffer_t *cbptr,
                                size_t elemsize,
                                int64_t maxelems,
                                gallus_cbuffer_value_freeup_proc_t proc) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (cbptr != NULL &&
      elemsize > 0 &&
      maxelems > 0) {
    gallus_cbuffer_t cb = (gallus_cbuffer_t)malloc(
                            sizeof(*cb) + elemsize * (size_t)(maxelems + N_EMPTY_ROOM));

    *cbptr = NULL;

    if (cb != NULL) {
      if (((ret = gallus_mutex_create(&(cb->m_lock))) ==
           GALLUS_RESULT_OK) &&
          ((ret = gallus_cond_create(&(cb->m_cond_put))) ==
           GALLUS_RESULT_OK) &&
          ((ret = gallus_cond_create(&(cb->m_cond_get))) ==
           GALLUS_RESULT_OK) &&
          ((ret = gallus_cond_create(&(cb->m_cond_awakened))) ==
           GALLUS_RESULT_OK)) {
        cb->m_r_idx = 0;
        cb->m_w_idx = 0;
        cb->m_n_elements = 0;
        cb->m_n_waiters = 0;
        cb->m_n_max_elements = maxelems;
        cb->m_n_max_allocd_elements = maxelems + N_EMPTY_ROOM;
        cb->m_element_size = elemsize;
        cb->m_del_proc = proc;
        cb->m_is_operational = true;
        cb->m_is_awakened = false;
        cb->m_qmuxer = NULL;
        cb->m_type = GALLUS_QMUXER_POLL_UNKNOWN;

        *cbptr = cb;

        ret = GALLUS_RESULT_OK;

      } else {
        free((void *)cb);
      }
    } else {
      ret = GALLUS_RESULT_NO_MEMORY;
    }
  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


void
gallus_cbuffer_shutdown(gallus_cbuffer_t *cbptr,
                        bool free_values) {
  if (cbptr != NULL &&
      *cbptr != NULL) {

    s_lock(*cbptr);
    {
      s_shutdown(*cbptr, free_values);
    }
    s_unlock(*cbptr);

  }
}


void
gallus_cbuffer_destroy(gallus_cbuffer_t *cbptr,
                       bool free_values) {
  if (cbptr != NULL &&
      *cbptr != NULL) {

    s_lock(*cbptr);
    {
      s_shutdown(*cbptr, free_values);
      gallus_cond_destroy(&((*cbptr)->m_cond_put));
      gallus_cond_destroy(&((*cbptr)->m_cond_get));
      gallus_cond_destroy(&((*cbptr)->m_cond_awakened));
    }
    s_unlock(*cbptr);

    gallus_mutex_destroy(&((*cbptr)->m_lock));

    free((void *)*cbptr);
    *cbptr = NULL;
  }
}


gallus_result_t
gallus_cbuffer_clear(gallus_cbuffer_t *cbptr,
                     bool free_values) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (cbptr != NULL &&
      *cbptr != NULL) {

    s_lock(*cbptr);
    {
      s_clean(*cbptr, free_values);
      if ((*cbptr)->m_qmuxer != NULL &&
          NEED_WAIT_READABLE((*cbptr)->m_type) == true) {
        qmuxer_notify((*cbptr)->m_qmuxer);
      }
      (void)gallus_cond_notify(&((*cbptr)->m_cond_put), true);
    }
    s_unlock(*cbptr);

    ret = GALLUS_RESULT_OK;

  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


gallus_result_t
gallus_cbuffer_wakeup(gallus_cbuffer_t *cbptr, gallus_chrono_t nsec) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (cbptr != NULL &&
      *cbptr != NULL) {
    size_t n_waiters;

    s_lock(*cbptr);
    {
      n_waiters = __sync_fetch_and_add(&((*cbptr)->m_n_waiters), 0);

      if (n_waiters > 0) {
        if ((*cbptr)->m_is_operational == true) {
          if ((*cbptr)->m_is_awakened == false) {
            /*
             * Wake all the waiters up.
             */
            (*cbptr)->m_is_awakened = true;
            (void)gallus_cond_notify(&((*cbptr)->m_cond_get), true);
            (void)gallus_cond_notify(&((*cbptr)->m_cond_put), true);

            if (nsec != 0LL) {
              /*
               * Then wait for one of the waiters wakes this thread up.
               */
            recheck:
              mbar();
              if ((*cbptr)->m_is_operational == true) {
                if ((*cbptr)->m_is_awakened == true) {

                  gallus_msg_debug(5, "sync wait a waiter wake me up...\n");

                  if ((ret = gallus_cond_wait(&((*cbptr)->m_cond_awakened),
                                              &((*cbptr)->m_lock), nsec)) ==
                      GALLUS_RESULT_OK) {
                    goto recheck;
                  }

                  gallus_msg_debug(5, "a waiter woke me up.\n");

                } else {
                  ret = GALLUS_RESULT_OK;
                }
              } else {
                ret = GALLUS_RESULT_NOT_OPERATIONAL;
              }
            } else {
              ret = GALLUS_RESULT_OK;
            }
          } else {
            ret = GALLUS_RESULT_OK;
          }
        } else {
          ret = GALLUS_RESULT_NOT_OPERATIONAL;
        }
      } else {
        ret = GALLUS_RESULT_OK;
      }

    }
    s_unlock(*cbptr);

  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


gallus_result_t
gallus_cbuffer_wait_gettable(gallus_cbuffer_t *cbptr,
                             gallus_chrono_t nsec) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (cbptr != NULL && *cbptr != NULL) {

    s_lock(*cbptr);
    {
      if ((*cbptr)->m_n_elements > 0) {
        ret = (*cbptr)->m_n_elements;
      } else {
        ret = s_wait_gettable(*cbptr, nsec);
        if (ret == GALLUS_RESULT_OK) {
          ret = (*cbptr)->m_n_elements;
        }
      }
    }
    s_unlock(*cbptr);

  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


gallus_result_t
gallus_cbuffer_wait_puttable(gallus_cbuffer_t *cbptr,
                             gallus_chrono_t nsec) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  int64_t remains;

  if (cbptr != NULL && *cbptr != NULL) {

    s_lock(*cbptr);
    {
      remains = (*cbptr)->m_n_max_elements - (*cbptr)->m_n_elements;
      if (remains > 0) {
        ret = (gallus_result_t)remains;
      } else {
        ret = s_wait_puttable(*cbptr, nsec);
        if (ret == GALLUS_RESULT_OK) {
          ret = (*cbptr)->m_n_max_elements - (*cbptr)->m_n_elements;
        }
      }
    }
    s_unlock(*cbptr);

  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}





gallus_result_t
gallus_cbuffer_put_with_size(gallus_cbuffer_t *cbptr,
                             void **valptr,
                             size_t valsz,
                             gallus_chrono_t nsec) {
  int64_t n = s_put_n(cbptr, (void *)valptr, 1LL, valsz, nsec, NULL);
  return (n == 1LL) ? GALLUS_RESULT_OK : n;
}


gallus_result_t
gallus_cbuffer_put_n_with_size(gallus_cbuffer_t *cbptr,
                               void **valptr,
                               size_t n_vals,
                               size_t valsz,
                               gallus_chrono_t nsec,
                               size_t *n_actual_put) {
  return s_put_n(cbptr, (void *)valptr, n_vals, valsz, nsec, n_actual_put);
}


gallus_result_t
gallus_cbuffer_get_with_size(gallus_cbuffer_t *cbptr,
                             void **valptr,
                             size_t valsz,
                             gallus_chrono_t nsec) {
  int64_t n = s_get_n(cbptr, (void *)valptr, 1LL, 1LL,
                      valsz, nsec, NULL, true);
  return (n == 1LL) ? GALLUS_RESULT_OK : n;
}


gallus_result_t
gallus_cbuffer_get_n_with_size(gallus_cbuffer_t *cbptr,
                               void **valptr,
                               size_t n_vals_max,
                               size_t n_at_least,
                               size_t valsz,
                               gallus_chrono_t nsec,
                               size_t *n_actual_get) {
  return s_get_n(cbptr, (void *)valptr, n_vals_max, n_at_least,
                 valsz, nsec, n_actual_get, true);
}


gallus_result_t
gallus_cbuffer_peek_with_size(gallus_cbuffer_t *cbptr,
                              void **valptr,
                              size_t valsz,
                              gallus_chrono_t nsec) {
  int64_t n = s_get_n(cbptr, (void *)valptr, 1LL, 1LL,
                      valsz, nsec, NULL, false);
  return (n == 1LL) ? GALLUS_RESULT_OK : n;
}


gallus_result_t
gallus_cbuffer_peek_n_with_size(gallus_cbuffer_t *cbptr,
                                void **valptr,
                                size_t n_vals_max,
                                size_t n_at_least,
                                size_t valsz,
                                gallus_chrono_t nsec,
                                size_t *n_actual_get) {
  return s_get_n(cbptr, (void *)valptr, n_vals_max, n_at_least,
                 valsz, nsec, n_actual_get, false);
}





gallus_result_t
gallus_cbuffer_size(gallus_cbuffer_t *cbptr) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (cbptr != NULL &&
      *cbptr != NULL) {

    s_lock(*cbptr);
    {
      if ((*cbptr)->m_is_operational == true) {
        ret = (*cbptr)->m_n_elements;
      } else {
        ret = GALLUS_RESULT_NOT_OPERATIONAL;
      }
    }
    s_unlock(*cbptr);

  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


gallus_result_t
gallus_cbuffer_remaining_capacity(gallus_cbuffer_t *cbptr) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (cbptr != NULL &&
      *cbptr != NULL) {

    s_lock(*cbptr);
    {
      if ((*cbptr)->m_is_operational == true) {
        ret = (*cbptr)->m_n_max_elements - (*cbptr)->m_n_elements;
      } else {
        ret = GALLUS_RESULT_NOT_OPERATIONAL;
      }
    }
    s_unlock(*cbptr);

  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


gallus_result_t
gallus_cbuffer_max_capacity(gallus_cbuffer_t *cbptr) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (cbptr != NULL &&
      *cbptr != NULL) {

    s_lock(*cbptr);
    {
      if ((*cbptr)->m_is_operational == true) {
        ret = (*cbptr)->m_n_max_elements;
      } else {
        ret = GALLUS_RESULT_NOT_OPERATIONAL;
      }
    }
    s_unlock(*cbptr);

  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


gallus_result_t
gallus_cbuffer_is_full(gallus_cbuffer_t *cbptr, bool *retptr) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (cbptr != NULL &&
      *cbptr != NULL &&
      retptr != NULL) {
    *retptr = false;

    s_lock(*cbptr);
    {
      if ((*cbptr)->m_is_operational == true) {
        *retptr = ((*cbptr)->m_n_elements >= (*cbptr)->m_n_max_elements) ?
                  true : false;
        ret = GALLUS_RESULT_OK;
      } else {
        ret = GALLUS_RESULT_NOT_OPERATIONAL;
      }
    }
    s_unlock(*cbptr);

  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


gallus_result_t
gallus_cbuffer_is_empty(gallus_cbuffer_t *cbptr, bool *retptr) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (cbptr != NULL &&
      *cbptr != NULL &&
      retptr != NULL) {
    *retptr = false;

    s_lock(*cbptr);
    {
      if ((*cbptr)->m_is_operational == true) {
        *retptr = ((*cbptr)->m_n_elements == 0) ? true : false;
        ret = GALLUS_RESULT_OK;
      } else {
        ret = GALLUS_RESULT_NOT_OPERATIONAL;
      }
    }
    s_unlock(*cbptr);

  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


gallus_result_t
gallus_cbuffer_is_operational(gallus_cbuffer_t *cbptr, bool *retptr) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (cbptr != NULL &&
      *cbptr != NULL &&
      retptr != NULL) {
    *retptr = false;

    s_lock(*cbptr);
    {
      *retptr = (*cbptr)->m_is_operational;
      ret = GALLUS_RESULT_OK;
    }
    s_unlock(*cbptr);

  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


void
gallus_cbuffer_cancel_janitor(gallus_cbuffer_t *cbptr) {
  if (cbptr != NULL &&
      *cbptr != NULL) {
    s_unlock(*cbptr);
  }
}


gallus_result_t
cbuffer_setup_for_qmuxer(gallus_cbuffer_t cb,
                         gallus_qmuxer_t qmx,
                         ssize_t *szptr,
                         ssize_t *remptr,
                         gallus_qmuxer_poll_event_t type,
                         bool is_pre) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (cb != NULL &&
      qmx != NULL &&
      szptr != NULL &&
      remptr != NULL) {

    s_lock(cb);
    {
      if (cb->m_is_operational == true) {
        *szptr = cb->m_n_elements;
        *remptr = cb->m_n_max_elements - cb->m_n_elements;

        ret = 0;
        /*
         * Note that a case; (*szptr == 0 && *remptr == 0) never happen.
         */
        if (NEED_WAIT_READABLE(type) == true && *szptr == 0) {
          /*
           * Current queue size equals to zero so we need to wait for
           * readable.
           */
          ret = (gallus_result_t)GALLUS_QMUXER_POLL_READABLE;
        } else if (NEED_WAIT_WRITABLE(type) == true && *remptr == 0) {
          /*
           * Current remaining capacity is zero so we need to wait for
           * writable.
           */
          ret = (gallus_result_t)GALLUS_QMUXER_POLL_WRITABLE;
        }

        if (is_pre == true && ret > 0) {
          cb->m_qmuxer = qmx;
          cb->m_type = ret;
        } else {
          /*
           * We need this since the qmx could be not available when the
           * next event occurs.
           */
          cb->m_qmuxer = NULL;
          cb->m_type = 0;
        }

      } else {
        ret = GALLUS_RESULT_NOT_OPERATIONAL;
      }
    }
    s_unlock(cb);

  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}

