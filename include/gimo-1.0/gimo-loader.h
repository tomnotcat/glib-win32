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
#ifndef __GIMO_LOADER_H__
#define __GIMO_LOADER_H__

#include "gimo-types.h"

G_BEGIN_DECLS

#define GIMO_TYPE_LOADER (gimo_loader_get_type())
#define GIMO_LOADER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GIMO_TYPE_LOADER, GimoLoader))
#define GIMO_IS_LOADER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIMO_TYPE_LOADER))
#define GIMO_LOADER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GIMO_TYPE_LOADER, GimoLoaderClass))
#define GIMO_IS_LOADER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GIMO_TYPE_LOADER))
#define GIMO_LOADER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GIMO_TYPE_LOADER, GimoLoaderClass))

typedef struct _GimoLoaderPrivate GimoLoaderPrivate;
typedef struct _GimoLoaderClass GimoLoaderClass;

struct _GimoLoader {
    GObject parent_instance;
    GimoLoaderPrivate *priv;
};

struct _GimoLoaderClass {
    GObjectClass parent_class;
};

GType gimo_loader_get_type (void) G_GNUC_CONST;

GimoLoader* gimo_loader_new (void);

GimoLoader* gimo_loader_new_cached (void);

void gimo_loader_add_paths (GimoLoader *self,
                            const gchar *paths);

void gimo_loader_remove_paths (GimoLoader *self,
                               const gchar *paths);

GPtrArray* gimo_loader_dup_paths (GimoLoader *self);

gboolean gimo_loader_register (GimoLoader *self,
                               const gchar *suffix,
                               GimoFactory *factory);

void gimo_loader_unregister (GimoLoader *self,
                             const gchar *suffix);

GimoLoadable* gimo_loader_load (GimoLoader *self,
                                const gchar *file_name);

GPtrArray* gimo_loader_query_cached (GimoLoader *self);

G_END_DECLS

#endif /* __GIMO_LOADER_H__ */
