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
#ifndef __OREN_CRYPTO_H__
#define __OREN_CRYPTO_H__

#include <glib.h>

G_BEGIN_DECLS

#define OREN_SHA1_LENGTH (20)
#define OREN_MD5_LENGTH (16)

void _oren_make_sha1 (gpointer buffer, gsize length, ...);

void _oren_make_md5 (gpointer buffer, gsize length, ...);

gchar* _oren_bin2hex (gchar *stringbuf,
                      gconstpointer buffer,
                      gsize length);

gpointer _oren_hex2bin (gpointer buffer,
                        const gchar *string,
                        gsize length);

gchar* oren_md5sum (const gchar *data);

G_END_DECLS

#endif /* __OREN_CRYPTO_H__ */
