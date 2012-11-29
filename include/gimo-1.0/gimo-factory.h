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
#ifndef __GIMO_FACTORY_H__
#define __GIMO_FACTORY_H__

#include "gimo-types.h"

G_BEGIN_DECLS

#define GIMO_TYPE_FACTORY (gimo_factory_get_type())
#define GIMO_FACTORY(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GIMO_TYPE_FACTORY, GimoFactory))
#define GIMO_IS_FACTORY(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIMO_TYPE_FACTORY))
#define GIMO_FACTORY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GIMO_TYPE_FACTORY, GimoFactoryClass))
#define GIMO_IS_FACTORY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GIMO_TYPE_FACTORY))
#define GIMO_FACTORY_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GIMO_TYPE_FACTORY, GimoFactoryClass))

typedef struct _GimoFactoryPrivate GimoFactoryPrivate;
typedef struct _GimoFactoryClass GimoFactoryClass;

struct _GimoFactory {
    GObject parent_instance;
    GimoFactoryPrivate *priv;
};

struct _GimoFactoryClass {
    GObjectClass parent_class;
    GObject* (*make) (GimoFactory *self);
};

/**
 * GimoFactoryFunc:
 * @user_data: (closure): user data to pass to the function
 *
 * Factory function used for make object.
 *
 * Returns: (allow-none) (transfer full): A #GObject if
 *          successful, %NULL on error. Free the returned
 *          object with g_object_unref().
 */
typedef GObject* (*GimoFactoryFunc) (gpointer user_data);

GType gimo_factory_get_type (void) G_GNUC_CONST;

GimoFactory* gimo_factory_new (GimoFactoryFunc func,
                               gpointer user_data);

GObject* gimo_factory_make (GimoFactory *self);

G_END_DECLS

#endif /* __GIMO_FACTORY_H__ */
