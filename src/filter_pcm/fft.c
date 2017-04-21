#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <fftw3.h>


int main(int ac, char** av)
{
  const unsigned int n = 256;
  unsigned int i;
  static int16_t x[256];
  void* buf;
  fftw_plan fplan;
  fftw_plan bplan;

  for (i = 0; i != n; ++i) x[i] = (int16_t)i - (int16_t)n / 2;
  buf = fftw_malloc((n / 2 + 1) * sizeof(fftw_complex));
  fplan = fftw_plan_dft_r2c_1d(n, buf, buf, FFTW_ESTIMATE);
  bplan = fftw_plan_dft_c2r_1d(n, buf, buf, FFTW_ESTIMATE);

  for (i = 0; i != n; ++i)
  {
    ((double*)buf)[i] = (double)x[i];
  }

  fftw_execute(fplan);
  fftw_execute(bplan);

  for (i = 0; i != n; ++i)
  {
    x[i] = (int16_t)((double*)buf)[i] / (int16_t)n;
  }

  for (i = 0; i != n; ++i) printf("%hd\n", x[i]);

  return 0;
}
