/* 
 * $__Copyright__$
 */
#include "gallus_apis.h"





#define MAX_MODULES		1024
#define MAX_MODULE_NAME		64





typedef enum {
  MODULE_GLOBAL_STATE_UNKNOWN = 0,
  MODULE_GLOBAL_STATE_INITIALIZING,
  MODULE_GLOBAL_STATE_INITIALIZED,
  MODULE_GLOBAL_STATE_STARTING,
  MODULE_GLOBAL_STATE_STARTED,
  MODULE_GLOBAL_STATE_SHUTTINGDOWN,
  MODULE_GLOBAL_STATE_STOPPING,
  MODULE_GLOBAL_STATE_WAITING,
  MODULE_GLOBAL_STATE_SHUTDOWN,
  MODULE_GLOBAL_STATE_FINALIZING,
  MODULE_GLOBAL_STATE_FINALIZED
} s_module_global_state_t;


typedef enum {
  MODULE_STATE_UNKNOWN = 0,
  MODULE_STATE_REGISTERED,
  MODULE_STATE_INITIALIZED,
  MODULE_STATE_STARTED,
  MODULE_STATE_SHUTDOWN,
  MODULE_STATE_CANCELLING,
  MODULE_STATE_STOPPED,
  MODULE_STATE_FINALIZED
} a_module_state_t;


typedef struct {
  char m_name[MAX_MODULE_NAME];
  gallus_module_initialize_proc_t m_init_proc;
  void *m_init_arg;
  gallus_thread_t *m_thdptr;
  gallus_module_start_proc_t m_start_proc;
  gallus_module_shutdown_proc_t m_shutdown_proc;
  gallus_module_stop_proc_t m_stop_proc;
  gallus_module_finalize_proc_t m_finalize_proc;
  gallus_module_usage_proc_t m_usage_proc;
  a_module_state_t m_status;
} a_module;





static volatile s_module_global_state_t s_gstate =
  MODULE_GLOBAL_STATE_UNKNOWN;
static pthread_t s_initializer_tid = GALLUS_INVALID_THREAD;

static size_t s_n_modules = 0;
static a_module s_modules[MAX_MODULES];
static volatile size_t s_n_finalized_modules = 0;
static volatile size_t s_cur_module_idx = 0;

static volatile bool s_is_unloading = false;

static pthread_once_t s_once = PTHREAD_ONCE_INIT;
static bool s_is_inited = false;
static bool s_is_mods_inited = false;
static gallus_mutex_t s_lck = NULL;
static gallus_cond_t s_cnd = NULL;

static volatile int s_is_exit_handler_called = 0;


/*
 * NOTE:
 *
 *	This module must be initialized at last of the static
 *	construction sequence. So check the priority when new modules
 *	are added.
 */
static void	s_ctors(void) __attr_constructor__(113);
static void	s_dtors(void) __attr_destructor__(113);

static void	s_atexit_handler(void);





static void
s_once_proc(void) {
  gallus_result_t r;

  s_gstate = MODULE_GLOBAL_STATE_UNKNOWN;
  s_n_modules = 0;
  s_n_finalized_modules = 0;
  s_is_unloading = false;
  s_cur_module_idx = 0;
  s_initializer_tid = pthread_self();
  (void)__sync_add_and_fetch(&s_is_exit_handler_called, 0);
  (void)memset((void *)s_modules, 0, sizeof(s_modules));
  mbar();

  if ((r = gallus_mutex_create(&s_lck)) != GALLUS_RESULT_OK) {
    gallus_perror(r);
    gallus_exit_fatal("can't initialize a mutex.\n");
  }
  if ((r = gallus_cond_create(&s_cnd)) != GALLUS_RESULT_OK) {
    gallus_perror(r);
    gallus_exit_fatal("can't initialize a cond.\n");
  }

  if (atexit(s_atexit_handler) != 0) {
    gallus_perror(GALLUS_RESULT_POSIX_API_ERROR);
    gallus_exit_fatal("can't add an exit handler.\n");
  }

  s_is_inited = true;
}


static inline void
s_init(void) {
  (void)pthread_once(&s_once, s_once_proc);
}


static void
s_ctors(void) {
  s_init();

  gallus_msg_debug(10, "The module manager is initialized.\n");
}


