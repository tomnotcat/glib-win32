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
#ifndef __OREN_THREAD_POOL_H__
#define __OREN_THREAD_POOL_H__

#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_THREAD_POOL (oren_thread_pool_get_type())
#define OREN_THREAD_POOL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_THREAD_POOL, OrenThreadPool))
#define OREN_IS_THREAD_POOL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_THREAD_POOL))
#define OREN_THREAD_POOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_THREAD_POOL, OrenThreadPoolClass))
#define OREN_IS_THREAD_POOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_THREAD_POOL))
#define OREN_THREAD_POOL_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_THREAD_POOL, OrenThreadPoolClass))

typedef struct _OrenThreadPoolPrivate OrenThreadPoolPrivate;
typedef struct _OrenThreadPoolClass OrenThreadPoolClass;

struct _OrenThreadPool {
    GObject parent_instance;
    OrenThreadPoolPrivate *priv;
};

struct _OrenThreadPoolClass {
    GObjectClass parent_class;
};

GType oren_thread_pool_get_type (void) G_GNUC_CONST;

OrenThreadPool* oren_thread_pool_new (guint thread_count);

void oren_thread_pool_set_queue_size (OrenThreadPool *self,
                                      guint size);

gboolean oren_thread_pool_add_task (OrenThreadPool *self,
                                    OrenTaskBase *task,
                                    gboolean important);

gboolean oren_thread_pool_remove_task (OrenThreadPool *self,
                                       OrenTaskBase *task,
                                       gboolean running);

void oren_thread_pool_remove_tasks (OrenThreadPool *self,
                                    GCompareFunc func,
                                    gconstpointer data,
                                    gboolean running);

void oren_thread_pool_clear_tasks (OrenThreadPool *self,
                                   gboolean running);

void oren_thread_pool_wait_for_finish (OrenThreadPool *self);

GQueue* oren_thread_pool_lock_tasks (OrenThreadPool *self);

void oren_thread_pool_unlock_tasks (OrenThreadPool *self);

G_END_DECLS

#endif /* __OREN_THREAD_POOL_H__ */
