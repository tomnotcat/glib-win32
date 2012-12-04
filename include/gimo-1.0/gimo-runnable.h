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
#ifndef __GIMO_RUNNABLE_H__
#define __GIMO_RUNNABLE_H__

#include "gimo-types.h"

G_BEGIN_DECLS

#define GIMO_TYPE_RUNNABLE (gimo_runnable_get_type())
#define GIMO_RUNNABLE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GIMO_TYPE_RUNNABLE, GimoRunnable))
#define GIMO_IS_RUNNABLE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIMO_TYPE_RUNNABLE))
#define GIMO_RUNNABLE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GIMO_TYPE_RUNNABLE, GimoRunnableClass))
#define GIMO_IS_RUNNABLE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GIMO_TYPE_RUNNABLE))
#define GIMO_RUNNABLE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GIMO_TYPE_RUNNABLE, GimoRunnableClass))

typedef struct _GimoRunnablePrivate GimoRunnablePrivate;
typedef struct _GimoRunnableClass GimoRunnableClass;

struct _GimoRunnable {
    GObject parent_instance;
};

struct _GimoRunnableClass {
    GObjectClass parent_class;
    void (*run) (GimoRunnable *self);
};

GType gimo_runnable_get_type (void) G_GNUC_CONST;

GimoRunnable* gimo_runnable_new (void);

void gimo_runnable_run (GimoRunnable *self);

G_END_DECLS

#endif /* __GIMO_RUNNABLE_H__ */