static void
s_final(void) {
  if (s_lck != NULL) {
    gallus_mutex_destroy(&s_lck);
  }
  if (s_cnd != NULL) {
    gallus_cond_destroy(&s_cnd);
  }
}


static void
s_dtors(void) {
  if (s_is_inited == true) {
    s_final();

    gallus_msg_debug(10, "The module manager is finalized.\n");
  }
}





static inline void
s_lock(void) {
  if (s_lck != NULL) {
    (void)gallus_mutex_lock(&s_lck);
  }
}


static inline gallus_result_t
s_trylock(void) {
  if (s_lck != NULL) {
    return gallus_mutex_trylock(&s_lck);
  }
  return GALLUS_RESULT_INVALID_ARGS;
}


static inline void
s_unlock(void) {
  if (s_lck != NULL) {
    (void)gallus_mutex_unlock(&s_lck);
  }
}


static inline gallus_result_t
s_wait(gallus_chrono_t to) {
  if (s_lck != NULL && s_cnd != NULL) {
    return gallus_cond_wait(&s_cnd, &s_lck, to);
  } else {
    return GALLUS_RESULT_NOT_OPERATIONAL;
  }
}


static inline void
s_wakeup(void) {
  if (s_lck != NULL && s_cnd != NULL) {
    (void)gallus_cond_notify(&s_cnd, true);
  }
}





static void
s_atexit_handler(void) {
  if (likely(__sync_fetch_and_add(&s_is_exit_handler_called, 1) == 0)) {
    gallus_result_t r;
    r = s_trylock();

    if (likely(r == GALLUS_RESULT_OK)) {
      bool is_finished_cleanly = false;

      if (s_n_modules > 0) {

      recheck:
        mbar();
        if (s_gstate == MODULE_GLOBAL_STATE_UNKNOWN) {
          is_finished_cleanly = true;
        } else if (s_gstate == MODULE_GLOBAL_STATE_STARTED) {
          (void)global_state_request_shutdown(SHUTDOWN_RIGHT_NOW);
        } else if (s_gstate != MODULE_GLOBAL_STATE_FINALIZED) {
          r = s_wait(100LL * 1000LL * 1000LL);
          if (r == GALLUS_RESULT_OK) {
            goto recheck;
          } else if (r == GALLUS_RESULT_TIMEDOUT) {
            gallus_msg_warning("Module finalization seems not completed.\n");
          } else {
            gallus_perror(r);
            gallus_msg_error("module finalization wait failed.\n");
          }
        } else {
          is_finished_cleanly = true;
        }
      }

      if (is_finished_cleanly == true) {
        s_is_unloading = true;
        mbar();
      }

      s_unlock();

    } else if (r == GALLUS_RESULT_BUSY) {
      /*
       * The lock failure. Snoop s_gstate anyway. Note that it's safe
       * since the modules are always accessesed only by a single
       * thread and the thread is calling exit(3) at this moment.
       */
      if (s_gstate == MODULE_GLOBAL_STATE_UNKNOWN) {
        /*
         * No modules are initialized. Just exit cleanly and let all
         * the static destructors run.
         */
        s_is_unloading = true;
        mbar();
      } else {
        if (pthread_self() == s_initializer_tid) {
          /*
           * Made sure that this very thread is the module
           * initializer. So we can safely unlock the lock.
           */

          switch (s_gstate) {

            case MODULE_GLOBAL_STATE_FINALIZING:
            case MODULE_GLOBAL_STATE_FINALIZED:
            case MODULE_GLOBAL_STATE_UNKNOWN: {
              s_unlock();
              /*
               * Nothing is needed to do.
               */
              break;
            }

            case MODULE_GLOBAL_STATE_INITIALIZING:
            case MODULE_GLOBAL_STATE_INITIALIZED:
            case MODULE_GLOBAL_STATE_STARTING: {
              s_unlock();
              /*
               * With this only modules safely finalizable so far are
               * finalized.
               */
              gallus_module_finalize_all();
              break;
            }

            case MODULE_GLOBAL_STATE_STARTED: {
              s_unlock();
              (void)global_state_request_shutdown(SHUTDOWN_RIGHT_NOW);
              break;
            }

            case MODULE_GLOBAL_STATE_SHUTTINGDOWN:
            case MODULE_GLOBAL_STATE_STOPPING:
            case MODULE_GLOBAL_STATE_WAITING:
            case MODULE_GLOBAL_STATE_SHUTDOWN: {
              s_unlock();
              /*
               * There's nothing we can do at this moment.
               */
              break;
            }

            default: {
              s_unlock();
              break;
            }
          }

        } else { /* (pthread_self() == s_initializer_tid) */
          /*
           * This menas that a thread other than module initialized is
           * locking the lock. There's nothing we can do at this moment.
           */
          return;
        }
      } /* (s_gstate == MODULE_GLOBAL_STATE_UNKNOWN) */
    }
  }
}





