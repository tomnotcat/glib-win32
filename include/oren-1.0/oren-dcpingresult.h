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
#ifndef __OREN_DC_PING_RESULT_H__
#define __OREN_DC_PING_RESULT_H__

#include "oren-cmtypes.h"
#include "oren-nctypes.h"
#include "oren-dctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_DCPING_RESULT (oren_dcping_result_get_type())
#define OREN_DCPING_RESULT(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_DCPING_RESULT, OrenDCPingResult))
#define OREN_IS_DCPING_RESULT(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_DCPING_RESULT))
#define OREN_DCPING_RESULT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_DCPING_RESULT, OrenDCPingResultClass))
#define OREN_IS_DCPING_RESULT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_DCPING_RESULT))
#define OREN_DCPING_RESULT_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_DCPING_RESULT, OrenDCPingResultClass))

typedef struct _OrenDCPingResultPrivate OrenDCPingResultPrivate;
typedef struct _OrenDCPingResultClass OrenDCPingResultClass;

struct _OrenDCPingResult {
    GObject parent_instance;
    OrenDCPingResultPrivate *priv;
};

struct _OrenDCPingResultClass {
    GObjectClass parent_class;
};

GType oren_dcping_result_get_type (void) G_GNUC_CONST;

OrenDCPingResult* oren_dcping_result_new (OrenCMItem *item,
                                          gint rtt,
                                          gint user_count);

OrenCMItem* oren_dcping_result_get_item (OrenDCPingResult *self);

gint oren_dcping_result_get_rtt (OrenDCPingResult *self);

gint oren_dcping_result_get_user_count (OrenDCPingResult *self);

gboolean oren_dcping_result_is_timeout (OrenDCPingResult *self);

G_END_DECLS

#endif /* __OREN_DC_PING_RESULT_H__ */
