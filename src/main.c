/*
  Copyright (c) 2012, 2013, 2014 Giuseppe Scrivano <gscrivano@gnu.org>
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <form.h>
#include <curses.h>
#include <signal.h>
#include <string.h>
#include <locale.h>
#include <menu.h>
#include "shpotify.h"
#include "queue.h"

#include <fcntl.h>

#include <assert.h>

static WINDOW *content_wnd;
static WINDOW *g_mainwin;
static int g_status, g_debug = 0;
static bool force_redraw = false;
int g_h, g_w;
struct search_result *g_search_results;
struct search_result g_result_to_browse;
static sp_session *g_session;
static sp_search *g_search;
static int g_force_refresh = 0;
static int g_end_of_track = 0, g_seek_off = -1;
static int g_paused = 0;
static queue_t *g_play_queue;
static void free_search_results (struct search_result *sr);
static const char *search_result_get_name (struct search_result *sr);
static sp_playlist *choose_playlist ();
static int show_art (FILE * infile);
static int g_elapsed_frames, g_sample_rate;
static sp_track *g_current_track;
static sp_playlist *g_browsed_playlist = NULL;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

static void
init_wd ()
{
  const char const *subdir = ".shpotify";
  char *cwd;
  char *home = getenv ("HOME");
  if (home == NULL)
    {
      fprintf (stderr, "Cannot find $HOME\n");
      _exit (EXIT_FAILURE);
    }
  cwd = malloc (strlen (home) + strlen (subdir) + 2);
  if (cwd == NULL)
    _exit (EXIT_FAILURE);

  sprintf (cwd, "%s/%s", home, subdir);
  mkdir (cwd, S_IRWXU);
  if (chdir (cwd) < 0)
    {
      fprintf (stderr, "Cannot chdir to %s\n", cwd);
      _exit (EXIT_FAILURE);
    }
  free (cwd);
}

static void
print_star (int y, int x, bool starred)
{
  char star_buffer[4];
  attrset (COLOR_PAIR (COLOR_STAR));
  sprintf (star_buffer, "%lc", starred ? 0x2605 : 0x2606);
  mvprintw (y, x, star_buffer);
  attrset (COLOR_PAIR (COLOR_DEFAULT));
}


static int
read_line (char *buffer, size_t len, const char const *prompt)
{
  size_t so_far = 0;
  size_t prompt_len = strlen (prompt);
  int ret = 0;
  buffer[0] = '\0';

  curs_set (1);

  attrset (COLOR_PAIR (COLOR_INPUT));

  nodelay (g_mainwin, false);

  for (;;)
    {
      size_t i;
      int ch;

      mvaddnstr (g_h - 3, 1, prompt, g_w - 2);
      mvaddnstr (g_h - 3, prompt_len + 1, buffer, g_w - 2);

      mvhline (g_h - 3, prompt_len + so_far + 1, ' ', g_w - 2 - prompt_len - so_far);

      if (so_far == len - 1)
        break;

      ch = getch ();
      if (ch == 27)
        {
          ret = 1;
          goto exit;
        }

      if (ch == KEY_BACKSPACE && so_far)
	{
	  buffer[--so_far] = '\0';
	  continue;
	}

      if (ch == '\n')
	break;

      if (isprint (ch))
        {
          buffer[so_far++] = ch;
          buffer[so_far] = '\0';
        }
    }

 exit:
  nodelay (g_mainwin, true);
  attrset (COLOR_PAIR (COLOR_DEFAULT));
  curs_set (0);
  return ret;
}

static void
trim (char *buffer)
{
  size_t len = strlen (buffer);
  if (len)
    {
      size_t it = len - 1;
      while (it)
	{
	  if (buffer[it] != ' ')
	    return;

	  buffer[it--] = '\0';
	}
    }
}

static void
msg_to_user (const char const *msg)
{
  int i;
  size_t len = strlen (msg);

  attrset (COLOR_PAIR (COLOR_MESSAGE));

  mvaddnstr (g_h - 2, 1, msg, g_w - 2);

  /*FIXME: is there a better way to fill the line?  */
  for (i = len + 1; i < g_w - 2; i++)
    mvprintw (g_h - 2, i, " ");
  attrset (COLOR_PAIR (COLOR_DEFAULT));
}

static void
reset_screen ()
{
  erase ();
  box (g_mainwin, 0, 0);
}

static int
transition_to (int new_status)
{
  reset_screen ();
  g_status = new_status;
}

