/*
  Copyright (c) 2012, Giuseppe Scrivano <giuseppe@spotify.com>
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

int g_lb[3];
int g_ub[3];

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
    return abs_diff (col[0], ri) * abs_diff (col[1], gi) * abs_diff (col[2],
								     bi);
}

static void
get_colour_avg (unsigned char *img, int *col, int i, int j, int orig_w,
		int orig_h, int img_w, int img_h, int components)
{
  int ii, jj, z, len = 0;

  i = i * orig_w / img_w;
  j = j * orig_h / img_h;

  memset (col, 0, components * sizeof (int));
  for (ii = i; ii < i + orig_w / img_w; ii++)
    for (jj = j; jj < j + orig_h / img_h; jj++)
      {
	for (z = 0; z < components; z++)
	  {
	    int f;
#define PALETTE_CORRECTION(x) ((x) * (g_ub[z] - g_lb[z]) / 255 + g_lb[z])
	    f = img[(ii + jj * orig_w) * components + z];

	    col[z] += PALETTE_CORRECTION (f);
	  }
	len++;
      }

  for (z = 0; z < 3; z++)
    col[z] /= len;
}

static int
pick_colour (unsigned char *img, int i, int j, int orig_w, int orig_h,
	     int img_w, int img_h, int components)
{
  int c;
  int best = 0;
  size_t off = (i + j * orig_w) * 3;
  unsigned int best_distance = (unsigned int) -1;
  for (c = 0; c < COLORS; c++)
    {
#define MAX_COL_COMPONENTS 4
      int col[MAX_COL_COMPONENTS];
      get_colour_avg (img, col, i, j, orig_w, orig_h, img_w, img_h,
		      components);
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
  short foreground, background, ri, gi, bi, i;
  int avg[3];
  int var[3];
  memset (avg, 0, sizeof avg);
  memset (var, 0, sizeof var);
  for (i = 0; i < COLORS; i++)
    {
      pair_content (i + COLOR_MAX, &foreground, &background);
      color_content (foreground, &ri, &gi, &bi);
#define SCALE(x) ((x) * 255 / 1000)
      avg[0] += SCALE (ri);
      avg[1] += SCALE (gi);
      avg[2] += SCALE (bi);
#undef SCALE
    }

  for (i = 0; i < 3; i++)
    avg[i] /= COLORS;

  for (i = 0; i < COLORS; i++)
    {
      pair_content (i + COLOR_MAX, &foreground, &background);
      color_content (foreground, &ri, &gi, &bi);

      var[0] += abs (avg[0] - ri * 255 / 1000);
      var[1] += abs (avg[1] - gi * 255 / 1000);
      var[2] += abs (avg[2] - bi * 255 / 1000);
    }

  for (i = 0; i < 3; i++)
    {
      var[i] /= COLORS;
      g_lb[i] = max (avg[i] - var[i] * 2, 0);
      g_ub[i] = min (avg[i] + var[i] * 2, 255);
    }
}

static unsigned char *
read_jpeg_file (FILE * infile, int *w, int *h, int *components)
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

int
img_show_art (FILE * infile)
{
  int i, j, c, s_h, s_w;
  int w, h, components;
  unsigned char *img;

  img = read_jpeg_file (infile, &w, &h, &components);

  s_w = min (g_w, w) - 2;
  s_h = min (g_h, h) - 2;

  for (i = 0; i < s_w; i++)
    for (j = 0; j < s_h; j++)
      {
	int offset = (g_w - s_w) / 2;
	int col = pick_colour (img, i, j, w, h, s_w, s_h,
			       components);
	color_set (col + COLOR_MAX, NULL);
	mvprintw (j + 1, i + offset, " ");
      }
}
