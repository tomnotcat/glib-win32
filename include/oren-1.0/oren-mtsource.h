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
#ifndef __OREN_MT_SOURCE_H__
#define __OREN_MT_SOURCE_H__

#include "oren-mttypes.h"
#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_MTSOURCE (oren_mtsource_get_type())
#define OREN_MTSOURCE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_MTSOURCE, OrenMTSource))
#define OREN_IS_MTSOURCE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_MTSOURCE))
#define OREN_MTSOURCE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_MTSOURCE, OrenMTSourceClass))
#define OREN_IS_MTSOURCE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_MTSOURCE))
#define OREN_MTSOURCE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_MTSOURCE, OrenMTSourceClass))

typedef struct _OrenMTSourcePrivate OrenMTSourcePrivate;
typedef struct _OrenMTSourceClass OrenMTSourceClass;

struct _OrenMTSource {
    GObject parent_instance;
    OrenMTSourcePrivate *priv;
};

struct _OrenMTSourceClass {
    GObjectClass parent_class;
    void (*send_to) (OrenMTSource *self,
                     OrenNCSocket *socket,
                     OrenNCSockaddr *address,
                     OrenNCBuffer *buffer);
};

GType oren_mtsource_get_type (void) G_GNUC_CONST;

OrenMTSource* oren_mtsource_new (const gchar *source_name,
                                 guint tree_count);

const gchar* oren_mtsource_get_name (OrenMTSource *self);

guint16 oren_mtsource_data_seq (OrenMTSource *self);

guint oren_mtsource_tree_count (OrenMTSource *self);

OrenMTTree* oren_mtsource_get_tree (OrenMTSource *self,
                                    guint tree_id);

OrenMTMember* oren_mtsource_add_member (OrenMTSource *self,
                                        const gchar *member_name,
                                        OrenNCSocket *socket,
                                        OrenNCSockaddr *observed_addr,
                                        OrenNCSockaddr *local_addr,
                                        guint16 network_type);

void oren_mtsource_remove_member (OrenMTSource *self,
                                  const gchar *member_name);

OrenMTMember* oren_mtsource_get_member (OrenMTSource *self,
                                        const gchar *member_name);

void oren_mtsource_set_buffer (OrenMTSource *self,
                               OrenNCBuffer *buffer,
                               gsize head_reserve);

void oren_mtsource_multicast (OrenMTSource *self,
                              gconstpointer data,
                              gsize size);

void oren_mtsource_multicast_packet (OrenMTSource *self,
                                     OrenNCBuffer *buffer);

void oren_mtsource_receive_from (OrenMTSource *self,
                                 OrenNCSockaddr *from,
                                 OrenNCBuffer *buffer);

void oren_mtsource_clear_send (OrenMTSource *self);

void oren_mtsource_work (OrenMTSource *self);

void _oren_mtsource_on_join (OrenMTSource *self,
                             OrenMTMember *member,
                             gboolean join);

void _oren_mtsource_send_to (OrenMTSource *self,
                             OrenNCSocket *socket,
                             OrenNCSockaddr *address,
                             OrenNCBuffer *buffer);

G_END_DECLS

#endif /* __OREN_MT_SOURCE_H__ */
