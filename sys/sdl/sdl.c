/*
 * sdl.c
 * sdl interfaces -- based on svga.c
 *
 * (C) 2001 Damian Gryski <dgryski@uwaterloo.ca>
 * Joystick code contributed by David Lau
 *
 * Licensed under the GPLv2, or later.
 */

#include <stdlib.h>
#include <stdio.h>

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>

#include "sys.h"
#include "fb.h"
#include "input.h"
#include "rc.h"
#include "loader.h"

/// -------------- DEFINES --------------
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define ABS(x) (((x) < 0) ? (-x) : (x))

//#define MENU_DEBUG
#define MENU_ERROR

#ifdef MENU_DEBUG
#define MENU_DEBUG_PRINTF(...)   printf(__VA_ARGS__);
#else
#define MENU_DEBUG_PRINTF(...)
#endif //MENU_DEBUG

#ifdef MENU_ERROR
#define MENU_ERROR_PRINTF(...)   printf(__VA_ARGS__);
#else
#define MENU_ERROR_PRINTF(...)
#endif //MENU_ERROR


#define GB_SCREEN_WIDTH 160
#define GB_SCREEN_HEIGHT 144
#define RES_HW_SCREEN_HORIZONTAL  240
#define RES_HW_SCREEN_VERTICAL    240

#define SCREEN_HORIZONTAL_SIZE      RES_HW_SCREEN_HORIZONTAL
#define SCREEN_VERTICAL_SIZE        RES_HW_SCREEN_VERTICAL

#define SCROLL_SPEED_PX             240 //This means no animations but also no tearing effect
#define FPS_MENU                    30
#define ARROWS_PADDING				8

#define MENU_ZONE_WIDTH             SCREEN_HORIZONTAL_SIZE
#define MENU_ZONE_HEIGHT            SCREEN_VERTICAL_SIZE
#define MENU_BG_SQURE_WIDTH         180
#define MENU_BG_SQUREE_HEIGHT       140

#define MENU_FONT_NAME_TITLE        "/usr/games/menu_resources/OpenSans-Bold.ttf"
#define MENU_FONT_SIZE_TITLE        22
#define MENU_FONT_NAME_INFO         "/usr/games/menu_resources/OpenSans-Bold.ttf"
#define MENU_FONT_SIZE_INFO         16
#define MENU_FONT_NAME_SMALL_INFO   "/usr/games/menu_resources/OpenSans-Regular.ttf"
#define MENU_FONT_SIZE_SMALL_INFO   13
#define MENU_PNG_BG_PATH            "/usr/games/menu_resources/zone_bg.png"
#define MENU_PNG_ARROW_TOP_PATH     "/usr/games/menu_resources/arrow_top.png"
#define MENU_PNG_ARROW_BOTTOM_PATH  "/usr/games/menu_resources/arrow_bottom.png"

#define GRAY_MAIN_R                 85
#define GRAY_MAIN_G                 85
#define GRAY_MAIN_B                 85
#define WHITE_MAIN_R                236
#define WHITE_MAIN_G                236
#define WHITE_MAIN_B                236

#define MAX_SAVE_SLOTS              9

#define MAXPATHLEN                  512

struct fb fb;

static int use_yuv = -1;
static int fullscreen = 0;
static int use_altenter = 1;
static int use_joy = 1, sdl_joy_num;
static SDL_Joystick * sdl_joy = NULL;
static const int joy_commit_range = 3276;
static char Xstatus, Ystatus;

SDL_Surface *hw_screen;
SDL_Surface *virtual_hw_screen;
SDL_Surface *gb_screen;
static SDL_Overlay *overlay;
static SDL_Rect overlay_rect;

static int vmode[3] = { 0, 0, 16 };

rcvar_t vid_exports[] =
{
	RCV_VECTOR("vmode", &vmode, 3),
	RCV_BOOL("yuv", &use_yuv),
	RCV_BOOL("fullscreen", &fullscreen),
	RCV_BOOL("altenter", &use_altenter),
	RCV_END
};

rcvar_t joy_exports[] =
{
	RCV_BOOL("joy", &use_joy),
	RCV_END
};

/* keymap - mappings of the form { scancode, localcode } - from sdl/keymap.c */
extern int keymap[][2];



/// -------------- STATIC VARIABLES for menu --------------
static int framelen = 16743;
static int framecount;

static int backup_key_repeat_delay, backup_key_repeat_interval;
static SDL_Surface * backup_hw_screen = NULL;
static TTF_Font *menu_title_font = NULL;
static TTF_Font *menu_info_font = NULL;
static TTF_Font *menu_small_info_font = NULL;
static SDL_Surface *img_arrow_top = NULL;
static SDL_Surface *img_arrow_bottom = NULL;
static SDL_Surface ** menu_zone_surfaces = NULL;
static int * idx_menus = NULL;
static int nb_menu_zones = 0;
static int menuItem = 0;
static int stop_menu_loop = 0;

static SDL_Color text_color = {GRAY_MAIN_R, GRAY_MAIN_G, GRAY_MAIN_B};
static int padding_y_from_center_menu_zone = 18;
static uint16_t width_progress_bar = 100;
static uint16_t height_progress_bar = 20;
static uint16_t x_volume_bar = 0;
static uint16_t y_volume_bar = 0;
static uint16_t x_brightness_bar = 0;
static uint16_t y_brightness_bar = 0;

int volume_percentage = 0;
int brightness_percentage = 0;

#undef X
#define X(a, b) b,
const char *aspect_ratio_name[] = {ASPECT_RATIOS};
int aspect_ratio = ASPECT_RATIOS_TYPE_STRECHED;
int aspect_ratio_factor_percent = 50;
int aspect_ratio_factor_step = 10;





static int mapscancode(SDLKey sym)
{
	/* this could be faster:  */
	/*  build keymap as int keymap[256], then ``return keymap[sym]'' */

	int i;
	for (i = 0; keymap[i][0]; i++)
		if (keymap[i][0] == sym)
			return keymap[i][1];
	if (sym >= '0' && sym <= '9')
		return sym;
	if (sym >= 'a' && sym <= 'z')
		return sym;
	return 0;
}


static void joystick_init()
{
	int i;
	int joy_count;
	
	/* Initilize the Joystick, and disable all later joystick code if an error occured */
	if (!use_joy) return;
	
	if (SDL_InitSubSystem(SDL_INIT_JOYSTICK))
		return;
	
	joy_count = SDL_NumJoysticks();
	
	if (!joy_count)
		return;

	/* now try and open one. If, for some reason it fails, move on to the next one */
	for (i = 0; i < joy_count; i++)
	{
		sdl_joy = SDL_JoystickOpen(i);
		if (sdl_joy)
		{
			sdl_joy_num = i;
			break;
		}	
	}
	
	/* make sure that Joystick event polling is a go */
	SDL_JoystickEventState(SDL_ENABLE);
}

