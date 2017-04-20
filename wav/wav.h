#ifndef WAV_H_INCLUDED
#define WAV_H_INCLUDED


#include <sys/types.h>


typedef struct wav_handle
{
#define WAV_FLAG_MMAP (1 << 0)
  uint32_t flags;

  size_t nchan;
  size_t nsampl;
  size_t wsampl;

  unsigned int fsampl;

  void* data;
  size_t size;

} wav_handle_t;


int wav_open(wav_handle_t*, const char*);
int wav_create(wav_handle_t*, size_t, size_t, size_t);
void wav_close(wav_handle_t*);
int wav_write(wav_handle_t*, const char*, unsigned int);
void* wav_get_sampl_buf(wav_handle_t*);


#endif /* ! WAV_H_INCLUDED */