static int
login ()
{
  FIELD *field[3];
  FORM *form;
  char username[33], password[33];
  int ch, selected = 0;
  int x = (g_w - 32) / 2;
  int y = g_h / 2;

  curs_set (1);

  keypad (stdscr, TRUE);

  field[0] = new_field (1, 32, y + 0, x, 0, 0);
  field[1] = new_field (1, 32, y + 1, x, 0, 0);
  field[2] = NULL;

  set_field_back (field[0], A_UNDERLINE);
  field_opts_off (field[0], O_AUTOSKIP);
  field_opts_off (field[0], O_STATIC);

  set_field_back (field[1], A_UNDERLINE);
  field_opts_off (field[1], O_AUTOSKIP);
  field_opts_off (field[1], O_PUBLIC);

  form = new_form (field);
  set_form_win (form, content_wnd);
  post_form (form);
  box (g_mainwin, 0, 0);

  mvprintw (y - 1, x - 10, "Shpotify");
  mvprintw (y + 0, x - 10, "Username:");
  mvprintw (y + 1, x - 10, "Password:");

  refresh ();

  form_driver (form, REQ_FIRST_FIELD);

  while (true)
    {
      ch = getch ();

      switch (ch)
	{
        case '\n':
        if (selected == 1)
          goto exit;
        /*Fall trough.  */
	case KEY_DOWN:
	  selected = !selected;
	  form_driver (form, REQ_NEXT_FIELD);
	  form_driver (form, REQ_END_LINE);
	  break;

	case KEY_UP:
	  selected = !selected;
	  form_driver (form, REQ_PREV_FIELD);
	  form_driver (form, REQ_END_LINE);
	  break;

	case KEY_BACKSPACE:
	  form_driver (form, REQ_LEFT_CHAR);
	  form_driver (form, REQ_DEL_CHAR);

	default:
	  form_driver (form, ch);
	  form_driver (form, REQ_VALIDATION);
	  break;
	}
    }
exit:
  strcpy (username, field_buffer (field[0], 0));
  strcpy (password, field_buffer (field[1], 0));
  trim (username);
  trim (password);

  /* Un post form and free the memory */
  unpost_form (form);
  free_form (form);

  sp_session_login (g_session, username, password, true, NULL);

  free_field (field[0]);
  free_field (field[1]);

  transition_to (STATUS_LOGGING_IN);
  msg_to_user ("logging in..");

  curs_set (0);

  return 0;
}

static int
automatic_login ()
{
  char name_len[1];
  char name[256];
  char blob_len[1];
  char blob[256];

  FILE *in = fopen ("blob.dat", "r");
  if (in == NULL)
    goto fail;

  fread (name_len, 1, 1, in);
  if (name_len[0] > sizeof name - 1)
    goto fail;
  fread (name, 1, name_len[0], in);
  name[name_len[0]] = '\0';

  fread (blob_len, 1, 1, in);
  if (blob_len[0] > sizeof blob - 1)
    goto fail;
  fread (blob, 1, blob_len[0], in);
  blob[blob_len[0]] = '\0';

  fclose (in);

  sp_session_login (g_session, name, NULL, true, blob);
  transition_to (STATUS_LOGGING_IN);

  return 0;

 fail:
  if (in)
    fclose (in);
  transition_to (STATUS_LOGIN);
  return 0;
}

static int
logging_in ()
{
  usleep (250 * 1000);
}

void
search_complete (sp_search *result, void *userdata)
{
}

static int
search_album ()
{
  char buffer[64];
  if (read_line (buffer, sizeof (buffer), "Album: "))
    return STATUS_HOME;

  if (g_search)
    sp_search_release (g_search);

  g_search = sp_search_create (g_session, buffer, 0, 0, 0, 50,
			       0, 0, 0, 0, SP_SEARCH_STANDARD,
			       search_complete, g_search);

  return STATUS_SEARCH_BROWSE;
}

static int
search_artist ()
{
  char buffer[64];
  if (read_line (buffer, sizeof (buffer), "Artist: "))
    return STATUS_HOME;

  if (g_search)
    sp_search_release (g_search);

  g_search = sp_search_create (g_session, buffer, 0, 0, 0, 0,
			       0, 50, 0, 0, SP_SEARCH_STANDARD,
			       search_complete, g_search);

  return STATUS_SEARCH_BROWSE;
}

static int
search_playlist ()
{
  char buffer[64];
  if (read_line (buffer, sizeof (buffer), "Playlist: "))
    return STATUS_HOME;

  if (g_search)
    sp_search_release (g_search);

  g_search = sp_search_create (g_session, buffer, 0, 0, 0, 0,
			       0, 0, 0, 50, SP_SEARCH_STANDARD,
			       search_complete, g_search);

  return STATUS_SEARCH_BROWSE;
}

static int
whats_new ()
{
  if (g_search)
    sp_search_release (g_search);

  g_search = sp_search_create (g_session, "tag:new", 0, 15, 0, 15,
			       0, 15, 0, 15, SP_SEARCH_STANDARD,
			       search_complete, g_search);

  return STATUS_SEARCH_BROWSE;
}

static int
starred ()
{
  int i, ret;
  sp_playlist *starred = sp_session_starred_create (g_session);
  time_t start = time (NULL);
  if (starred == NULL)
    return STATUS_HOME;

  while (!sp_playlist_is_loaded (starred))
    {
      int to;
      sp_session_process_events (g_session, &to);
      if (time (NULL) - start > TIMEOUT)
	{
	  sp_playlist_release (starred);
	  return STATUS_HOME;
	}
      usleep (min (to, 250) * 1000);
    }

  ret = sp_playlist_num_tracks (starred);

  free_search_results (g_search_results);
  g_search_results = calloc ((ret + 1) * sizeof (struct search_result), 1);
  for (i = 0; i < ret; i++)
    {
      g_search_results[i].type = TYPE_TRACK;
      g_search_results[i].track = sp_playlist_track (starred, i);
      sp_track_add_ref (g_search_results[i].track);
    }

  sp_playlist_release (starred);
  return STATUS_BROWSE_SHOW;
}

