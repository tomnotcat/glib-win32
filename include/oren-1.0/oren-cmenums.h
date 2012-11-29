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
#ifndef __OREN_CM_ENUMS_H__
#define __OREN_CM_ENUMS_H__

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * OrenCMLoginResult:
 * @OREN_CMLOGIN_RESULT_UNKNOWN: Unknown value
 * @OREN_CMLOGIN_RESULT_SUCCESS: Login success
 * @OREN_CMLOGIN_RESULT_TIMEOUT: Login timeout
 * @OREN_CMLOGIN_RESULT_ERRVER: Invalid protocol version
 * @OREN_CMLOGIN_RESULT_ERRSIG: Invalid signature
 * @OREN_CMLOGIN_RESULT_ERRNAME: Invalid name
 * @OREN_CMLOGIN_RESULT_ERRGROUP: Invalid group
 * @OREN_CMLOGIN_RESULT_ERRSVC: Invalid service
 */
typedef enum {
    OREN_CMLOGIN_RESULT_UNKNOWN,
    OREN_CMLOGIN_RESULT_SUCCESS,
    OREN_CMLOGIN_RESULT_TIMEOUT,
    OREN_CMLOGIN_RESULT_ERRVER,
    OREN_CMLOGIN_RESULT_ERRSIG,
    OREN_CMLOGIN_RESULT_ERRNAME,
    OREN_CMLOGIN_RESULT_ERRGROUP,
    OREN_CMLOGIN_RESULT_ERRSVC
} OrenCMLoginResult;

/**
 * OrenCMLogoutReason:
 * @OREN_CMLOGOUT_REASON_UNKNOWN: Unknown value
 * @OREN_CMLOGOUT_REASON_NORMAL: Normally logout
 * @OREN_CMLOGOUT_REASON_DISCONNECT: Disconnected
 * @OREN_CMLOGOUT_REASON_KICKOUT: Kickout by server
 */
typedef enum {
    OREN_CMLOGOUT_REASON_UNKNOWN,
    OREN_CMLOGOUT_REASON_NORMAL,
    OREN_CMLOGOUT_REASON_DISCONNECT,
    OREN_CMLOGOUT_REASON_KICKOUT
} OrenCMLogoutReason;

/**
 * OrenCMRequestResult:
 * @OREN_CMREQUEST_RESULT_UNKNOWN: Unknown value
 * @OREN_CMREQUEST_RESULT_SUCCESS: Request success
 * @OREN_CMREQUEST_RESULT_TIMEOUT: Request timeout
 * @OREN_CMREQUEST_RESULT_SVRINIT: Server initialization
 * @OREN_CMREQUEST_RESULT_NOSIG: No signature
 * @OREN_CMREQUEST_RESULT_ERRSIG: Invalid signature
 * @OREN_CMREQUEST_RESULT_ERRCVER: Invalid client protocol version
 * @OREN_CMREQUEST_RESULT_ERRSVER: Invalid server protocol version
 */
typedef enum {
    OREN_CMREQUEST_RESULT_UNKNOWN,
    OREN_CMREQUEST_RESULT_SUCCESS,
    OREN_CMREQUEST_RESULT_TIMEOUT,
    OREN_CMREQUEST_RESULT_SVRINIT,
    OREN_CMREQUEST_RESULT_NOSIG,
    OREN_CMREQUEST_RESULT_ERRSIG,
    OREN_CMREQUEST_RESULT_ERRCVER,
    OREN_CMREQUEST_RESULT_ERRSVER
} OrenCMRequestResult;

G_END_DECLS

#endif /* __OREN_CM_ENUMS_H__ */
