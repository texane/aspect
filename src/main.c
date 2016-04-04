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
#include <fftw3.h>


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
  CMDLINE_ID_FILT,
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
    else if (strcmp(k, "-filter") == 0)
    {
      if (strcmp(v, "yes") == 0) cmd->flags |= CMDLINE_FLAG(FILT);
      else cmd->flags &= ~CMDLINE_FLAG(FILT);
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
  desc->nchan = 1;
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


/* modifier */
/* http://www.fftw.org/doc/One_002dDimensional-DFTs-of-Real-Data.html */

typedef struct
{
  void* buf;
  fftw_plan fplan;
  fftw_plan bplan;
  size_t n;
} mod_handle_t;

static int mod_open(mod_handle_t* mod, size_t n)
{
  mod->n = n;

  mod->buf = fftw_malloc((n / 2 + 1) * sizeof(fftw_complex));
  if (mod->buf == NULL) goto on_error_0;

  mod->fplan = fftw_plan_dft_r2c_1d(n, mod->buf, mod->buf, FFTW_ESTIMATE);
  if (mod->fplan == NULL) goto on_error_1;

  mod->bplan = fftw_plan_dft_c2r_1d(n, mod->buf, mod->buf, FFTW_ESTIMATE);
  if (mod->bplan == NULL) goto on_error_2;

  return 0;

 on_error_2:
  fftw_destroy_plan(mod->fplan);
 on_error_1:
  fftw_free(mod->buf);
 on_error_0:
  return -1;
}

static void mod_close(mod_handle_t* mod)
{
  fftw_destroy_plan(mod->bplan);
  fftw_destroy_plan(mod->fplan);
  fftw_free(mod->buf);
}

static size_t mod_apply(mod_handle_t* mod, uint8_t* buf, size_t n)
{
  size_t i;

  if (n < mod->n) return 0;
  n = mod->n;

  for (i = 0; i != n; ++i)
  {
    ((double*)mod->buf)[i] = (double)((int16_t*)buf)[i];
  }

  fftw_execute(mod->fplan);

  /* TODO: process mod->buf, fftw_complex format */

  fftw_execute(mod->bplan);

  for (i = 0; i != n; ++i)
  {
    /* results normalized, divide by n */
    ((int16_t*)buf)[i] = (int16_t)((double*)mod->buf)[i] / (int16_t)n;
  }

  return n;
}


#if 0 /* fir */

__attribute__((unused))
static int filter(pcm_handle_t* ipcm)
{
  static const double beta = 0.1;
  double lfilt;
  double rfilt;
  size_t n;
  size_t i;
  static size_t j = 0;

  if (ipcm->wpos >= ipcm->rpos) n = ipcm->wpos - ipcm->rpos;
  else n = ipcm->nsampl - ipcm->rpos;

  {
    const size_t off = ipcm->rpos + 0 * ipcm->scale;
    int16_t* const p = (int16_t*)(ipcm->buf + off);
    lfilt = (double)p[0];
    rfilt = (double)p[1];
  }

  for (i = 1; i != n; ++i, ++j)
  {
    const size_t off = ipcm->rpos + i * ipcm->scale;
    int16_t* const p = (int16_t*)(ipcm->buf + off);

    const double l = (double)p[0];
    const double r = (double)p[1];

    lfilt = lfilt - beta * (lfilt - l);
    rfilt = rfilt - beta * (rfilt - r);

    p[0] = (int16_t)lfilt;
    p[1] = (int16_t)rfilt;

    printf("%zu %lf %lf\n", j, l, lfilt);
  }

  return 0;
}

#endif /* fir */


/* main */

int main(int ac, char** av)
{
  pcm_desc_t desc;
  pcm_handle_t ipcm;
  pcm_handle_t opcm;
  mod_handle_t mod;
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

  if (mod_open(&mod, 1024)) goto on_error_2;

  if (pcm_start(&ipcm)) goto on_error_3;
  if (pcm_start(&opcm)) goto on_error_3;

  signal(SIGINT, on_sigint);

  for (i = 0; is_sigint == 0; i += 1)
  {
    size_t nsampl;
    size_t navail;
    size_t off;

    /* read ipcm */

    err = snd_pcm_wait(ipcm.pcm, -1);
    if (is_sigint) break ;
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

    /* apply modifier and write opcm */

    if (ipcm.wpos >= ipcm.rpos) nsampl = ipcm.wpos - ipcm.rpos;
    else nsampl = ipcm.nsampl - ipcm.rpos;

    off = ipcm.rpos * ipcm.scale;

    if (cmd.flags & CMDLINE_FLAG(FILT))
    {
      const size_t n = mod_apply(&mod, ipcm.buf + off, nsampl);
      if (n == 0) continue ;
    }

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

 on_error_3:
  mod_close(&mod);
 on_error_2:
  pcm_close(&opcm);
 on_error_1:
  pcm_close(&ipcm);
 on_error_0:
  return err;
}
