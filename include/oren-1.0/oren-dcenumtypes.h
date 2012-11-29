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
#ifndef __OREN_DC_ENUMTYPES_H__
#define __OREN_DC_ENUMTYPES_H__

#include "oren-dcenums.h"

G_BEGIN_DECLS

/* Enumerations from "oren-dcenums.h" */
GType oren_dclogin_result_get_type (void) G_GNUC_CONST;
#define OREN_TYPE_DCLOGIN_RESULT (oren_dclogin_result_get_type ())

GType oren_dclogout_reason_get_type (void) G_GNUC_CONST;
#define OREN_TYPE_DCLOGOUT_REASON (oren_dclogout_reason_get_type ())

GType oren_dcsession_id_get_type (void) G_GNUC_CONST;
#define OREN_TYPE_DCSESSION_ID (oren_dcsession_id_get_type ())

const gchar* oren_dclogin_result_to_string (OrenDCLoginResult result);

const gchar* oren_dclogout_reason_to_string (OrenDCLogoutReason reason);

G_END_DECLS

#endif /* __OREN_DC_ENUMTYPES_H__ */