static struct search_result *
playlists ()
{
  int i, n_playlists;
  time_t start = time (NULL);
  struct search_result *res;
  sp_playlistcontainer *pc = sp_session_playlistcontainer (g_session);
  if (pc == NULL)
    return NULL;
  while (!sp_playlistcontainer_is_loaded (pc))
    {
      int to;
      sp_session_process_events (g_session, &to);
      if (time (NULL) - start > TIMEOUT)
        return NULL;
      usleep (min (to, 250) * 1000);
    }

  n_playlists = sp_playlistcontainer_num_playlists (pc);

  res = calloc ((n_playlists + 1) * sizeof (struct search_result), 1);

  for (i = 0; i < n_playlists; i++)
    {
      sp_playlist *pl;
      switch (sp_playlistcontainer_playlist_type (pc, i))
	{

	case SP_PLAYLIST_TYPE_PLAYLIST:
	  pl = sp_playlistcontainer_playlist (pc, i);
	  sp_playlist_add_ref (pl);

	  res[i].type = TYPE_PLAYLIST;
	  res[i].playlist = pl;
	  break;

	case SP_PLAYLIST_TYPE_START_FOLDER:
	  sp_playlistcontainer_playlist_folder_name (pc, i,
						     res
						     [i].folder,
						     sizeof (res[i].folder));
	  res[i].type = TYPE_PLAYLISTCONTAINER_START;
	  break;

	case SP_PLAYLIST_TYPE_END_FOLDER:
	  res[i].type = TYPE_PLAYLISTCONTAINER_END;
	  break;
	}
    }

  return res;
}

static int
playlists_handler ()
{
  struct search_result *sr = playlists ();
  if (sr == NULL)
    return STATUS_HOME;

  free_search_results (g_search_results);
  g_search_results = sr;

  return STATUS_BROWSE_SHOW_PLAYLISTS;
}

static int
exit_application ()
{
  sp_session_logout (g_session);
  sp_session_player_play (g_session, false);
  delwin (content_wnd);
  delwin (g_mainwin);
  endwin ();
  sound_clean ();
  _exit (0);
}

static void
atexit_cleanup ()
{
  exit_application ();
}

static int
show_playing ()
{
  int tmp, off;
  sp_track *last_showed_track = NULL;
  sp_error err;

  do
    {
      g_current_track = queue_get_next (g_play_queue);
      if (g_current_track == NULL)
        {
          transition_to (STATUS_HOME);
          return 0;
        }

      err = sp_session_player_load (g_session, g_current_track);
    }
  while (err == SP_ERROR_TRACK_NOT_PLAYABLE);

  sp_session_player_play (g_session, true);
  g_paused = 0;
  g_elapsed_frames = 0;

  while (1)
    {
      int to, c, skip_track = 0;
      sp_track *to_star[1];

      if (last_showed_track != g_current_track || force_redraw)
	{
	  sp_album *album = sp_track_album (g_current_track);
	  const byte *data = sp_album_cover (album, SP_IMAGE_SIZE_NORMAL);
	  if (data)
            {
              sp_image *i = sp_image_create (g_session, data);
              if (sp_image_is_loaded (i))
                {
                  FILE *memstream;
                  size_t l;
                  data = sp_image_data (i, &l);
                  memstream = fmemopen ((char *) data, l, "rb");
                  img_show_art (memstream);
                  fclose (memstream);
                  last_showed_track = g_current_track;
                  force_redraw = false;
                }
            }
	}
      sp_session_process_events (g_session, &to);

      if (g_sample_rate)
	{
	  int i, bar_len, elapsed_seconds, duration_seconds;
          sp_artist *artist;
          const char *tmp;
	  elapsed_seconds = g_elapsed_frames / g_sample_rate;
	  duration_seconds = sp_track_duration (g_current_track) / 1000;
	  bar_len = g_w - 2 * (3 + 6);

	  attrset (COLOR_PAIR (COLOR_SEEK_BAR_ELAPSED));
	  mvhline (g_h - 4, 3 + 6, '*', bar_len * elapsed_seconds / duration_seconds);

	  attrset (COLOR_PAIR (COLOR_SEEK_BAR_FUTURE));
	  mvhline (g_h - 4, 3 + 6 + bar_len * elapsed_seconds / duration_seconds, '*',
		   bar_len * (duration_seconds - elapsed_seconds) / duration_seconds);


	  attrset (COLOR_PAIR (COLOR_DEFAULT));
	  mvprintw (g_h - 4, 3, "%.2i:%.2i",
		    elapsed_seconds / 60, elapsed_seconds % 60);
	  mvprintw (g_h - 4, g_w - 3 - 6, "%.2i:%.2i",
		    duration_seconds / 60, duration_seconds % 60);

          tmp = sp_track_name (g_current_track);
          i = g_w / 2 - strlen (tmp) / 2;
	  mvprintw (g_h - 3, i, "%s", tmp);
          print_star (g_h - 3, i - 1, sp_track_is_starred (g_session, g_current_track));

          artist = sp_track_artist (g_current_track, 0);
          if (artist)
            {
              tmp = sp_artist_name (artist);
              mvprintw (g_h - 2, g_w / 2 - strlen (tmp) / 2, "%s", tmp);
            }
	  move (0, 0);
	}

      nodelay (g_mainwin, true);
      switch ((c = getch ()))
	{
	case KEY_LEFT:
	  g_elapsed_frames -= 10 * g_sample_rate;
	  if (g_elapsed_frames < 0)
	    g_elapsed_frames = 0;
          if (g_sample_rate)
            g_seek_off = g_elapsed_frames / g_sample_rate * 1000;
	  assert (g_seek_off >= 0);

	  g_paused = false;
          sound_pause (g_paused);

	  sp_session_player_play (g_session, false);
	  sp_session_player_seek (g_session, g_seek_off);
	  sp_session_player_play (g_session, true);
	  break;

	case KEY_RIGHT:
	  tmp = g_elapsed_frames + 10 * g_sample_rate;
          if (g_sample_rate)
            g_seek_off = tmp / g_sample_rate * 1000;
	  assert (g_seek_off >= 0);

	  g_paused = false;
          sound_pause (g_paused);

	  sp_session_player_play (g_session, false);
	  sp_session_player_seek (g_session, g_seek_off);
	  sp_session_player_play (g_session, true);
	  break;

	case KEY_DOWN:
	  skip_track = 1;
	  break;

	case 27:
	  sp_session_player_play (g_session, false);
	  nodelay (g_mainwin, false);
	  transition_to (STATUS_HOME);
	  return STATUS_HOME;

	case ' ':
	  g_paused = !g_paused;
          sound_pause (g_paused);
	  sp_session_player_play (g_session, !g_paused);
	  break;

          /* Star/Unstar.  */
        case 'u':
        case 's':
          to_star[0] = g_current_track;
          sp_track_set_starred (g_session, to_star, 1, c == 's');
          break;
	}
      nodelay (g_mainwin, false);

      if (g_end_of_track || skip_track)
        {
          sp_session_player_play (g_session, false);
          do
            {
              sp_track_release (g_current_track);
              g_current_track = queue_get_next (g_play_queue);
              if (g_current_track == NULL)
                return STATUS_HOME;

              err = sp_session_player_load (g_session, g_current_track);
            }
          while (err == SP_ERROR_TRACK_NOT_PLAYABLE);
          g_end_of_track = 0;
          sp_session_player_play (g_session, true);
          reset_screen ();
          g_elapsed_frames = 0;
        }

      usleep (min (to, 250) * 1000);
    }

  return STATUS_HOME;
}

