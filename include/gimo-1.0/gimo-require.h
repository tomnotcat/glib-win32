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
#ifndef __GIMO_REQUIRE_H__
#define __GIMO_REQUIRE_H__

#include "gimo-types.h"

G_BEGIN_DECLS

#define GIMO_TYPE_REQUIRE (gimo_require_get_type())
#define GIMO_REQUIRE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GIMO_TYPE_REQUIRE, GimoRequire))
#define GIMO_IS_REQUIRE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIMO_TYPE_REQUIRE))
#define GIMO_REQUIRE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GIMO_TYPE_REQUIRE, GimoRequireClass))
#define GIMO_IS_REQUIRE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GIMO_TYPE_REQUIRE))
#define GIMO_REQUIRE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GIMO_TYPE_REQUIRE, GimoRequireClass))

typedef struct _GimoRequirePrivate GimoRequirePrivate;
typedef struct _GimoRequireClass GimoRequireClass;

struct _GimoRequire {
    GObject parent_instance;
    GimoRequirePrivate *priv;
};

struct _GimoRequireClass {
    GObjectClass parent_class;
};

GType gimo_require_get_type (void) G_GNUC_CONST;

GimoRequire* gimo_require_new (const gchar *plugin,
                               const gchar *version,
                               gboolean optional);

const gchar* gimo_require_get_plugin_id (GimoRequire *self);

const gchar* gimo_require_get_version (GimoRequire *self);

gboolean gimo_require_is_optional (GimoRequire *self);

G_END_DECLS

#endif /* __GIMO_REQUIRE_H__ */
