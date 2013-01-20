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
#ifndef __OREN_NC_TYPES_H__
#define __OREN_NC_TYPES_H__

#include "oren-ncenums.h"

G_BEGIN_DECLS

typedef struct _OrenNCLogger OrenNCLogger;
typedef struct _OrenNCSockaddr OrenNCSockaddr;
typedef struct _OrenNCInetAddress OrenNCInetAddress;
typedef struct _OrenNCInetSockaddr OrenNCInetSockaddr;
typedef struct _OrenNCPingMgr OrenNCPingMgr;
typedef struct _OrenNCPingResult OrenNCPingResult;
typedef struct _OrenNCSocket OrenNCSocket;
typedef struct _OrenNCBuffer OrenNCBuffer;
typedef struct _OrenNCPacker OrenNCPacker;
typedef struct _OrenNCUnpacker OrenNCUnpacker;
typedef struct _OrenNCHandler OrenNCHandler;
typedef struct _OrenNCReactor OrenNCReactor;
typedef struct _OrenNCSession OrenNCSession;
typedef struct _OrenNCAQueue OrenNCAQueue;
typedef struct _OrenNCUdproxy OrenNCUdproxy;
typedef struct _OrenNCStatistics OrenNCStatistics;
typedef struct _OrenHttpRequest OrenHttpRequest;
typedef struct _OrenTaskBase OrenTaskBase;
typedef struct _OrenTask OrenTask;
typedef struct _OrenThreadPool OrenThreadPool;
typedef struct _OrenMd5 OrenMd5;

G_END_DECLS

#endif /* __OREN_NC_TYPES_H__ */
