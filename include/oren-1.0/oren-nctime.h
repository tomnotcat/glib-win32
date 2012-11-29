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
#ifndef __OREN_NC_TIME_H__
#define __OREN_NC_TIME_H__

#include "oren-nctypes.h"

#ifdef G_OS_WIN32
# include <WinSock2.h>
int gettimeofday (struct timeval *tv,
                  struct timezone *tz);

void timeradd (const struct timeval *a,
               const struct timeval *b,
               struct timeval *res);

void timersub (const struct timeval *a,
               const struct timeval *b,
               struct timeval *res);
#else
# include <sys/time.h>
#endif

#endif /* __OREN_NC_TIME_H__ */
