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
#ifndef __OREN_BYTEORDER_H__
#define __OREN_BYTEORDER_H__

#include "oren-endian.h"

G_BEGIN_DECLS

#define oren_read_le16(_ptr) \
    oren_swap_le16 (*(guint16*) (_ptr))

#define oren_read_be16(_ptr) \
    oren_swap_be16 (*(guint16*) (_ptr))

#define oren_read_le32(_ptr) \
    oren_swap_le32 (*(guint32*) (_ptr))

#define oren_read_be32(_ptr) \
    oren_swap_be32 (*(guint32*) (_ptr))

#define oren_read_le64(_ptr) \
    oren_swap_le64 (*(guint64*) (_ptr))

#define oren_read_be64(_ptr) \
    oren_swap_be64 (*(guint64*) (_ptr))

#define oren_write_le16(_ptr, _val) \
    do { \
        *(guint16*) (_ptr) = oren_swap_le16 (_val); \
    } while (0)

#define oren_write_be16(_ptr, _val) \
    do { \
        *(guint16*) (_ptr) = oren_swap_be16 (_val); \
    } while (0)

#define oren_write_le32(_ptr, _val) \
    do { \
        *(guint32*) (_ptr) = oren_swap_le32 (_val); \
    } while (0)

#define oren_write_be32(_ptr, _val) \
    do { \
        *(guint32*) (_ptr) = oren_swap_be32 (_val); \
    } while (0)

#define oren_write_le64(_ptr, _val) \
    do { \
        *(guint64*) (_ptr) = oren_swap_le64 (_val); \
    } while (0)

#define oren_write_be64(_ptr, _val) \
    do { \
        *(guint64*) (_ptr) = oren_swap_be64 (_val); \
    } while (0)

G_END_DECLS

#endif /* __OREN_BYTEORDER_H__ */
