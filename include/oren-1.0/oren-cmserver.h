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
#ifndef __OREN_CM_SERVER_H__
#define __OREN_CM_SERVER_H__

#include "oren-nchandler.h"
#include "oren-cmtypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_CMSERVER (oren_cmserver_get_type())
#define OREN_CMSERVER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_CMSERVER, OrenCMServer))
#define OREN_IS_CMSERVER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_CMSERVER))
#define OREN_CMSERVER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_CMSERVER, OrenCMServerClass))
#define OREN_IS_CMSERVER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_CMSERVER))
#define OREN_CMSERVER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_CMSERVER, OrenCMServerClass))

typedef struct _OrenCMServerPrivate OrenCMServerPrivate;
typedef struct _OrenCMServerClass OrenCMServerClass;

struct _OrenCMServer {
    OrenNCHandler parent_instance;
    OrenCMServerPrivate *priv;
};

struct _OrenCMServerClass {
    OrenNCHandlerClass parent_class;
    void (*add_slave) (OrenCMServer *self, OrenCMNode *slave);
    void (*remove_slave) (OrenCMServer *self, OrenCMNode *slave);
};

GType oren_cmserver_get_type(void) G_GNUC_CONST;

OrenCMServer* oren_cmserver_new (void);

gboolean oren_cmserver_open (OrenCMServer *self,
                             const gchar *server_name,
                             gint initialize_time,
                             OrenNCReactor *reactor,
                             OrenNCSockaddr *slave_addr,
                             OrenNCSockaddr *client_addr,
                             gint max_req_slaves,
                             const gchar *signature,
                             gboolean fixed_schedule);

void oren_cmserver_close (OrenCMServer *self);

const gchar* oren_cmserver_get_name (OrenCMServer *self);

gint oren_cmserver_slave_count (OrenCMServer *self);

OrenCMNode* oren_cmserver_lookup_slave (OrenCMServer *self,
                                        const gchar *slave_name);

void _oren_cmserver_remove_slave (OrenCMServer *self,
                                  OrenCMNode *slave);

void _oren_cmserver_add_channel (OrenCMServer *self,
                                 const gchar *channel_name,
                                 OrenCMNode *slave);

void _oren_cmserver_remove_channel (OrenCMServer *self,
                                    const gchar *channel_name,
                                    OrenCMNode *slave);

void _oren_cmserver_set_modified (OrenCMServer *self,
                                  gboolean workload);

G_END_DECLS

#endif /* __OREN_CM_SERVER_H__ */
