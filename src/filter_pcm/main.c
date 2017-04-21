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
#include <SDL.h>


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
    (pcm->pcm, pcm->sw_params, 1024);
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
  double* spectrum;
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

  mod->spectrum = malloc((n / 2 + 1) * sizeof(double));
  if (mod->spectrum == NULL) goto on_error_3;

  return 0;

 on_error_3:
  fftw_destroy_plan(mod->bplan);
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

static size_t mod_apply
(
 mod_handle_t* mod,
 uint8_t* buf, size_t size,
 size_t off, size_t n
)
{
  size_t i;

  if (n < mod->n) return 0;
  n = mod->n;

  for (i = 0; i != n; ++i)
  {
    ((double*)mod->buf)[i] = (double)buf[(off + i) % size];
  }

  fftw_execute(mod->fplan);

  {
    double sum = 0.0;

    for (i = 0; i != n / 2 + 1; ++i)
    {
      const double re = ((fftw_complex*)mod->buf)[i][0];
      const double im = ((fftw_complex*)mod->buf)[i][1];
      sum += sqrt(re * re + im * im);
    }

    for (i = 0; i != n / 2 + 1; ++i)
    {
      const double re = ((fftw_complex*)mod->buf)[i][0];
      const double im = ((fftw_complex*)mod->buf)[i][1];
      mod->spectrum[i] = sqrt(re * re + im * im) / sum;
    }
  }

  /* TODO: process mod->buf, fftw_complex format */
  fftw_execute(mod->bplan);

  for (i = 0; i != n; ++i)
  {
    buf[(off + i) % size] = (int16_t)((double*)mod->buf)[i] / (int16_t)n;
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


/* user interface */

typedef struct
{
  unsigned int w;
  unsigned int h;
} ui_desc_t;


typedef struct
{
  SDL_Window* win;
  SDL_Renderer* ren;
  SDL_Texture* tex;
  uint8_t* buf;
  unsigned int w;
  unsigned int h;
} ui_handle_t;


static void ui_init_desc(ui_desc_t* desc)
{
  desc->w = 640;
  desc->h = 480;
}


static int ui_open(ui_handle_t* ui, const ui_desc_t* desc)
{
  if (SDL_Init(SDL_INIT_VIDEO)) goto on_error_0;

  ui->win = SDL_CreateWindow
  (
   "main",
   0, 0,
   (int)desc->w, (int)desc->h,
   SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS
  );
  if (ui->win == NULL) goto on_error_1;

  ui->ren = SDL_CreateRenderer
  (
   ui->win,
   -1,
   SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
  );
  if (ui->ren == NULL) goto on_error_2;

  ui->tex = SDL_CreateTexture
  (
   ui->ren,
   SDL_PIXELFORMAT_ARGB8888,
   SDL_TEXTUREACCESS_STATIC,
   (int)desc->w, (int)desc->h
  );
  if (ui->tex == NULL) goto on_error_3;

  ui->buf = malloc(desc->w * desc->h * sizeof(Uint32));
  if (ui->buf == NULL) goto on_error_4;

  ui->w = desc->w;
  ui->h = desc->h;

  return 0;

 on_error_4:
  SDL_DestroyTexture(ui->tex);
 on_error_3:
  SDL_DestroyRenderer(ui->ren);
 on_error_2:
  SDL_DestroyWindow(ui->win);
 on_error_1:
  SDL_Quit();
 on_error_0:
  return -1;
}


static int ui_open_default(ui_handle_t* ui)
{
  ui_desc_t desc;
  ui_init_desc(&desc);
  return ui_open(ui, &desc);
}


static void ui_close(ui_handle_t* ui)
{
  free(ui->buf);
  SDL_DestroyTexture(ui->tex);
  SDL_DestroyRenderer(ui->ren);
  SDL_DestroyWindow(ui->win);
  SDL_Quit();
}


static void ui_put_pixel
(ui_handle_t* ui, unsigned int x, unsigned y, Uint32 c)
{
  ((Uint32*)ui->buf)[y * ui->w + x] = c;
}


static void ui_clear_buf(ui_handle_t* ui)
{
  memset(ui->buf, 0, ui->h * ui->w * sizeof(Uint32));
}


static void ui_draw_bar
(ui_handle_t* ui, unsigned int x, unsigned int h, unsigned int w)
{
  unsigned int i;
  unsigned int j;

  for (i = 0; i != h; ++i)
  {
    for (j = 0; j != w; ++j)
      ui_put_pixel(ui, x + j, ui->h - 1 - i, 0x00ff0000);
  }
}


static int ui_handle_events
(ui_handle_t* ui, const double* spectrum, size_t n)
{
  SDL_Event e;
  size_t i;

  if (SDL_PollEvent(&e))
  {
    if (e.type == SDL_QUIT) return -1;

    if (e.type == SDL_KEYDOWN)
    {
      if (e.key.keysym.sym == SDLK_ESCAPE)
      {
	return -1;
      }
    }
  }

  ui_clear_buf(ui);

  for (i = 0; i != n; ++i)
  {
    const double x = spectrum[i] * (double)ui->h;
    ui_draw_bar(ui, i, (unsigned int)x, 1);
  }

  SDL_RenderClear(ui->ren);
  SDL_UpdateTexture(ui->tex, NULL, ui->buf, ui->w * sizeof(Uint32));
  SDL_RenderCopy(ui->ren, ui->tex, NULL, NULL);
  SDL_RenderPresent(ui->ren);

  return 0;
}


/* main */

int main(int ac, char** av)
{
  pcm_desc_t desc;
  pcm_handle_t ipcm;
  pcm_handle_t opcm;
  mod_handle_t mod;
  ui_handle_t ui;
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

  if (ui_open_default(&ui)) goto on_error_3;

  if (pcm_start(&ipcm)) goto on_error_4;
  if (pcm_start(&opcm)) goto on_error_4;

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

    /* apply modifier */

  redo_mod:
    if (ipcm.wpos >= ipcm.rpos) nsampl = ipcm.wpos - ipcm.rpos;
    else nsampl = ipcm.nsampl - ipcm.rpos + ipcm.wpos;

    if (cmd.flags & CMDLINE_FLAG(FILT))
    {
      const size_t n = mod_apply(&mod, ipcm.buf, ipcm.nsampl, ipcm.rpos, nsampl);
      nsampl = n;
      if (nsampl)
      {
	if (ui_handle_events(&ui, mod.spectrum, mod.n / 2 + 1)) break ;
      }
    }

    if (nsampl == 0) continue ;

    if ((ipcm.rpos + nsampl) > ipcm.nsampl)
    {
      const size_t n = ipcm.nsampl - ipcm.rpos;
      off = ipcm.rpos * ipcm.scale;
      err = snd_pcm_writei(opcm.pcm, ipcm.buf + off, n);
      if (err < 0) goto on_opcm_xrun;
      nsampl -= n;
      ipcm.rpos = 0;
    }

    off = ipcm.rpos * ipcm.scale;
    err = snd_pcm_writei(opcm.pcm, ipcm.buf + off, nsampl);
    if (err < 0) goto on_opcm_xrun;
    ipcm.rpos += nsampl;
    if (ipcm.rpos == ipcm.nsampl) ipcm.rpos = 0;

    goto redo_mod;

    continue ;

  on_ipcm_xrun:
    if (pcm_recover_xrun(&ipcm, err)) PERROR_GOTO("", on_error_4);
    continue ;

  on_opcm_xrun:
    if (pcm_recover_xrun(&opcm, err)) PERROR_GOTO("", on_error_4);
    continue ;
  }

  err = 0;

 on_error_4:
  ui_close(&ui);
 on_error_3:
  mod_close(&mod);
 on_error_2:
  pcm_close(&opcm);
 on_error_1:
  pcm_close(&ipcm);
 on_error_0:
  return err;
}
