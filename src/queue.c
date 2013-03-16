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

#include "queue.h"

#include <stdlib.h>

struct queue_s
{
  sp_session *session;

  size_t current;
  size_t current_add;
  size_t used;
  size_t length;
  sp_track *tracks[];
};

queue_t *
queue_make (sp_session * session, size_t len)
{
  queue_t *q = calloc (sizeof (struct queue_s)
		       + sizeof (sp_track *) * len, 1);
  if (q == NULL)
    return q;

  q->session = session;
  q->length = len;

  return q;
}

int
queue_add (queue_t *queue, sp_track * track)
{
  if (queue->used == queue->length)
    return 0;

  queue->used++;
  queue->tracks[queue->current_add] = track;
  sp_track_add_ref (track);
  queue->current_add = (queue->current_add + 1) % (queue->length);
}

static void
queue_free_list (queue_t *queue)
{
  sp_track *track;
  while ((track = queue_get_next (queue)))
    sp_track_release (track);
}

void
queue_free (queue_t *queue)
{
  queue_free_list (queue);
  free (queue);
}

sp_track *
queue_get_next (queue_t *queue)
{
  sp_track *track = queue_peek_next (queue, 0);
  if (track == NULL)
    return NULL;

  queue->used--;
  queue->current = (queue->current + 1) % queue->length;
  return track;
}

sp_track *
queue_peek_next (queue_t *queue, size_t start)
{
  if (queue->used <= start)
    return NULL;

  return queue->tracks[(queue->current + start) % queue->length];
}

void
queue_play_with_future (queue_t *queue, struct search_result *sr)
{
  queue_free_list (queue);
  while (sr->type)
    {
      if (sr->type == TYPE_TRACK)
	queue_add (queue, sr->track);

      sr++;
    }
}
