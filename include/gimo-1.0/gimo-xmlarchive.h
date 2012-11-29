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
#ifndef __GIMO_XMLARCHIVE_H__
#define __GIMO_XMLARCHIVE_H__

#include "gimo-archive.h"
#include "gimo-loadable.h"

G_BEGIN_DECLS

#define GIMO_TYPE_XMLARCHIVE (gimo_xmlarchive_get_type())
#define GIMO_XMLARCHIVE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GIMO_TYPE_XMLARCHIVE, GimoXmlArchive))
#define GIMO_IS_XMLARCHIVE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIMO_TYPE_XMLARCHIVE))
#define GIMO_XMLARCHIVE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), GIMO_TYPE_XMLARCHIVE, GimoXmlArchiveClass))
#define GIMO_IS_XMLARCHIVE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GIMO_TYPE_XMLARCHIVE))
#define GIMO_XMLARCHIVE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GIMO_TYPE_XMLARCHIVE, GimoXmlArchiveClass))

typedef struct _GimoXmlArchive GimoXmlArchive;
typedef struct _GimoXmlArchivePrivate GimoXmlArchivePrivate;
typedef struct _GimoXmlArchiveClass GimoXmlArchiveClass;

struct _GimoXmlArchive {
    GimoArchive parent_instance;
};

struct _GimoXmlArchiveClass {
    GimoArchiveClass parent_class;
};

GType gimo_xmlarchive_get_type (void) G_GNUC_CONST;

GimoXmlArchive* gimo_xmlarchive_new (void);

G_END_DECLS

#endif /* __GIMO_XMLARCHIVE_H__ */