static void overlay_init()
{
	if (!use_yuv) return;
	
	if (use_yuv < 0)
		if (vmode[0] < 320 || vmode[1] < 288)
			return;
	
	overlay = SDL_CreateYUVOverlay(320, 144, SDL_YUY2_OVERLAY, gb_screen);

	if (!overlay) return;

	if (!overlay->hw_overlay || overlay->planes > 1)
	{
		SDL_FreeYUVOverlay(overlay);
		overlay = 0;
		return;
	}

	SDL_LockYUVOverlay(overlay);
	
	fb.w = 160;
	fb.h = 144;
	fb.pelsize = 4;
	fb.pitch = overlay->pitches[0];
	fb.ptr = overlay->pixels[0];
	fb.yuv = 1;
	fb.cc[0].r = fb.cc[1].r = fb.cc[2].r = fb.cc[3].r = 0;
	fb.dirty = 1;
	fb.enabled = 1;
	
	overlay_rect.x = 0;
	overlay_rect.y = 0;
	overlay_rect.w = vmode[0];
	overlay_rect.h = vmode[1];

	/* Color channels are 0=Y, 1=U, 2=V, 3=Y1 */
	switch (overlay->format)
	{
		/* FIXME - support more formats */
	case SDL_YUY2_OVERLAY:
	default:
		fb.cc[0].l = 0;
		fb.cc[1].l = 24;
		fb.cc[2].l = 8;
		fb.cc[3].l = 16;
		break;
	}
	
	SDL_UnlockYUVOverlay(overlay);
}

void vid_init()
{
	int flags;

	if (!vmode[0] || !vmode[1])
	{
		int scale = rc_getint("scale");
		if (scale < 1) scale = 1;
		vmode[0] = 160 * scale;
		vmode[1] = 144 * scale;
	}
	
	//flags = SDL_ANYFORMAT | SDL_HWPALETTE | SDL_HWSURFACE;
	flags = SDL_HWPALETTE | SDL_HWSURFACE;

	if (fullscreen)
		flags |= SDL_FULLSCREEN;

	if (SDL_Init(SDL_INIT_VIDEO))
		die("SDL: Couldn't initialize SDL: %s\n", SDL_GetError());

	if(TTF_Init()){
        die("Error TTF_Init: %s\n", TTF_GetError());
	}

	/*if (!(gb_screen = SDL_SetVideoMode(RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL, 16, flags)))
		die("SDL: can't set video mode: %s\n", SDL_GetError());
	gb_screen = SDL_CreateRGBSurface(SDL_SWSURFACE,
		vmode[0], vmode[1], 16, 0xFFFF, 0xFFFF, 0xFFFF, 0);***/

	if (!(hw_screen = SDL_SetVideoMode(RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL, 16, flags))){
		die("SDL: can't set video mode: %s\n", SDL_GetError());
	}
	//printf("hw_surface pixel format: %d bpb, expected: 16 bpb\n", hw_screen->format->BitsPerPixel);
	gb_screen = SDL_CreateRGBSurface(SDL_SWSURFACE, vmode[0], vmode[1], 16, 0, 0, 0, 0);
	virtual_hw_screen = SDL_CreateRGBSurface(SDL_SWSURFACE, RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL, 16, 0, 0, 0, 0);


	SDL_ShowCursor(0);

	joystick_init();

	overlay_init();

	init_menu_SDL();
	
	if (fb.yuv) return;
	
	SDL_LockSurface(gb_screen);
	
	fb.w = gb_screen->w;
	fb.h = gb_screen->h;
	fb.pelsize = gb_screen->format->BytesPerPixel;
	fb.pitch = gb_screen->pitch;
	fb.indexed = fb.pelsize == 1;
	fb.ptr = gb_screen->pixels;
	fb.cc[0].r = gb_screen->format->Rloss;
	fb.cc[0].l = gb_screen->format->Rshift;
	fb.cc[1].r = gb_screen->format->Gloss;
	fb.cc[1].l = gb_screen->format->Gshift;
	fb.cc[2].r = gb_screen->format->Bloss;
	fb.cc[2].l = gb_screen->format->Bshift;

	SDL_UnlockSurface(gb_screen);

	fb.enabled = 1;
	fb.dirty = 0;
	
}


void ev_poll()
{
	event_t ev;
	SDL_Event event;
	int axisval;

	while (SDL_PollEvent(&event))
	{
		switch(event.type)
		{
		case SDL_ACTIVEEVENT:
			if (event.active.state == SDL_APPACTIVE)
				fb.enabled = event.active.gain;
			break;
		case SDL_KEYDOWN:
			if ((event.key.keysym.sym == SDLK_RETURN) && (event.key.keysym.mod & KMOD_ALT))
				SDL_WM_ToggleFullScreen(gb_screen);
			ev.type = EV_PRESS;
			ev.code = mapscancode(event.key.keysym.sym);
			ev_postevent(&ev);
			break;
		case SDL_KEYUP:
			ev.type = EV_RELEASE;
			ev.code = mapscancode(event.key.keysym.sym);
			ev_postevent(&ev);
			break;
		case SDL_JOYAXISMOTION:
			switch (event.jaxis.axis)
			{
			case 0: /* X axis */
				axisval = event.jaxis.value;
				if (axisval > joy_commit_range)
				{
					if (Xstatus==2) break;
					
					if (Xstatus==0)
					{
						ev.type = EV_RELEASE;
						ev.code = K_JOYLEFT;
        			  		ev_postevent(&ev);				 		
					}
					
					ev.type = EV_PRESS;
					ev.code = K_JOYRIGHT;
					ev_postevent(&ev);
					Xstatus=2;
					break;
				}
				
				if (axisval < -(joy_commit_range))
				{
					if (Xstatus==0) break;
					
					if (Xstatus==2)
					{
						ev.type = EV_RELEASE;
						ev.code = K_JOYRIGHT;
        			  		ev_postevent(&ev);				 		
					}
					
					ev.type = EV_PRESS;
					ev.code = K_JOYLEFT;
					ev_postevent(&ev);
					Xstatus=0;
					break;
				}
				
				/* if control reaches here, the axis is centered,
				 * so just send a release signal if necisary */
				
				if (Xstatus==2)
				{
					ev.type = EV_RELEASE;
					ev.code = K_JOYRIGHT;
					ev_postevent(&ev);
				}
				
				if (Xstatus==0)
				{
					ev.type = EV_RELEASE;
					ev.code = K_JOYLEFT;
					ev_postevent(&ev);
				}
				Xstatus=1;
				break;
				
			case 1: /* Y axis*/
				axisval = event.jaxis.value;
				if (axisval > joy_commit_range)
				{
					if (Ystatus==2) break;
					
					if (Ystatus==0)
					{
						ev.type = EV_RELEASE;
						ev.code = K_JOYUP;
        			  		ev_postevent(&ev);				 		
					}
					
					ev.type = EV_PRESS;
					ev.code = K_JOYDOWN;
					ev_postevent(&ev);
					Ystatus=2;
					break;
				}
				
				if (axisval < -joy_commit_range)
				{
					if (Ystatus==0) break;
					
					if (Ystatus==2)
					{
						ev.type = EV_RELEASE;
						ev.code = K_JOYDOWN;
        			  		ev_postevent(&ev);
					}
					
					ev.type = EV_PRESS;
					ev.code = K_JOYUP;
					ev_postevent(&ev);
					Ystatus=0;
					break;
				}
				
				/* if control reaches here, the axis is centered,
				 * so just send a release signal if necisary */
				
				if (Ystatus==2)
				{
					ev.type = EV_RELEASE;
					ev.code = K_JOYDOWN;
					ev_postevent(&ev);
				}
				
				if (Ystatus==0)
				{
					ev.type = EV_RELEASE;
					ev.code = K_JOYUP;
					ev_postevent(&ev);
				}
				Ystatus=1;
				break;
			}
			break;
		case SDL_JOYBUTTONUP:
			if (event.jbutton.button>15) break;
			ev.type = EV_RELEASE;
			ev.code = K_JOY0 + event.jbutton.button;
			ev_postevent(&ev);
			break;
		case SDL_JOYBUTTONDOWN:
			if (event.jbutton.button>15) break;
			ev.type = EV_PRESS;
			ev.code = K_JOY0+event.jbutton.button;
			ev_postevent(&ev);
			break;
		case SDL_QUIT:
			exit(1);
			break;
		default:
			break;
		}
	}
}

