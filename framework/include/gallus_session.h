/* 
 * $__Copyright__$
 */
#ifndef __GALLUS_SESSION_H__
#define __GALLUS_SESSION_H__

#ifdef ENABLE_DEPRECATED

/*
 * DEPRECATED.
 */

/**
 * @file       gallus_session.h
 */
#include "gallus_ip_addr.h"





__BEGIN_DECLS





typedef struct session *gallus_session_t;

/*
 * If you have changed, also changed following.
 * - session_type_to_str
 * - session_type_to_enum
 */
typedef enum {
  SESSION_PASSIVE     = 0x0001,
  SESSION_ACTIVE      = 0x0002,
  SESSION_ACCEPTED    = 0x0004,
  SESSION_TCP         = 0x0010,
  SESSION_TCP6        = 0x0020,
  SESSION_UNIX_DGRAM  = 0x0100,
  SESSION_UNIX_STREAM = 0x0200,
  SESSION_TLS         = 0x1000,
} session_type_t;


/**
 * Create a session.
 *
 *  @param[in]  session_type  A session type for creating.
 *  @param[out] session       A created session.
 *
 *  @retval GALLUS_RESULT_OK      Succeeded.
 *  @retval !=GALLUS_RESULT_OK    Failed.
 *
 *  @details Call this function for creating a session for tcp, tcp6, tls etc.
 *  Session_type must be set socket type(SESSION_PASSIVE or SESSION_ACCEPTED)
 *  and protocol type(SESSION_TCP or SESSION_TCP6).
 */
gallus_result_t
session_create(session_type_t session_type, gallus_session_t *session);

/**
 * Create a session pair.
 *
 *  @param[in]  session_type  A session type for creating but only SESSION_UNIX_DGRAM or SESSION_UNIX_STREAM are allowed.
 *  @param[out] session[2]    created session pair.
 *
 *  @retval GALLUS_RESULT_OK      Succeeded.
 *  @retval !=GALLUS_RESULT_OK    Failed.
 *
 *  @details Call this function for creating session pair for unix domain sockets.
 *  Session_type must be set SESSION_UNIX_DGRAM or SESSION_UNIX_STREAM.
 */
gallus_result_t
session_pair(session_type_t session_type, gallus_session_t session[2]);

/**
 * Destroy session.
 *
 *  @param[in] session A session.
 *
 *  @details Call this function to destroy a session object.
 */
void
session_destroy(gallus_session_t session);

/**
 * Return a session is alive or not.
 *
 *  @param[in] session  A session pointer.
 *
 *  @retval TRUE  Alive.
 *  @retval FALSE Dead.
 *
 */
bool
session_is_alive(gallus_session_t session);

/**
 * Connect a session to a server.
 *
 *  @param[in]  s       A session.
 *  @param[in]  daddr   A server address.
 *  @param[in]  dport   A server port.
 *  @param[in]  saddr   A source address or NULL.
 *  @param[in]  sport   A source port or 0.
 *
 *  @retval GALLUS_RESULT_OK          Succeeded.
 *  @retval GALLUS_RESULT_EINPROGRESS The connection cannot be
 *  completed immediately.
 *
 *  @details Connect a non-blocking socket session to a server socket.
 *  Set a source address and a source port if needed.
 *
 */
gallus_result_t
session_connect(gallus_session_t s, gallus_ip_address_t *daddr,
                uint16_t dport,
                gallus_ip_address_t *saddr, uint16_t sport);

/**
 * Close a session.
 *
 *  @param[in]  s  A session.
 *
 */
void
session_close(gallus_session_t s);

/**
 * Check a session socket.
 *
 *  @param[in]  s       A session.
 *
 *  @retval GALLUS_RESULT_OK            Succeeded.
 *  @retval GALLUS_RESULT_SOCKET_ERROR  Failed, a session socket is invalid.
 *
 */
gallus_result_t
session_connect_check(gallus_session_t s);

