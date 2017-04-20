#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "wav.h"



/* wav format header */
/* http://soundfile.sapp.org/doc/WaveFormat/ */

typedef struct wav_header
{
#define WAV_RIFF_MAGIC "RIFF"
  uint8_t riff_magic[4];
  uint32_t file_size;

#define WAV_WAVE_MAGIC "WAVE"
  uint8_t wave_magic[4];

#define WAV_FORMAT_MAGIC "fmt "
  uint8_t fmt_magic[4];
  uint32_t block_size;

#define WAV_PCM_FORMAT 1
  uint16_t audio_format;
  uint16_t channels;
  uint32_t freq;
  uint32_t bytes_per_second;
  uint16_t bytes_per_block;
  uint16_t bits_per_sample;

#define WAV_DATA_MAGIC "data"
  uint8_t data_magic[4];
  uint32_t data_size;
} __attribute__((packed)) wav_header_t;


static int wav_check_header(const struct wav_header* h, size_t file_size)
{
  /* return 0 for success, -1 on error */

  if (file_size < sizeof(wav_header_t)) return -1;

  /* pcm */

  if (h->audio_format != WAV_PCM_FORMAT) return -1;

  /* magics */

#define MEMCMP(A, B) memcmp(A, B, sizeof(B) - 1)

  if (MEMCMP(h->riff_magic, WAV_RIFF_MAGIC)) return -1;

  if (MEMCMP(h->wave_magic, WAV_WAVE_MAGIC)) return -1;

  if (MEMCMP(h->fmt_magic, WAV_FORMAT_MAGIC)) return -1;

  if (MEMCMP(h->data_magic, WAV_DATA_MAGIC)) return -1;

  /* sizes */

  if ((file_size - 8) != h->file_size) return -1;

  if ((file_size - sizeof(wav_header_t)) < h->data_size) return -1;

  return 0;
}


void wav_close(wav_handle_t* w)
{
  if (w->flags & WAV_FLAG_MMAP) munmap(w->data, w->size);
  else free(w->data);
}


int wav_open(wav_handle_t* w, const char* path)
{
  int fd = -1;
  int err = -1;
  struct stat st;
  const wav_header_t* h;

  w->flags = 0;

  /* map the file */

  fd = open(path, O_RDONLY);
  if (fd == -1) goto on_error_0;

  if (fstat(fd, &st) == -1) goto on_error_1;

  w->size = st.st_size;
  w->data = mmap(NULL, w->size, PROT_READ, MAP_SHARED, fd, 0);
  if (w->data == MAP_FAILED) goto on_error_1;

  w->flags |= WAV_FLAG_MMAP;

  /* get header informations */

  h = w->data;
  if (wav_check_header(h, w->size) == -1) goto on_error_2;

  w->nchan = (size_t)h->channels;
  if ((h->bits_per_sample % 8)) goto on_error_2;
  w->wsampl = (size_t)h->bits_per_sample / 8;
  w->nsampl = (size_t)(h->data_size / (w->wsampl * w->nchan));

  w->fsampl = (unsigned int)h->freq;

  err = 0;

  return 0;

 on_error_2:
  if (err) munmap(w->data, w->size);
 on_error_1:
  close(fd);
 on_error_0:
  return err;
}


int wav_create
(wav_handle_t* w, size_t nchan, size_t wsampl, size_t nsampl)
{
  w->flags = 0;

  w->nchan = nchan;
  w->wsampl = wsampl;
  w->nsampl = nsampl;

  w->fsampl = 0;

  w->size = sizeof(wav_header_t) + nchan * wsampl * nsampl;
  w->data = malloc(w->size);
  if (w->data == NULL) goto on_error_0;

  return 0;

 on_error_0:
  return -1;
}


int wav_write(wav_handle_t* w, const char* path, unsigned int fsampl)
{
  wav_header_t* const h = (wav_header_t*)w->data;
  int err = -1;
  int fd;

  fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 00755);
  if (fd == -1) goto on_error_0;

#define MEMCPY(A, B) memcpy(A, B, sizeof(B) - 1)
  MEMCPY(h->riff_magic, WAV_RIFF_MAGIC);
  h->file_size = w->size - 8;

  MEMCPY(h->wave_magic, WAV_WAVE_MAGIC);

  MEMCPY(h->fmt_magic, WAV_FORMAT_MAGIC);
  h->block_size = 16;
  h->audio_format = WAV_PCM_FORMAT;
  h->channels = (uint16_t)w->nchan;
  h->freq = (uint32_t)fsampl;
  h->bytes_per_second = (uint32_t)(fsampl * w->nchan * w->wsampl);
  h->bytes_per_block = (uint16_t)(w->nchan * w->wsampl);
  h->bits_per_sample = (uint16_t)(w->wsampl * 8);

  MEMCPY(h->data_magic, WAV_DATA_MAGIC);
  h->data_size = w->size - sizeof(wav_header_t);

  if ((size_t)write(fd, w->data, w->size) != w->size) goto on_error_1;

  err = 0;

 on_error_1:
  close(fd);
 on_error_0:
  return err;
}


void* wav_get_sampl_buf(wav_handle_t* w)
{
  return w->data + sizeof(wav_header_t);
}