static int
logout ()
{
  unlink ("blob.dat");
  sp_session_forget_me (g_session);
  sp_session_logout (g_session);
  return STATUS_NOT_LOGGED;
}

static int
show_menu ()
{
  struct
  {
    const char *name;
    int (*handler) ();
  } g_menu_choices[] =
      {
        {
          "Search Album", search_album},
        {
          "Search Artist", search_artist},
        {
          "Search Playlist", search_playlist},
        {
          "What's new", whats_new},
        {
          "Starred", starred},
        {
          "Playlists", playlists_handler},
        {
          "Logout", logout},
        {
          "Exit", exit_application}};
  int next_status;
  MENU *menu;
  WINDOW *list_wnd = subwin (g_mainwin, 20, g_h - 2, 1, g_w / 2 - 10);
  int i, c;
  static int last_selected_item = 0;
  int n_choices = ARRAY_SIZE (g_menu_choices);
  ITEM **items = (ITEM **) calloc (n_choices + 1, sizeof (ITEM *));

  reset_screen ();
  for (i = 0; i < n_choices; ++i)
    items[i] = new_item (g_menu_choices[i].name, g_menu_choices[i].name);
  items[n_choices] = NULL;

  menu = new_menu ((ITEM **) items);
  menu_opts_off (menu, O_SHOWDESC);
  set_menu_format (menu, 10, 0);
  set_menu_mark (menu, " ");
  set_menu_win (menu, list_wnd);
  set_current_item (menu, items[last_selected_item]);
  post_menu (menu);
  refresh ();

  wcursyncup (list_wnd);

  for (;;)
    {
      c = getch ();
      switch (c)
	{
        case KEY_RIGHT:
        case '\n':
          goto exit;

	case KEY_DOWN:
	  menu_driver (menu, REQ_DOWN_ITEM);
	  break;

	case KEY_UP:
	  menu_driver (menu, REQ_UP_ITEM);
	  break;
	}

      wrefresh (list_wnd);
      refresh ();
    }
 exit:
  last_selected_item = item_index (current_item (menu));
  next_status = g_menu_choices[last_selected_item].handler ();

  for (i = 0; i < n_choices; ++i)
    free_item (items[i]);

  free_menu (menu);
  delwin (list_wnd);

  if (next_status)
    transition_to (next_status);
}

static void
free_search_results (struct search_result *sr)
{
  struct search_result *it = sr;
  if (!sr)
    return;
  while (it->type != TYPE_LAST)
    {
      switch (it->type)
	{
	case TYPE_ARTIST:
	  sp_artist_release (it->artist);
	  break;

	case TYPE_TRACK:
	  sp_track_release (it->track);
	  break;

	case TYPE_PLAYLIST:
	  sp_playlist_release (it->playlist);
	  break;

	case TYPE_ALBUM:
	  sp_album_release (it->album);
	  break;
	}
      it++;
    }
}

static const char *
search_result_get_name (struct search_result *sr)
{
  switch (sr->type)
    {
    case TYPE_ARTIST:
      if (!sp_artist_is_loaded (sr->artist))
	return "<loading>";
      return sp_artist_name (sr->artist);

    case TYPE_TRACK:
      if (!sp_track_is_loaded (sr->track))
	return "<loading>";
      return sp_track_name (sr->track);

    case TYPE_PLAYLIST:
      if (!sp_playlist_is_loaded (sr->playlist))
	return "<loading>";
      return sp_playlist_name (sr->playlist);

    case TYPE_PLAYLISTCONTAINER_START:
      return sr->folder;
      break;

    case TYPE_PLAYLISTCONTAINER_END:
      return " ";
      break;

    case TYPE_ALBUM:
      if (!sp_album_is_loaded (sr->album))
	return "<loading>";
      return sp_album_name (sr->album);
    }

  return NULL;
}

