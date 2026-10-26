/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © 2012 Igalia S.L.
 *
 *  This file is part of Epiphany.
 *
 *  Epiphany is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Epiphany is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Epiphany.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "ephy-favicon-helpers.h"

#include <gdk/gdk.h>
#include <glib.h>

#include "ephy-pixbuf-utils.h"

GIcon *
ephy_favicon_get_from_texture_scaled (GdkTexture *texture,
                                      int         width,
                                      int         height)
{
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  int favicon_width;
  int favicon_height;

  if (!texture)
    return NULL;

  /* A size of (0, 0) means the original size of the favicon. */
  if (width == 0 && height == 0)
    return G_ICON (g_object_ref (texture));

  favicon_width = gdk_texture_get_width (texture);
  favicon_height = gdk_texture_get_height (texture);
  if (favicon_width == width && favicon_height == height)
    return G_ICON (g_object_ref (texture));

  pixbuf = ephy_texture_to_pixbuf (texture, TRUE);
  return G_ICON (gdk_pixbuf_scale_simple (pixbuf, width, height, GDK_INTERP_BILINEAR));
}

typedef struct {
  guint64 weight;
  guint64 red;
  guint64 green;
  guint64 blue;
} EdgeColor;

static gboolean
sample_edge (GdkPixbuf *pixbuf,
             int        x,
             int        y,
             int        dx,
             int        dy,
             guint      min_alpha,
             EdgeColor *colors)
{
  int width = gdk_pixbuf_get_width (pixbuf);
  int height = gdk_pixbuf_get_height (pixbuf);
  int channels = gdk_pixbuf_get_n_channels (pixbuf);
  int stride = gdk_pixbuf_get_rowstride (pixbuf);
  const guchar *pixels = gdk_pixbuf_read_pixels (pixbuf);

  /* Enter from outside: transparent margins and holes inside the logo must
   * not turn an interior brand color into the tile background. */
  while (x >= 0 && x < width && y >= 0 && y < height) {
    const guchar *pixel = pixels + y * stride + x * channels;
    guint alpha = channels == 4 ? pixel[3] : 255;

    if (alpha >= min_alpha) {
      guint bin = ((pixel[0] >> 4) << 8) | ((pixel[1] >> 4) << 4) | (pixel[2] >> 4);

      colors[bin].weight += alpha;
      colors[bin].red += pixel[0] * alpha;
      colors[bin].green += pixel[1] * alpha;
      colors[bin].blue += pixel[2] * alpha;
      return TRUE;
    }
    x += dx;
    y += dy;
  }
  return FALSE;
}

gboolean
ephy_favicon_get_edge_color (GdkTexture *texture,
                             GdkRGBA    *color)
{
  g_autoptr (GdkPixbuf) pixbuf = ephy_texture_to_pixbuf (texture, TRUE);
  g_autofree EdgeColor *colors = g_new0 (EdgeColor, 4096);
  guint best = 0;
  int width = gdk_pixbuf_get_width (pixbuf);
  int height = gdk_pixbuf_get_height (pixbuf);

  /* Prefer opaque edge pixels over antialiasing/shadows. Translucent-only
   * icons get a second pass; fully transparent icons have no edge color. */
  for (guint pass = 0; pass < 2; pass++) {
    guint min_alpha = pass == 0 ? 192 : 32;

    for (int y = 0; y < height; y++) {
      sample_edge (pixbuf, 0, y, 1, 0, min_alpha, colors);
      sample_edge (pixbuf, width - 1, y, -1, 0, min_alpha, colors);
    }
    for (int x = 0; x < width; x++) {
      sample_edge (pixbuf, x, 0, 0, 1, min_alpha, colors);
      sample_edge (pixbuf, x, height - 1, 0, -1, min_alpha, colors);
    }
    for (guint i = 1; i < 4096; i++)
      if (colors[i].weight > colors[best].weight)
        best = i;
    if (colors[best].weight) {
      color->red = (double)colors[best].red / colors[best].weight / 255;
      color->green = (double)colors[best].green / colors[best].weight / 255;
      color->blue = (double)colors[best].blue / colors[best].weight / 255;
      color->alpha = 1;
      return TRUE;
    }
  }
  return FALSE;
}