static inline gallus_result_t
s_find_module(const char *name, a_module **retmptr) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (IS_VALID_STRING(name) == true) {
    ret = GALLUS_RESULT_NOT_FOUND;

    if (retmptr != NULL) {
      *retmptr = NULL;
    }

    if (s_n_modules > 0) {
      a_module *mptr;
      size_t i;

      for (i = 0; i < s_n_modules; i++) {
        mptr = &(s_modules[i]);
        if (strcmp(mptr->m_name, name) == 0) {
          if (retmptr != NULL) {
            *retmptr = mptr;
            return (gallus_result_t)i;
          }
        }
      }
    }
  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


static inline gallus_result_t
s_register_module(const char *name,
                  gallus_module_initialize_proc_t init_proc,
                  void *extarg,
                  gallus_module_start_proc_t start_proc,
                  gallus_module_shutdown_proc_t shutdown_proc,
                  gallus_module_stop_proc_t stop_proc,
                  gallus_module_finalize_proc_t finalize_proc,
                  gallus_module_usage_proc_t usage_proc) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;
  a_module *mptr = NULL;

  if (s_n_modules < MAX_MODULES &&
      (mptr = &(s_modules[s_n_modules])) != NULL) {
    if (IS_VALID_STRING(name) == true &&
        init_proc != NULL &&
        start_proc != NULL &&
        shutdown_proc != NULL &&
        finalize_proc != NULL) {
      if (s_find_module(name, NULL) == GALLUS_RESULT_NOT_FOUND) {
        snprintf(mptr->m_name, sizeof(mptr->m_name), "%s", name);
        mptr->m_init_proc = init_proc;
        mptr->m_init_arg = extarg;
        mptr->m_thdptr = NULL;
        mptr->m_start_proc = start_proc;
        mptr->m_shutdown_proc = shutdown_proc;
        mptr->m_stop_proc = stop_proc;
        mptr->m_finalize_proc = finalize_proc;
        mptr->m_usage_proc = usage_proc;
        mptr->m_status = MODULE_STATE_REGISTERED;

        s_n_modules++;

        ret = GALLUS_RESULT_OK;

      } else {
        ret = GALLUS_RESULT_ALREADY_EXISTS;
      }
    } else {
      ret = GALLUS_RESULT_INVALID_ARGS;
    }
  } else {
    ret = GALLUS_RESULT_NO_MEMORY;
  }

  return ret;
}


