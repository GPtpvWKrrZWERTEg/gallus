/* 
 * $__Copyright__$
 */
#ifndef __GALLUS_MAINLOOP_H__
#define __GALLUS_MAINLOOP_H__





/**
 *	@file	gallus_mainloop.h
 */





__BEGIN_DECLS





/**
 * The signature of the mainloop pre/post-startup hook functions.
 *
 *	@param[in]	argc		A # of arguments, passed to
 *					the \b gallus_mainloop().
 *	@param[in]	argv		Arguments passed to
 *					the \b gallus_mainloop().
 *
 *	@retval GALLUS_RESULT_OK	Succeeded.
 *	@retval <0			Any failures. Returning
 *					these values makes the mainloop exit.
 *
 *	@detals Use these functions to install a hook for the mainloop
 *	pre/post-startup.
 */
typedef gallus_result_t (*gallus_mainloop_startup_hook_proc_t)(
  int argc, const char *const argv[]);





/**
 * Request abortion before the mainloop start.
 *
 *	@details Use this function if you want to abort the
 *	application before entering the mainloop, like from a signal
 *	handler.
 */
void
gallus_abort_before_mainloop(void);


/**
 * Query abortion request.
 *
 *	@details Use this function if you need to provide a custom
 *	mainloop function and its abortion.
 */
bool
gallus_is_abort_before_mainloop(void);


/**
 * Set name of the pid file.
 *
 *	@param[in]	file	A filename for the pid file.
 *
 *	@retval	GALLUS_RESULT_OK	Suceeded.
 *	@retval	<0			Any failures.
 *
 *	@details If the filename is not specified, the \b
 *	gallus_mainloop() uses "/var/run/%commandname%.pid", where
 *	the %commandname% is, on Linux, retrieved by /proc.
 */
gallus_result_t
gallus_set_pidfile(const char *file);


/**
 * Create the pid file.
 *
 *	@details The filename could be specified by the \b
 *	gallus_set_pidfile(). Not needed to be called explicitly if
 *	the mainloop APIs are used.
 */
void
gallus_create_pidfile(void);


/**
 * Remove the pid file.
 *
 *	@details Not needed to be called explicitly if the mainloop
 *	APIs are used.
 */
void
gallus_remove_pidfile(void);


/**
 * Set # of callout workers.
 *
 *	@param[in]	n	A # of the workers.
 *
 *	@retval	GALLUS_RESULT_OK	Suceeded.
 *	@retval LAGOUS_RESULT_TOO_LARGE	Too large, must be <= 4.
 *	@retval <0			Any failures.
 */
gallus_result_t
gallus_mainloop_set_callout_workers_number(size_t n);


/**
 * Set shutdown request check interval in the main loop.
 *
 *	@param[in]	nsec	An interval (in nsec.)
 *
 *	@retval	GALLUS_RESULT_OK	Suceeded.
 *	@retval GALLUS_RESULT_TO_SMALL	To small, must be > 1 sec.
 *	@retval GALLUS_RESULT_TO_LARGE	TO large, must be <= 10 sec.
 */
gallus_result_t
gallus_mainloop_set_shutdown_check_interval(gallus_chrono_t nsec);


/**
 * Set shutdown timeout for the main loop.
 *
 *	@param[in]	nsec	A timeout (in nsec.)
 *
 *	@retval	GALLUS_RESULT_OK	Suceeded.
 *	@retval GALLUS_RESULT_TO_SMALL	To small, must be > 1 sec.
 *	@retval GALLUS_RESULT_TO_LARGE	TO large, must be <= 30 sec.
 *
 *	@details When the main loop is requested to shut the
 *	application down, it broadcasts the shutdown message to all
 *	the modules running. If any modules still remain after the \b
 *	nsec, the main loop forcibly terminate them.
 */
gallus_result_t
gallus_mainloop_set_shutdown_timeout(gallus_chrono_t nsec);


