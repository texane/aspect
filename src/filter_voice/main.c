#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <fftw3.h>
#include "wav.h"


#if 1
#include <stdio.h>
#define PERROR() \
 do { printf("[!] %s,%u\n", __FILE__, __LINE__); fflush(stdout); } while(0)
#else
#define PERROR()
#endif



/* cmd */

typedef struct
{
#define CMD_FLAG_IPATH (1 << 0)
#define CMD_FLAG_OPATH (1 << 1)
  uint32_t flags;
  const char* ipath;
  const char* opath;
} cmd_handle_t;

static int cmd_init(cmd_handle_t* cmd, int ac, char** av)
{
  size_t i;

  cmd->flags = 0;
  cmd->ipath = NULL;
  cmd->opath = NULL;

  if ((ac % 2)) goto on_error;

  for (i = 0; i != ac; i += 2)
  {
    const char* const k = av[i + 0];
    const char* const v = av[i + 1];

    if (strcmp(k, "-ipath") == 0)
    {
      cmd->flags |= CMD_FLAG_IPATH;
      cmd->ipath = v;
    }
    else if (strcmp(k, "-opath") == 0)
    {
      cmd->flags |= CMD_FLAG_OPATH;
      cmd->opath = v;
    }
    else goto on_error;
  }

  return 0;

 on_error:
  return -1;
}


/* filter */

typedef struct
{
  void* buf;
  fftw_plan fplan;
  fftw_plan bplan;
  size_t n;
} filter_handle_t;

static int filter_init(filter_handle_t* f, size_t n)
{
  f->n = n;

  f->buf = fftw_malloc((n / 2 + 1) * sizeof(fftw_complex));
  if (f->buf == NULL) goto on_error_0;

  f->fplan = fftw_plan_dft_r2c_1d(n, f->buf, f->buf, FFTW_ESTIMATE);
  if (f->fplan == NULL) goto on_error_1;

  f->bplan = fftw_plan_dft_c2r_1d(n, f->buf, f->buf, FFTW_ESTIMATE);
  if (f->bplan == NULL) goto on_error_2;

  return 0;

 on_error_2:
  fftw_destroy_plan(f->fplan);
 on_error_1:
  fftw_free(f->buf);
 on_error_0:
  return -1;
}

static void filter_fini(filter_handle_t* f)
{
  fftw_destroy_plan(f->bplan);
  fftw_destroy_plan(f->fplan);
  fftw_free(f->buf);
}

static size_t filter_apply
(
 filter_handle_t* f,
 uint8_t* buf, size_t size,
 size_t off, size_t n
)
{
  size_t i;

  if (n < f->n) return 0;
  n = f->n;

  for (i = 0; i != n; ++i)
  {
    ((double*)f->buf)[i] = (double)buf[(off + i) % size];
  }

  fftw_execute(mod->fplan);

  /* TODO: process mod->buf, fftw_complex format */

  fftw_execute(mod->bplan);

  for (i = 0; i != n; ++i)
  {
    buf[(off + i) % size] = (int16_t)((double*)mod->buf)[i] / (int16_t)n;
  }

  return n;
}

static void int16_to_double
(double* obuf, const int16_t* ibuf, size_t n, size_t w)
{
  size_t i;
  for (i = 0; i != n; ++i, ibuf += w, ++obuf) *obuf = (double)*ibuf;
}

static void double_to_int16
(int16_t* obuf, const double* ibuf, size_t n, size_t w)
{
  size_t i;
  for (i = 0; i != n; ++i, ++ibuf, obuf += w) *obuf = (int16_t)*ibuf;
}

static void filter_one_chunk
(filter_handle_t* f, uint8_t* obuf, const uint8_t* ibuf, size_t w)
{
  /* prepare one chunk */
  /* cast from ibuf (wsampl format) into f->buf (double format) */

  int16_to_double(f->buf, ibuf, f->n, w);

  int16_t* p;
  size_t i;

  p = (int16_t*)ibuf;

  for (i = 0; i != filter->n; ++i, p)
  {
    real_buf[i] = (double)(*((uint16_t*)ibuf));
  }

}

static void filter_one_chan
(
 filter_handle_t* f,
 uint8_t* obuf, const uint8_t* ibuf,
 size_t nsampl, size_t wsampl, size_t nchan
)
{
  size_t i;

  for (i = 0; i != (n / f->n); ++i, obuf += (f->n * w), ibuf += (f->n * w))
  {
    filter_one_chunk(f, obuf, ibuf, w);
  }
}

static void filter_wav(wav_handle_t* ow, wav_handle_t* iw)
{
  filter_handle_t f;
  size_t i;

  /* https://en.wikipedia.org/wiki/Voice_frequency */
  /* male: from 85 to 180 Hz */
  /* female: from 165 to 255 Hz */
  /* resolution: 5 Hz */
  /* fres = fsampl / (nsampl * 2) */
  /* nsampl = 44100 / (5 * 2) = 4410 */
  /* thus, nsampl of 8192 (next power of 2) */

  if (filter_init(&f, 8192)) return -1;

  for (i = 0; i != iw->nchan; ++i)
  {
    uint8_t* const ibuf = (uint8_t*)wav_get_sampl_buf(iw) + i * iw->wsampl;
    uint8_t* const obuf = (uint8_t*)wav_get_sampl_buf(ow) + i * ow->wsampl;
    filter_one_chan(&f, obuf, ibuf, iw->nsampl, i->wsampl);
  }

  filter_fini(&f);

  return 0;

}


/* main */

int main(int ac, char** av)
{
  wav_handle_t iw;
  wav_handle_t ow;
  cmd_handle_t cmd;
  int err = -1;

  if (cmd_init(&cmd, ac - 1, av + 1))
  {
    PERROR();
    goto on_error_0;
  }

  if ((cmd.flags & CMD_FLAG_IPATH) == 0)
  {
    PERROR();
    goto on_error_0;
  }

  if ((cmd.flags & CMD_FLAG_OPATH) == 0)
  {
    PERROR();
    goto on_error_0;
  }

  if (wav_open(&iw, cmd.ipath))
  {
    PERROR();
    goto on_error_0;
  }

  if (wav_copy(&ow, &iw))
  {
    PERROR();
    goto on_error_1;
  }

  if (wav_write(&ow, cmd.opath))
  {
    PERROR();
    goto on_error_2;
  }

  err = 0;
 on_error_2:
  wav_close(&ow);
 on_error_1:
  wav_close(&iw);
 on_error_0:
  return err;
}
