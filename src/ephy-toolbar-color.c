/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include "ephy-toolbar-color.h"

gboolean
ephy_toolbar_color_from_texture (GdkTexture *texture,
                                 GdkRGBA    *color)
{
  typedef struct {
    guint64 weight;
    guint64 red, green, blue;
  } Bucket;
  g_autofree Bucket *buckets = g_new0 (Bucket, 4096);
  g_autoptr (GdkTextureDownloader) downloader = NULL;
  g_autoptr (GBytes) bytes = NULL;
  const guint8 *pixels;
  gsize stride;
  guint best = 0;

  /* WebKit's snapshot API supplies a shared-memory bitmap. Never attempt a
   * GPU/DMA-BUF download as a fallback: retain the page background instead. */
  if (!GDK_IS_MEMORY_TEXTURE (texture))
    return FALSE;
  downloader = gdk_texture_downloader_new (texture);
  gdk_texture_downloader_set_format (downloader, GDK_MEMORY_R8G8B8A8);
  bytes = gdk_texture_downloader_download_bytes (downloader, &stride);
  pixels = g_bytes_get_data (bytes, NULL);
  /* Only y=0 contributes, regardless of the rest of the visible viewport. */
  for (int x = 0; x < gdk_texture_get_width (texture); x++) {
    const guint8 *p = pixels + x * 4;
    guint bucket = ((p[0] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[2] >> 4);

    buckets[bucket].weight += p[3];
    buckets[bucket].red += p[0] * p[3];
    buckets[bucket].green += p[1] * p[3];
    buckets[bucket].blue += p[2] * p[3];
  }
  for (guint i = 1; i < 4096; i++)
    if (buckets[i].weight > buckets[best].weight)
      best = i;
  if (!buckets[best].weight)
    return FALSE;
  color->red = (double)buckets[best].red / buckets[best].weight / 255;
  color->green = (double)buckets[best].green / buckets[best].weight / 255;
  color->blue = (double)buckets[best].blue / buckets[best].weight / 255;
  color->alpha = 1;
  return TRUE;
}
