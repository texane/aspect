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

typedef struct
{
  snd_pcm_t* pcm;
  snd_pcm_hw_params_t* hw_params;
  snd_pcm_sw_params_t* sw_params;
  snd_pcm_channel_area_t* areas;

  size_t nchan;
  size_t wchan;
  size_t scale;

  uint8_t* buf;
  size_t rpos;
  size_t wpos;
  size_t nsampl;

} pcm_handle_t;


typedef struct
{
#define PCM_FLAG_IN (1 << 0)
#define PCM_FLAG_OUT (1 << 1)
  uint32_t flags;
  const char* name;
  size_t nchan;
  unsigned int fsampl;
} pcm_desc_t;


static void pcm_init_desc(pcm_desc_t* desc)
{
  desc->flags = 0;
  desc->name = "default";
  desc->nchan = 2;
  desc->fsampl = 44100;
}


static int pcm_open(pcm_handle_t* pcm, const pcm_desc_t* desc)
{
  const snd_pcm_format_t fmt = SND_PCM_FORMAT_S16_LE;
  snd_pcm_stream_t stm;
  int err;

  if (desc->flags & PCM_FLAG_IN) stm = SND_PCM_STREAM_CAPTURE;
  else stm = SND_PCM_STREAM_PLAYBACK;

  err = snd_pcm_open
    (&pcm->pcm, desc->name, stm, SND_PCM_NONBLOCK);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_0);

  err = snd_pcm_hw_params_malloc(&pcm->hw_params);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_1);

  err = snd_pcm_hw_params_any(pcm->pcm, pcm->hw_params);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_2);

  err = snd_pcm_hw_params_set_access
    (pcm->pcm, pcm->hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_2);

  err = snd_pcm_hw_params_set_format(pcm->pcm, pcm->hw_params, fmt);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_2);

  err = snd_pcm_hw_params_set_rate
    (pcm->pcm, pcm->hw_params, desc->fsampl, 0);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_2);

  pcm->nchan = desc->nchan;
  pcm->wchan = (size_t)snd_pcm_format_physical_width(fmt) / 8;
  pcm->scale = pcm->nchan * pcm->wchan;

  err = snd_pcm_hw_params_set_channels
    (pcm->pcm, pcm->hw_params, desc->nchan);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_2);
  
  err = snd_pcm_hw_params(pcm->pcm, pcm->hw_params);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_2);

  err = snd_pcm_sw_params_malloc(&pcm->sw_params);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_2);

  err = snd_pcm_sw_params_current(pcm->pcm, pcm->sw_params);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_3);

#if 1
  err = snd_pcm_sw_params_set_avail_min
    (pcm->pcm, pcm->sw_params, 4096);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_3);
#endif

#if 1
  err = snd_pcm_sw_params_set_start_threshold
    (pcm->pcm, pcm->sw_params, 0U);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_3);
#endif

  err = snd_pcm_sw_params(pcm->pcm, pcm->sw_params);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_3);
  
  err = snd_pcm_prepare(pcm->pcm);
  if (err) PERROR_GOTO(snd_strerror(err), on_error_3);

  pcm->rpos = 0;
  pcm->wpos = 0;
  pcm->nsampl = (size_t)desc->fsampl * 10;
  pcm->buf = malloc(pcm->nsampl * pcm->scale);
  if (pcm->buf == NULL) goto on_error_3;

  return 0;

 on_error_3:
  snd_pcm_sw_params_free(pcm->sw_params);
 on_error_2:
  snd_pcm_hw_params_free(pcm->hw_params);
 on_error_1:
  snd_pcm_close(pcm->pcm);
 on_error_0:
  return -1;
  
}


static void pcm_close(pcm_handle_t* pcm)
{
  free(pcm->buf);
  snd_pcm_hw_params_free(pcm->hw_params);
  snd_pcm_sw_params_free(pcm->sw_params);
  snd_pcm_close(pcm->pcm);
}


static int pcm_start(pcm_handle_t* pcm)
{
  return snd_pcm_start(pcm->pcm);
}


