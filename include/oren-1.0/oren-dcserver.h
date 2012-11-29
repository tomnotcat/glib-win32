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
#ifndef __OREN_DC_SERVER_H__
#define __OREN_DC_SERVER_H__

#include "oren-nchandler.h"
#include "oren-dctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_DCSERVER (oren_dcserver_get_type())
#define OREN_DCSERVER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_DCSERVER, OrenDCServer))
#define OREN_IS_DCSERVER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_DCSERVER))
#define OREN_DCSERVER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_DCSERVER, OrenDCServerClass))
#define OREN_IS_DCSERVER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_DCSERVER))
#define OREN_DCSERVER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_DCSERVER, OrenDCServerClass))

typedef struct _OrenDCServerPrivate OrenDCServerPrivate;
typedef struct _OrenDCServerClass OrenDCServerClass;

struct _OrenDCServer {
    OrenNCHandler parent_instance;
    OrenDCServerPrivate *priv;
};

struct _OrenDCServerClass {
    OrenNCHandlerClass parent_class;
    void (*recv_ping) (OrenDCServer *self, OrenNCSockaddr *from);
    void (*add_channel) (OrenDCServer *self, OrenDCChannel *channel);
    void (*remove_channel) (OrenDCServer *self, OrenDCChannel *channel);
};

GType oren_dcserver_get_type (void) G_GNUC_CONST;

OrenDCServer* oren_dcserver_new (void);

gboolean oren_dcserver_open (OrenDCServer *self,
                             OrenNCReactor *reactor,
                             GPtrArray *service_addr,
                             guint16 service_port,
                             OrenNCSockaddr *cluster_addr,
                             OrenNCSockaddr *parent_addr,
                             OrenDCFactory *factory,
                             const gchar *parent_network,
                             const gchar *server_name,
                             const gchar *server_network,
                             const gchar *signature,
                             gint max_thread_count,
                             gint max_channel_count,
                             gint max_user_count,
                             gint max_cpu_usage,
                             gint data_quality,
                             gboolean enable_p2p,
                             gboolean auto_freeze);

void oren_dcserver_close (OrenDCServer *self);

const gchar* oren_dcserver_get_name (OrenDCServer *self);

const gchar* oren_dcserver_get_network (OrenDCServer *self);

const gchar* oren_dcserver_parent_network (OrenDCServer *self);

const gchar* oren_dcserver_get_signature (OrenDCServer *self);

OrenDCFactory* oren_dcserver_get_factory (OrenDCServer *self);

gint oren_dcserver_get_data_quality (OrenDCServer *self);

gint oren_dcserver_get_channel_count (OrenDCServer *self);

OrenDCChannel* oren_dcserver_get_channel (OrenDCServer *self,
                                          const gchar *channel_name,
                                          gboolean create_it);

void _oren_dcserver_send_login_result (OrenDCServer *self,
                                       guint8 version,
                                       guint8 is_alone,
                                       OrenNCBuffer *buffer,
                                       guint32 user_id,
                                       guint32 login_code,
                                       OrenNCSockaddr *user_addr,
                                       guint16 channel_port,
                                       OrenDCLoginResult result);

G_END_DECLS

#endif /* __OREN_DC_SERVER_H__ */
