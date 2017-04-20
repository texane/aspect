#include <stdlib.h>
#include <stdint.h>
#include <string.h>
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
#define CMD_FLAG_START (1 << 2)
#define CMD_FLAG_LENGTH (1 << 3)
  uint32_t flags;
  const char* ipath;
  const char* opath;
  uint32_t start;
  uint32_t length;
} cmd_handle_t;

static int cmd_init(cmd_handle_t* cmd, int ac, char** av)
{
  size_t i;

  cmd->flags = 0;
  cmd->ipath = NULL;
  cmd->opath = NULL;
  cmd->start = 0;
  cmd->length = 0;

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
    else if (strcmp(k, "-start") == 0)
    {
      cmd->flags |= CMD_FLAG_START;
      cmd->start = (uint32_t)strtoul(v, NULL, 10);
    }
    else if (strcmp(k, "-length") == 0)
    {
      cmd->flags |= CMD_FLAG_LENGTH;
      cmd->length = (uint32_t)strtoul(v, NULL, 10);
    }
    else goto on_error;
  }

  return 0;

 on_error:
  return -1;
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