SDL_Surface * vid_getwindow(){
	return virtual_hw_screen;
}

void vid_setpal(int i, int r, int g, int b)
{
	SDL_Color col;

	col.r = r; col.g = g; col.b = b;

	SDL_SetColors(gb_screen, &col, i, 1);
}

void vid_preinit()
{
}

void vid_close()
{
	if (overlay)
	{
		SDL_UnlockYUVOverlay(overlay);
		SDL_FreeYUVOverlay(overlay);
	}
	else{
		SDL_UnlockSurface(gb_screen);
	}

	deinit_menu_SDL();
	SDL_FreeSurface(gb_screen);
	SDL_FreeSurface(virtual_hw_screen);
	TTF_Quit();
	SDL_Quit();
	fb.enabled = 0;
}

void vid_settitle(char *title)
{
	SDL_WM_SetCaption(title, title);
}

void vid_begin()
{
	if (overlay)
	{
		SDL_LockYUVOverlay(overlay);
		fb.ptr = overlay->pixels[0];
		return;
	}
	SDL_LockSurface(gb_screen);
	fb.ptr = gb_screen->pixels;
}

/// Nearest neighboor optimized with possible out of gb_screen coordinates (for cropping)
void flip_NNOptimized_AllowOutOfScreen(SDL_Surface *virtual_screen, SDL_Surface *hardware_screen, int new_w, int new_h){
  int w1=virtual_screen->w;
  //int h1=virtual_screen->h;
  int w2=new_w;
  int h2=new_h;
  int x_ratio = (int)((virtual_screen->w<<16)/w2);
  int y_ratio = (int)((virtual_screen->h<<16)/h2);
  int x2, y2 ;

  /// --- Compute padding for centering when out of bounds ---
  int y_padding = (RES_HW_SCREEN_VERTICAL-new_h)/2;
  int x_padding = 0;
  if(w2>RES_HW_SCREEN_HORIZONTAL){
    x_padding = (w2-RES_HW_SCREEN_HORIZONTAL)/2 + 1;
  }
  int x_padding_ratio = x_padding*w1/w2;
  //printf("virtual_screen->h=%d, h2=%d\n", virtual_screen->h, h2);

  for (int i=0;i<h2;i++)
  {
    if(i>=RES_HW_SCREEN_VERTICAL){
      continue;
    }

    uint16_t* t = ( (uint16_t*)hardware_screen->pixels + (i+y_padding)* ((w2>RES_HW_SCREEN_HORIZONTAL)?RES_HW_SCREEN_HORIZONTAL:w2) );
    y2 = ((i*y_ratio)>>16);
    uint16_t* p = ( (uint16_t*)virtual_screen->pixels + y2*w1 + x_padding_ratio );
    int rat = 0;
    for (int j=0;j<w2;j++)
    {
      if(j>=RES_HW_SCREEN_HORIZONTAL){
        continue;
      }
      x2 = (rat>>16);
#ifdef BLACKER_BLACKS
      *t++ = p[x2] & 0xFFDF; /// Optimization for blacker blacks
#else
      *t++ = p[x2]; /// Optimization for blacker blacks
#endif
      rat += x_ratio;
      //printf("y=%d, x=%d, y2=%d, x2=%d, (y2*virtual_screen->w)+x2=%d\n", i, j, y2, x2, (y2*virtual_screen->w)+x2);
    }
  }
}


void SDL_Rotate_270(SDL_Surface * hw_surface, SDL_Surface * virtual_hw_surface){
  int i, j;
    uint16_t *source_pixels = (uint16_t*) virtual_hw_surface->pixels;
    uint16_t *dest_pixels = (uint16_t*) hw_surface->pixels;

    /// --- Checking for right pixel format ---
    //MENU_DEBUG_PRINTF("Source bpb = %d, Dest bpb = %d\n", virtual_hw_surface->format->BitsPerPixel, hw_surface->format->BitsPerPixel);
    if(virtual_hw_surface->format->BitsPerPixel != 16){
      printf("Error in SDL_Rotate_270, Wrong virtual_hw_surface pixel format: %d bpb, expected: 16 bpb\n", virtual_hw_surface->format->BitsPerPixel);
      return;
    }
    if(hw_surface->format->BitsPerPixel != 16){
      printf("Error in SDL_Rotate_270, Wrong hw_surface pixel format: %d bpb, expected: 16 bpb\n", hw_surface->format->BitsPerPixel);
      return;
    }

    /// --- Checking if same dimensions ---
    if(hw_surface->w != virtual_hw_surface->w || hw_surface->h != virtual_hw_surface->h){
      printf("Error in SDL_Rotate_270, hw_surface (%dx%d) and virtual_hw_surface (%dx%d) have different dimensions\n",
        hw_surface->w, hw_surface->h, virtual_hw_surface->w, virtual_hw_surface->h);
      return;
    }

  /// --- Pixel copy and rotation (270) ---
  uint16_t *cur_p_src, *cur_p_dst;
  for(i=0; i<virtual_hw_surface->h; i++){
    for(j=0; j<virtual_hw_surface->w; j++){
      cur_p_src = source_pixels + i*virtual_hw_surface->w + j;
      cur_p_dst = dest_pixels + (hw_surface->h-1-j)*hw_surface->w + i;
      *cur_p_dst = *cur_p_src;
    }
  }
}

void vid_flip(){
#if 0
	/// Rotate
	SDL_Rotate_270(hw_screen, virtual_hw_screen);
#endif

	//SDL_BlitSurface(virtual_hw_screen, NULL, hw_screen, NULL);
    memcpy(hw_screen->pixels, virtual_hw_screen->pixels, hw_screen->h*hw_screen->w*sizeof(uint16_t));

	SDL_Flip(hw_screen);
}

