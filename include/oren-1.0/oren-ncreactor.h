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
#ifndef __OREN_NC_REACTOR_H__
#define __OREN_NC_REACTOR_H__

#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_NCREACTOR (oren_ncreactor_get_type())
#define OREN_NCREACTOR(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_NCREACTOR, OrenNCReactor))
#define OREN_IS_NCREACTOR(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_NCREACTOR))
#define OREN_NCREACTOR_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_NCREACTOR, OrenNCReactorClass))
#define OREN_IS_NCREACTOR_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_NCREACTOR))
#define OREN_NCREACTOR_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_NCREACTOR, OrenNCReactorClass))

#define OREN_NCREACTOR_DEFAULT_QUEUE_SIZE (256)

typedef struct _OrenNCReactorPrivate OrenNCReactorPrivate;
typedef struct _OrenNCReactorClass OrenNCReactorClass;

struct _OrenNCReactor {
    GObject parent_instance;
    OrenNCReactorPrivate *priv;
};

struct _OrenNCReactorClass {
    GObjectClass parent_class;
};

GType oren_ncreactor_get_type (void) G_GNUC_CONST;

OrenNCReactor* oren_ncreactor_new (void);

void oren_ncreactor_set_queue_size (OrenNCReactor *self,
                                    guint size);

gboolean oren_ncreactor_register_handler (OrenNCReactor *self,
                                          OrenNCSocket *socket,
                                          OrenNCHandler *handler);

guint oren_ncreactor_handler_count (OrenNCReactor *self);

guint oren_ncreactor_schedule_timer (OrenNCReactor *self,
                                     guint microseconds,
                                     OrenNCHandler *handler);

void oren_ncreactor_remove_handler (OrenNCReactor *self,
                                    OrenNCHandler *handler,
                                    OrenNCSocket *socket);

void oren_ncreactor_cancel_timer (OrenNCReactor *self,
                                  guint timer_id);

void oren_ncreactor_once (OrenNCReactor *self,
                          void (*callback) (gpointer),
                          gpointer user_data,
                          guint timeout);

void oren_ncreactor_sync (OrenNCReactor *self);

void oren_ncreactor_run_loop (OrenNCReactor *self,
                              gboolean new_thread);

void oren_ncreactor_end_loop (OrenNCReactor *self);

G_END_DECLS

#endif /* __OREN_NC_REACTOR_H__ */