/**
 * Read data from a session.
 *
 *  @param[in]  s       A session.
 *  @param[in]  buf     Read data buffer.
 *  @param[in]  n       Read data size.
 *
 *  @retval Size of read data.
 *
 */
ssize_t
session_read(gallus_session_t s, void *buf, size_t n);

/**
 * Write data to a session.
 *
 *  @param[in]  s       A session.
 *  @param[in]  buf     Write data buffer.
 *  @param[in]  n       Write data size.
 *
 *  @retval Size of wrote data.
 *
 */
ssize_t
session_write(gallus_session_t s, void *buf, size_t n);

/**
 * Get socket descriptor in a session.
 *
 *  @param[in]  s       A session.
 *
 *  @retval A descripor in session.
 *
 *  @details TEST USE ONLY.
 *
 */
int
session_sockfd_get(gallus_session_t s);

/**
 * Put socket descriptor into a session.
 *
 *  @param[in]  s       A session.
 *  @param[in]  sock    A descripor.
 *
 *  @details TEST USE ONLY.
 *
 */
void
session_sockfd_set(gallus_session_t s, int sock);

/**
 * Put socket writer into a session.
 *
 *  @param[in]  s       A session.
 *  @param[in]  write   A writer
 *
 *  @details TEST USE ONLY.
 *
 */
void
session_write_set(gallus_session_t s, ssize_t (*write)(gallus_session_t,
                  void *, size_t));

/**
 * Fgets for a session.
 *
 *  @param[in]  str     Store a read string to.
 *  @param[in]  size    Size of str.
 *  @param[in]  s       A session.
 *
 *  @retval Pointer to str.
 *
 */
char *
session_fgets(char *restrict str, int size, gallus_session_t s);

/**
 * Bind for a session.
 *
 *  @param[in]  s      A session.
 *  @param[in]  addr   A source address.
 *  @param[in]  port   A source port.
 *
 *  @retval GALLUS_RESULT_OK              Succeeded.
 *  @retval GALLUS_RESULT_POSIX_API_ERROR Failed, in systemcalls.
 *
 */
gallus_result_t
session_bind(gallus_session_t s, gallus_ip_address_t *saddr, uint16_t port);

/**
 * Accept for a session.
 *
 *  @param[in]   s1      A session.
 *  @param[out]  s2     A accepted session.
 *
 *  @retval GALLUS_RESULT_OK              Succeeded.
 *  @retval GALLUS_RESULT_POSIX_API_ERROR Failed, in systemcalls.
 *
 *  @details Allocated a accepted new sessesion, if succeeded.
 */
gallus_result_t
session_accept(gallus_session_t s1, gallus_session_t *s2);

/**
 * Clear a read/write event in a session.
 *
 *  @param[in]   s     A session.
 *
 *  @details Clear a read/write event in a session for polling.
 */
void
session_event_clear(gallus_session_t s);

/**
 * Set a read event into a session.
 *
 *  @param[in]   s     A session.
 *
 *  @details Set a read event into a session for polling.
 */
void
session_read_event_set(gallus_session_t s);

/**
 * Unset a read event into a session.
 *
 *  @param[in]   s     A session.
 *
 *  @details Unset a read event into a session for polling.
 */
void
session_read_event_unset(gallus_session_t s);

/**
 * Return a session is readable or not.
 *
 *  @param[in]   s     A session.
 *  @param[out]  b     Readable or not.
 *
 *  @retval GALLUS_RESULT_OK Succeeded.
 *
 */
gallus_result_t
session_is_readable(gallus_session_t s, bool *b);

/**
 * Return a session is read on or not.
 *
 *  @param[in]   s     A session.
 *  @param[out]  b     Read on or not.
 *
 *  @retval GALLUS_RESULT_OK Succeeded.
 *
 */
gallus_result_t
session_is_read_on(gallus_session_t s, bool *b);

/**
 * Set a write event into a session.
 *
 *  @param[in]   s     A session.
 *
 *  @details Set a write event into a session for polling.
 */
void
session_write_event_set(gallus_session_t s);

