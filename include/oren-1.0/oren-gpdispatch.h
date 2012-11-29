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
#ifndef __OREN_GP_DISPATCH_H__
#define __OREN_GP_DISPATCH_H__

#include "oren-gptypes.h"
#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_GPDISPATCH (oren_gpdispatch_get_type())
#define OREN_GPDISPATCH(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_GPDISPATCH, OrenGPDispatch))
#define OREN_IS_GPDISPATCH(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_GPDISPATCH))
#define OREN_GPDISPATCH_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_GPDISPATCH, OrenGPDispatchClass))
#define OREN_IS_GPDISPATCH_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_GPDISPATCH))
#define OREN_GPDISPATCH_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_GPDISPATCH, OrenGPDispatchClass))

typedef struct _OrenGPDispatchPrivate OrenGPDispatchPrivate;
typedef struct _OrenGPDispatchClass OrenGPDispatchClass;

struct _OrenGPDispatch {
    GObject parent_instance;
    OrenGPDispatchPrivate *priv;
};

struct _OrenGPDispatchClass {
    GObjectClass parent_class;
    void (*request) (OrenGPDispatch *self, const gchar *message);
    void (*queued) (OrenGPDispatch *self);
};

GType oren_gpdispatch_get_type (void) G_GNUC_CONST;

OrenGPDispatch* oren_gpdispatch_new (gboolean queued);

void oren_gpdispatch_dispatch (OrenGPDispatch *self,
                               GObject *sender,
                               OrenNCBuffer *buffer);

gboolean oren_gpdispatch_handle_queued (OrenGPDispatch *self);

gboolean oren_gpdispatch_register (OrenGPDispatch *self,
                                   const gchar *message,
                                   guint message_id,
                                   OrenGPHandler *handler);

void oren_gpdispatch_unregister (OrenGPDispatch *self,
                                 const gchar *message,
                                 OrenGPHandler *handler);

G_END_DECLS

#endif /* __OREN_GP_DISPATCH_H__ */
