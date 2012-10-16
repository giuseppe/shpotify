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

#ifndef SHPOTIFY_H
#define SHPOTIFY_H

#include <libspotify/api.h>
#include <stdio.h>
#include <stdlib.h>

#define TIMEOUT 10

#define max(a,b) ((a) > (b) ? (a) : (b))
#define min(a,b) ((a) < (b) ? (a) : (b))

enum
  {
    STATUS_NOT_LOGGED         = 1,
    STATUS_LOGIN,
    STATUS_AUTOMATIC_LOGIN,
    STATUS_LOGGING_IN,
    STATUS_HOME,
    STATUS_SEARCH_BROWSE,
    STATUS_BROWSE_SHOW,
    STATUS_BROWSE_SHOW_PLAYLISTS,
    STATUS_BROWSE_RESULT,
    STATUS_SEARCH_BROWSE_PLAYLIST,
    STATUS_PLAYING
  };

enum
  {
    COLOR_DEFAULT = 1,
    COLOR_MESSAGE,
    COLOR_INPUT,
    COLOR_SEEK_BAR_ELAPSED,
    COLOR_SEEK_BAR_FUTURE,
    COLOR_STAR,
    COLOR_MAX
  };

enum
  {
    TYPE_LAST = 0,
    TYPE_ALBUM = 1,
    TYPE_TRACK = 2,
    TYPE_ARTIST = 3,
    TYPE_PLAYLIST = 4,
    TYPE_PLAYLISTCONTAINER_START = 5,
    TYPE_PLAYLISTCONTAINER_END = 6,
  };

struct search_result
{
  union
  {
    sp_album *album;
    sp_track *track;
    sp_artist *artist;
    sp_playlist *playlist;
    char folder[32];
  };
  int type;
};

extern int g_h, g_w;

int sound_init ();
int sound_write (const char *buffer, int frames);
int sound_flush ();
int sound_clean ();

/* img.c.  */
void img_initialize_palette ();
int img_show_art (FILE *infile);


#endif
