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
#ifndef __OREN_GP_FACTORY_H__
#define __OREN_GP_FACTORY_H__

#include "oren-dcfactory.h"
#include "oren-gptypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_GPFACTORY (oren_gpfactory_get_type())
#define OREN_GPFACTORY(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_GPFACTORY, OrenGPFactory))
#define OREN_IS_GPFACTORY(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_GPFACTORY))
#define OREN_GPFACTORY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_GPFACTORY, OrenGPFactoryClass))
#define OREN_IS_GPFACTORY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_GPFACTORY))
#define OREN_GPFACTORY_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_GPFACTORY, OrenGPFactoryClass))

typedef struct _OrenGPFactoryPrivate OrenGPFactoryPrivate;
typedef struct _OrenGPFactoryClass OrenGPFactoryClass;

struct _OrenGPFactory {
    OrenDCFactory parent_instance;
    OrenGPFactoryPrivate *priv;
};

struct _OrenGPFactoryClass {
    OrenDCFactoryClass parent_class;
};

GType oren_gpfactory_get_type (void) G_GNUC_CONST;

OrenGPFactory* oren_gpfactory_new (const gchar *plugin_paths);

G_END_DECLS

#endif /* __OREN_GP_FACTORY_H__ */
