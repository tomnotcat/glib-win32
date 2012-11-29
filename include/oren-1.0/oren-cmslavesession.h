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
#ifndef __OREN_CM_SLAVE_SESSION_H__
#define __OREN_CM_SLAVE_SESSION_H__

#include "oren-nctypes.h"
#include "oren-cmtypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_CMSLAVE_SESSION (oren_cmslave_session_get_type())
#define OREN_CMSLAVE_SESSION(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_CMSLAVE_SESSION, OrenCMSlaveSession))
#define OREN_IS_CMSLAVE_SESSION(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_CMSLAVE_SESSION))
#define OREN_CMSLAVE_SESSION_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_CMSLAVE_SESSION, OrenCMSlaveSessionClass))
#define OREN_IS_CMSLAVE_SESSION_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_CMSLAVE_SESSION))
#define OREN_CMSLAVE_SESSION_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_CMSLAVE_SESSION, OrenCMSlaveSessionClass))

#define OREN_CMSLAVE_SESSION_HEAD_SIZE  (sizeof (guint32))
#define OREN_CMSLAVE_SESSION_LOST_LIMIT (500) /* 500/10000 */
#define OREN_CMSLAVE_SESSION_LOST_DIFF  (300) /* 300/10000 */
#define OREN_CMSLAVE_SESSION_USER_LIMIT (100)

struct timeval;

typedef struct _OrenCMSlaveWorkload OrenCMSlaveWorkload;
typedef struct _OrenCMSlaveSessionPrivate OrenCMSlaveSessionPrivate;
typedef struct _OrenCMSlaveSessionClass OrenCMSlaveSessionClass;

struct _OrenCMSlaveWorkload {
    guint32 user_count;
    guint32 byte_rate;
    guint32 lost_rate;
};

struct _OrenCMSlaveSession {
    GObject parent_instance;
    OrenCMSlaveSessionPrivate *priv;
};

struct _OrenCMSlaveSessionClass {
    GObjectClass parent_class;
    void (*add_channel) (OrenCMServer *self, const gchar *channel);
    void (*remove_channel) (OrenCMServer *self, const gchar *channel);
    void (*update_workload) (OrenCMServer *self, OrenCMSlaveWorkload *info);
};

GType oren_cmslave_session_get_type (void) G_GNUC_CONST;

OrenCMSlaveSession* oren_cmslave_session_new (guint8 protocol_version,
                                              const gchar *client_version,
                                              guint32 slave_id,
                                              guint32 login_code,
                                              const gchar *slave_name,
                                              const gchar *slave_network,
                                              guint32 max_channel_count,
                                              guint32 max_user_count,
                                              OrenCMServer *server,
                                              OrenNCSocket *socket,
                                              OrenNCSockaddr *address,
                                              GPtrArray *service_addr,
                                              guint16 service_port,
                                              OrenNCBuffer *buffer);

gboolean oren_cmslave_session_send (OrenCMSlaveSession *self,
                                    OrenNCBuffer *buffer);

gboolean oren_cmslave_session_recv (OrenCMSlaveSession *self,
                                    OrenNCSockaddr *from,
                                    OrenNCBuffer *buffer);

OrenCMServer* oren_cmslave_session_get_server (OrenCMSlaveSession *self);

guint32 oren_cmslave_session_get_id (OrenCMSlaveSession *self);

guint32 oren_cmslave_session_get_login_code (OrenCMSlaveSession *self);

guint8 oren_cmslave_session_protocol_version (OrenCMSlaveSession *self);

const gchar* oren_cmslave_session_client_version (OrenCMSlaveSession *self);

const gchar* oren_cmslave_session_get_name (OrenCMSlaveSession *self);

const gchar* oren_cmslave_session_get_network (OrenCMSlaveSession *self);

OrenNCSockaddr* oren_cmslave_session_get_address (OrenCMSlaveSession *self);

GPtrArray* oren_cmslave_session_get_service_addr (OrenCMSlaveSession *self);

guint16 oren_cmslave_session_get_service_port (OrenCMSlaveSession *self);

gboolean oren_cmslave_session_has_channel (OrenCMSlaveSession *self,
                                           const gchar *channel_name,
                                           gboolean active);

gboolean oren_cmslave_session_req_channel (OrenCMSlaveSession *self,
                                           const gchar *channel_name);

void oren_cmslave_session_get_workload (OrenCMSlaveSession *self,
                                        OrenCMSlaveWorkload *info);

gboolean oren_cmslave_session_work (OrenCMSlaveSession *self,
                                    struct timeval *tv,
                                    guint timeout);

G_END_DECLS

#endif /* __OREN_CM_SLAVE_SESSION_H__ */
