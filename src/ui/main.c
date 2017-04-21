#include <stdio.h>
#include <SDL.h>


#define PERROR() \
 do { printf("error: %s,%u\n", __FILE__, __LINE__); } while(0)


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


static int ui_handle_events(ui_handle_t* ui)
{
  SDL_Event e;

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
  ui_draw_bar(ui, 100, rand() % 100, 5);
  ui_draw_bar(ui, 105, rand() % 100, 5);
  ui_draw_bar(ui, 110, rand() % 100, 5);

  SDL_RenderClear(ui->ren);
  SDL_UpdateTexture(ui->tex, NULL, ui->buf, ui->w * sizeof(Uint32));
  SDL_RenderCopy(ui->ren, ui->tex, NULL, NULL);
  SDL_RenderPresent(ui->ren);

  SDL_Delay(100);

  return 0;
}


int main(int ac, char** av)
{
  ui_desc_t desc;
  ui_handle_t ui;
  int err = -1;

  ui_init_desc(&desc);
  if (ui_open(&ui, &desc)) goto on_error_0;
  while (ui_handle_events(&ui) == 0) ;
  err = 0;
  ui_close(&ui);

 on_error_0:
  return err;
}