void vid_end()
{
	if (overlay)
	{
		SDL_UnlockYUVOverlay(overlay);
		if (fb.enabled)
			SDL_DisplayYUVOverlay(overlay, &overlay_rect);
		return;
	}
	SDL_UnlockSurface(gb_screen);

	/// Fullscreen
	flip_NNOptimized_AllowOutOfScreen(gb_screen, virtual_hw_screen,
        RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL);

	if (fb.enabled) vid_flip();
}



















































/// --------------------------------------------
/// ------------- MENU Stuff added -------------
/// --------------------------------------------
void init_menu_SDL(){
	MENU_DEBUG_PRINTF("Init Menu\n");

    /// ----- Loading the fonts -----
    menu_title_font = TTF_OpenFont(MENU_FONT_NAME_TITLE, MENU_FONT_SIZE_TITLE);
    if(!menu_title_font){
        MENU_ERROR_PRINTF("ERROR in init_menu_SDL: Could not open menu font %s, %s\n", MENU_FONT_NAME_TITLE, SDL_GetError());
    }
    menu_info_font = TTF_OpenFont(MENU_FONT_NAME_INFO, MENU_FONT_SIZE_INFO);
    if(!menu_info_font){
        MENU_ERROR_PRINTF("ERROR in init_menu_SDL: Could not open menu font %s, %s\n", MENU_FONT_NAME_INFO, SDL_GetError());
    }
    menu_small_info_font = TTF_OpenFont(MENU_FONT_NAME_SMALL_INFO, MENU_FONT_SIZE_SMALL_INFO);
    if(!menu_small_info_font){
        MENU_ERROR_PRINTF("ERROR in init_menu_SDL: Could not open menu font %s, %s\n", MENU_FONT_NAME_SMALL_INFO, SDL_GetError());
    }

    /// ----- Copy virtual_hw_screen at init ------
    SDL_Surface *virtual_hw_screen = vid_getwindow();
    backup_hw_screen = SDL_CreateRGBSurface(SDL_SWSURFACE,
        virtual_hw_screen->w, virtual_hw_screen->h, 16, 0, 0, 0, 0);
    if(backup_hw_screen == NULL){
		MENU_ERROR_PRINTF("ERROR in init_menu_SDL: Could not create backup_hw_screen: %s\n", SDL_GetError());
	}

	/// ------ Load arrows imgs -------
	img_arrow_top = IMG_Load(MENU_PNG_ARROW_TOP_PATH);
	if(!img_arrow_top) {
		MENU_ERROR_PRINTF("ERROR IMG_Load: %s\n", IMG_GetError());
	}
	img_arrow_bottom = IMG_Load(MENU_PNG_ARROW_BOTTOM_PATH);
	if(!img_arrow_bottom) {
		MENU_ERROR_PRINTF("ERROR IMG_Load: %s\n", IMG_GetError());
	}

	/// ------ Init menu zones ------
	init_menu_zones();
}

void deinit_menu_SDL(){
	MENU_DEBUG_PRINTF("End Menu \n");

    /// ------ Close font -------
    TTF_CloseFont(menu_title_font);
    TTF_CloseFont(menu_info_font);
    TTF_CloseFont(menu_small_info_font);

    /// ------ Free Surfaces -------
    for(int i=0; i < nb_menu_zones; i++){
        SDL_FreeSurface(menu_zone_surfaces[i]);
    }

	if(backup_hw_screen != NULL){
		SDL_FreeSurface(backup_hw_screen);
	}

	SDL_FreeSurface(img_arrow_top);
	SDL_FreeSurface(img_arrow_bottom);

    /// ------ Free Menu memory and reset vars -----
    if(idx_menus){
        free(idx_menus);
    }
    idx_menus=NULL;
    nb_menu_zones = 0;
}


void draw_progress_bar(SDL_Surface * surface, uint16_t x, uint16_t y, uint16_t width,
                        uint16_t height, uint8_t percentage, uint16_t nb_bars){
    /// ------ Init Variables ------
    uint16_t line_width = 1; //px
    uint16_t padding_bars_ratio = 3;
    uint16_t nb_full_bars = 0;

    /// ------ Check values ------
    percentage = (percentage > 100)?100:percentage;
    x = (x > (surface->w-1))?(surface->w-1):x;
    y = (y > surface->h-1)?(surface->h-1):y;
    width = (width < line_width*2+1)?(line_width*2+1):width;
    width = (width > surface->w-x-1)?(surface->w-x-1):width;
    height = (height < line_width*2+1)?(line_width*2+1):height;
    height = (height > surface->h-y-1)?(surface->h-y-1):height;
    uint16_t nb_bars_max = ( width * padding_bars_ratio  /  (line_width*2+1) + 1 ) / (padding_bars_ratio+1);
    nb_bars = (nb_bars > nb_bars_max)?nb_bars_max:nb_bars;
    uint16_t bar_width = (width / nb_bars)*padding_bars_ratio/(padding_bars_ratio+1)+1;
    uint16_t bar_padding_x = bar_width/padding_bars_ratio;
    nb_full_bars = nb_bars*percentage/100;

    /// ------ draw full bars ------
    for (int i = 0; i < nb_full_bars; ++i)
    {
        /// ---- draw one bar ----
        //MENU_DEBUG_PRINTF("Drawing filled bar %d\n", i);
        SDL_Rect rect = {x+ i*(bar_width +bar_padding_x),
            y, bar_width, height};
        SDL_FillRect(surface, &rect, SDL_MapRGB(surface->format, GRAY_MAIN_R, GRAY_MAIN_G, GRAY_MAIN_B));
    }

    /// ------ draw full bars ------
    for (int i = 0; i < (nb_bars-nb_full_bars); ++i)
    {
        /// ---- draw one bar ----
        //MENU_DEBUG_PRINTF("Drawing empty bar %d\n", i);
        SDL_Rect rect = {x+ i*(bar_width +bar_padding_x) + nb_full_bars*(bar_width +bar_padding_x),
            y, bar_width, height};
        SDL_FillRect(surface, &rect, SDL_MapRGB(surface->format, GRAY_MAIN_R, GRAY_MAIN_G, GRAY_MAIN_B));

        SDL_Rect rect2 = {x+ i*(bar_width +bar_padding_x) + line_width + nb_full_bars*(bar_width +bar_padding_x),
            y + line_width, bar_width - line_width*2, height - line_width*2};
        SDL_FillRect(surface, &rect2, SDL_MapRGB(surface->format, WHITE_MAIN_R, WHITE_MAIN_R, WHITE_MAIN_R));
    }


}


