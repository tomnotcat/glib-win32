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
#ifndef __OREN_CM_SLAVE_H__
#define __OREN_CM_SLAVE_H__

#include "oren-nchandler.h"
#include "oren-cmtypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_CMSLAVE (oren_cmslave_get_type())
#define OREN_CMSLAVE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_CMSLAVE, OrenCMSlave))
#define OREN_IS_CMSLAVE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_CMSLAVE))
#define OREN_CMSLAVE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_CMSLAVE, OrenCMSlaveClass))
#define OREN_IS_CMSLAVE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_CMSLAVE))
#define OREN_CMSLAVE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_CMSLAVE, OrenCMSlaveClass))

#define OREN_CMSLAVE_HEAD_SIZE (sizeof (guint32))

typedef struct _OrenCMSlavePrivate OrenCMSlavePrivate;
typedef struct _OrenCMSlaveClass OrenCMSlaveClass;

struct _OrenCMSlave {
    OrenNCHandler parent_instance;
    OrenCMSlavePrivate *priv;
};

struct _OrenCMSlaveClass {
    OrenNCHandlerClass parent_class;
    void (*login) (OrenCMSlave *self, OrenCMLoginResult result);
    void (*logout) (OrenCMSlave *self, OrenCMLogoutReason reason);
};

GType oren_cmslave_get_type (void) G_GNUC_CONST;

OrenCMSlave* oren_cmslave_new (void);

gboolean oren_cmslave_open (OrenCMSlave *self,
                            OrenNCReactor *reactor,
                            GPtrArray *service_addr,
                            guint16 service_port,
                            const gchar *signature,
                            gint max_channel_count,
                            gint max_user_count);

void oren_cmslave_close (OrenCMSlave *self);

void oren_cmslave_login (OrenCMSlave *self,
                         OrenNCSockaddr *address,
                         const gchar *slave_name,
                         const gchar *slave_network);

void oren_cmslave_logout (OrenCMSlave *self);

void oren_cmslave_add_channels (OrenCMSlave *self,
                                GSList *channels);

void oren_cmslave_remove_channels (OrenCMSlave *self,
                                   GSList *channels);

void oren_cmslave_update_workload (OrenCMSlave *self,
                                   guint32 user_count,
                                   guint32 byte_rate,
                                   guint32 lost_rate);

OrenNCOnlineState oren_cmslave_get_state (OrenCMSlave *self);

const gchar* oren_cmslave_server_version (OrenCMSlave *self);

const gchar* oren_cmslave_get_name (OrenCMSlave *self);

const gchar* oren_cmslave_get_network (OrenCMSlave *self);

GSList* _oren_cmslave_create_channels_list (gboolean dup, ...);

G_END_DECLS

#endif /* __OREN_CM_SLAVE_H__ */