static void
contrast_monochrome_icon (GdkPixbuf     *source,
                          const GdkRGBA *background)
{
  int width = gdk_pixbuf_get_width (source);
  int height = gdk_pixbuf_get_height (source);
  int stride = gdk_pixbuf_get_rowstride (source);
  int channels = gdk_pixbuf_get_n_channels (source);
  guchar *pixels = gdk_pixbuf_get_pixels (source);
  guint64 total = 0, matching = 0;
  int red = background->red * 255 + .5;
  int green = background->green * 255 + .5;
  int blue = background->blue * 255 + .5;
  guint foreground = red > 230 && green > 230 && blue > 230 ? 40 : 255;

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const guchar *pixel = pixels + y * stride + x * channels;
      guint alpha = channels == 4 ? pixel[3] : 255;

      if (alpha < 32)
        continue;
      total += alpha;
      if (ABS (pixel[0] - red) <= 32 && ABS (pixel[1] - green) <= 32 && ABS (pixel[2] - blue) <= 32)
        matching += alpha;
    }
  }
  if (!total || matching * 100 < total * 95)
    return;

  /* Preserve the original silhouette/antialiasing, including alpha. A white
   * glyph contrasts with brand colors; near-white icons need a dark glyph. */
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      guchar *pixel = pixels + y * stride + x * channels;

      pixel[0] = pixel[1] = pixel[2] = foreground;
    }
  }
}

static void
round_tile_corners (GdkPixbuf *canvas)
{
  int size = gdk_pixbuf_get_width (canvas);
  int stride = gdk_pixbuf_get_rowstride (canvas);
  double radius = size / 5.0;
  guchar *pixels = gdk_pixbuf_get_pixels (canvas);

  /* Clip the actual pixels: GtkImage's CSS border-radius does not clip its
   * paintable. Supersampling keeps the curved edge antialiased at either size. */
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      double cx = x < radius ? radius : size - radius;
      double cy = y < radius ? radius : size - radius;
      guint coverage = 0;

      if ((x >= radius && x < size - radius) || (y >= radius && y < size - radius))
        continue;
      for (int sy = 0; sy < 4; sy++) {
        for (int sx = 0; sx < 4; sx++) {
          double dx = x + (sx + .5) / 4 - cx;
          double dy = y + (sy + .5) / 4 - cy;

          coverage += dx * dx + dy * dy <= radius * radius;
        }
      }
      pixels[y * stride + x * 4 + 3] = 255 * coverage / 16;
    }
  }
}

GdkTexture *
ephy_favicon_create_tile (GdkTexture *texture,
                          int         size)
{
  GQuark key = g_quark_from_static_string ("savanna-favicon-tiles");
  GHashTable *tiles = g_object_get_qdata (G_OBJECT (texture), key);
  GdkTexture *tile;
  GdkRGBA color;
  g_autoptr (GdkPixbuf) original = NULL;
  g_autoptr (GdkPixbuf) source = NULL;
  g_autoptr (GdkPixbuf) canvas = NULL;
  g_autoptr (GdkPixbuf) scaled = NULL;
  g_autoptr (GBytes) pixels = NULL;
  double scale;
  int width, height, x, y;
  guint32 rgba;

  g_return_val_if_fail (size > 0 && size <= 512, NULL);
  tile = tiles ? g_hash_table_lookup (tiles, GINT_TO_POINTER (size)) : NULL;
  if (tile)
    return g_object_ref (tile);
  if (!ephy_favicon_get_edge_color (texture, &color))
    return g_object_ref (texture);

  /* Texture downloads may share immutable storage with the original texture.
   * Recolor only a private copy so other tile sizes and URL icons stay intact. */
  original = ephy_texture_to_pixbuf (texture, TRUE);
  source = gdk_pixbuf_copy (original);
  contrast_monochrome_icon (source, &color);
  canvas = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, size, size);
  rgba = ((guint32)(color.red * 255 + .5) << 24) |
         ((guint32)(color.green * 255 + .5) << 16) |
         ((guint32)(color.blue * 255 + .5) << 8) | 255;
  gdk_pixbuf_fill (canvas, rgba);
  scale = MIN (1.0, MIN (size * .75 / gdk_pixbuf_get_width (source), size * .75 / gdk_pixbuf_get_height (source)));
  width = MAX (1, (int)(gdk_pixbuf_get_width (source) * scale));
  height = MAX (1, (int)(gdk_pixbuf_get_height (source) * scale));
  x = (size - width) / 2;
  y = (size - height) / 2;
  if (scale < 1)
    scaled = gdk_pixbuf_scale_simple (source, width, height, GDK_INTERP_HYPER);
  gdk_pixbuf_composite (scaled ? scaled : source, canvas, x, y, width, height,
                        x, y, 1, 1, GDK_INTERP_NEAREST, 255);
  round_tile_corners (canvas);
  pixels = gdk_pixbuf_read_pixel_bytes (canvas);
  tile = gdk_memory_texture_new (size, size, GDK_MEMORY_R8G8B8A8, pixels, gdk_pixbuf_get_rowstride (canvas));
  if (!tiles) {
    tiles = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
    g_object_set_qdata_full (G_OBJECT (texture), key, tiles, (GDestroyNotify)g_hash_table_unref);
  }
  g_hash_table_insert (tiles, GINT_TO_POINTER (size), tile);
  return g_object_ref (tile);
}
