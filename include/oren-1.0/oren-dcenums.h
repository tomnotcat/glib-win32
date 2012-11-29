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
#ifndef __OREN_DC_ENUMS_H__
#define __OREN_DC_ENUMS_H__

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * OrenDCLoginResult:
 * @OREN_DCLOGIN_RESULT_UNKNOWN: Unknown value
 * @OREN_DCLOGIN_RESULT_SUCCESS: Login success
 * @OREN_DCLOGIN_RESULT_TIMEOUT: Login timeout
 * @OREN_DCLOGIN_RESULT_ERRCVER: Invalid client protocol version
 * @OREN_DCLOGIN_RESULT_ERRSVER: Invalid server protocol version
 * @OREN_DCLOGIN_RESULT_ERRSIG: Invalid signature
 * @OREN_DCLOGIN_RESULT_ERRUSERNAME: Invalid user name
 * @OREN_DCLOGIN_RESULT_ERRCNLNAME: Invalid channel name
 * @OREN_DCLOGIN_RESULT_ERROVERLOAD: Server overloaded
 * @OREN_DCLOGIN_RESULT_ERRSIZE: Invalid packet size
 * @OREN_DCLOGIN_RESULT_ERRLCODE: Invalid login code
 * @OREN_DCLOGIN_RESULT_ERRADDR: Invalid channel address
 * @OREN_DCLOGIN_RESULT_NOSNAME: No server name
 * @OREN_DCLOGIN_RESULT_NOCNAME: No client name
 * @OREN_DCLOGIN_RESULT_NOSVER: No server version
 * @OREN_DCLOGIN_RESULT_NOCVER: No client version
 * @OREN_DCLOGIN_RESULT_NONETYPE: No network type
 * @OREN_DCLOGIN_RESULT_NOSIG: No signature
 * @OREN_DCLOGIN_RESULT_SVRINIT: Server initialization
 * @OREN_DCLOGIN_RESULT_ERRCMREQ: Request servers failed
 */
typedef enum {
    OREN_DCLOGIN_RESULT_UNKNOWN,
    OREN_DCLOGIN_RESULT_SUCCESS,
    OREN_DCLOGIN_RESULT_TIMEOUT,
    OREN_DCLOGIN_RESULT_ERRCVER,
    OREN_DCLOGIN_RESULT_ERRSVER,
    OREN_DCLOGIN_RESULT_ERRSIG,
    OREN_DCLOGIN_RESULT_ERRUSERNAME,
    OREN_DCLOGIN_RESULT_ERRCNLNAME,
    OREN_DCLOGIN_RESULT_ERROVERLOAD,
    OREN_DCLOGIN_RESULT_ERRSIZE,
    OREN_DCLOGIN_RESULT_ERRLCODE,
    OREN_DCLOGIN_RESULT_ERRADDR,
    OREN_DCLOGIN_RESULT_NOSNAME,
    OREN_DCLOGIN_RESULT_NOCNAME,
    OREN_DCLOGIN_RESULT_NOSVER,
    OREN_DCLOGIN_RESULT_NOCVER,
    OREN_DCLOGIN_RESULT_NONETYPE,
    OREN_DCLOGIN_RESULT_NOSIG,
    OREN_DCLOGIN_RESULT_SVRINIT,
    OREN_DCLOGIN_RESULT_ERRCMREQ
} OrenDCLoginResult;

/**
 * OrenDCLogoutReason:
 * @OREN_DCLOGOUT_REASON_UNKNOWN: Unknown value
 * @OREN_DCLOGOUT_REASON_NORMAL: Normally logout
 * @OREN_DCLOGOUT_REASON_DISCONNECT: Disconnected
 * @OREN_DCLOGOUT_REASON_KICKOUT: Kickout by server
 * @OREN_DCLOGOUT_REASON_FROZEN: Channel frozen
 */
typedef enum {
    OREN_DCLOGOUT_REASON_UNKNOWN,
    OREN_DCLOGOUT_REASON_NORMAL,
    OREN_DCLOGOUT_REASON_DISCONNECT,
    OREN_DCLOGOUT_REASON_KICKOUT,
    OREN_DCLOGOUT_REASON_FROZEN
} OrenDCLogoutReason;

G_END_DECLS

#endif /* __OREN_DC_ENUMS_H__ */
