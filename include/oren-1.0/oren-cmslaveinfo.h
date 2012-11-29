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
#ifndef __OREN_CM_SLAVE_INFO_H__
#define __OREN_CM_SLAVE_INFO_H__

#include "oren-nctypes.h"
#include "oren-cmtypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_CMSLAVE_INFO (oren_cmslave_info_get_type())
#define OREN_CMSLAVE_INFO(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_CMSLAVE_INFO, OrenCMSlaveInfo))
#define OREN_IS_CMSLAVE_INFO(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_CMSLAVE_INFO))
#define OREN_CMSLAVE_INFO_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_CMSLAVE_INFO, OrenCMSlaveInfoClass))
#define OREN_IS_CMSLAVE_INFO_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_CMSLAVE_INFO))
#define OREN_CMSLAVE_INFO_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_CMSLAVE_INFO, OrenCMSlaveInfoClass))

typedef struct _OrenCMSlaveInfoPrivate OrenCMSlaveInfoPrivate;
typedef struct _OrenCMSlaveInfoClass OrenCMSlaveInfoClass;

struct _OrenCMSlaveInfo {
    GObject parent_instance;
    OrenCMSlaveInfoPrivate *priv;
};

struct _OrenCMSlaveInfoClass {
    GObjectClass parent_class;
};

GType oren_cmslave_info_get_type (void) G_GNUC_CONST;

OrenCMSlaveInfo* oren_cmslave_info_new (const gchar *slave_name,
                                        const gchar *slave_group,
                                        const gchar *channel,
                                        GPtrArray *service_addr,
                                        guint16 service_port);

const gchar* oren_cmslave_info_get_name (OrenCMSlaveInfo *self);

const gchar* oren_cmslave_info_get_group (OrenCMSlaveInfo *self);

const gchar* oren_cmslave_info_get_channel (OrenCMSlaveInfo *self);

GPtrArray* oren_cmslave_info_get_service_addr (OrenCMSlaveInfo *self);

guint16 oren_cmslave_info_get_service_port (OrenCMSlaveInfo *self);

void oren_cmslave_info_set_address (OrenCMSlaveInfo *self,
                                    OrenNCSockaddr *address);

OrenNCSockaddr* oren_cmslave_info_get_address (OrenCMSlaveInfo *self);

G_END_DECLS

#endif /* __OREN_CM_SLAVE_INFO_H__ */
