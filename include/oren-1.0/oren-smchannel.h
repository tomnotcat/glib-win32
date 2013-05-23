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
#ifndef __OREN_SM_CHANNEL_H__
#define __OREN_SM_CHANNEL_H__

#include "oren-dcchannel.h"
#include "oren-smtypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_SMCHANNEL (oren_smchannel_get_type())
#define OREN_SMCHANNEL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_SMCHANNEL, OrenSMChannel))
#define OREN_IS_SMCHANNEL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_SMCHANNEL))
#define OREN_SMCHANNEL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_SMCHANNEL, OrenSMChannelClass))
#define OREN_IS_SMCHANNEL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_SMCHANNEL))
#define OREN_SMCHANNEL_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_SMCHANNEL, OrenSMChannelClass))

typedef struct _OrenSMChannelPrivate OrenSMChannelPrivate;
typedef struct _OrenSMChannelClass OrenSMChannelClass;

struct _OrenSMChannel {
    OrenDCChannel parent_instance;
    OrenSMChannelPrivate *priv;
};

struct _OrenSMChannelClass {
    OrenDCChannelClass parent_class;
};

GType oren_smchannel_get_type (void) G_GNUC_CONST;

OrenSMChannel* oren_smchannel_new (void);

gboolean oren_smchannel_open (OrenSMChannel *self,
                              OrenDCServer *server,
                              OrenNCReactor *reactor,
                              GPtrArray *address,
                              OrenNCSockaddr *parent_addr,
                              const gchar *channel_name,
                              gboolean enable_p2p);

guint32 oren_smchannel_get_source_id (OrenSMChannel *self,
                                      guint line_number);

guint8 oren_smchannel_get_source_code (OrenSMChannel *self,
                                       guint line_number);

void _oren_smchannel_start_line (OrenSMChannel *self,
                                 OrenSMUser *user,
                                 guint line_number,
                                 guint8 source_code,
                                 gconstpointer param,
                                 gsize param_size);

void _oren_smchannel_send_meta (OrenSMChannel *self,
                                OrenSMUser *from,
                                guint line_number,
                                gconstpointer data,
                                gsize size);

void _oren_smchannel_send_data (OrenSMChannel *self,
                                OrenSMUser *from,
                                guint line_number,
                                guint data_type,
                                guint8 max_retry,
                                guint8 source_code,
                                guint8 data_hops,
                                gconstpointer data,
                                gsize size);

void _oren_smchannel_send_refuse (OrenSMChannel *self,
                                  OrenSMUser *user,
                                  guint line_number,
                                  guint data_types);

G_END_DECLS

#endif /* __OREN_SM_CHANNEL_H__ */