void add_menu_zone(ENUM_MENU_TYPE menu_type){
    /// ------ Increase nb of menu zones -------
    nb_menu_zones++;

    /// ------ Realoc idx Menus array -------
    if(!idx_menus){
        idx_menus = (int*) malloc(nb_menu_zones*sizeof(int));
        menu_zone_surfaces = (SDL_Surface**) malloc(nb_menu_zones*sizeof(SDL_Surface*));
    }
    else{
        int *temp = (int*) realloc(idx_menus, nb_menu_zones*sizeof(int));
        idx_menus = temp;
        menu_zone_surfaces = (SDL_Surface**) realloc(menu_zone_surfaces, nb_menu_zones*sizeof(SDL_Surface*));
    }
    idx_menus[nb_menu_zones-1] = menu_type;

    /// ------ Reinit menu surface with height increased -------
    menu_zone_surfaces[nb_menu_zones-1] = IMG_Load(MENU_PNG_BG_PATH);
    if(!menu_zone_surfaces[nb_menu_zones-1]) {
        MENU_ERROR_PRINTF("ERROR IMG_Load: %s\n", IMG_GetError());
    }
    /// --------- Init Common Variables --------
    SDL_Surface *text_surface = NULL;
    SDL_Surface *surface = menu_zone_surfaces[nb_menu_zones-1];
    SDL_Rect text_pos;

    /// --------- Add new zone ---------
    switch(menu_type){
    case MENU_TYPE_VOLUME:
        MENU_DEBUG_PRINTF("Init MENU_TYPE_VOLUME\n");
        /// ------ Text ------
        text_surface = TTF_RenderText_Blended(menu_title_font, "VOLUME", text_color);
        text_pos.x = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
        text_pos.y = surface->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 - padding_y_from_center_menu_zone;
        SDL_BlitSurface(text_surface, NULL, surface, &text_pos);

        x_volume_bar = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - width_progress_bar)/2;
        y_volume_bar = surface->h - MENU_ZONE_HEIGHT/2 - height_progress_bar/2 + padding_y_from_center_menu_zone;
        draw_progress_bar(surface, x_volume_bar, y_volume_bar,
            width_progress_bar, height_progress_bar, 0, 100/STEP_CHANGE_VOLUME);
        break;
    case MENU_TYPE_BRIGHTNESS:
        MENU_DEBUG_PRINTF("Init MENU_TYPE_BRIGHTNESS\n");
        /// ------ Text ------
        text_surface = TTF_RenderText_Blended(menu_title_font, "BRIGHTNESS", text_color);
        text_pos.x = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
        text_pos.y = surface->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 - padding_y_from_center_menu_zone;
        SDL_BlitSurface(text_surface, NULL, surface, &text_pos);

        x_brightness_bar = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - width_progress_bar)/2;
        y_brightness_bar = surface->h - MENU_ZONE_HEIGHT/2 - height_progress_bar/2 + padding_y_from_center_menu_zone;
        draw_progress_bar(surface, x_brightness_bar, y_brightness_bar,
            width_progress_bar, height_progress_bar, 0, 100/STEP_CHANGE_BRIGHTNESS);
        break;
    case MENU_TYPE_SAVE:
        MENU_DEBUG_PRINTF("Init MENU_TYPE_SAVE\n");
        /// ------ Text ------
        text_surface = TTF_RenderText_Blended(menu_title_font, "SAVE", text_color);
        text_pos.x = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
        text_pos.y = surface->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 - padding_y_from_center_menu_zone*2;
        SDL_BlitSurface(text_surface, NULL, surface, &text_pos);
        break;
    case MENU_TYPE_LOAD:
        MENU_DEBUG_PRINTF("Init MENU_TYPE_LOAD\n");
        /// ------ Text ------
        text_surface = TTF_RenderText_Blended(menu_title_font, "LOAD", text_color);
        text_pos.x = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
        text_pos.y = surface->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 - padding_y_from_center_menu_zone*2;
        SDL_BlitSurface(text_surface, NULL, surface, &text_pos);
        break;
    case MENU_TYPE_ASPECT_RATIO:
        MENU_DEBUG_PRINTF("Init MENU_TYPE_ASPECT_RATIO\n");
        /// ------ Text ------
        text_surface = TTF_RenderText_Blended(menu_title_font, "ASPECT RATIO", text_color);
        text_pos.x = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
        text_pos.y = surface->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 - padding_y_from_center_menu_zone;
        SDL_BlitSurface(text_surface, NULL, surface, &text_pos);
        break;
    case MENU_TYPE_EXIT:
        MENU_DEBUG_PRINTF("Init MENU_TYPE_EXIT\n");
        /// ------ Text ------
        text_surface = TTF_RenderText_Blended(menu_title_font, "EXIT GAME", text_color);
        text_pos.x = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
        text_pos.y = surface->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2;
        SDL_BlitSurface(text_surface, NULL, surface, &text_pos);
        break;
    case MENU_TYPE_POWERDOWN:
        MENU_DEBUG_PRINTF("Init MENU_TYPE_POWERDOWN\n");
        /// ------ Text ------
        text_surface = TTF_RenderText_Blended(menu_title_font, "POWERDOWN", text_color);
        text_pos.x = (surface->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
        text_pos.y = surface->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2;
        SDL_BlitSurface(text_surface, NULL, surface, &text_pos);
        break;
    default:
        MENU_DEBUG_PRINTF("Warning - In add_menu_zone, unknown MENU_TYPE: %d\n", menu_type);
        break;
    }

    /// ------ Free Surfaces -------
    SDL_FreeSurface(text_surface);
}

void init_menu_zones(){
    /// Init Volume Menu
    add_menu_zone(MENU_TYPE_VOLUME);
    /// Init Brightness Menu
    add_menu_zone(MENU_TYPE_BRIGHTNESS);
    /// Init Save Menu
    add_menu_zone(MENU_TYPE_SAVE);
    /// Init Load Menu
    add_menu_zone(MENU_TYPE_LOAD);
    /// Init Aspect Ratio Menu
    //add_menu_zone(MENU_TYPE_ASPECT_RATIO);
    /// Init Exit Menu
    add_menu_zone(MENU_TYPE_EXIT);
    /// Init Powerdown Menu
    //add_menu_zone(MENU_TYPE_POWERDOWN);
}


void init_menu_system_values(){
    FILE *fp;
    char res[100];

    /// ------- Get system volume percentage --------
    fp = popen(SHELL_CMD_VOLUME_GET, "r");
    if (fp == NULL) {
        MENU_ERROR_PRINTF("Failed to run command %s\n", SHELL_CMD_VOLUME_GET );
        volume_percentage = 50; ///wrong value: setting default to 50
    }
    else{
        fgets(res, sizeof(res)-1, fp);

        /// Check if Volume is a number (at least the first char)
        if(res[0] < '0' || res[0] > '9'){
            MENU_ERROR_PRINTF("Wrong return value: %s for volume cmd: %s\n",res, SHELL_CMD_VOLUME_GET);
            volume_percentage = 50; ///wrong value: setting default to 50
        }
        else{
            volume_percentage = atoi(res);
            MENU_DEBUG_PRINTF("System volume = %d%%\n", volume_percentage);
        }
    }

    /// ------- Get system brightness percentage -------
    fp = popen(SHELL_CMD_BRIGHTNESS_GET, "r");
    if (fp == NULL) {
        MENU_ERROR_PRINTF("Failed to run command %s\n", SHELL_CMD_BRIGHTNESS_GET );
        brightness_percentage = 50; ///wrong value: setting default to 50
    }
    else{
        fgets(res, sizeof(res)-1, fp);

        /// Check if brightness is a number (at least the first char)
        if(res[0] < '0' || res[0] > '9'){
            MENU_ERROR_PRINTF("Wrong return value: %s for volume cmd: %s\n",res, SHELL_CMD_BRIGHTNESS_GET);
            brightness_percentage = 50; ///wrong value: setting default to 50
        }
        else{
            brightness_percentage = atoi(res);
            MENU_DEBUG_PRINTF("System brightness = %d%%\n", brightness_percentage);
        }
    }

	/// ------ Save prev key repeat params and set new Key repeat -------
	SDL_GetKeyRepeat(&backup_key_repeat_delay, &backup_key_repeat_interval);
	if(SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL)){
		MENU_ERROR_PRINTF("ERROR with SDL_EnableKeyRepeat: %s\n", SDL_GetError());
	}

    /// Get save slot from game
    saveslot = (saveslot%MAX_SAVE_SLOTS); // security
}

