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
#ifndef __OREN_MT_BRANCH_H__
#define __OREN_MT_BRANCH_H__

#include "oren-mttypes.h"
#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_MTBRANCH (oren_mtbranch_get_type())
#define OREN_MTBRANCH(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_MTBRANCH, OrenMTBranch))
#define OREN_IS_MTBRANCH(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_MTBRANCH))
#define OREN_MTBRANCH_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_MTBRANCH, OrenMTBranchClass))
#define OREN_IS_MTBRANCH_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_MTBRANCH))
#define OREN_MTBRANCH_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_MTBRANCH, OrenMTBranchClass))

typedef struct _OrenMTBranchPrivate OrenMTBranchPrivate;
typedef struct _OrenMTBranchClass OrenMTBranchClass;

struct _OrenMTBranch {
    GObject parent_instance;
    OrenMTBranchPrivate *priv;
};

struct _OrenMTBranchClass {
    GObjectClass parent_class;
};

GType oren_mtbranch_get_type (void) G_GNUC_CONST;

OrenMTBranch* oren_mtbranch_new (OrenMTPeer *peer,
                                 guint branch_id);

guint oren_mtbranch_get_id (OrenMTBranch *self);

OrenMTMate* oren_mtbranch_get_parent (OrenMTBranch *self);

GSList* oren_mtbranch_get_children (OrenMTBranch *self);

void oren_mtbranch_try_parent (OrenMTBranch *self,
                               const gchar *mate_name,
                               guint32 mate_id,
                               OrenNCSockaddr *mate_addr);

void oren_mtbranch_try_child (OrenMTBranch *self,
                              const gchar *mate_name,
                              guint32 mate_id,
                              OrenNCSockaddr *mate_addr);

OrenMTPeer* _oren_mtbranch_get_peer (OrenMTBranch *self);

void _oren_mtbranch_send_chpnt (OrenMTBranch *self,
                                OrenMTMate *parent);

void _oren_mtbranch_on_chpnt (OrenMTBranch *self,
                              guint32 parent_id,
                              gboolean result);

void _oren_mtbranch_work (OrenMTBranch *self);

void _oren_mtbranch_send_to_children (OrenMTBranch *self,
                                      gconstpointer data,
                                      gsize size);

OrenNCBuffer* _oren_mtbranch_init_p2pbuf (OrenMTBranch *self,
                                          guint32 mate_id);

void _oren_mtbranch_send_p2pbuf (OrenMTBranch *self,
                                 OrenNCSockaddr *addr,
                                 OrenNCBuffer *buffer);

void _oren_mtbranch_receive_from (OrenMTBranch *self,
                                  OrenNCSockaddr *from,
                                  OrenNCBuffer *buffer);

void _oren_mtbranch_disconnect (OrenMTBranch *self);

G_END_DECLS

#endif /* __OREN_MT_BRANCH_H__ */
