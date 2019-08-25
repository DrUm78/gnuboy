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


#include "fb.h"
#include "input.h"
#include "rc.h"

#define GB_SCREEN_WIDTH 160
#define GB_SCREEN_HEIGHT 144
#define RES_HW_SCREEN_HORIZONTAL  240
#define RES_HW_SCREEN_VERTICAL    240

//#define BLACKER_BLACKS

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define ABS(x) (((x) < 0) ? (-x) : (x))

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


static void joy_init()
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
	
	flags = SDL_ANYFORMAT | SDL_HWPALETTE | SDL_HWSURFACE;

	if (fullscreen)
		flags |= SDL_FULLSCREEN;

	if (SDL_Init(SDL_INIT_VIDEO))
		die("SDL: Couldn't initialize SDL: %s\n", SDL_GetError());

	/*if (!(gb_screen = SDL_SetVideoMode(RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL, 16, flags)))
		die("SDL: can't set video mode: %s\n", SDL_GetError());
	gb_screen = SDL_CreateRGBSurface(SDL_SWSURFACE,
		vmode[0], vmode[1], 16, 0xFFFF, 0xFFFF, 0xFFFF, 0);***/

	if (!(hw_screen = SDL_SetVideoMode(RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL, 16, flags)))
		die("SDL: can't set video mode: %s\n", SDL_GetError());
	gb_screen = SDL_CreateRGBSurface(SDL_SWSURFACE, vmode[0], vmode[1], 16, 0, 0, 0, 0);
	virtual_hw_screen = SDL_CreateRGBSurface(SDL_SWSURFACE, RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL, 16, 0, 0, 0, 0);


	SDL_ShowCursor(0);

	joy_init();

	overlay_init();
	
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

	/// Rotate
	SDL_Rotate_270(hw_screen, virtual_hw_screen);

	if (fb.enabled) SDL_Flip(hw_screen);
}








