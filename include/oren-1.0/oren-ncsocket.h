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
#ifndef __OREN_NC_SOCKET_H__
#define __OREN_NC_SOCKET_H__

#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_NCSOCKET (oren_ncsocket_get_type())
#define OREN_NCSOCKET(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_NCSOCKET, OrenNCSocket))
#define OREN_IS_NCSOCKET(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_NCSOCKET))
#define OREN_NCSOCKET_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_NCSOCKET, OrenNCSocketClass))
#define OREN_IS_NCSOCKET_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_NCSOCKET))
#define OREN_NCSOCKET_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_NCSOCKET, OrenNCSocketClass))

typedef struct _OrenNCSocketPrivate OrenNCSocketPrivate;
typedef struct _OrenNCSocketClass OrenNCSocketClass;

struct _OrenNCSocket
{
    GObject parent_instance;
    OrenNCSocketPrivate *priv;
};

struct _OrenNCSocketClass
{
    GObjectClass parent_class;
};

GType oren_ncsocket_get_type (void) G_GNUC_CONST;

OrenNCSocket* oren_ncsocket_new (OrenNCSocketFamily family,
                                 OrenNCSocketType type);

OrenNCSocket* oren_ncsocket_new_from_fd (gint fd);

gint oren_ncsocket_get_fd (OrenNCSocket *self);

OrenNCSocketFamily oren_ncsocket_get_family (OrenNCSocket *self);

OrenNCSocketType oren_ncsocket_get_socket_type (OrenNCSocket *self);

OrenNCSockaddr* oren_ncsocket_get_local_address (OrenNCSocket *self) G_GNUC_WARN_UNUSED_RESULT;

gboolean oren_ncsocket_bind (OrenNCSocket *self,
                             OrenNCSockaddr *address,
                             gboolean allow_reuse);

gssize oren_ncsocket_receive_from (OrenNCSocket *self,
                                   OrenNCSockaddr **address,
                                   gpointer buffer,
                                   gsize size);

gssize oren_ncsocket_receive_packet_from (OrenNCSocket *self,
                                          OrenNCSockaddr **address,
                                          OrenNCBuffer *buffer,
                                          gsize size);

gssize oren_ncsocket_send_to (OrenNCSocket *self,
                              OrenNCSockaddr *address,
                              gconstpointer buffer,
                              gsize size);

gssize oren_ncsocket_send_packet_to (OrenNCSocket *self,
                                     OrenNCSockaddr *address,
                                     OrenNCBuffer *buffer);

G_END_DECLS

#endif /* __OREN_NC_SOCKET_H__ */
