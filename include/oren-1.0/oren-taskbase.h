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
#ifndef __OREN_TASK_BASE_H__
#define __OREN_TASK_BASE_H__

#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_TASK_BASE (oren_task_base_get_type ())
#define OREN_TASK_BASE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_TASK_BASE, OrenTaskBase))
#define OREN_IS_TASK_BASE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_TASK_BASE))
#define OREN_TASK_BASE_GET_IFACE(inst) \
    (G_TYPE_INSTANCE_GET_INTERFACE ((inst), OREN_TYPE_TASK_BASE, OrenTaskBaseInterface))

typedef struct _OrenTaskBaseInterface OrenTaskBaseInterface;

struct _OrenTaskBaseInterface {
    GTypeInterface base_iface;
    void (*run) (OrenTaskBase *self);
    void (*cancel) (OrenTaskBase *self);
};

GType oren_task_base_get_type (void) G_GNUC_CONST;

void oren_task_base_run (OrenTaskBase *self);

void oren_task_base_cancel (OrenTaskBase *self);

G_END_DECLS

#endif /* __OREN_TASK_BASE_H__ */
