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
#ifndef __OREN_MT_NODE_H__
#define __OREN_MT_NODE_H__

#include "oren-mttypes.h"
#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_MTNODE (oren_mtnode_get_type())
#define OREN_MTNODE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_MTNODE, OrenMTNode))
#define OREN_IS_MTNODE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_MTNODE))
#define OREN_MTNODE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_MTNODE, OrenMTNodeClass))
#define OREN_IS_MTNODE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_MTNODE))
#define OREN_MTNODE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_MTNODE, OrenMTNodeClass))

typedef struct _OrenMTNodePrivate OrenMTNodePrivate;
typedef struct _OrenMTNodeClass OrenMTNodeClass;

struct _OrenMTNode {
    GObject parent_instance;
    OrenMTNodePrivate *priv;
};

struct _OrenMTNodeClass {
    GObjectClass parent_class;
};

GType oren_mtnode_get_type (void) G_GNUC_CONST;

OrenMTNode* oren_mtnode_new (OrenMTMember *member);

OrenMTMember* oren_mtnode_get_member (OrenMTNode *self);

OrenMTNode* oren_mtnode_get_parent (OrenMTNode *self);

GSList* oren_mtnode_get_children (OrenMTNode *self);

guint oren_mtnode_get_height (OrenMTNode *self, guint max_val);

guint oren_mtnode_get_depth (OrenMTNode *self);

G_END_DECLS

#endif /* __OREN_MT_NODE_H__ */
