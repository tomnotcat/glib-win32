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
#ifndef __OREN_NC_HANDLER_H__
#define __OREN_NC_HANDLER_H__

#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_NCHANDLER (oren_nchandler_get_type())
#define OREN_NCHANDLER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_NCHANDLER, OrenNCHandler))
#define OREN_IS_NCHANDLER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_NCHANDLER))
#define OREN_NCHANDLER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_NCHANDLER, OrenNCHandlerClass))
#define OREN_IS_NCHANDLER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_NCHANDLER))
#define OREN_NCHANDLER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_NCHANDLER, OrenNCHandlerClass))

typedef struct _OrenNCHandlerPrivate OrenNCHandlerPrivate;
typedef struct _OrenNCHandlerClass OrenNCHandlerClass;

struct _OrenNCHandler {
    GObject parent_instance;
    OrenNCHandlerPrivate *priv;
};

struct _OrenNCHandlerClass {
    GObjectClass parent_class;
    void (*handle_input)(OrenNCHandler *self,
                         OrenNCSocket *socket);
    void (*handle_packet) (OrenNCHandler *self,
                           OrenNCSocket *socket,
                           OrenNCSockaddr *from,
                           OrenNCBuffer *buffer);
    void (*handle_timer)(OrenNCHandler *self,
                         guint timer_id);
    void (*remove_input)(OrenNCHandler *self,
                      OrenNCSocket *socket);
    void (*remove_timer)(OrenNCHandler *self,
                         guint timer_id);
};

GType oren_nchandler_get_type (void) G_GNUC_CONST;

OrenNCHandler* oren_nchandler_new (void);

G_END_DECLS

#endif /* __OREN_NC_HANDLER_H__ */