static void
add_track_to_playlist (sp_track *track)
{
  sp_track *tracks[1];
  sp_playlist *pl = choose_playlist ();
  if (pl == NULL)
    return;

  tracks[0] = track;
  sp_playlist_add_tracks (pl, tracks, 1, sp_playlist_num_tracks (pl), g_session);
}

static int
search_result_select (struct search_result *sr, struct search_result *selected)
{
  selected->type = TYPE_LAST;

  if (sr->type == TYPE_TRACK)
    {
      queue_play_with_future (g_play_queue, sr);
      return STATUS_PLAYING;
    }

  *selected = *sr;

  switch (sr->type)
    {
    case TYPE_ARTIST:
      sp_artist_add_ref (sr->artist);
      break;

    case TYPE_PLAYLIST:
      sp_playlist_add_ref (sr->playlist);
      break;

    case TYPE_ALBUM:
      sp_album_add_ref (sr->album);
      break;
    }

  return 0;
}

static int
search_results_display (struct search_result *results, WINDOW *list_wnd, int offset_x,
                        struct search_result *selected)
{
  struct search_result *sr;
  size_t size;
  int x;
  int y;
  MENU *menu;
  int i, j, c, selected_item = 0;
  int level;
  ITEM **items;
  char **items_name;
  int next_status;
  bool is_playlists = g_status == STATUS_BROWSE_SHOW_PLAYLISTS;

 restart:
  sr = results;
  size = 0;
  level = 0;
  next_status = STATUS_BROWSE_RESULT;

  while (sr->type)
    {
      size++;
      sr++;
    }

  items = (ITEM **) malloc ((size + 1) * sizeof (ITEM *));
  items_name = (char **) malloc ((size + 1) * sizeof (char *));

  sr = results;
  for (i = 0; i < size; ++i)
    {
      char *name;
      char buffer[256];
      const char *item_name;
      for (j = 0; j < level; j++)
	buffer[j] = ' ';

      buffer[j] = '\0';

      level += (sr[i].type == TYPE_PLAYLISTCONTAINER_START ? 1 : 0)
	+ (sr[i].type == TYPE_PLAYLISTCONTAINER_END ? -1 : 0);

      item_name = search_result_get_name (&sr[i]);

      strncat (buffer, item_name, sizeof (buffer) - j - 1);
      buffer[(sizeof buffer) - 1] = '\0';
      name = strdup (buffer);

      items_name[i] = (char *) name;
      items[i] = new_item (items_name[i], items_name[i]);
    }
  items[size] = NULL;

  menu = new_menu ((ITEM **) items);
  menu_opts_off (menu, O_SHOWDESC);

  set_menu_format (menu, g_h - 2, 0);
  set_menu_mark (menu, " ");
  set_menu_win (menu, list_wnd);
  post_menu (menu);
  wcursyncup (list_wnd);

  set_current_item (menu, items[selected_item]);
  move (1 + selected_item, offset_x);
  wrefresh (list_wnd);

  nodelay (g_mainwin, true);

  for (;;)
    {
      c = getch ();
      int next_timeout, cur_x, cur_y;
      sp_session_process_events (g_session, &next_timeout);
      sp_track *to_star[1];
      char buffer[32];
      if (c == 'D' && g_browsed_playlist
          && read_line (buffer, sizeof (buffer), "Are you sure (type yes)?: ") == 0
          && strcasecmp (buffer, "yes") == 0)
        {
          selected_item = item_index (current_item (menu));
          if (sr[selected_item].type = TYPE_TRACK)
            {
              int tracks[1];
              tracks[0] = selected_item;
              sp_playlist_remove_tracks (g_browsed_playlist, tracks, 1);
              g_result_to_browse.type = TYPE_PLAYLIST;
              g_result_to_browse.playlist = g_browsed_playlist;
              next_status = STATUS_BROWSE_RESULT;
              goto exit_no_handler;

            }
        }

      if (is_playlists)
        {
          sp_playlistcontainer *pc;
          char buffer[32];
          switch (c)
            {
            case 'n':
              if (! read_line (buffer, sizeof (buffer), "New playlist: "))
                {
                  pc = sp_session_playlistcontainer (g_session);
                  if (pc)
                    {
                      sp_playlist *pl = sp_playlistcontainer_add_new_playlist (pc, buffer);
                      struct search_result *sr = playlists ();
                      if (sr)
                        {
                          free_search_results (g_search_results);
                          g_search_results = sr;
                        }
                      sp_playlist_release (pl);
                      goto restart;
                    }
                }
              break;

            case 'D':
              selected_item = item_index (current_item (menu));
              pc = sp_session_playlistcontainer (g_session);

              if (pc && sr[selected_item].type == TYPE_PLAYLIST)
                {
                  if (read_line (buffer, sizeof (buffer), "Are you sure (type yes)?: ") == 0
                      && strcasecmp (buffer, "yes") == 0)
                    {
                      struct search_result *sr = playlists ();
                      sp_playlistcontainer_remove_playlist (pc, selected_item);
                      sr = playlists ();
                      if (sr)
                        {
                          free_search_results (g_search_results);
                          g_search_results = sr;
                        }
                    }
                  goto restart;
                }
              break;
            }
        }

      switch (c)
	{
        case '\n':
	case KEY_RIGHT:
          goto exit;

	case ERR:
	  if (g_force_refresh)
	    goto exit;

	  usleep (100000);
	  break;

          /* Add to playlist.  */
        case 'a':
          selected_item = item_index (current_item (menu));
          if (selected_item >= 0 && sr[selected_item].type == TYPE_TRACK)
            add_track_to_playlist (sr[selected_item].track);

          /* FIXME: A nicer way to redraw the menu?  */
          unpost_menu (menu);
          post_menu (menu);
          break;

          /* Star/Unstar.  */
        case 'u':
        case 's':
          selected_item = item_index (current_item (menu));
          if (selected_item >= 0 && sr[selected_item].type == TYPE_TRACK)
            {
              to_star[0] = sr[selected_item].track;
              sp_track_set_starred (g_session, to_star, 1, c == 's');
            }
          break;

	case KEY_DOWN:
	  menu_driver (menu, REQ_DOWN_ITEM);
	  break;

	case KEY_UP:
	  menu_driver (menu, REQ_UP_ITEM);
	  break;

	case KEY_LEFT:
	case 27:		/* ESCAPE */
	  next_status = STATUS_HOME;
	  goto exit_no_handler;
	}

      wrefresh (list_wnd);
      getyx (list_wnd, cur_y, cur_x);

      /* Find the first visible item.  */
      for (i = 0; i < size; i++)
        if (item_visible (items[i]))
          break;

      for (j = 0; sr[i + j].type && j < g_h - 2; j++)
        {
          if (sr[i + j].type == TYPE_TRACK)
            {
              bool starred;
              starred = sp_track_is_starred (g_session, sr[i + j].track);
              print_star (j + 1, offset_x - 1, starred);
            }
        }

      move (cur_y + 1, cur_x + offset_x);
    }

