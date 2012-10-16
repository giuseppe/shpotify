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

/* Use the newer ALSA API */
#define ALSA_PCM_NEW_HW_PARAMS_API

#include <alsa/asoundlib.h>

static snd_pcm_t *handle;
static snd_pcm_hw_params_t *params;
static unsigned int val;
static int dir;
static snd_pcm_uframes_t frames;

#define CHANNELS 2
#define RATE     44100
#define FRAMES   32

int
sound_init ()
{
  int rc;

  rc = snd_pcm_open (&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
  if (rc < 0)
    {
      fprintf (stderr, "unable to open pcm device: %s\n", snd_strerror (rc));
      return rc;
    }

  snd_pcm_hw_params_alloca (&params);

  snd_pcm_hw_params_any (handle, params);

  snd_pcm_hw_params_set_access (handle, params,
				SND_PCM_ACCESS_RW_INTERLEAVED);

  snd_pcm_hw_params_set_format (handle, params, SND_PCM_FORMAT_S16_LE);

  snd_pcm_hw_params_set_channels (handle, params, CHANNELS);

  val = RATE;
  snd_pcm_hw_params_set_rate_near (handle, params, &val, &dir);

  frames = FRAMES;
  snd_pcm_hw_params_set_period_size_near (handle, params, &frames, &dir);

  rc = snd_pcm_hw_params (handle, params);
  if (rc < 0)
    return rc;

  snd_pcm_hw_params_get_period_size (params, &frames, &dir);

  snd_pcm_hw_params_get_period_time (params, &val, &dir);

  return 0;
}

int
sound_flush ()
{
  snd_pcm_drop (handle);
  snd_pcm_prepare (handle);
  return 0;
}

int
sound_write (const char *buffer, int frames)
{
  int rc;

  if (frames == 0)
    return sound_flush ();

 restart:
  rc = snd_pcm_writei (handle, buffer, frames);
  if (rc == -EPIPE)
    {
      snd_pcm_prepare (handle);
      goto restart;
    }

  return rc;
}

int
sound_clean ()
{
  snd_pcm_drop (handle);
  snd_pcm_close (handle);

  return 0;
}