/**
 * Enter the application main loop.
 *
 *	@paran[in] argc		The \b argc for the \b
 *				gallus_module_initialize_all().
 *	@paran[in] argv		The \b argv for the \b
 *				gallus_module_initialize_all().
 *	@param[in] pre_hook	The pre-startup hook (NULL allowed.)
 *	@param[in] post_hook	The post-startup hook (NULL allowed.)
 *	@param[in] do_fork	If \b true call fork(2) before startup.
 *	@param[in] do_pidfile	If \b true create a pid file.
 *				The file name can be specified
 *				by the \b gallus_set_pidfile().
 *	@param[in] do_thread	If \b true the main loop runs in a newly
 *				spawned thread.
 *
 *	@retval	GALLUS_RESULT_OK	Succeeded.
 *	@retval <0			Any failures.
 *
 *	@details If the \b proc is NULL, the default main loop
 *	function, that a) initializa and start all the registered
 *	module; b) Wait for the shutdown request; c) If got the
 *	request, shutdown and finalize all the modules; d) Exit the
 *	application.
 */
gallus_result_t
gallus_mainloop(int argc, const char *const argv[],
                gallus_mainloop_startup_hook_proc_t pre_hook,
                gallus_mainloop_startup_hook_proc_t post_hook,
                bool do_fork, bool do_pidfile, bool do_thread);


/**
 * Enter the application main loop with the callout support.
 *
 *	@paran[in] argc		The \b argc for the \b
 *				gallus_module_initialize_all().
 *	@paran[in] argv		The \b argv for the \b
 *				gallus_module_initialize_all().
 *	@param[in] pre_hook	The pre-startup hook (NULL allowed.)
 *	@param[in] post_hook	The post-startup hook (NULL allowed.)
 *	@param[in] do_fork	If \b true call fork(2) before startup.
 *	@param[in] do_pidfile	If \b true create a pid file.
 *				The file name can be specified
 *				by the \b gallus_set_pidfile().
 *	@param[in] do_thread	If \b true the main loop runs in a newly
 *				spawned thread.
 *
 *	@retval	GALLUS_RESULT_OK	Succeeded.
 *	@retval <0			Any failures.
 *
 *	@details If the \b proc is NULL, the default main loop
 *	function, that a) initializa and start all the registered
 *	module; b) Wait for the shutdown request; c) If got the
 *	request, shutdown and finalize all the modules; d) Exit the
 *	application.
 */
gallus_result_t
gallus_mainloop_with_callout(int argc, const char *const argv[],
                             gallus_mainloop_startup_hook_proc_t pre_hook,
                             gallus_mainloop_startup_hook_proc_t post_hook,
                             bool do_fork, bool do_pidfile,
                             bool do_thread);


/**
 * Wait for the finish of the mainloop thread.
 *
 *	@details This API is meaningful only when the mainloop runs in
 *	a dedicated thread that is spawned by passing \b true for the
 *	\b do_thread parameter of the \b gallus_mainloop() or the \b
 *	gallus_mainloop_with_callout().
 *
 *	@detail Note that it is required that the main loop itself
 *	must be stopped by proper methodology (usually calling the \b
 *	global_state_request_shutdown()), which should be suitable for
 *	the each main loop. Otherwise this API blocks waiting for the
 *	main loop finish.
 */
void
gallus_mainloop_wait_thread(void);


/**
 * The prologue of the application main loop.
 *
 *	@paran[in] argc The \b argc for the \b
 *      		gallus_module_initialize_all().
 *	@paran[in] argv The \b argv for the \b
 *			gallus_module_initialize_all().
 *	@param[in] pre_hook	The pre-startup hook (NULL allowed.)
 *	@param[in] post_hook	The post-startup hook (NULL allowed.)
 *
 *	@retval GALLUS_RESULT_OK       Succeeded.
 *	@retval <0                      Any failures.
 *
 *	@details This function mainly performs the initialization and
 *	startup of the registerd modules. Use this function if you
 *	nned to provide a custom main loop function.
 */
gallus_result_t
gallus_mainloop_prologue(int argc, const char *const argv[],
                         gallus_mainloop_startup_hook_proc_t pre_hook,
                         gallus_mainloop_startup_hook_proc_t post_hook);


/**
 * The epilogue of the application main loop.
 *
 *	@param[in]      l       The shutdown grace level.
 *	@param[in]      to      The shutdown timeout.
 *
 *	@retval GALLUS_RESULT_OK       Succeeded.
 *	@retval <0                      Any failures.
 *
 *	@detail This function mainly performs the shutdown and
 *	finalization of the registered modules. Use this function if
 *	you need to provide a custom main loop function.
 */
gallus_result_t
gallus_mainloop_epilogue(shutdown_grace_level_t l, gallus_chrono_t to);





__END_DECLS





#endif /* __GALLUS_MAINLOOP_H__ */
