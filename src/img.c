/*
  Copyright (c) 2012, Giuseppe Scrivano <gscrivano@gnu.org>
  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Spotify AB nor the names of its contributors
    * may be used to endorse or promote products derived from this
    * software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "shpotify.h"
#include <jpeglib.h>
#include <stdio.h>
#include <unistd.h>
#include <form.h>
#include <curses.h>
#include <string.h>
#include <locale.h>
#include <menu.h>

#include <assert.h>

#define MAX_COL_COMPONENTS 4

static int
abs_diff (int a, int b)
{
  return a > b ? a - b : b - a;
}

static int
distance (int *col, int components, int c)
{
  short foreground, background, ri, gi, bi;
  pair_content (c + COLOR_MAX, &foreground, &background);
  color_content (foreground, &ri, &gi, &bi);

  ri = ri * 255 / 1000;
  gi = gi * 255 / 1000;
  bi = bi * 255 / 1000;

  if (components == 1)
    return abs_diff ((ri + gi + bi) / 3, col[0]);
  else
    return abs_diff (col[0], ri) + abs_diff (col[1], gi) + abs_diff (col[2],
								     bi);
}

static int
pick_best_distance_color (int components, int *col)
{
  int best = 0, c;
  unsigned int best_distance = 256 * components + 1;

  for (c = 0; c < COLORS; c++)
    {
      unsigned int d = distance (col, components, c);
      if (d < best_distance)
	{
	  best_distance = d;
	  best = c;
	}
    }

  return best;
}

void
img_initialize_palette ()
{
}

static unsigned char *
read_jpeg_file (FILE *infile, int *w, int *h, int *components)
{
  size_t size;
  unsigned char *raw_image;
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;
  JSAMPROW row_pointer[1];
  unsigned long location = 0;
  int i = 0;
  if (!infile)
    return NULL;

  cinfo.err = jpeg_std_error (&jerr);
  jpeg_create_decompress (&cinfo);
  jpeg_stdio_src (&cinfo, infile);
  jpeg_read_header (&cinfo, TRUE);
  jpeg_start_decompress (&cinfo);

  *w = cinfo.output_width;
  *h = cinfo.output_height;
  *components = cinfo.num_components;

  size = cinfo.output_width * cinfo.output_height * cinfo.num_components;

  raw_image = (unsigned char *) malloc (size);
  row_pointer[0] =
    (unsigned char *) malloc (cinfo.output_width * cinfo.num_components);
  while (cinfo.output_scanline < cinfo.image_height)
    {
      jpeg_read_scanlines (&cinfo, row_pointer, 1);
      for (i = 0; i < cinfo.image_width * cinfo.num_components; i++)
	raw_image[location++] = row_pointer[0][i];
    }
  jpeg_finish_decompress (&cinfo);
  jpeg_destroy_decompress (&cinfo);
  free (row_pointer[0]);
  return raw_image;
}

#define CLAMP(x) (min (max (x, 0), 255))
#define INDEX_EXT(i, j, w, h) (((j * w) + i) * components + c)
#define INDEX(i, j) INDEX_EXT(i, j, w, h)

static void
img_dithering (unsigned char *img, int w, int h, int components)
{
  int i, j, c;

  for (j = 0; j < h - 1; j++)
    for (i = 1; i < w - 1; i++)
      {
        int old_components[MAX_COL_COMPONENTS];
        int new_components[MAX_COL_COMPONENTS];
        short foreground, background, ro, go, bo;
        int best_palette;

        c = 0;

        for (c = 0; c < components; c++)
          old_components[c] = img[INDEX(i, j)];

        best_palette = pick_best_distance_color (components, old_components);
        pair_content (best_palette + COLOR_MAX, &foreground, &background);
        color_content (foreground, &ro, &go, &bo);

        new_components[0] = ro * 255 / 1000;
        new_components[1] = go * 255 / 1000;
        new_components[2] = bo * 255 / 1000;
        for (c = 0; c < components; c++)
          {
            int quant_error = old_components[c] - new_components[c];
#define TRANSFORM(ii, jj, n) do {                                       \
              int index = INDEX((i + ii), (j + jj));                    \
              int delta = (quant_error * n) / 16;                       \
              img[index] = CLAMP (img[index] + delta);                  \
            } while (0)

            TRANSFORM (1, 0, 7);
            TRANSFORM (-1, 1, 3);
            TRANSFORM (0, 1, 5);
            TRANSFORM (1, 1, 1);

            img[INDEX(i, j)] = new_components[c];
          }
      }
}

static unsigned char
img_blur (const unsigned char *img, int w, int h, int components, int si, int sj, int s_w, int s_h, int c, int blur)
{
  int i, j;
  unsigned int ret = 0;
  int nw = w / s_w + blur;
  int nh = h / s_h + blur;
  int cells = 0;

  for (i = si * w / s_w - nw; i <= si * w / s_w + nw; i++)
    for (j = sj * h / s_h - nh; j <= sj * h / s_h + nh; j++)
      {
        if (i < 0 || j < 0 || i >= w || j >= h)
          continue;

        cells++;
        ret += img[INDEX(i, j)];
      }

  return cells ? CLAMP (ret / cells) : 0;
}

static void
img_scaling (const unsigned char *img, unsigned char *scaled_img, int w, int h, int components, int s_w, int s_h)
{
  int i, j, c;
  for (i = 0; i < s_w; i++)
    for (j = 0; j < s_h; j++)
      for (c = 0; c < components; c++)
        {
          int ind = INDEX_EXT (i, j, s_w, s_h);
          scaled_img[ind] = img_blur (img, w, h, components, i, j, s_w, s_h, c, 1);
        }
}
#undef INDEX
#undef INDEX_EXT
#undef CLAMP

int
img_show_art (FILE *infile)
{
  int i, j, s_h, s_w, c;
  int w, h, components, ret = 0;
  unsigned char *img, *scaled_img;

  img = read_jpeg_file (infile, &w, &h, &components);
  if (img == NULL)
    return -1;

  assert (components <= MAX_COMPONENTS);

  s_w = min (g_w, w) - 2;
  s_h = min (g_h, h) - 2;

  scaled_img = malloc (s_h * s_w * components);
  if (scaled_img == NULL)
    {
      ret = -1;
      goto exit_img;
    }

  img_scaling (img, scaled_img, w, h, components, s_w, s_h);
  img_dithering (scaled_img, s_w, s_h, components);
  for (i = 0; i < s_w; i++)
    for (j = 0; j < s_h; j++)
      {
	int offset = (g_w - s_w) / 2;
        int ind = ((j * s_w) + i) * components;
        int palette_col, col[MAX_COMPONENTS];
        for (c = 0; c < components; c++)
          col[c] = scaled_img[ind + c];

	palette_col = pick_best_distance_color (components, col);
        color_set (palette_col + COLOR_MAX, NULL);
	mvprintw (j + 1, i + offset, " ");
      }
  free(scaled_img);
exit_img:
  free (img);
  return ret;
}
