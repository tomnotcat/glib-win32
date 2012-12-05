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
#ifndef __OREN_CM_NODE_H__
#define __OREN_CM_NODE_H__

#include "oren-nctypes.h"
#include "oren-cmtypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_CMNODE (oren_cmnode_get_type())
#define OREN_CMNODE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_CMNODE, OrenCMNode))
#define OREN_IS_CMNODE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_CMNODE))
#define OREN_CMNODE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_CMNODE, OrenCMNodeClass))
#define OREN_IS_CMNODE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_CMNODE))
#define OREN_CMNODE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_CMNODE, OrenCMNodeClass))

#define OREN_CMNODE_HEAD_SIZE  (sizeof (guint32))
#define OREN_CMNODE_LOST_LIMIT (500) /* 500/10000 */
#define OREN_CMNODE_LOST_DIFF  (300) /* 300/10000 */
#define OREN_CMNODE_USER_LIMIT (100)

struct timeval;

typedef struct _OrenCMNodeLoad OrenCMNodeLoad;
typedef struct _OrenCMNodePrivate OrenCMNodePrivate;
typedef struct _OrenCMNodeClass OrenCMNodeClass;

struct _OrenCMNodeLoad {
    guint32 channel_count;
    guint32 user_count;
    guint32 byte_rate;
    guint32 lost_rate;
};

struct _OrenCMNode {
    GObject parent_instance;
    OrenCMNodePrivate *priv;
};

struct _OrenCMNodeClass {
    GObjectClass parent_class;
    void (*add_channel) (OrenCMServer *self, const gchar *channel);
    void (*remove_channel) (OrenCMServer *self, const gchar *channel);
    void (*update_workload) (OrenCMServer *self, OrenCMNodeLoad *load);
};

GType oren_cmnode_get_type (void) G_GNUC_CONST;

OrenCMNode* oren_cmnode_new (guint8 protocol_version,
                             const gchar *client_version,
                             guint32 slave_id,
                             guint32 login_code,
                             const gchar *slave_name,
                             const gchar *slave_group,
                             const gchar *slave_place,
                             guint32 max_channel_count,
                             guint32 max_user_count,
                             OrenCMServer *server,
                             OrenNCSocket *socket,
                             OrenNCSockaddr *address,
                             GPtrArray *service_addr,
                             guint16 service_port,
                             OrenNCBuffer *buffer);

gboolean oren_cmnode_send (OrenCMNode *self,
                           OrenNCBuffer *buffer);

gboolean oren_cmnode_recv (OrenCMNode *self,
                           OrenNCSockaddr *from,
                           OrenNCBuffer *buffer);

OrenCMServer* oren_cmnode_get_server (OrenCMNode *self);

guint32 oren_cmnode_get_id (OrenCMNode *self);

guint32 oren_cmnode_get_login_code (OrenCMNode *self);

guint8 oren_cmnode_protocol_version (OrenCMNode *self);

const gchar* oren_cmnode_client_version (OrenCMNode *self);

const gchar* oren_cmnode_get_name (OrenCMNode *self);

const gchar* oren_cmnode_get_group (OrenCMNode *self);

const gchar* oren_cmnode_get_place (OrenCMNode *self);

OrenNCSockaddr* oren_cmnode_get_address (OrenCMNode *self);

GPtrArray* oren_cmnode_get_service_addr (OrenCMNode *self);

guint16 oren_cmnode_get_service_port (OrenCMNode *self);

gboolean oren_cmnode_has_channel (OrenCMNode *self,
                                  const gchar *channel_name,
                                  gboolean active);

gboolean oren_cmnode_req_channel (OrenCMNode *self,
                                  const gchar *channel_name);

void oren_cmnode_get_workload (OrenCMNode *self,
                               OrenCMNodeLoad *load);

gboolean oren_cmnode_is_overloaded (OrenCMNode *self);

gboolean _oren_cmnode_work (OrenCMNode *self,
                            struct timeval *tv,
                            guint timeout);

G_END_DECLS

#endif /* __OREN_CM_NODE_H__ */
