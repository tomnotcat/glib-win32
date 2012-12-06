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
#ifndef __GIMO_SIGNALBUS_H__
#define __GIMO_SIGNALBUS_H__

#include "gimo-runnable.h"

G_BEGIN_DECLS

#define GIMO_TYPE_SIGNALBUS (gimo_signal_bus_get_type())
#define GIMO_SIGNALBUS(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GIMO_TYPE_SIGNALBUS, GimoSignalBus))
#define GIMO_IS_SIGNALBUS(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIMO_TYPE_SIGNALBUS))
#define GIMO_SIGNALBUS_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GIMO_TYPE_SIGNALBUS, GimoSignalBusClass))
#define GIMO_IS_SIGNALBUS_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GIMO_TYPE_SIGNALBUS))
#define GIMO_SIGNALBUS_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GIMO_TYPE_SIGNALBUS, GimoSignalBusClass))

typedef struct _GimoSignalBusPrivate GimoSignalBusPrivate;
typedef struct _GimoSignalBusClass GimoSignalBusClass;

struct _GimoSignalBus {
    GimoRunnable parent_instance;
    GimoSignalBusPrivate *priv;
};

struct _GimoSignalBusClass {
    GimoRunnableClass parent_class;
};

GType gimo_signal_bus_get_type (void) G_GNUC_CONST;

void gimo_signal_bus_set_capacity (GimoSignalBus *self,
                                   gsize size);

#define GIMO_SIGNALBUS_BEGIN(TN, t_n, s_c) \
    struct TN##Bus { \
        GimoSignalBus parent_instance; \
    }; \
    struct TN##BusClass { \
        GimoSignalBusClass parent_class; \
        GCallback funcs[s_c]; \
    }; \
    static void t_n##_bus_class_init (struct TN##BusClass *klass);  \
    GType t_n##_bus_get_type (void) {                               \
        static volatile gsize g_define_type_id__volatile = 0;       \
        if (g_once_init_enter (&g_define_type_id__volatile)) {      \
            GType g_define_type_id =                                \
                g_type_register_static_simple (                     \
                    GIMO_TYPE_SIGNALBUS,                            \
                    g_intern_static_string (#TN "Bus"),             \
                    sizeof (struct TN##BusClass),                   \
                    (GClassInitFunc) t_n##_bus_class_init,          \
                    sizeof (struct TN##Bus),                        \
                    (GInstanceInitFunc) NULL,                       \
                    (GTypeFlags) 0);                                \
            g_once_init_leave (&g_define_type_id__volatile, g_define_type_id); \
        }                                                           \
        return g_define_type_id__volatile;                          \
    }                                                               \
    GimoSignalBus* t_n##_get_bus (TN *self, GimoContext *context) { \
        static GQuark bus_quark;                                    \
        GimoSignalBus *result;                                      \
        if (!bus_quark)                                             \
            bus_quark = g_quark_from_static_string ("gimo_signalbus_bus"); \
        result = g_object_get_qdata (G_OBJECT (self), bus_quark);   \
        if (result)                                                 \
            return result;                                          \
        if (context) {                                              \
            result = g_object_new (t_n##_bus_get_type (),           \
                                   "context", context,              \
                                   "object", self, NULL);           \
            g_object_set_qdata_full (G_OBJECT (self),               \
                                     bus_quark,                     \
                                     result,                        \
                                     g_object_unref);               \
        }                                                           \
        return result;                                              \
    }                                                               \
    static void t_n##_bus_class_init (struct TN##BusClass *klass) { \
        GObjectClass *gobject_class = G_OBJECT_CLASS (klass);       \
        guint i, count = s_c;                                       \
        glong offset = G_STRUCT_OFFSET (struct TN##BusClass, funcs);\
        for (i = 0; i < s_c; ++i)                                   \
            klass->funcs[i] = NULL;                                 \
        i = 0;                                                      \
        do {

#define GIMO_SIGNALBUS_CLASS_OFFSET (offset + sizeof (GCallback) * i++)

#define GIMO_SIGNALBUS_END                      \
    } while (0);                                \
    g_assert (i == count);                      \
    }

G_END_DECLS

#endif /* __GIMO_SIGNALBUS_H__ */
