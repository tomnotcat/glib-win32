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
#ifndef __OREN_NC_ENUMTYPES_H__
#define __OREN_NC_ENUMTYPES_H__

#include <glib-object.h>

G_BEGIN_DECLS

/* enumerations from "oren-ncenums.h" */
GType oren_ncsocket_family_get_type (void) G_GNUC_CONST;
#define OREN_TYPE_NCSOCKET_FAMILY (oren_ncsocket_family_get_type ())

GType oren_ncsocket_type_get_type (void) G_GNUC_CONST;
#define OREN_TYPE_NCSOCKET_TYPE (oren_ncsocket_type_get_type ())

GType oren_ncbuffer_lock_get_type (void) G_GNUC_CONST;
#define OREN_TYPE_NCBUFFER_LOCK (oren_ncbuffer_lock_get_type ())

GType oren_nconline_state_get_type (void) G_GNUC_CONST;
#define OREN_TYPE_NCONLINE_STATE (oren_nconline_state_get_type ())

GType oren_ncsession_work_get_type (void) G_GNUC_CONST;
#define OREN_TYPE_NCSESSION_WORK (oren_ncsession_work_get_type ())

GType oren_ncsession_full_get_type (void) G_GNUC_CONST;
#define OREN_TYPE_NCSESSION_FULL (oren_ncsession_full_get_type ())

G_END_DECLS

#endif /* __OREN_NC_ENUMTYPES_H__ */
