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
#ifndef __OREN_CM_ITEM_H__
#define __OREN_CM_ITEM_H__

#include "oren-nctypes.h"
#include "oren-cmtypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_CMITEM (oren_cmitem_get_type())
#define OREN_CMITEM(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_CMITEM, OrenCMItem))
#define OREN_IS_CMITEM(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_CMITEM))
#define OREN_CMITEM_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_CMITEM, OrenCMItemClass))
#define OREN_IS_CMITEM_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_CMITEM))
#define OREN_CMITEM_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_CMITEM, OrenCMItemClass))

typedef struct _OrenCMItemPrivate OrenCMItemPrivate;
typedef struct _OrenCMItemClass OrenCMItemClass;

struct _OrenCMItem {
    GObject parent_instance;
    OrenCMItemPrivate *priv;
};

struct _OrenCMItemClass {
    GObjectClass parent_class;
};

GType oren_cmitem_get_type (void) G_GNUC_CONST;

OrenCMItem* oren_cmitem_new (const gchar *slave_name,
                             const gchar *slave_group,
                             const gchar *channel,
                             const gchar *place,
                             guint32 server_id,
                             GPtrArray *service_addr,
                             guint16 service_port);

const gchar* oren_cmitem_get_name (OrenCMItem *self);

const gchar* oren_cmitem_get_group (OrenCMItem *self);

const gchar* oren_cmitem_get_channel (OrenCMItem *self);

const gchar* oren_cmitem_get_place (OrenCMItem *item);

guint32 oren_cmitem_get_server_id (OrenCMItem *self);

GPtrArray* oren_cmitem_get_service_addr (OrenCMItem *self);

guint16 oren_cmitem_get_service_port (OrenCMItem *self);

void oren_cmitem_set_address (OrenCMItem *self,
                              OrenNCSockaddr *address);

OrenNCSockaddr* oren_cmitem_get_address (OrenCMItem *self);

G_END_DECLS

#endif /* __OREN_CM_ITEM_H__ */