/**
 * Unset a write event into a session.
 *
 *  @param[in]   s     A session.
 *
 *  @details Unset a write event into a session for polling.
 */
void
session_write_event_unset(gallus_session_t s);

/**
 * Return a session is writable or not.
 *
 *  @param[in]   s     A session.
 *  @param[out]  b     Writable or not.
 *
 *  @retval GALLUS_RESULT_OK Succeeded.
 *
 */
gallus_result_t
session_is_writable(gallus_session_t s, bool *b);

/**
 * Return a session is write on or not.
 *
 *  @param[in]   s     A session.
 *  @param[out]  b     Write on or not.
 *
 *  @retval GALLUS_RESULT_OK Succeeded.
 *
 */
gallus_result_t
session_is_write_on(gallus_session_t s, bool *b);

/**
 * Poll for sessions.
 *
 *  @param[in]  s[]      Sessions.
 *  @param[in]  n        Number of sessions.
 *  @param[in]  timeout  Timeout(msec).
 *
 *  @retval > 0                            Succeeded, return value is number of readable/writable session.
 *  @retval GALLUS_RESULT_TIMEDOUT        Timeouted.
 *  @retval GALLUS_RESULT_INTERRUPTED     Interrupted.
 *  @retval GALLUS_RESULT_POSIX_API_ERROR Failed, in systemcalls.
 *
 */
gallus_result_t
session_poll(gallus_session_t s[], int n, int timeout);

/**
 * Return a session is passive or not.
 *
 *  @param[in]   s     A session.
 *
 *  @retval Passive or not.
 *
 */
bool
session_is_passive(gallus_session_t s);

/**
 * Fprintf for a session.
 *
 *  @param[in]  s       A session.
 *  @param[in]  fmt     format.
 *  @param[in]  ...     variable args.
 *
 *
 */
int
session_printf(gallus_session_t s, const char *fmt, ...)
__attribute__((format(printf, 2, 3)));

/**
 * Vprintf for a session.
 *
 *  @param[in]  s       A session.
 *  @param[in]  fmt     format.
 *  @param[in]  ap      variable args.
 *
 *
 */
int
session_vprintf(gallus_session_t s, const char *fmt, va_list ap)
__attribute__((format(printf, 2, 0)));;

/**
 * Return a session is passive or not.
 *
 *  @param[in]   session_type     session type.
 *
 *  @retval TRUE  passive.
 *  @retval FALSE not passive.
 *
 */
bool
session_type_is_passive(const session_type_t session_type);

/**
 * Return a session is active or not.
 *
 *  @param[in]   session_type     session type.
 *
 *  @retval TRUE  active.
 *  @retval FALSE not active.
 *
 */
bool
session_type_is_active(const session_type_t session_type);

/**
 * Return a session is accepted or not.
 *
 *  @param[in]   session_type     session type.
 *
 *  @retval TRUE  accepted.
 *  @retval FALSE not accepted.
 *
 */
bool
session_type_is_accepted(const session_type_t session_type);

/**
 * Return a session is tcp or not.
 *
 *  @param[in]   session_type     session type.
 *
 *  @retval TRUE  TCP.
 *  @retval FALSE not TCP.
 *
 */
bool
session_type_is_tcp(const session_type_t session_type);

/**
 * Return a session is tcp6 or not.
 *
 *  @param[in]   session_type     session type.
 *
 *  @retval TRUE  TCP6.
 *  @retval FALSE not TCP6.
 *
 */
bool
session_type_is_tcp6(const session_type_t session_type);

/**
 * Return a session is tls or not.
 *
 *  @param[in]   session_type     session type.
 *
 *  @retval TRUE  TLS.
 *  @retval FALSE not TLS.
 *
 */
bool
session_type_is_tls(const session_type_t session_type);


/**
 * Get a session id.
 *
 *  @param[in]   s     A session.
 *
 *  @retval Session id.
 *
 */
uint64_t
session_id_get(gallus_session_t s);





__END_DECLS




#endif /* ENABLE_DEPRECATED */

#endif /* __SESSION_H__ */
