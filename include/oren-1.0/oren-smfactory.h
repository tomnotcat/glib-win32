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
#ifndef __OREN_SM_FACTORY_H__
#define __OREN_SM_FACTORY_H__

#include "oren-dcfactory.h"
#include "oren-smtypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_SMFACTORY (oren_smfactory_get_type())
#define OREN_SMFACTORY(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_SMFACTORY, OrenSMFactory))
#define OREN_IS_SMFACTORY(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_SMFACTORY))
#define OREN_SMFACTORY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_SMFACTORY, OrenSMFactoryClass))
#define OREN_IS_SMFACTORY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_SMFACTORY))
#define OREN_SMFACTORY_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_SMFACTORY, OrenSMFactoryClass))

typedef struct _OrenSMFactoryPrivate OrenSMFactoryPrivate;
typedef struct _OrenSMFactoryClass OrenSMFactoryClass;

struct _OrenSMFactory {
    OrenDCFactory parent_instance;
    OrenSMFactoryPrivate *priv;
};

struct _OrenSMFactoryClass {
    OrenDCFactoryClass parent_class;
};

GType oren_smfactory_get_type (void) G_GNUC_CONST;

OrenSMFactory* oren_smfactory_new (gboolean pack_data);

G_END_DECLS

#endif /* __OREN_SM_FACTORY_H__ */
