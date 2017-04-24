#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
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
  size_t nband;
  double bands[32 * 2];
} cmd_handle_t;

static int cmd_parse_band(const char* s, double* lo, double* hi)
{
  char* e;

#define FILTER_BAND_MIN 0.0
#define FILTER_BAND_MAX 10000000.0
  *lo = FILTER_BAND_MIN;
  *hi = FILTER_BAND_MAX;

  if (*s != ':')
  {
    *lo = strtod(s, &e);
    if (e == s) return -1;
    s = e;
  }

  if (*s == ':')
  {
    ++s;
    *hi = strtod(s, &e);
    if (e == s) return -1;
  }

  return 0;
}

static int cmd_init(cmd_handle_t* cmd, int ac, char** av)
{
  size_t i;

  cmd->flags = 0;
  cmd->ipath = NULL;
  cmd->opath = NULL;
  cmd->nband = 0;

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
    else if (strcmp(k, "-band") == 0)
    {
      double* const lo = &cmd->bands[cmd->nband * 2 + 0];
      double* const hi = &cmd->bands[cmd->nband * 2 + 1];

      if (cmd->nband == (sizeof(cmd->bands) / (2 * sizeof(cmd->bands[0]))))
	goto on_error;

      if (cmd_parse_band(v, lo, hi)) goto on_error;

      if (*lo < FILTER_BAND_MIN) goto on_error;
      if (*hi > FILTER_BAND_MAX) goto on_error;

      ++cmd->nband;
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

  const double* bands;
  size_t nband;

} filter_handle_t;

static int filter_init
(filter_handle_t* f, size_t n, const double* bands, size_t nband)
{
  f->n = n;

  f->buf = fftw_malloc((n / 2 + 1) * sizeof(fftw_complex));
  if (f->buf == NULL) goto on_error_0;

  f->fplan = fftw_plan_dft_r2c_1d(n, f->buf, f->buf, FFTW_ESTIMATE);
  if (f->fplan == NULL) goto on_error_1;

  f->bplan = fftw_plan_dft_c2r_1d(n, f->buf, f->buf, FFTW_ESTIMATE);
  if (f->bplan == NULL) goto on_error_2;

  f->bands = bands;
  f->nband = nband;

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

static void filter_one_chunk(filter_handle_t* f)
{
  static const double fsampl = 44100;
#if 0
  static const double flo = 200.0;
  static const double fhi = 1000.0;
  const size_t ilo = (size_t)ceil((flo * (double)f->n) / fsampl);
  const size_t ihi = (size_t)floor((fhi * (double)f->n) / fsampl);
#endif

  size_t i;
  size_t j;

  fftw_execute(f->fplan);

#if 0
  for (i = 0; i != ilo; ++i)
  {
    ((fftw_complex*)f->buf)[i][0] = 0.0;
    ((fftw_complex*)f->buf)[i][1] = 0.0;
  }

  for (i = ihi; i != ((f->n / 2) + 1); ++i)
  {
    ((fftw_complex*)f->buf)[i][0] = 0.0;
    ((fftw_complex*)f->buf)[i][1] = 0.0;
  }
#else
  for (i = 0; i != ((f->n / 2) + 1); ++i)
  {
    const double freq = ((double)i * fsampl) / (double)f->n;

    for (j = 0; j != f->nband; ++j)
    {
      const double flo = f->bands[j * 2 + 0];
      const double fhi = f->bands[j * 2 + 1];
      if ((freq >= flo) && (freq <= fhi)) break ;
    }

    if (j != f->nband) continue ;

    ((fftw_complex*)f->buf)[i][0] = 0.0;
    ((fftw_complex*)f->buf)[i][1] = 0.0;
  }
#endif

  fftw_execute(f->bplan);

  for (i = 0; i != f->n; ++i) ((double*)f->buf)[i] /= f->n;
}

static void filter_one_chan
(
 filter_handle_t* f,
 uint8_t* obuf, const uint8_t* ibuf,
 size_t nchan, size_t nsampl, size_t wsampl
)
{
  /* filter one chan by chunk of f->n samples */

  const size_t n = nsampl / f->n;
  const size_t w = f->n * nchan * wsampl;
  size_t i;

  /* unused since assuming int16 */
  wsampl = wsampl;

  for (i = 0; i != n; ++i, obuf += w, ibuf += w)
  {
    int16_to_double(f->buf, (const int16_t*)ibuf, f->n, nchan);
    filter_one_chunk(f);
    double_to_int16((int16_t*)obuf, f->buf, f->n, nchan);
  }

  /* remaining partial chunk */
  if ((n * f->n) != nsampl)
  {
    const size_t r = nsampl - (n * f->n);
    int16_to_double(f->buf, (const int16_t*)ibuf, r, nchan);
    for (i = r; i != f->n; ++i) ((double*)f->buf)[i] = 0.0;
    filter_one_chunk(f);
    double_to_int16((int16_t*)obuf, f->buf, r, nchan);
  }
}

static int filter_voice
(
 uint8_t* obuf, const uint8_t* ibuf,
 size_t nchan, size_t nsampl, size_t wsampl,
 const double* bands, size_t nband
)
{
  filter_handle_t f;
  size_t i;

  /* resolution: 5 Hz */
  /* fres = fsampl / (nsampl * 2) */
  /* nsampl = 44100 / (5 * 2) = 4410 */
  /* thus, nsampl of 8192 (next power of 2) */

  if (filter_init(&f, 8192, bands, nband)) return -1;

  for (i = 0; i != nchan; ++i, ibuf += wsampl, obuf += wsampl)
  {
    filter_one_chan(&f, obuf, ibuf, nchan, nsampl, wsampl);
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

  if (cmd.nband == 0)
  {
    /* https://en.wikipedia.org/wiki/Voice_frequency */
    /* male: from 85 to 180 Hz */
    /* female: from 165 to 255 Hz */

    cmd.bands[0 * 2 + 0] = 80.0;
    cmd.bands[0 * 2 + 1] = 260.0;
    cmd.nband = 1;
  }

  if (wav_open(&iw, cmd.ipath))
  {
    PERROR();
    goto on_error_0;
  }

  if (iw.wsampl != 2)
  {
    /* only int16_t supported */
    PERROR();
    goto on_error_0;
  }

  if (iw.fsampl != 44100)
  {
    PERROR();
    goto on_error_0;
  }

  if (wav_create2(&ow, &iw))
  {
    PERROR();
    goto on_error_1;
  }

  filter_voice
  (
   wav_get_sampl_buf(&ow), (const void*)wav_get_sampl_buf(&iw),
   iw.nchan, iw.nsampl, iw.wsampl,
   cmd.bands, cmd.nband
  );

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