 exit:
  selected_item = item_index (current_item (menu));
  if (selected_item < 0)
    selected_item = 0;

  c = search_result_select (&sr[selected_item], selected);
  if (c)
    next_status = c;

 exit_no_handler:
  nodelay (g_mainwin, false);

  unpost_menu (menu);
  free_menu (menu);

  for (i = 0; i < size; ++i)
    {
      free_item (items[i]);
      free (items_name[i]);
    }

  if (g_force_refresh)
    {
      g_force_refresh = 0;
      goto restart;
    }

  if (g_browsed_playlist)
    {
      sp_playlist_release (g_browsed_playlist);
      g_browsed_playlist = NULL;
    }

  if (next_status == STATUS_BROWSE_RESULT
      && selected->type == TYPE_PLAYLIST)
    next_status = STATUS_SEARCH_BROWSE_PLAYLIST;


  return next_status;
}

static sp_playlist *
choose_playlist ()
{
  int res;
  int w = g_w < 40 ? 20 : g_w < 80 ? 40 : 60;
  int off_x = g_w / 2 - w / 2 + 1;
  WINDOW *wnd;
  struct search_result selected, *sr = playlists ();
  if (sr == NULL)
    return NULL;

  wnd = subwin (g_mainwin, g_h - 2, w, 1, off_x);
  res = search_results_display (sr, wnd, off_x, &selected);
  delwin (wnd);
  free_search_results (sr);

  return selected.type == TYPE_PLAYLIST ? selected.playlist : NULL;
}

static void
search_results_handler ()
{
  int res;
  int w = g_w < 40 ? 20 : g_w < 80 ? 40 : 60;
  int off_x = g_w / 2 - w / 2 + 1;
  WINDOW *wnd = subwin (g_mainwin, g_h - 2, w, 1, off_x);
  res = search_results_display (g_search_results, wnd, off_x, &g_result_to_browse);
  delwin (wnd);

  if (res)
    transition_to (res);
}

static void
show_search_results ()
{
  time_t start = time (NULL);

  if (!g_search)
    {
      transition_to (STATUS_HOME);
      return;
    }

  while (!sp_search_is_loaded (g_search))
    {
      int to;
      sp_session_process_events (g_session, &to);
      if (time (NULL) - start > TIMEOUT)
	{
	  transition_to (STATUS_HOME);
	  sp_search_release (g_search);
	  return;
	}
      usleep (min (to, 250) * 1000);
    }

  size_t n_el, i, j;
  n_el = sp_search_num_tracks (g_search)
    + sp_search_num_albums (g_search)
    + sp_search_num_playlists (g_search) + sp_search_num_artists (g_search);

  free_search_results (g_search_results);
  g_search_results = calloc ((n_el + 1) * sizeof (struct search_result), 1);
  i = 0;
  for (j = 0; j < sp_search_num_tracks (g_search); j++)
    {
      g_search_results[i].type = TYPE_TRACK;
      g_search_results[i].track = sp_search_track (g_search, j);
      sp_track_add_ref (g_search_results[i++].track);
    }

  for (j = 0; j < sp_search_num_albums (g_search); j++)
    {
      g_search_results[i].type = TYPE_ALBUM;
      g_search_results[i].album = sp_search_album (g_search, j);
      sp_album_add_ref (g_search_results[i++].album);
    }

  for (j = 0; j < sp_search_num_playlists (g_search); j++)
    {
      g_search_results[i].type = TYPE_PLAYLIST;
      g_search_results[i].playlist = sp_search_playlist (g_search, j);
      sp_playlist_add_ref (g_search_results[i++].playlist);
    }

  for (j = 0; j < sp_search_num_artists (g_search); j++)
    {
      g_search_results[i].type = TYPE_ARTIST;
      g_search_results[i].artist = sp_search_artist (g_search, j);
      sp_artist_add_ref (g_search_results[i++].artist);
    }

  sp_search_release (g_search);

  g_search = NULL;

  transition_to (STATUS_BROWSE_SHOW);
}

