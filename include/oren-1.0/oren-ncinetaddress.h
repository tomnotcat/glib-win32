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
#ifndef __OREN_NC_INET_ADDRESS_H__
#define __OREN_NC_INET_ADDRESS_H__

#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_NCINET_ADDRESS (oren_ncinet_address_get_type())
#define OREN_NCINET_ADDRESS(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_NCINET_ADDRESS, OrenNCInetAddress))
#define OREN_IS_NCINET_ADDRESS(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_NCINET_ADDRESS))
#define OREN_NCINET_ADDRESS_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_NCINET_ADDRESS, OrenNCInetAddressClass))
#define OREN_IS_NCINET_ADDRESS_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_NCINET_ADDRESS))
#define OREN_NCINET_ADDRESS_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_NCINET_ADDRESS, OrenNCInetAddressClass))

typedef struct _OrenNCInetAddressPrivate OrenNCInetAddressPrivate;
typedef struct _OrenNCInetAddressClass OrenNCInetAddressClass;

struct _OrenNCInetAddress {
    GObject parent_instance;
    OrenNCInetAddressPrivate *priv;
};

struct _OrenNCInetAddressClass {
    GObjectClass parent_class;
};

GType oren_ncinet_address_get_type(void) G_GNUC_CONST;

OrenNCInetAddress* oren_ncinet_address_new_from_string(const gchar *string);

OrenNCInetAddress* oren_ncinet_address_new_from_bytes(const guint8 *bytes,
                                                      OrenNCSocketFamily family);

OrenNCInetAddress* oren_ncinet_address_new_loopback(OrenNCSocketFamily family);

OrenNCInetAddress* oren_ncinet_address_new_any(OrenNCSocketFamily family);

gboolean oren_ncinet_address_equal(OrenNCInetAddress *self,
                                   OrenNCInetAddress *other);

gchar* oren_ncinet_address_to_string(OrenNCInetAddress *self);

const guint8* oren_ncinet_address_to_bytes(OrenNCInetAddress *self);

gsize oren_ncinet_address_get_native_size(OrenNCInetAddress *self);

OrenNCSocketFamily oren_ncinet_address_get_family(OrenNCInetAddress *self);

gboolean oren_ncinet_address_is_any(OrenNCInetAddress *self);

gboolean oren_ncinet_address_is_loopback(OrenNCInetAddress *self);

G_END_DECLS

#endif /* __OREN_NC_INET_ADDRESS_H__ */
