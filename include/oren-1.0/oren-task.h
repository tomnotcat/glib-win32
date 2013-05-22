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
#ifndef __OREN_TASK_H__
#define __OREN_TASK_H__

#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_TASK (oren_task_get_type())
#define OREN_TASK(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_TASK, OrenTask))
#define OREN_IS_TASK(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_TASK))
#define OREN_TASK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_TASK, OrenTaskClass))
#define OREN_IS_TASK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_TASK))
#define OREN_TASK_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_TASK, OrenTaskClass))

typedef struct _OrenTaskPrivate OrenTaskPrivate;
typedef struct _OrenTaskClass OrenTaskClass;

struct _OrenTask {
    GObject parent_instance;
    OrenTaskPrivate *priv;
};

struct _OrenTaskClass {
    GObjectClass parent_class;
    void (*run) (OrenTask *self);
    void (*cancelled) (OrenTask *self);
    void (*finished) (OrenTask *self);
};

GType oren_task_get_type (void) G_GNUC_CONST;

OrenTask* oren_task_new (void);

gboolean oren_task_is_cancelled (OrenTask *self);

G_END_DECLS

#endif /* __OREN_TASK_H__ */
