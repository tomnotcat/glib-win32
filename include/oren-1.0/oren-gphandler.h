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
#ifndef __OREN_GP_HANDLER_H__
#define __OREN_GP_HANDLER_H__

#include "oren-gptypes.h"
#include "oren-nchandler.h"

G_BEGIN_DECLS

#define OREN_TYPE_GPHANDLER (oren_gphandler_get_type())
#define OREN_GPHANDLER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_GPHANDLER, OrenGPHandler))
#define OREN_IS_GPHANDLER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_GPHANDLER))
#define OREN_GPHANDLER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_GPHANDLER, OrenGPHandlerClass))
#define OREN_IS_GPHANDLER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_GPHANDLER))
#define OREN_GPHANDLER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_GPHANDLER, OrenGPHandlerClass))

typedef struct _OrenGPHandlerPrivate OrenGPHandlerPrivate;
typedef struct _OrenGPHandlerClass OrenGPHandlerClass;

struct _OrenGPHandler {
    OrenNCHandler parent_instance;
    OrenGPHandlerPrivate *priv;
};

struct _OrenGPHandlerClass {
    OrenNCHandlerClass parent_class;
    void (*handle_protocol) (OrenGPHandler *self,
                             GObject *sender,
                             const gchar *message,
                             OrenNCBuffer *buffer,
                             guint message_id);
};

GType oren_gphandler_get_type (void) G_GNUC_CONST;

OrenGPHandler* oren_gphandler_new (void);

void oren_gphandler_handle_protocol (OrenGPHandler *self,
                                     GObject *sender,
                                     const gchar *message,
                                     OrenNCBuffer *buffer,
                                     guint message_id);

G_END_DECLS

#endif /* __OREN_GP_HANDLER_H__ */