static int pcm_recover_xrun(pcm_handle_t* pcm, int err)
{
  switch (err)
  {
  case -EPIPE:
    /* underrun */
    err = snd_pcm_prepare(pcm->pcm);
    if (err < 0) PERROR_GOTO(snd_strerror(err), on_error);
    snd_pcm_start(pcm->pcm);
    break ;

  case -ESTRPIPE:
    while (1)
    {
      err = snd_pcm_resume(pcm->pcm);
      if (err != -EAGAIN) break ;
      usleep(10000);
    }

    if (err < 0)
    {
      err = snd_pcm_prepare(pcm->pcm);
      if (err < 0) PERROR_GOTO(snd_strerror(err), on_error);
    }

    snd_pcm_start(pcm->pcm);

    break ;

  default: break ;
  }

  return 0;
 on_error:
  return -1;
}

#if 0

static int process_pcm(snd_pcm_t* in_pcm, snd_pcm_t* out_pcm)
{
  while (1)
  {
    wait_for_in_pcm(samples);
  }

  return 0;
}


static snd_pcm_channel_area_t* alloc_buffer(size_t nchan, size_t nsampl)
{
  snd_pcm_channel_area_t* areas;
  size_t i;

  areas = malloc(nchan * sizeof(snd_pcm_channel_area_t));
  if (areas == NULL) return -1;

  for (i = 0; i != nchan; ++i)
  {
    areas[i].addr = 
  }

  

  unsigned int nchan;

  snd_pcm_hw_params_get_channels();

  const size_t nchan = ;

  for (chn = 0; chn < channels; chn++) {
    areas[chn].addr = samples;
    areas[chn].first = chn * snd_pcm_format_physical_width(format);
    areas[chn].step = channels * snd_pcm_format_physical_width(format);
  }
}

#endif


/* main */

int main(int ac, char** av)
{
  pcm_desc_t desc;
  pcm_handle_t ipcm;
  pcm_handle_t opcm;
  int err;
  cmdline_t cmd;
  size_t i;

  err = -1;

  if (get_cmdline(&cmd, ac - 1, av + 1)) goto on_error_0;

  pcm_init_desc(&desc);
  desc.flags |= PCM_FLAG_IN;
  if (cmd.flags & CMDLINE_FLAG(IPCM)) desc.name = cmd.ipcm;
  if (pcm_open(&ipcm, &desc)) goto on_error_0;

  pcm_init_desc(&desc);
  desc.flags |= PCM_FLAG_OUT;
  if (cmd.flags & CMDLINE_FLAG(OPCM)) desc.name = cmd.opcm;
  if (pcm_open(&opcm, &desc)) goto on_error_1;

  if (pcm_start(&ipcm)) goto on_error_2;
  if (pcm_start(&opcm)) goto on_error_2;

  signal(SIGINT, on_sigint);

  for (i = 0; is_sigint == 0; i += 1)
  {
    size_t nsampl;
    size_t navail;
    size_t off;

    /* read ipcm */

    err = snd_pcm_wait(ipcm.pcm, -1);
    if (err < 0) goto on_ipcm_xrun;

    navail = (size_t)snd_pcm_avail_update(ipcm.pcm);

    if (ipcm.wpos >= ipcm.rpos) nsampl = ipcm.nsampl - ipcm.wpos;
    else nsampl = ipcm.rpos - ipcm.wpos;
    if (nsampl > navail) nsampl = navail;

    off = ipcm.wpos * ipcm.scale;
    err = snd_pcm_readi(ipcm.pcm, ipcm.buf + off, nsampl);
    if (err < 0) goto on_ipcm_xrun;

    ipcm.wpos += (size_t)err;
    if (ipcm.wpos == ipcm.nsampl) ipcm.wpos = 0;

    /* write opcm */

    if (ipcm.wpos >= ipcm.rpos) nsampl = ipcm.wpos - ipcm.rpos;
    else nsampl = ipcm.nsampl - ipcm.rpos;

    off = ipcm.rpos * ipcm.scale;
    err = snd_pcm_writei(opcm.pcm, ipcm.buf + off, nsampl);
    if (err < 0) goto on_opcm_xrun;

    ipcm.rpos += (size_t)err;
    if (ipcm.rpos == ipcm.nsampl) ipcm.rpos = 0;

    continue ;

  on_ipcm_xrun:
    if (pcm_recover_xrun(&ipcm, err)) PERROR_GOTO("", on_error_2);
    continue ;

  on_opcm_xrun:
    if (pcm_recover_xrun(&opcm, err)) PERROR_GOTO("", on_error_2);
    continue ;
  }

  err = 0;

 on_error_2:
  pcm_close(&opcm);
 on_error_1:
  pcm_close(&ipcm);
 on_error_0:
  return err;
}
