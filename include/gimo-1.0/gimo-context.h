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
#ifndef __GIMO_CONTEXT_H__
#define __GIMO_CONTEXT_H__

#include "gimo-types.h"
#include <gio/gio.h>

G_BEGIN_DECLS

#define GIMO_TYPE_CONTEXT (gimo_context_get_type())
#define GIMO_CONTEXT(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GIMO_TYPE_CONTEXT, GimoContext))
#define GIMO_IS_CONTEXT(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIMO_TYPE_CONTEXT))
#define GIMO_CONTEXT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GIMO_TYPE_CONTEXT, GimoContextClass))
#define GIMO_IS_CONTEXT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GIMO_TYPE_CONTEXT))
#define GIMO_CONTEXT_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GIMO_TYPE_CONTEXT, GimoContextClass))

typedef struct _GimoContextPrivate GimoContextPrivate;
typedef struct _GimoContextClass GimoContextClass;

struct _GimoContext {
    GObject parent_instance;
    GimoContextPrivate *priv;
};

struct _GimoContextClass {
    GObjectClass parent_class;
    void (*state_changed) (GimoContext *self,
                           GimoPlugin *plugin,
                           GimoPluginState old_state,
                           GimoPluginState new_state);
};

GType gimo_context_get_type (void) G_GNUC_CONST;

GimoContext* gimo_context_new (void);

gboolean gimo_context_install_plugin (GimoContext *self,
                                      const gchar *path,
                                      GimoPlugin *plugin);

void gimo_context_uninstall_plugin (GimoContext *self,
                                    const gchar *plugin_id);

void gimo_context_add_paths (GimoContext *self,
                             const gchar *paths);

guint gimo_context_load_plugin (GimoContext *self,
                                const gchar *file_path,
                                GCancellable *cancellable,
                                gboolean start);

GimoPlugin* gimo_context_query_plugin (GimoContext *self,
                                       const gchar *plugin_id);

GPtrArray* gimo_context_query_plugins (GimoContext *self);

GimoExtPoint* gimo_context_query_extpoint (GimoContext *self,
                                           const gchar *extpt_id);

GPtrArray* gimo_context_query_extensions (GimoContext *self,
                                          const gchar *extpt_id);

GObject* gimo_context_resolve_extpoint (GimoContext *self,
                                        const gchar *extpt_id);

void gimo_context_destroy (GimoContext *self);

G_END_DECLS

#endif /* __GIMO_CONTEXT_H__ */
