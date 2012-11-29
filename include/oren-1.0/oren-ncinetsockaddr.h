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
#ifndef __OREN_NC_INET_SOCKADDR_H__
#define __OREN_NC_INET_SOCKADDR_H__

#include "oren-ncsockaddr.h"

G_BEGIN_DECLS

#define OREN_TYPE_NCINET_SOCKADDR (oren_ncinet_sockaddr_get_type())
#define OREN_NCINET_SOCKADDR(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_NCINET_SOCKADDR, OrenNCInetSockaddr))
#define OREN_IS_NCINET_SOCKADDR(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_NCINET_SOCKADDR))
#define OREN_NCINET_SOCKADDR_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_NCINET_SOCKADDR, OrenNCInetSockaddrClass))
#define OREN_IS_NCINET_SOCKADDR_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_NCINET_SOCKADDR))
#define OREN_NCINET_SOCKADDR_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_NCINET_SOCKADDR, OrenNCInetSockaddrClass))

typedef struct _OrenNCInetSockaddrClass OrenNCInetSockaddrClass;

struct _OrenNCInetSockaddr {
    OrenNCSockaddr parent_instance;
    OrenNCInetAddress *address;
    guint16 port;
    guint32 flowinfo;
    guint32 scope_id;
};

struct _OrenNCInetSockaddrClass {
    OrenNCSockaddrClass parent_class;
};

GType oren_ncinet_sockaddr_get_type (void) G_GNUC_CONST;

OrenNCInetSockaddr* oren_ncinet_sockaddr_new (OrenNCInetAddress *address,
                                              guint16 port);

OrenNCInetAddress* oren_ncinet_sockaddr_get_address (OrenNCInetSockaddr *self);

guint16 oren_ncinet_sockaddr_get_port (OrenNCInetSockaddr *self);

guint32 oren_ncinet_sockaddr_get_flowinfo (OrenNCInetSockaddr *self);

guint32 oren_ncinet_sockaddr_get_scope_id (OrenNCInetSockaddr *self);

G_END_DECLS

#endif /* __OREN_NC_INET_SOCKADDR_H__ */