static inline gallus_result_t
s_initialize_module(a_module *mptr, int argc, const char *const argv[]) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (mptr != NULL &&
      mptr->m_init_proc != NULL) {
    if (mptr->m_status == MODULE_STATE_REGISTERED) {
      ret = (mptr->m_init_proc)(argc, argv, mptr->m_init_arg,
                                &(mptr->m_thdptr));
      if (ret == GALLUS_RESULT_OK) {
        mptr->m_status = MODULE_STATE_INITIALIZED;
      }
    } else {
      ret = GALLUS_RESULT_INVALID_STATE_TRANSITION;
    }
  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


static inline gallus_result_t
s_start_module(a_module *mptr) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (mptr != NULL &&
      mptr->m_start_proc != NULL) {
    if (mptr->m_status == MODULE_STATE_INITIALIZED) {
      ret = (mptr->m_start_proc)();
      if (ret == GALLUS_RESULT_OK) {
        mptr->m_status = MODULE_STATE_STARTED;
      }
    } else {
      if (mptr->m_status == MODULE_STATE_STARTED) {
        ret = GALLUS_RESULT_OK;
      } else {
        ret = GALLUS_RESULT_INVALID_STATE_TRANSITION;
      }
    }
  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


static inline gallus_result_t
s_shutdown_module(a_module *mptr, shutdown_grace_level_t level) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (mptr != NULL &&
      mptr->m_shutdown_proc != NULL &&
      IS_VALID_SHUTDOWN(level) == true) {
    if (mptr->m_status == MODULE_STATE_STARTED) {
      ret = (mptr->m_shutdown_proc)(level);
      if (mptr->m_thdptr == NULL) {
        /*
         * Means that this module doesn't have any gallus'd threads
         * nor pthreads. Just change the state to shutdwon.
         */
        mptr->m_status = MODULE_STATE_SHUTDOWN;
      }
#if 0
      else {
        /*
         * Note that we don't update the module status.
         */
      }
#endif
    } else {
      if (mptr->m_status == MODULE_STATE_SHUTDOWN ||
          mptr->m_status == MODULE_STATE_STOPPED) {
        ret = GALLUS_RESULT_OK;
      } else {
        ret = GALLUS_RESULT_INVALID_STATE_TRANSITION;
      }
    }
  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


static inline gallus_result_t
s_stop_module(a_module *mptr) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (mptr != NULL) {
    if (mptr->m_status == MODULE_STATE_STARTED) {
      if (mptr->m_stop_proc != NULL) {
        ret = (mptr->m_stop_proc)();
      } else {
        if (mptr->m_thdptr != NULL) {
          ret = gallus_thread_cancel(mptr->m_thdptr);
        } else {
          /*
           * Means this module doesn't support any stop/cancel
           * methods. Do nothing.
           */
          return GALLUS_RESULT_OK;
        }
      }
      if (ret == GALLUS_RESULT_OK) {
        if (mptr->m_thdptr == NULL) {
          /*
           * Means that this module doesn't have any gallus'd threads
           * nor pthreads. Just change the state to shutdwon.
           */
          mptr->m_status = MODULE_STATE_SHUTDOWN;
        } else {
          mptr->m_status = MODULE_STATE_CANCELLING;
        }
      }
    } else {
      if (mptr->m_status == MODULE_STATE_CANCELLING ||
          mptr->m_status == MODULE_STATE_SHUTDOWN ||
          mptr->m_status == MODULE_STATE_STOPPED) {
        ret = GALLUS_RESULT_OK;
      } else {
        ret = GALLUS_RESULT_INVALID_STATE_TRANSITION;
      }
    }
  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


static inline gallus_result_t
s_wait_module(a_module *mptr, gallus_chrono_t nsec) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (mptr != NULL) {
    if ((mptr->m_status == MODULE_STATE_CANCELLING ||
         mptr->m_status == MODULE_STATE_STARTED) &&
        mptr->m_thdptr != NULL) {
      ret = gallus_thread_wait(mptr->m_thdptr, nsec);
      if (ret == GALLUS_RESULT_OK) {
        bool is_canceled = false;
        (void)gallus_thread_is_canceled(mptr->m_thdptr, &is_canceled);
        mptr->m_status = (is_canceled == false) ?
                         MODULE_STATE_SHUTDOWN : MODULE_STATE_STOPPED;
      }
    } else {
      if (mptr->m_status == MODULE_STATE_SHUTDOWN ||
          mptr->m_status == MODULE_STATE_STOPPED) {
        ret = GALLUS_RESULT_OK;
      } else {
        ret = GALLUS_RESULT_INVALID_STATE_TRANSITION;
      }
    }
  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


static inline void
s_finalize_module(a_module *mptr) {
  if (mptr != NULL &&
      mptr->m_finalize_proc != NULL) {
    if (mptr->m_status == MODULE_STATE_INITIALIZED ||
        mptr->m_status == MODULE_STATE_SHUTDOWN ||
        mptr->m_status == MODULE_STATE_STOPPED) {
      (mptr->m_finalize_proc)();
      (void)__sync_fetch_and_add(&s_n_finalized_modules, 1);
    } else if (mptr->m_status == MODULE_STATE_UNKNOWN ||
               mptr->m_status == MODULE_STATE_REGISTERED) {
      return;
    } else {
      gallus_msg_warning("the module \"%s\" seems to be still running, "
                         "won't destruct it for safe.\n",
                         mptr->m_name);
    }
  }
}


static inline void
s_usage_module(a_module *mptr, FILE *fd) {
  if (mptr != NULL && fd != NULL &&
      mptr->m_usage_proc != NULL) {
    (mptr->m_usage_proc)(fd);
  }
}





gallus_result_t
gallus_module_register(const char *name,
                       gallus_module_initialize_proc_t init_proc,
                       void *extarg,
                       gallus_module_start_proc_t start_proc,
                       gallus_module_shutdown_proc_t shutdown_proc,
                       gallus_module_stop_proc_t stop_proc,
                       gallus_module_finalize_proc_t finalize_proc,
                       gallus_module_usage_proc_t usage_proc) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  s_lock();
  {
    ret = s_register_module(name,
                            init_proc, extarg,
                            start_proc,
                            shutdown_proc,
                            stop_proc,
                            finalize_proc,
                            usage_proc);
  }
  s_unlock();

  return ret;
}





void
gallus_module_usage_all(FILE *fd) {
  if (fd != NULL) {

    s_lock();
    {
      if (s_n_modules > 0) {
        size_t i;
        a_module *mptr;

        for (i = 0; i < s_n_modules; i++) {
          mptr = &(s_modules[i]);
          s_usage_module(mptr, fd);
        }
      }
    }
    s_unlock();

  }
}


gallus_result_t
gallus_module_initialize_all(int argc, const char *const argv[]) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  s_lock();
  {

    if (s_n_modules > 0) {
      size_t i;
      a_module *mptr;

      s_gstate = MODULE_GLOBAL_STATE_INITIALIZING;

      for (ret = GALLUS_RESULT_OK, i = 0;
           ret == GALLUS_RESULT_OK && i < s_n_modules;
           i++) {
        mptr = &(s_modules[i]);
        ret = s_initialize_module(mptr, argc, argv);
        if (ret != GALLUS_RESULT_OK) {
          gallus_perror(ret);
          gallus_msg_error("can't initialize module \"%s\".\n",
                           mptr->m_name);
        }
      }
    } else {
      ret = GALLUS_RESULT_OK;
    }

    if (ret == GALLUS_RESULT_OK) {
      s_gstate = MODULE_GLOBAL_STATE_INITIALIZED;
      s_is_mods_inited = true;
    }

  }
  s_unlock();

  return ret;
}


gallus_result_t
gallus_module_start_all(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  s_lock();
  {

    s_gstate = MODULE_GLOBAL_STATE_STARTING;

    if (s_n_modules > 0) {
      size_t i;
      a_module *mptr;

      for (ret = GALLUS_RESULT_OK, i = 0;
           ret == GALLUS_RESULT_OK && i < s_n_modules;
           i++) {
        mptr = &(s_modules[i]);
        ret = s_start_module(mptr);
        if (ret != GALLUS_RESULT_OK) {
          gallus_perror(ret);
          gallus_msg_error("can't start module \"%s\".\n",
                           mptr->m_name);
        }
      }
    } else {
      ret = GALLUS_RESULT_OK;
    }

    if (ret == GALLUS_RESULT_OK) {
      s_gstate = MODULE_GLOBAL_STATE_STARTED;
    }

  }
  s_unlock();

  return ret;
}


gallus_result_t
gallus_module_shutdown_all(shutdown_grace_level_t level) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  if (IS_VALID_SHUTDOWN(level) == true) {

    s_lock();
    {

      s_gstate = MODULE_GLOBAL_STATE_SHUTTINGDOWN;

      if (s_n_modules > 0) {
        gallus_result_t first_err = GALLUS_RESULT_OK;
        size_t i;
        a_module *mptr;

        /*
         * Reverse order.
         */
        for (i = 0; i < s_n_modules; i++) {
          mptr = &(s_modules[s_n_modules - i - 1]);
          ret = s_shutdown_module(mptr, level);
          if (ret != GALLUS_RESULT_OK) {
            gallus_perror(ret);
            gallus_msg_error("can't shutdown module \"%s\".\n",
                             mptr->m_name);
            if (first_err == GALLUS_RESULT_OK) {
              first_err = ret;
            }
          }
          /*
           * Just carry on shutting down no matter what kind of errors
           * occur.
           */
        }

        ret = first_err;

      } else {
        ret = GALLUS_RESULT_OK;
      }

    }
    s_unlock();

  } else {
    ret = GALLUS_RESULT_INVALID_ARGS;
  }

  return ret;
}


gallus_result_t
gallus_module_stop_all(void) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  s_lock();
  {

    s_gstate = MODULE_GLOBAL_STATE_STOPPING;

    if (s_n_modules > 0) {
      gallus_result_t first_err = GALLUS_RESULT_OK;
      size_t i;
      a_module *mptr;

      /*
       * Reverse order.
       */
      for (i = 0; i < s_n_modules; i++) {
        mptr = &(s_modules[s_n_modules - i - 1]);
        ret = s_stop_module(mptr);
        if (ret != GALLUS_RESULT_OK) {
          gallus_perror(ret);
          gallus_msg_error("can't stop module \"%s\".\n",
                           mptr->m_name);
          if (first_err == GALLUS_RESULT_OK) {
            first_err = ret;
          }
        }
        /*
         * Just carry on stopping no matter what kind of errors
         * occur.
         */
      }

      ret = first_err;

    } else {
      ret = GALLUS_RESULT_OK;
    }

  }
  s_unlock();

  return ret;
}


gallus_result_t
gallus_module_wait_all(gallus_chrono_t nsec) {
  gallus_result_t ret = GALLUS_RESULT_ANY_FAILURES;

  s_lock();
  {

    s_gstate = MODULE_GLOBAL_STATE_WAITING;

    if (s_n_modules > 0) {
      gallus_result_t first_err = GALLUS_RESULT_OK;
      size_t i;
      a_module *mptr;

      /*
       * Reverse order.
       */

      if (nsec < 0LL) {

        for (i = 0; i < s_n_modules; i++) {
          mptr = &(s_modules[s_n_modules - i - 1]);
          ret = s_wait_module(mptr, -1LL);
          if (ret != GALLUS_RESULT_OK) {
            gallus_perror(ret);
            gallus_msg_error("can't wait module \"%s\".\n",
                             mptr->m_name);
            if (first_err == GALLUS_RESULT_OK) {
              first_err = ret;
            }
          }
          /*
           * Just carry on wait no matter what kind of errors
           * occur.
           */
        }

      } else {

        gallus_chrono_t w_begin;
        gallus_chrono_t w_end;
        gallus_chrono_t w = nsec;

        for (i = 0; i < s_n_modules; i++) {
          mptr = &(s_modules[s_n_modules - i - 1]);
          WHAT_TIME_IS_IT_NOW_IN_NSEC(w_begin);
          ret = s_wait_module(mptr, w);
          WHAT_TIME_IS_IT_NOW_IN_NSEC(w_end);
          if (ret != GALLUS_RESULT_OK) {
            gallus_perror(ret);
            gallus_msg_error("can't wait module \"%s\".\n",
                             mptr->m_name);
            if (first_err == GALLUS_RESULT_OK) {
              first_err = ret;
            }
          }
          /*
           * Just carry on wait no matter what kind of errors
           * occur.
           */
          w = nsec - (w_end - w_begin);
          if (w < 0LL) {
            w = 0LL;
          }
        }
      }

      ret = first_err;

    } else {
      ret = GALLUS_RESULT_OK;
    }

    if (ret == GALLUS_RESULT_OK) {
      s_gstate = MODULE_GLOBAL_STATE_SHUTDOWN;
    }

  }
  s_unlock();

  return ret;
}


void
gallus_module_finalize_all(void) {
  s_lock();
  {

    s_gstate = MODULE_GLOBAL_STATE_FINALIZING;

    if (s_n_modules > 0) {
      size_t i;
      a_module *mptr;

      /*
       * Reverse order.
       */
      for (i = 0; i < s_n_modules; i++) {
        mptr = &(s_modules[s_n_modules - i - 1]);
        s_finalize_module(mptr);
      }
    }

    s_gstate = MODULE_GLOBAL_STATE_FINALIZED;

    s_wakeup();
  }
  s_unlock();
}





gallus_result_t
gallus_module_find(const char *name) {
  return s_find_module(name, NULL);
}





bool
gallus_module_is_finalized_cleanly(void) {
  if (s_is_inited == true && s_is_mods_inited == true) {
    return (s_n_modules == s_n_finalized_modules) ? true : false;
  } else {
    /*
     * Module API is not used in the application.
     * Just mimic it is used and finished cleanly.
     */
    return true;
  }
}


bool
gallus_module_is_unloading(void) {
  if (s_is_inited == true && s_is_mods_inited == true) {
    mbar();
    return (s_n_modules > 0) ? s_is_unloading : true;
  } else {
    /*
     * Module API is not used in the application.
     * Just mimic it is unloading.
     */
    return true;
  }
}