void
artistbrowse_complete (sp_artistbrowse * result, void *userdata)
{

}

void
albumbrowse_complete (sp_albumbrowse * result, void *userdata)
{

}

static void
show_browse_result ()
{
  sp_artistbrowse *arb;
  sp_albumbrowse *alb;
  size_t ret, i, j;
  time_t start = time (NULL);

  free_search_results (g_search_results);
  g_search_results = NULL;

  switch (g_result_to_browse.type)
    {
    case TYPE_ARTIST:
      if (!sp_artist_is_loaded (g_result_to_browse.artist))
	return;

      arb = sp_artistbrowse_create (g_session, g_result_to_browse.artist,
				    SP_ARTISTBROWSE_FULL,
				    artistbrowse_complete, g_search_results);
      while (!sp_artistbrowse_is_loaded (arb))
	{
	  int to;
	  sp_session_process_events (g_session, &to);
	  if (time (NULL) - start > TIMEOUT)
	    return;
	  usleep (min (to, 250) * 1000);
	}
      ret = sp_artistbrowse_num_tracks (arb)
	+ sp_artistbrowse_num_albums (arb);

      g_search_results =
	calloc ((ret + 1) * sizeof (struct search_result), 1);
      i = 0;
      for (j = 0; j < sp_artistbrowse_num_albums (arb); j++)
	{
	  g_search_results[i].type = TYPE_ALBUM;
	  g_search_results[i].album = sp_artistbrowse_album (arb, j);
	  sp_album_add_ref (g_search_results[i].album);
	}

      for (j = 0; j < sp_artistbrowse_num_tracks (arb); j++)
	{
	  g_search_results[i].type = TYPE_TRACK;
	  g_search_results[i].track = sp_artistbrowse_track (arb, j);
	  sp_track_add_ref (g_search_results[i++].track);
	}
      sp_artistbrowse_release (arb);
      sp_artist_release (g_result_to_browse.artist);
      break;

    case TYPE_ALBUM:
      if (!sp_album_is_loaded (g_result_to_browse.album))
	return;

      alb = sp_albumbrowse_create (g_session, g_result_to_browse.album,
				   albumbrowse_complete, g_search_results);
      while (!sp_albumbrowse_is_loaded (alb))
	{
	  int to;
	  sp_session_process_events (g_session, &to);
	  if (time (NULL) - start > TIMEOUT)
	    return;
	  usleep (min (to, 250) * 1000);
	}

      ret = sp_albumbrowse_num_tracks (alb);

      g_search_results =
	calloc ((ret + 1) * sizeof (struct search_result), 1);
      i = 0;
      for (j = 0; i < ret; j++)
	{
	  g_search_results[i].type = TYPE_TRACK;
	  g_search_results[i].track = sp_albumbrowse_track (alb, j);
	  sp_track_add_ref (g_search_results[i++].track);
	}

      sp_albumbrowse_release (alb);
      sp_album_release (g_result_to_browse.album);
      break;

    case TYPE_PLAYLIST:
      while (!sp_playlist_is_loaded (g_result_to_browse.playlist))
	{
	  int to;
	  sp_session_process_events (g_session, &to);
	  if (time (NULL) - start > TIMEOUT)
	    return;
	  usleep (min (to, 250) * 1000);
	}

      ret = sp_playlist_num_tracks (g_result_to_browse.playlist);

      g_search_results =
	calloc ((ret + 1) * sizeof (struct search_result), 1);
      i = 0;
      for (j = 0; i < ret; j++)
	{
	  g_search_results[i].type = TYPE_TRACK;
	  g_search_results[i].track =
	    sp_playlist_track (g_result_to_browse.playlist, j);
	  sp_track_add_ref (g_search_results[i++].track);
	}

      /*FIXME: keep the whole sr, not just playlist.  */
      g_browsed_playlist = g_result_to_browse.playlist;
      sp_playlist_add_ref (g_browsed_playlist);

      sp_playlist_release (g_result_to_browse.playlist);
      break;
    }

  transition_to (g_search_results ? STATUS_BROWSE_SHOW : STATUS_HOME);
  g_result_to_browse.type = TYPE_LAST;
}

static int
main_loop ()
{
  int next_timeout;
  for (;;)
    {
      if (g_status != STATUS_NOT_LOGGED)
	sp_session_process_events (g_session, &next_timeout);

      switch (g_status)
	{
	case STATUS_NOT_LOGGED:
	  transition_to (STATUS_AUTOMATIC_LOGIN);
	  break;

	case STATUS_AUTOMATIC_LOGIN:
	  automatic_login ();
	  break;

	case STATUS_LOGIN:
	  login ();
	  break;

	case STATUS_LOGGING_IN:
	  logging_in ();
	  break;

	case STATUS_HOME:
	  show_menu ();
	  break;

	case STATUS_SEARCH_BROWSE:
	  show_search_results ();
	  break;

	case STATUS_BROWSE_SHOW:
	case STATUS_BROWSE_SHOW_PLAYLISTS:
	  search_results_handler ();
	  break;

	case STATUS_BROWSE_RESULT:
	case STATUS_SEARCH_BROWSE_PLAYLIST:
	  show_browse_result ();
	  break;

	case STATUS_PLAYING:
	  show_playing ();
	  break;

	default:
	  exit (EXIT_FAILURE);
	}
    }
}

static void
logged_in (sp_session *session, sp_error error)
{
  if (error == SP_ERROR_OK)
    transition_to (STATUS_HOME);
  else
    transition_to (STATUS_LOGIN);
}

