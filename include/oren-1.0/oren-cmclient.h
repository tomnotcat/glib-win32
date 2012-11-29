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
#ifndef __OREN_CM_CLIENT_H__
#define __OREN_CM_CLIENT_H__

#include "oren-nchandler.h"
#include "oren-cmtypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_CMCLIENT (oren_cmclient_get_type())
#define OREN_CMCLIENT(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_CMCLIENT, OrenCMClient))
#define OREN_IS_CMCLIENT(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_CMCLIENT))
#define OREN_CMCLIENT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_CMCLIENT, OrenCMClientClass))
#define OREN_IS_CMCLIENT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_CMCLIENT))
#define OREN_CMCLIENT_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_CMCLIENT, OrenCMClientClass))

typedef struct _OrenCMClientPrivate OrenCMClientPrivate;
typedef struct _OrenCMClientClass OrenCMClientClass;

struct _OrenCMClient {
    OrenNCHandler parent_instance;
    OrenCMClientPrivate *priv;
};

struct _OrenCMClientClass {
    OrenNCHandlerClass parent_class;
    void (*time) (OrenCMClient *self, guint tv_sec, guint tv_usec);
    void (*groups) (OrenCMClient *self, GSList *groups);
    void (*slaves) (OrenCMClient *self, GSList *slaves);
    void (*fail) (OrenCMClient *self, OrenCMRequestResult result);
};

GType oren_cmclient_get_type (void) G_GNUC_CONST;

OrenCMClient* oren_cmclient_new (void);

gboolean oren_cmclient_open (OrenCMClient *self,
                             OrenNCReactor *reactor,
                             const gchar *signature,
                             guint timeout);

void oren_cmclient_close (OrenCMClient *self);

void oren_cmclient_set_svraddr (OrenCMClient *self,
                                OrenNCSockaddr *addr);

OrenNCSockaddr* oren_cmclient_get_svraddr (OrenCMClient *self);

void oren_cmclient_request_time (OrenCMClient *self);

void oren_cmclient_request_groups (OrenCMClient *self);

void oren_cmclient_request_slaves (OrenCMClient *self,
                                   const gchar *group,
                                   const gchar *channel,
                                   guint16 count);

G_END_DECLS

#endif /* __OREN_CM_CLIENT_H__ */
