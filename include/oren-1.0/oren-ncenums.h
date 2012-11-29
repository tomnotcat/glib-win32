/* Oren - a streaming media software development kit.
 *
 * Copyright © 2012 TinySoft, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __OREN_NC_ENUMS_H__
#define __OREN_NC_ENUMS_H__

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * OrenNCSocketFamily:
 * @OREN_SOCKET_FAMILY_INVALID: no address family
 * @OREN_SOCKET_FAMILY_IPV4: the IPv4 family
 * @OREN_SOCKET_FAMILY_IPV6: the IPv6 family
 */
typedef enum {
    OREN_NCSOCKET_FAMILY_INVALID,
    OREN_NCSOCKET_FAMILY_IPV4 = GLIB_SYSDEF_AF_INET,
    OREN_NCSOCKET_FAMILY_IPV6 = GLIB_SYSDEF_AF_INET6
} OrenNCSocketFamily;

/**
 * OrenNCSocketType:
 * @OREN_SOCKET_TYPE_INVALID: Type unknown or wrong
 * @OREN_SOCKET_TYPE_DATAGRAM: Connectionless, unreliable datagram passing.
 */
typedef enum
{
    OREN_NCSOCKET_TYPE_INVALID,
    OREN_NCSOCKET_TYPE_DATAGRAM
} OrenNCSocketType;

/**
 * OrenNCBufferLock:
 * @OREN_NCBUFFER_LOCK_READ: Lock read
 * @OREN_NCBUFFER_LOCK_WRITE: Lock write
 * @OREN_NCBUFFER_LOCK_ALL: Lock read and write
 */
typedef enum {
    OREN_NCBUFFER_LOCK_READ = 1 << 0,
    OREN_NCBUFFER_LOCK_WRITE = 1 << 1,
    OREN_NCBUFFER_LOCK_ALL = (OREN_NCBUFFER_LOCK_READ |
                              OREN_NCBUFFER_LOCK_WRITE)
} OrenNCBufferLock;

/**
 * OrenNCOnlineState:
 * @OREN_NCONLINE_STATE_UNKNOWN: Unknown
 * @OREN_NCONLINE_STATE_OFFLINE: Offline
 * @OREN_NCONLINE_STATE_LOGINING: In login
 * @OREN_NCONLINE_STATE_ONLINE: Online
 */
typedef enum {
    OREN_NCONLINE_STATE_UNKNOWN,
    OREN_NCONLINE_STATE_OFFLINE,
    OREN_NCONLINE_STATE_LOGINING,
    OREN_NCONLINE_STATE_ONLINE
} OrenNCOnlineState;

/**
 * OrenNCSessionWork:
 * @OREN_NCSESSION_WORK_SEND: Send data
 * @OREN_NCSESSION_WORK_PING: Send ping
 * @OREN_NCSESSION_WORK_LOST: Check lost
 * @OREN_NCSESSION_WORK_ALL: Do all work
 */
typedef enum {
    OREN_NCSESSION_WORK_SEND = 1 << 0,
    OREN_NCSESSION_WORK_PING = 1 << 1,
    OREN_NCSESSION_WORK_LOST = 1 << 2,
    OREN_NCSESSION_WORK_NOPING = (OREN_NCSESSION_WORK_SEND |
                                  OREN_NCSESSION_WORK_LOST),
    OREN_NCSESSION_WORK_ALL = (OREN_NCSESSION_WORK_SEND |
                               OREN_NCSESSION_WORK_PING |
                               OREN_NCSESSION_WORK_LOST)
} OrenNCSessionWork;

/**
 * OrenNCSessionFull:
 * @OREN_NCSESSION_FULL_SEND: Send buffer full
 * @OREN_NCSESSION_FULL_RECV: Receive buffer full
 * @OREN_NCSESSION_FULL_LOST: Lost buffer full
 */
typedef enum {
    OREN_NCSESSION_FULL_SEND,
    OREN_NCSESSION_FULL_RECV,
    OREN_NCSESSION_FULL_LOST
} OrenNCSessionFull;

G_END_DECLS

#endif /* __OREN_NC_ENUMS_H__ */
