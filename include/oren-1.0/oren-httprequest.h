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
#ifndef __OREN_HTTP_REQUEST_H__
#define __OREN_HTTP_REQUEST_H__

#include "oren-nctypes.h"

G_BEGIN_DECLS

#define OREN_TYPE_HTTP_REQUEST (oren_http_request_get_type())
#define OREN_HTTP_REQUEST(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), OREN_TYPE_HTTP_REQUEST, OrenHttpRequest))
#define OREN_IS_HTTP_REQUEST(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), OREN_TYPE_HTTP_REQUEST))
#define OREN_HTTP_REQUEST_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), OREN_TYPE_HTTP_REQUEST, OrenHttpRequestClass))
#define OREN_IS_HTTP_REQUEST_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), OREN_TYPE_HTTP_REQUEST))
#define OREN_HTTP_REQUEST_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), OREN_TYPE_HTTP_REQUEST, OrenHttpRequestClass))

typedef struct _OrenHttpRequestPrivate OrenHttpRequestPrivate;
typedef struct _OrenHttpRequestClass OrenHttpRequestClass;

struct _OrenHttpRequest {
    GObject parent_instance;
    OrenHttpRequestPrivate *priv;
};

struct _OrenHttpRequestClass {
    GObjectClass parent_class;
    void (*response) (OrenHttpRequest *self, OrenNCBuffer *buffer);
    void (*error) (OrenHttpRequest *self, gint error);
};

GType oren_http_request_get_type (void) G_GNUC_CONST;

OrenHttpRequest* oren_http_request_new (const gchar *url);

OrenHttpRequest* oren_http_request_new_raw (const gchar *url);

OrenHttpRequest* oren_http_request_new_post (const gchar *url,
                                             OrenNCBuffer *data);

const gchar* oren_http_request_get_url (OrenHttpRequest *self);

gboolean oren_http_request_perform (OrenHttpRequest *self);

gchar* oren_http_request_escape (OrenHttpRequest *self,
                                 const gchar *string);

gchar* oren_http_request_unescape (OrenHttpRequest *self,
                                   const gchar *string);

G_END_DECLS

#endif /* __OREN_HTTP_REQUEST_H__ */
