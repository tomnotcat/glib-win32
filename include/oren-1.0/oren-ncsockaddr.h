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
#ifndef __OREN_NC_SOCKADDR_H__
#define __OREN_NC_SOCKADDR_H__

#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_NCSOCKADDR (oren_ncsockaddr_get_type())
#define OREN_NCSOCKADDR(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_NCSOCKADDR, OrenNCSockaddr))
#define OREN_IS_NCSOCKADDR(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_NCSOCKADDR))
#define OREN_NCSOCKADDR_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_NCSOCKADDR, OrenNCSockaddrClass))
#define OREN_IS_NCSOCKADDR_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_NCSOCKADDR))
#define OREN_NCSOCKADDR_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_NCSOCKADDR, OrenNCSockaddrClass))

typedef struct _OrenNCSockaddrClass OrenNCSockaddrClass;

struct _OrenNCSockaddr
{
    GObject parent_instance;
};

struct _OrenNCSockaddrClass
{
    GObjectClass parent_class;
    OrenNCSocketFamily  (*get_family) (OrenNCSockaddr *self);
    gboolean (*to_native) (OrenNCSockaddr *self,
                           gpointer dest,
                           gsize destlen);
    gsize (*get_native_size) (OrenNCSockaddr *self);
    gboolean (*equal) (OrenNCSockaddr *self,
                       OrenNCSockaddr *other);
};

GType oren_ncsockaddr_get_type (void) G_GNUC_CONST;

OrenNCSockaddr* oren_ncsockaddr_new_from_native (gconstpointer native,
                                                 gsize len);

OrenNCSocketFamily oren_ncsockaddr_get_family (OrenNCSockaddr *self);

gboolean oren_ncsockaddr_to_native (OrenNCSockaddr *self,
                                    gpointer dest,
                                    gsize destlen);

gsize oren_ncsockaddr_get_native_size (OrenNCSockaddr *self);

gboolean oren_ncsockaddr_equal (OrenNCSockaddr *self,
                                OrenNCSockaddr *other);

G_END_DECLS

#endif /* __OREN_NC_SOCKADDR_H__ */
