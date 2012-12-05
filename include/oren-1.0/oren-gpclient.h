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
#ifndef __OREN_GP_CLIENT_H__
#define __OREN_GP_CLIENT_H__

#include "oren-dcclient.h"
#include "oren-gptypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_GPCLIENT (oren_gpclient_get_type())
#define OREN_GPCLIENT(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_GPCLIENT, OrenGPClient))
#define OREN_IS_GPCLIENT(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_GPCLIENT))
#define OREN_GPCLIENT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_GPCLIENT, OrenGPClientClass))
#define OREN_IS_GPCLIENT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_GPCLIENT))
#define OREN_GPCLIENT_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_GPCLIENT, OrenGPClientClass))

typedef struct _OrenGPClientPrivate OrenGPClientPrivate;
typedef struct _OrenGPClientClass OrenGPClientClass;

struct _OrenGPClient {
    OrenDCClient parent_instance;
    OrenGPClientPrivate *priv;
};

struct _OrenGPClientClass {
    OrenDCClientClass parent_class;
    void (*protocol) (OrenGPClient *self, OrenNCBuffer *buffer);
};

GType oren_gpclient_get_type (void) G_GNUC_CONST;

OrenGPClient* oren_gpclient_new (void);

gboolean oren_gpclient_open (OrenGPClient *self,
                             OrenNCReactor *reactor,
                             const gchar *signature,
                             gboolean statistics,
                             GimoContext *context,
                             const gchar *extpoint,
                             OrenGPDispatch *disp);

OrenGPDispatch* oren_gpclient_get_dispatch (OrenGPClient *self);

OrenNCBuffer* oren_gpclient_make_protocol (OrenGPClient *self,
                                           const gchar *message);

void oren_gpclient_send_protocol (OrenGPClient *self,
                                  OrenNCBuffer *buffer);

void oren_gpclient_set_privilege (OrenGPClient *self,
                                  OrenGPUserPrivilege privilege);

OrenGPUserPrivilege oren_gpclient_get_privilege (OrenGPClient *self);

gboolean oren_gpclient_has_privilege (OrenGPClient *self,
                                      OrenGPUserPrivilege privilege);

G_END_DECLS

#endif /* __OREN_GP_CLIENT_H__ */
