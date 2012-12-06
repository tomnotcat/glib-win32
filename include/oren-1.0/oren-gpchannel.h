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
#ifndef __OREN_GP_CHANNEL_H__
#define __OREN_GP_CHANNEL_H__

#include "oren-dcchannel.h"
#include "oren-gptypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_GPCHANNEL (oren_gpchannel_get_type())
#define OREN_GPCHANNEL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_GPCHANNEL, OrenGPChannel))
#define OREN_IS_GPCHANNEL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_GPCHANNEL))
#define OREN_GPCHANNEL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_GPCHANNEL, OrenGPChannelClass))
#define OREN_IS_GPCHANNEL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_GPCHANNEL))
#define OREN_GPCHANNEL_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_GPCHANNEL, OrenGPChannelClass))

typedef struct _OrenGPChannelPrivate OrenGPChannelPrivate;
typedef struct _OrenGPChannelClass OrenGPChannelClass;

struct _OrenGPChannel {
    OrenDCChannel parent_instance;
    OrenGPChannelPrivate *priv;
};

struct _OrenGPChannelClass {
    OrenDCChannelClass parent_class;
};

GType oren_gpchannel_get_type (void) G_GNUC_CONST;

OrenGPChannel* oren_gpchannel_new (const gchar *plugin_paths);

OrenNCBuffer* oren_gpchannel_make_protocol (OrenGPChannel *self,
                                            const gchar *message);

void oren_gpchannel_multicast_protocol (OrenGPChannel *self,
                                        OrenGPUser *skip,
                                        OrenNCBuffer *buffer);

void _oren_gpchannel_dispatch_protocol (OrenGPChannel *self,
                                        OrenGPUser *user,
                                        OrenNCBuffer *buffer);

OrenNCPacker* _oren_gpchannel_pack_buffer (OrenGPChannel *self,
                                           OrenNCBuffer *buffer,
                                           guint16 *protocol_seq);

G_END_DECLS

#endif /* __OREN_GP_CHANNEL_H__ */