void menu_screen_refresh(int menuItem, int prevItem, int scroll, uint8_t menu_confirmation, uint8_t menu_action){
    /// --------- Vars ---------
	int print_arrows = 1;

    /// --------- Get screen ----------
    SDL_Surface *virtual_hw_screen = vid_getwindow();

    /// --------- Clear HW screen ----------
    if(SDL_BlitSurface(backup_hw_screen, NULL, virtual_hw_screen, NULL)){
        MENU_ERROR_PRINTF("ERROR Could not Clear virtual_hw_screen: %s\n", SDL_GetError());
    }

    /// --------- Setup Blit Window ----------
    SDL_Rect menu_blit_window;
    menu_blit_window.x = 0;
    menu_blit_window.w = SCREEN_HORIZONTAL_SIZE;

    /// --------- Blit prev menu Zone going away ----------
    menu_blit_window.y = scroll;
    menu_blit_window.h = SCREEN_VERTICAL_SIZE;
    if(SDL_BlitSurface(menu_zone_surfaces[prevItem], &menu_blit_window, virtual_hw_screen, NULL)){
        MENU_ERROR_PRINTF("ERROR Could not Blit surface on virtual_hw_screen: %s\n", SDL_GetError());
    }

    /// --------- Blit new menu Zone going in (only during animations) ----------
    if(scroll>0){
        menu_blit_window.y = SCREEN_VERTICAL_SIZE-scroll;
        menu_blit_window.h = SCREEN_VERTICAL_SIZE;
        if(SDL_BlitSurface(menu_zone_surfaces[menuItem], NULL, virtual_hw_screen, &menu_blit_window)){
            MENU_ERROR_PRINTF("ERROR Could not Blit surface on virtual_hw_screen: %s\n", SDL_GetError());
        }
    }
    else if(scroll<0){
        menu_blit_window.y = SCREEN_VERTICAL_SIZE+scroll;
        menu_blit_window.h = SCREEN_VERTICAL_SIZE;
        if(SDL_BlitSurface(menu_zone_surfaces[menuItem], &menu_blit_window, virtual_hw_screen, NULL)){
            MENU_ERROR_PRINTF("ERROR Could not Blit surface on virtual_hw_screen: %s\n", SDL_GetError());
        }
    }
    /// --------- No Scroll ? Blitting menu-specific info
    else{
        SDL_Surface * text_surface = NULL;
        char text_tmp[40];
        SDL_Rect text_pos;
        char fname[MAXPATHLEN];
        memset(fname, 0, MAXPATHLEN);

        switch(idx_menus[menuItem]){
        case MENU_TYPE_VOLUME:
            draw_progress_bar(virtual_hw_screen, x_volume_bar, y_volume_bar,
                            width_progress_bar, height_progress_bar, volume_percentage, 100/STEP_CHANGE_VOLUME);
            break;

        case MENU_TYPE_BRIGHTNESS:
            draw_progress_bar(virtual_hw_screen, x_volume_bar, y_volume_bar,
                            width_progress_bar, height_progress_bar, brightness_percentage, 100/STEP_CHANGE_BRIGHTNESS);
            break;

        case MENU_TYPE_SAVE:
            /// ---- Write slot -----
            sprintf(text_tmp, "IN SLOT   < %d >", saveslot+1);
            text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
            text_pos.x = (virtual_hw_screen->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
            text_pos.y = virtual_hw_screen->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2;
            SDL_BlitSurface(text_surface, NULL, virtual_hw_screen, &text_pos);

            if(menu_action){
                sprintf(text_tmp, "Saving...");
                text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
            }
            else{
                if(menu_confirmation){
                    sprintf(text_tmp, "Are you sure ?");
                    text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
                }
                else{
                    /// ---- Write current Save state ----
                    if(check_savefile(-1, fname)){
		        //printf("Found Save slot: %s\n", fname);
                        char *p = strrchr (fname, '/');
                        char *basename = p ? p + 1 : (char *) fname;
                        char file_name_short[24];
                        snprintf(file_name_short, 24, "%s", basename);
                        text_surface = TTF_RenderText_Blended(menu_small_info_font, file_name_short, text_color);
                    }
                    else{
                        text_surface = TTF_RenderText_Blended(menu_info_font, "Free", text_color);
                    }
                }
            }
            text_pos.x = (virtual_hw_screen->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
            text_pos.y = virtual_hw_screen->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 + 2*padding_y_from_center_menu_zone;
            SDL_BlitSurface(text_surface, NULL, virtual_hw_screen, &text_pos);
            break;

        case MENU_TYPE_LOAD:
            /// ---- Write slot -----
            sprintf(text_tmp, "FROM SLOT   < %d >", saveslot+1);
            text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
            text_pos.x = (virtual_hw_screen->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
            text_pos.y = virtual_hw_screen->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2;
            SDL_BlitSurface(text_surface, NULL, virtual_hw_screen, &text_pos);

            if(menu_action){
                sprintf(text_tmp, "Loading...");
                text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
            }
            else{
                if(menu_confirmation){
                    sprintf(text_tmp, "Are you sure ?");
                    text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
                }
                else{
                    /// ---- Write current Load state ----
                    if(check_savefile(-1, fname)){
		        //printf("Found Load slot: %s\n", fname);
                        char *p = strrchr (fname, '/');
                        char *basename = p ? p + 1 : (char *) fname;
                        char file_name_short[24];
                        snprintf(file_name_short, 24, "%s", basename);
                        text_surface = TTF_RenderText_Blended(menu_small_info_font, file_name_short, text_color);
                    }
                    else{
                        text_surface = TTF_RenderText_Blended(menu_info_font, "Free", text_color);
                    }
                }
            }
            text_pos.x = (virtual_hw_screen->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
            text_pos.y = virtual_hw_screen->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 + 2*padding_y_from_center_menu_zone;
            SDL_BlitSurface(text_surface, NULL, virtual_hw_screen, &text_pos);
            break;

        case MENU_TYPE_ASPECT_RATIO:
            sprintf(text_tmp, "<   %s   >", aspect_ratio_name[aspect_ratio]);
            text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
            text_pos.x = (virtual_hw_screen->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
            text_pos.y = virtual_hw_screen->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 + padding_y_from_center_menu_zone;
            SDL_BlitSurface(text_surface, NULL, virtual_hw_screen, &text_pos);
            break;

        case MENU_TYPE_EXIT:
        case MENU_TYPE_POWERDOWN:
            if(menu_confirmation){
                sprintf(text_tmp, "Are you sure ?");
                text_surface = TTF_RenderText_Blended(menu_info_font, text_tmp, text_color);
                text_pos.x = (virtual_hw_screen->w - MENU_ZONE_WIDTH)/2 + (MENU_ZONE_WIDTH - text_surface->w)/2;
                text_pos.y = virtual_hw_screen->h - MENU_ZONE_HEIGHT/2 - text_surface->h/2 + 2*padding_y_from_center_menu_zone;
                SDL_BlitSurface(text_surface, NULL, virtual_hw_screen, &text_pos);
            }
            break;
        default:
            break;
        }

        /// ------ Free Surfaces -------
        if(text_surface)
             SDL_FreeSurface(text_surface);
    }

	/// --------- Print arrows --------
	if(print_arrows){
		/// Top arrow
		SDL_Rect pos_arrow_top;
		pos_arrow_top.x = (virtual_hw_screen->w - img_arrow_top->w)/2;
		pos_arrow_top.y = (virtual_hw_screen->h - MENU_BG_SQUREE_HEIGHT)/4 - img_arrow_top->h/2;
		SDL_BlitSurface(img_arrow_top, NULL, virtual_hw_screen, &pos_arrow_top);

		/// Bottom arrow
		SDL_Rect pos_arrow_bottom;
		pos_arrow_bottom.x = (virtual_hw_screen->w - img_arrow_bottom->w)/2;
		pos_arrow_bottom.y = virtual_hw_screen->h -
			(virtual_hw_screen->h - MENU_BG_SQUREE_HEIGHT)/4 - img_arrow_bottom->h/2;
		SDL_BlitSurface(img_arrow_bottom, NULL, virtual_hw_screen, &pos_arrow_bottom);
	}

    /// --------- Flip Screen ----------
    vid_flip();
}


void run_menu_loop()
{
	MENU_DEBUG_PRINTF("Launch Menu\n");

    SDL_Event event;
    uint32_t prev_ms = SDL_GetTicks();
    uint32_t cur_ms = SDL_GetTicks();
    int scroll=0;
	int start_scroll=0;
    uint8_t screen_refresh = 1;
    char shell_cmd[100];
    FILE *fp;
    uint8_t menu_confirmation = 0;
    stop_menu_loop = 0;
    char fname[MAXPATHLEN];

	/// ------ Get init values -------
	init_menu_system_values();
	int prevItem=menuItem;

    /// ------ Get screen -------
    SDL_Surface *virtual_hw_screen = vid_getwindow();

    /// ------ Copy currently displayed screen -------
    if(SDL_BlitSurface(virtual_hw_screen, NULL, backup_hw_screen, NULL)){
        MENU_ERROR_PRINTF("ERROR Could not copy virtual_hw_screen: %s\n", SDL_GetError());
    }

    /// -------- Main loop ---------
    while (!stop_menu_loop)
    {
        /// -------- Handle Keyboard Events ---------
        if(!scroll){
            while (SDL_PollEvent(&event))
            switch(event.type)
            {
	    case SDL_QUIT:
		exit(0);
                stop_menu_loop = 1;
		break;
	    case SDL_KEYDOWN:
                switch (event.key.keysym.sym)
                {
                    case SDLK_b:
                        if(menu_confirmation){
                            /// ------ Reset menu confirmation ------
                            menu_confirmation = 0;
                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        /*else{
                            stop_menu_loop = 1;
                        }*/
                        break;

                    case SDLK_q:
                    case SDLK_ESCAPE:
                        stop_menu_loop = 1;
                        break;

                    case SDLK_d:
                    case SDLK_DOWN:
                        MENU_DEBUG_PRINTF("DOWN\n");

						/// ------ Start scrolling to new menu -------
						menuItem++;
						if (menuItem>=nb_menu_zones) menuItem=0;
						start_scroll=1;

						/// ------ Reset menu confirmation ------
						menu_confirmation = 0;

						/// ------ Refresh screen ------
						screen_refresh = 1;
                        break;

                    case SDLK_u:
                    case SDLK_UP:
                        MENU_DEBUG_PRINTF("UP\n");

						/// ------ Start scrolling to new menu -------
						menuItem--;
						if (menuItem<0) menuItem=nb_menu_zones-1;
						start_scroll=-1;

						/// ------ Reset menu confirmation ------
						menu_confirmation = 0;

						/// ------ Refresh screen ------
						screen_refresh = 1;
                        break;

                    case SDLK_l:
                    case SDLK_LEFT:
                        //MENU_DEBUG_PRINTF("LEFT\n");
                        if(idx_menus[menuItem] == MENU_TYPE_VOLUME){
                            MENU_DEBUG_PRINTF("Volume DOWN\n");
                            /// ----- Compute new value -----
                            volume_percentage = (volume_percentage < STEP_CHANGE_VOLUME)?
                                                    0:(volume_percentage-STEP_CHANGE_VOLUME);

                            /// ----- Shell cmd ----
                            sprintf(shell_cmd, "%s %d", SHELL_CMD_VOLUME_SET, volume_percentage);
                            fp = popen(shell_cmd, "r");
                            if (fp == NULL) {
                                MENU_ERROR_PRINTF("Failed to run command %s\n", shell_cmd);
                            }

                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_BRIGHTNESS){
                            MENU_DEBUG_PRINTF("Brightness DOWN\n");
                            /// ----- Compute new value -----
                            brightness_percentage = (brightness_percentage < STEP_CHANGE_BRIGHTNESS)?
                                                    0:(brightness_percentage-STEP_CHANGE_BRIGHTNESS);

                            /// ----- Shell cmd ----
                            sprintf(shell_cmd, "%s %d", SHELL_CMD_BRIGHTNESS_SET, brightness_percentage);
                            fp = popen(shell_cmd, "r");
                            if (fp == NULL) {
                                MENU_ERROR_PRINTF("Failed to run command %s\n", shell_cmd);
                            }
                        /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_SAVE){
                            MENU_DEBUG_PRINTF("Save Slot DOWN\n");
                            saveslot = (!saveslot)?(MAX_SAVE_SLOTS-1):(saveslot-1);
                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_LOAD){
                            MENU_DEBUG_PRINTF("Load Slot DOWN\n");
                            saveslot = (!saveslot)?(MAX_SAVE_SLOTS-1):(saveslot-1);
                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_ASPECT_RATIO){
                            MENU_DEBUG_PRINTF("Aspect Ratio DOWN\n");
                            aspect_ratio = (!aspect_ratio)?(NB_ASPECT_RATIOS_TYPES-1):(aspect_ratio-1);
                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        break;

                    case SDLK_r:
                    case SDLK_RIGHT:
                        //MENU_DEBUG_PRINTF("RIGHT\n");
                        if(idx_menus[menuItem] == MENU_TYPE_VOLUME){
                            MENU_DEBUG_PRINTF("Volume UP\n");
                            /// ----- Compute new value -----
                            volume_percentage = (volume_percentage > 100 - STEP_CHANGE_VOLUME)?
                                                    100:(volume_percentage+STEP_CHANGE_VOLUME);

                            /// ----- Shell cmd ----
                            sprintf(shell_cmd, "%s %d", SHELL_CMD_VOLUME_SET, volume_percentage);
                            fp = popen(shell_cmd, "r");
                            if (fp == NULL) {
                                MENU_ERROR_PRINTF("Failed to run command %s\n", shell_cmd);
                            }
                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_BRIGHTNESS){
                            MENU_DEBUG_PRINTF("Brightness UP\n");
                            /// ----- Compute new value -----
                            brightness_percentage = (brightness_percentage > 100 - STEP_CHANGE_BRIGHTNESS)?
                                                    100:(brightness_percentage+STEP_CHANGE_BRIGHTNESS);

                            /// ----- Shell cmd ----
                            sprintf(shell_cmd, "%s %d", SHELL_CMD_BRIGHTNESS_SET, brightness_percentage);
                            fp = popen(shell_cmd, "r");
                            if (fp == NULL) {
                                MENU_ERROR_PRINTF("Failed to run command %s\n", shell_cmd);
                            }
                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_SAVE){
                            MENU_DEBUG_PRINTF("Save Slot UP\n");
                            saveslot = (saveslot+1)%MAX_SAVE_SLOTS;
                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_LOAD){
                            MENU_DEBUG_PRINTF("Load Slot UP\n");
                            //idx_load_slot = (idx_load_slot+1)%MAX_SAVE_SLOTS;
                            saveslot = (saveslot+1)%MAX_SAVE_SLOTS;
                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_ASPECT_RATIO){
                            MENU_DEBUG_PRINTF("Aspect Ratio UP\n");
                            aspect_ratio = (aspect_ratio+1)%NB_ASPECT_RATIOS_TYPES;
                            /// ------ Refresh screen ------
                            screen_refresh = 1;
                        }
                        break;

                    case SDLK_a:
                    case SDLK_RETURN:
                        if(idx_menus[menuItem] == MENU_TYPE_SAVE){
                            if(menu_confirmation){
                                MENU_DEBUG_PRINTF("Saving in slot %d\n", saveslot);
                                /// ------ Refresh Screen -------
                                menu_screen_refresh(menuItem, prevItem, scroll, menu_confirmation, 1);

                                /// ------ Save game ------
                                state_save(-1);

                                /// ----- Hud Msg -----
                                /*sprintf(hud_msg, "SAVED IN SLOT %d", saveslot);
                                set_hud_msg(hud_msg, 4);*/

                                stop_menu_loop = 1;
                            }
                            else{
                                MENU_DEBUG_PRINTF("Save game - asking confirmation\n");
                                menu_confirmation = 1;
                                /// ------ Refresh screen ------
                                screen_refresh = 1;
                            }
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_LOAD){
                            if(menu_confirmation){
                                MENU_DEBUG_PRINTF("Loading in slot %d\n", saveslot);
                                /// ------ Refresh Screen -------
                                menu_screen_refresh(menuItem, prevItem, scroll, menu_confirmation, 1);

                                /// ------ Load game ------
                                state_load(-1);

                                /// ----- Hud Msg -----
                                /*sprintf(hud_msg, "LOADED FROM SLOT %d", saveslot);
                                set_hud_msg(hud_msg, 4);*/

                                stop_menu_loop = 1;
                            }
                            else{
                                MENU_DEBUG_PRINTF("Save game - asking confirmation\n");
                                menu_confirmation = 1;
                                /// ------ Refresh screen ------
                                screen_refresh = 1;
                            }
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_EXIT){
                            MENU_DEBUG_PRINTF("Exit game\n");
                            if(menu_confirmation){
                                MENU_DEBUG_PRINTF("Exit game - confirmed\n");
                                /// ----- The game should be saved here ----

                                /// ----- Exit game and back to launcher ----
                                exit(0);
                                stop_menu_loop = 1;
                            }
                            else{
                                MENU_DEBUG_PRINTF("Exit game - asking confirmation\n");
                                menu_confirmation = 1;
                                /// ------ Refresh screen ------
                                screen_refresh = 1;
                            }
                        }
                        else if(idx_menus[menuItem] == MENU_TYPE_POWERDOWN){
                            if(menu_confirmation){
                                MENU_DEBUG_PRINTF("Powerdown - confirmed\n");
                                /// ----- Shell cmd ----
                                sprintf(shell_cmd, "%s", SHELL_CMD_POWERDOWN);
                                fp = popen(shell_cmd, "r");
                                if (fp == NULL) {
                                    MENU_ERROR_PRINTF("Failed to run command %s\n", shell_cmd);
                                }
                            }
                            else{
                                MENU_DEBUG_PRINTF("Powerdown - asking confirmation\n");
                                menu_confirmation = 1;
                                /// ------ Refresh screen ------
                                screen_refresh = 1;
                            }
                        }
                        break;

                    default:
                        //MENU_DEBUG_PRINTF("Keydown: %d\n", event.key.keysym.sym);
                        break;
                }
                break;
            }
        }

		/// --------- Handle Scroll effect ---------
        if ((scroll>0) || (start_scroll>0)){
	    scroll+=MIN(SCROLL_SPEED_PX, MENU_ZONE_HEIGHT-scroll);
            start_scroll = 0;
            screen_refresh = 1;
        }
        else if ((scroll<0) || (start_scroll<0)){
            scroll-=MIN(SCROLL_SPEED_PX, MENU_ZONE_HEIGHT+scroll);
            start_scroll = 0;
            screen_refresh = 1;
        }
        if (scroll>=MENU_ZONE_HEIGHT || scroll<=-MENU_ZONE_HEIGHT) {
            prevItem=menuItem;
            scroll=0;
            screen_refresh = 1;
        }
        if (scroll>MENU_ZONE_HEIGHT || scroll<-MENU_ZONE_HEIGHT) {
            prevItem=menuItem;
            scroll=0;
            screen_refresh = 1;
        }

		/// --------- Handle FPS ---------
		cur_ms = SDL_GetTicks();
		if(cur_ms-prev_ms < 1000/FPS_MENU){
			SDL_Delay(1000/FPS_MENU - (cur_ms-prev_ms));
		}
		prev_ms = SDL_GetTicks();


		/// --------- Refresh screen
		if(screen_refresh){
			menu_screen_refresh(menuItem, prevItem, scroll, menu_confirmation, 0);
		}

		/// --------- reset screen refresh ---------
		screen_refresh = 0;
    }

	/// ------ Reset prev key repeat params -------
	if(SDL_EnableKeyRepeat(backup_key_repeat_delay, backup_key_repeat_interval)){
		MENU_ERROR_PRINTF("ERROR with SDL_EnableKeyRepeat: %s\n", SDL_GetError());
	}
}



