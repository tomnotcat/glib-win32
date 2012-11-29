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
#ifndef __OREN_MT_TREE_H__
#define __OREN_MT_TREE_H__

#include "oren-mttypes.h"
#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_MTTREE (oren_mttree_get_type())
#define OREN_MTTREE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_MTTREE, OrenMTTree))
#define OREN_IS_MTTREE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_MTTREE))
#define OREN_MTTREE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_MTTREE, OrenMTTreeClass))
#define OREN_IS_MTTREE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_MTTREE))
#define OREN_MTTREE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_MTTREE, OrenMTTreeClass))

typedef struct _OrenMTTreePrivate OrenMTTreePrivate;
typedef struct _OrenMTTreeClass OrenMTTreeClass;

struct _OrenMTTree {
    GObject parent_instance;
    OrenMTTreePrivate *priv;
};

struct _OrenMTTreeClass {
    GObjectClass parent_class;
};

GType oren_mttree_get_type (void) G_GNUC_CONST;

OrenMTTree* oren_mttree_new (OrenMTSource *source,
                             guint tree_id);

guint oren_mttree_get_id (OrenMTTree *self);

void oren_mttree_add_node (OrenMTTree *self,
                           OrenMTMember *member);

void oren_mttree_remove_node (OrenMTTree *self,
                              guint32 member_id);

OrenMTNode* oren_mttree_get_node (OrenMTTree *self,
                                  guint32 member_id);

OrenMTNode* oren_mttree_root (OrenMTTree *self);

void oren_mttree_build (OrenMTTree *self);

void _oren_mttree_send_s2p (OrenMTTree *self,
                            OrenNCBuffer *buffer);

void _oren_mttree_on_join (OrenMTTree *self,
                           OrenMTMember *member,
                           gboolean join);

gboolean _oren_mttree_change_parent (OrenMTTree *self,
                                     OrenMTMember *member,
                                     guint32 parent_id);

G_END_DECLS

#endif /* __OREN_MT_TREE_H__ */
