/* GIMO - A plugin framework based on GObject.
 *
 * Copyright (C) 2012 TinySoft, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifndef __GIMO_LOADABLE_H__
#define __GIMO_LOADABLE_H__

#include "gimo-types.h"

G_BEGIN_DECLS

#define GIMO_TYPE_LOADABLE (gimo_loadable_get_type())
#define GIMO_LOADABLE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GIMO_TYPE_LOADABLE, GimoLoadable))
#define GIMO_IS_LOADABLE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIMO_TYPE_LOADABLE))
#define GIMO_LOADABLE_GET_IFACE(inst) \
    (G_TYPE_INSTANCE_GET_INTERFACE ((inst), GIMO_TYPE_LOADABLE, GimoLoadableInterface))

typedef struct _GimoLoadableInterface GimoLoadableInterface;

typedef gboolean (*GimoLoadableLoadFunc) (GimoLoadable *self,
                                          const gchar *file_name);
typedef gboolean (*GimoLoadableUnloadFunc) (GimoLoadable *self);

struct _GimoLoadableInterface {
    GTypeInterface base_iface;
    gboolean (*load) (GimoLoadable *self,
                      const gchar *file_name);
    gboolean (*unload) (GimoLoadable *self);
};

GType gimo_loadable_get_type (void) G_GNUC_CONST;

gboolean gimo_loadable_load (GimoLoadable *self,
                             const gchar *file_name);

gboolean gimo_loadable_unload (GimoLoadable *self);

G_END_DECLS

#endif /* __GIMO_LOADABLE_H__ */
