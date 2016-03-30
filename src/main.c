/* http://equalarea.com/paul/alsa-audio.html */
/* http://www.alsa-project.org/alsa-doc/alsa-lib/pcm.html */


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <sys/types.h>
#include <alsa/asoundlib.h>


#define PERROR(__s) \
 do { printf("%s,%u: %s\n", __FILE__, __LINE__, __s); } while(0)

#define PERROR_GOTO(__s, __l) \
 do { PERROR(__s); goto __l; } while (0)


static volatile unsigned int is_sigint = 0;

static void on_sigint(int n)
{
  is_sigint = 1;
}


/* cmdline */

enum cmdline_id
{
  CMDLINE_ID_REC = 0,
  CMDLINE_ID_PLAY,
  CMDLINE_ID_IPCM,
  CMDLINE_ID_OPCM,
  CMDLINE_ID_DUR,
  CMDLINE_ID_INVALID = 32
};

#define CMDLINE_FLAG(__i) (1 << (uint32_t)(CMDLINE_ID_ ## __i))

typedef struct
{
  uint32_t flags;
  const char* ipcm;
  const char* opcm;
  unsigned int dur_ms;
} cmdline_t;

static int get_cmdline(cmdline_t* cmd, int ac, char** av)
{
  size_t i;

  cmd->flags = 0;
  cmd->ipcm = NULL;
  cmd->opcm = NULL;
  cmd->dur_ms = 0;

  if ((ac % 2)) goto on_error;

  for (i = 0; i != ac; i += 2)
  {
    const char* const k = av[i + 0];
    const char* const v = av[i + 1];

    if (strcmp(k, "-do") == 0)
    {
      if (strcmp(v, "rec") == 0) cmd->flags |= CMDLINE_FLAG(REC);
      else if (strcmp(v, "play") == 0) cmd->flags |= CMDLINE_FLAG(PLAY);
      else goto on_error;
    }
    else if (strcmp(k, "-ipcm") == 0)
    {
      cmd->flags |= CMDLINE_FLAG(IPCM);
      cmd->ipcm = v;
    }
    else if (strcmp(k, "-opcm") == 0)
    {
      cmd->flags |= CMDLINE_FLAG(OPCM);
      cmd->opcm = v;
    }
    else goto on_error;
  }

  return 0;

 on_error:
  return -1;
}


/* audio device */

static snd_pcm_t* open_pcm
(const cmdline_t* cmd, unsigned int is_in)
{
  const char* name;
  snd_pcm_hw_params_t* params;
  snd_pcm_stream_t s;
  snd_pcm_t* pcm;
  int err;

  if (is_in)
  {
    s = SND_PCM_STREAM_CAPTURE;
    if ((cmd->flags & CMDLINE_FLAG(IPCM)) == 0) name = "default";
    else name = cmd->ipcm;
  }
  else
  {
    s = SND_PCM_STREAM_PLAYBACK;
    if ((cmd->flags & CMDLINE_FLAG(OPCM)) == 0) name = "default";
    else name = cmd->opcm;
  }

  err = snd_pcm_open(&pcm, name, s, 0);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_0);

  err = snd_pcm_hw_params_malloc(&params);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_1);

  err = snd_pcm_hw_params_any(pcm, params);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_2);

  err = snd_pcm_hw_params_set_access
    (pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_2);

  err = snd_pcm_hw_params_set_format
    (pcm, params, SND_PCM_FORMAT_S16_LE);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_2);

  err = snd_pcm_hw_params_set_rate(pcm, params, 44100, 0);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_2);

  err = snd_pcm_hw_params_set_channels(pcm, params, 2);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_2);
  
  err = snd_pcm_hw_params(pcm, params);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_2);
  
  err = snd_pcm_prepare(pcm);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_2);

  snd_pcm_hw_params_free(params);

  return pcm;

 on_error_2:
  snd_pcm_hw_params_free(params);
 on_error_1:
  snd_pcm_close(pcm);
 on_error_0:
  return NULL;
  
}

static snd_pcm_t* open_in_pcm(const cmdline_t* cmd)
{
  static const unsigned int is_in = 1;
  return open_pcm(cmd, is_in);
}

static snd_pcm_t* open_out_pcm(const cmdline_t* cmd)
{
  static const unsigned int is_in = 0;
  return open_pcm(cmd, is_in);
}


/* main loop */
/* http://www.alsa-project.org/alsa-doc/alsa-lib
/* /_2test_2pcm_8c-example.html */

#if 0

static int recover_xrun(snd_pcm_t* pcm, int err)
{
  switch (err)
  {
  case -EPIPE:
    /* underrun */
    err = snd_pcm_prepare(pcm);
    if (err < 0) PERROR_GOTO(snd_strerror(err), on_error);
    break ;

  case -ESTRPIPE:
    while (1)
    {
      err = snd_pcm_resume(pcm);
      if (err != -EAGAIN) break ;
      usleep(1000000);
    }
    if (err < 0)
    {
      err = snd_pcm_prepare(pcm);
      if (err < 0) PERROR_GOTO(snd_strerror(err), on_error);
    }
    break ;

  default: break ;
  }

  return 0;
 on_error:
  return -1;
}

static int process_pcm(snd_pcm_t* in_pcm, snd_pcm_t* out_pcm)
{
  while (1)
  {
    wait_for_in_pcm(samples);
  }

  return 0;
}

#endif


/* main */

int main(int ac, char** av)
{
  snd_pcm_t* in_pcm;
  snd_pcm_t* out_pcm;
  int err;
#define IN_FRAME_COUNT 2048
#define IN_CHAN_COUNT 2
  int16_t in_buf[IN_FRAME_COUNT * IN_CHAN_COUNT];
  cmdline_t cmd;
  size_t i;

  if (get_cmdline(&cmd, ac - 1, av + 1)) goto on_error_0;

  in_pcm = open_in_pcm(&cmd);
  if (in_pcm == NULL) goto on_error_0;

  out_pcm = open_out_pcm(&cmd);
  if (out_pcm == NULL) goto on_error_1;
  
  signal(SIGINT, on_sigint);

  for (i = 0; is_sigint == 0; i += IN_FRAME_COUNT)
  {
    err = snd_pcm_readi(in_pcm, in_buf, IN_FRAME_COUNT);
    if (err != IN_FRAME_COUNT) PERROR_GOTO("", on_error_2);
    snd_pcm_writei(out_pcm, in_buf, IN_FRAME_COUNT);
  }

 on_error_2:
  snd_pcm_close(out_pcm);
 on_error_1:
  snd_pcm_close(in_pcm);
 on_error_0:
  return 0;
}