static void
message_to_user (sp_session *session, const char *message)
{
  msg_to_user (message);
}

static void
log_message (sp_session *session, const char *data)
{
  if (g_debug)
    msg_to_user (data);
}

static void
metadata_updated (sp_session *session)
{
  g_force_refresh = 1;
}

static int
music_delivery (sp_session *session, const sp_audioformat * format,
                const void *frames, int num_frames)
{
  if (num_frames == 0)
    {
      if (g_seek_off >= 0)
	g_elapsed_frames = g_seek_off / 1000 * g_sample_rate;
      g_seek_off = -1;
      sound_write (frames, num_frames);
      return 0;
    }

  g_elapsed_frames += num_frames;
  g_sample_rate = format->sample_rate;
  return sound_write (frames, num_frames);
}

static void
end_of_track (sp_session *session)
{
  g_end_of_track = 1;
}

static void
play_token_lost (sp_session *session)
{
  msg_to_user ("Play token lost");
  sound_flush ();
}


static void
start_playback (sp_session *session)
{
}

static void
stop_playback (sp_session *session)
{
}

static void
get_audio_buffer_stats (sp_session *session, sp_audio_buffer_stats *stats)
{
  stats->samples = sound_get_buffer ();
}

static void
credentials_blob_updated (sp_session *session, const char *blob)
{
  const char *username;
  FILE *out = fopen ("blob.dat", "w+");
  if (out == NULL)
    return;

  username = sp_user_canonical_name (sp_session_user (g_session));

  fprintf (out, "%c%s%c%s", strlen (username), username, strlen (blob), blob);
  fclose (out);
}

static unsigned char *g_appkey = NULL;

static void
init_session ()
{
  extern const unsigned char g_appkey_[];
  extern const size_t g_appkey_size;

  sp_session_callbacks callbacks;
  sp_session_config config;

  if (g_appkey == NULL)
    {
      g_appkey = malloc (g_appkey_size);
      memcpy (g_appkey, g_appkey_, g_appkey_size);
      g_appkey[0]--;
      g_appkey[1]--;
      g_appkey[2]--;
    }

  memset (&callbacks, 0, sizeof callbacks);
  memset (&config, 0, sizeof config);

  callbacks.logged_in = logged_in;
  callbacks.credentials_blob_updated = credentials_blob_updated;
  callbacks.message_to_user = message_to_user;
  callbacks.log_message = log_message;
  callbacks.metadata_updated = metadata_updated;
  callbacks.end_of_track = end_of_track;
  callbacks.start_playback = start_playback;
  callbacks.stop_playback = stop_playback;
  callbacks.get_audio_buffer_stats = get_audio_buffer_stats;
  callbacks.music_delivery = music_delivery;
  callbacks.play_token_lost = play_token_lost;
  config.api_version = 12;
  config.cache_location = "cache";
  config.settings_location = "settings";
  config.application_key = g_appkey;
  config.application_key_size = g_appkey_size;
  config.user_agent = "libspotify";
  config.callbacks = &callbacks;

  g_search = NULL;
  g_search_results = NULL;
  g_result_to_browse.type = TYPE_LAST;
  sp_session_create (&config, &g_session);
  g_play_queue = queue_make (g_session, 128);
}

static int
reset_graphics (bool resize)
{
  if (resize)
    {
      endwin();
      refresh ();
      clear ();
    }

  cbreak ();
  noecho ();
  keypad (stdscr, TRUE);

  start_color ();
  if (has_colors () && COLOR_PAIRS >= 8)
    {
      int i;
      init_pair (COLOR_DEFAULT, COLOR_GREEN, COLOR_BLACK);
      init_pair (COLOR_MESSAGE, COLOR_BLACK, COLOR_YELLOW);
      init_pair (COLOR_INPUT, COLOR_GREEN, COLOR_RED);
      init_pair (COLOR_SEEK_BAR_ELAPSED, COLOR_BLACK, COLOR_MAGENTA);
      init_pair (COLOR_SEEK_BAR_FUTURE, COLOR_BLACK, COLOR_YELLOW);
      init_pair (COLOR_STAR, COLOR_YELLOW, COLOR_BLACK);

      for (i = 0; i < COLORS; i++)
	init_pair (i + COLOR_MAX, i, i);
    }

  getmaxyx (g_mainwin, g_h, g_w);

  img_initialize_palette ();
  color_set (1, NULL);

  content_wnd = subwin (g_mainwin, g_w - 2, g_h - 2, 1, 1);

  curs_set (0);

  force_redraw = true;
}

static void
on_sigwinch ()
{
  reset_graphics (true);
}

int
main (int argc, char *const *argv)
{
  int opt;
  setlocale (LC_ALL, "");
  if ((g_mainwin = initscr ()) == NULL)
    {
      fprintf (stderr, "Error loading ncurses.\n");
      exit (EXIT_FAILURE);
    }

  if (sound_init () < 0)
    {
      fprintf (stderr, "Error loading the sound driver.\n");
      exit (EXIT_FAILURE);
    }

  while ((opt = getopt (argc, argv, "d")) >= 0)
    {
      switch (opt)
	{
	case 'd':
	  g_debug = 1;
	  break;
	}
    }

  init_wd ();
  atexit (atexit_cleanup);
  g_status = STATUS_NOT_LOGGED;

  reset_graphics (false);
  signal(SIGWINCH, on_sigwinch);

  init_session ();

  main_loop ();

  return EXIT_SUCCESS;
}
