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



#define AVERAGE(z, x) ((((z) & 0xF7DEF7DE) >> 1) + (((x) & 0xF7DEF7DE) >> 1))
#define AVERAGEHI(AB) ((((AB) & 0xF7DE0000) >> 1) + (((AB) & 0xF7DE) << 15))
#define AVERAGELO(CD) ((((CD) & 0xF7DE) >> 1) + (((CD) & 0xF7DE0000) >> 17))

// Support math
#define Half(A) (((A) >> 1) & 0x7BEF)
#define Quarter(A) (((A) >> 2) & 0x39E7)
// Error correction expressions to piece back the lower bits together
#define RestHalf(A) ((A) & 0x0821)
#define RestQuarter(A) ((A) & 0x1863)

// Error correction expressions for quarters of pixels
#define Corr1_3(A, B)     Quarter(RestQuarter(A) + (RestHalf(B) << 1) + RestQuarter(B))
#define Corr3_1(A, B)     Quarter((RestHalf(A) << 1) + RestQuarter(A) + RestQuarter(B))

// Error correction expressions for halves
#define Corr1_1(A, B)     ((A) & (B) & 0x0821)

// Quarters
#define Weight1_3(A, B)   (Quarter(A) + Half(B) + Quarter(B) + Corr1_3(A, B))
#define Weight3_1(A, B)   (Half(A) + Quarter(A) + Quarter(B) + Corr3_1(A, B))

// Halves
#define Weight1_1(A, B)   (Half(A) + Half(B) + Corr1_1(A, B))



struct fb fb;

static int use_yuv = -1;
static int fullscreen = 0;
static int use_altenter = 1;
static int use_joy = 1, sdl_joy_num;
static SDL_Joystick * sdl_joy = NULL;
static const int joy_commit_range = 3276;
static char Xstatus, Ystatus;

SDL_Surface *hw_screen;
//SDL_Surface *virtual_hw_screen;
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

	/* Set env var for no mouse */
	putenv(strdup("SDL_NOMOUSE=1"));
	
	//flags = SDL_ANYFORMAT | SDL_HWPALETTE | SDL_HWSURFACE;
	flags = SDL_HWPALETTE | SDL_HWSURFACE | SDL_DOUBLEBUF;

	if (fullscreen)
		flags |= SDL_FULLSCREEN;

	if (SDL_Init(SDL_INIT_VIDEO))
		die("SDL: Couldn't initialize SDL: %s\n", SDL_GetError());

	if(TTF_Init()){
        die("Error TTF_Init: %s\n", TTF_GetError());
	}

	if (!(hw_screen = SDL_SetVideoMode(RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL, 16, flags))){
		die("SDL: can't set video mode: %s\n", SDL_GetError());
	}
	gb_screen = SDL_CreateRGBSurface(SDL_SWSURFACE, vmode[0], vmode[1], 16, 0, 0, 0, 0);

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

SDL_Surface * vid_getwindow()
{
	return hw_screen;
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
	//SDL_FreeSurface(virtual_hw_screen);
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
void flip_NNOptimized_AllowOutOfScreen(SDL_Surface *virtual_screen, SDL_Surface *hardware_screen, int new_w, int new_h)
{
	int w1 = virtual_screen->w;
	//int h1 = virtual_screen->h;
	int w2 = new_w;
	int h2 = new_h;
	int x_ratio = (int) ((virtual_screen->w << 16) / w2);
	int y_ratio = (int) ((virtual_screen->h << 16) / h2);
	int x2, y2;

	/// --- Compute padding for centering when out of bounds ---
	int y_padding = (RES_HW_SCREEN_VERTICAL - new_h) / 2;
	int x_padding = 0;
	if (w2 > RES_HW_SCREEN_HORIZONTAL)
	{
		x_padding = (w2 - RES_HW_SCREEN_HORIZONTAL) / 2 + 1;
	}
	int x_padding_ratio = x_padding * w1 / w2;
	//printf("virtual_screen->h=%d, h2=%d\n", virtual_screen->h, h2);

	for (int i = 0; i < h2; i++)
	{
		if (i >= RES_HW_SCREEN_VERTICAL)
		{
			continue;
		}

		uint16_t *t = ((uint16_t *) hardware_screen->pixels + (i + y_padding) * ((w2 > RES_HW_SCREEN_HORIZONTAL) ? RES_HW_SCREEN_HORIZONTAL : w2));
		y2 = (i * y_ratio) >> 16;
		uint16_t *p = ((uint16_t *) virtual_screen->pixels + y2 * w1 + x_padding_ratio);
		int rat = 0;
		for (int j = 0; j < w2; j++)
		{
			if (j >= RES_HW_SCREEN_HORIZONTAL)
			{
				continue;
			}
			x2 = rat >> 16;
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


void upscale_160x144_to_240x240_bilinearish(SDL_Surface *src_surface, SDL_Surface *dst_surface)
{
	if (src_surface->w != 160)
	{
		printf("src_surface->w (%d) != 160 \n", src_surface->w);
		return;
	}
	if (src_surface->h != 144)
	{
		printf("src_surface->h (%d) != 144 \n", src_surface->h);
		return;
	}

	uint16_t *Src16 = (uint16_t *) src_surface->pixels;
	uint16_t *Dst16 = (uint16_t *) dst_surface->pixels;

	// There are 80 blocks of 2 pixels horizontally, and 48 of 3 horizontally.
	// Horizontally: 240=80*3 160=80*2
	// Vertically: 240=48*5 144=48*3
	// Each block of 2*3 becomes 3x5.
	uint32_t BlockX, BlockY;
	uint16_t *BlockSrc;
	uint16_t *BlockDst;
	uint16_t _a, _b, _ab, __a, __b, __ab;
	for (BlockY = 0; BlockY < 48; BlockY++)
	{
		BlockSrc = Src16 + BlockY * 160 * 3;
		BlockDst = Dst16 + BlockY * 240 * 5;
		for (BlockX = 0; BlockX < 80; BlockX++)
		{
			/* Horizontaly:
			 * Before(2):
			 * (a)(b)
			 * After(3):
			 * (a)(ab)(b)
			 */

			/* Verticaly:
			 * Before(3):
			 * (1)(2)(3)
			 * After(5):
			 * (1)(12)(2)(23)(3)
			 */

			// -- Line 1 --
			_a = *(BlockSrc                          );
			_b = *(BlockSrc                       + 1);
			_ab = Weight1_1( _a,  _b);
			*(BlockDst                               ) = _a;
			*(BlockDst                            + 1) = _ab;
			*(BlockDst                            + 2) = _b;

			// -- Line 2 --
			__a = *(BlockSrc            + 160 * 1    );
			__b = *(BlockSrc            + 160 * 1 + 1);
			__ab = Weight1_1( __a,  __b);
			*(BlockDst                  + 240 * 1    ) = Weight1_1(_a, __a);
			*(BlockDst                  + 240 * 1 + 1) = Weight1_1(_ab, __ab);
			*(BlockDst                  + 240 * 1 + 2) = Weight1_1(_b, __b);

			// -- Line 3 --
			*(BlockDst                  + 240 * 2    ) = __a;
			*(BlockDst                  + 240 * 2 + 1) = __ab;
			*(BlockDst                  + 240 * 2 + 2) = __b;

			// -- Line 4 --
			_a = __a;
			_b = __b;
			_ab = __ab;
			__a = *(BlockSrc            + 160 * 2    );
			__b = *(BlockSrc            + 160 * 2 + 1);
			__ab = Weight1_1( __a,  __b);
			*(BlockDst                  + 240 * 3    ) = Weight1_1(_a, __a);
			*(BlockDst                  + 240 * 3 + 1) = Weight1_1(_ab, __ab);
			*(BlockDst                  + 240 * 3 + 2) = Weight1_1(_b, __b);

			// -- Line 5 --
			*(BlockDst                  + 240 * 4    ) = __a;
			*(BlockDst                  + 240 * 4 + 1) = __ab;
			*(BlockDst                  + 240 * 4 + 2) = __b;

			BlockSrc += 2;
			BlockDst += 3;
		}
	}
}


void upscale_160x144_to_240x216_bilinearish(SDL_Surface *src_surface, SDL_Surface *dst_surface)
{
	if (src_surface->w != 160)
	{
		printf("src_surface->w (%d) != 160 \n", src_surface->w);
		return;
	}
	if (src_surface->h != 144)
	{
		printf("src_surface->h (%d) != 144 \n", src_surface->h);
		return;
	}

	/* Y padding for centering */
	uint32_t y_padding = (240 - 216) / 2 + 1;

	uint16_t *Src16 = (uint16_t *) src_surface->pixels;
	uint16_t *Dst16 = ((uint16_t *) dst_surface->pixels) + y_padding * 240;

	// There are 80 blocks of 2 pixels horizontally, and 72 of 2 horizontally.
	// Horizontally: 240=80*3 160=80*2
	// Vertically: 216=72*3 144=72*2
	// Each block of 2*3 becomes 3x5.
	uint32_t BlockX, BlockY;
	uint16_t *BlockSrc;
	uint16_t *BlockDst;
	volatile uint16_t _a, _b, _ab, __a, __b, __ab;
	for (BlockY = 0; BlockY < 72; BlockY++)
	{

		BlockSrc = Src16 + BlockY * 160 * 2;
		BlockDst = Dst16 + BlockY * 240 * 3;
		for (BlockX = 0; BlockX < 80; BlockX++)
		{
			/* Horizontaly:
			 * Before(2):
			 * (a)(b)
			 * After(3):
			 * (a)(ab)(b)
			 */

			/* Verticaly:
			 * Before(2):
			 * (1)(2)
			 * After(3):
			 * (1)(12)(2)
			 */

			// -- Line 1 --
			_a = *(BlockSrc                    );
			_b = *(BlockSrc                 + 1);
			_ab = Weight1_1( _a,  _b);
			*(BlockDst                         ) = _a;
			*(BlockDst                      + 1) = _ab;
			*(BlockDst                      + 2) = _b;

			// -- Line 2 --
			__a = *(BlockSrc      + 160 * 1    );
			__b = *(BlockSrc      + 160 * 1 + 1);
			__ab = Weight1_1( __a,  __b);
			*(BlockDst            + 240 * 1    ) = Weight1_1(_a, __a);
			*(BlockDst            + 240 * 1 + 1) = Weight1_1(_ab, __ab);
			*(BlockDst            + 240 * 1 + 2) = Weight1_1(_b, __b);

			// -- Line 3 --
			*(BlockDst            + 240 * 2    ) = __a;
			*(BlockDst            + 240 * 2 + 1) = __ab;
			*(BlockDst            + 240 * 2 + 2) = __b;

			BlockSrc += 2;
			BlockDst += 3;
		}
	}
}



void SDL_Rotate_270(SDL_Surface * hw_surface, SDL_Surface * virtual_hw_surface){
	int i, j;
	uint16_t *source_pixels = (uint16_t *) virtual_hw_surface->pixels;
	uint16_t *dest_pixels = (uint16_t *) hw_surface->pixels;

	/// --- Checking for right pixel format ---
	//MENU_DEBUG_PRINTF("Source bpb = %d, Dest bpb = %d\n", virtual_hw_surface->format->BitsPerPixel, hw_surface->format->BitsPerPixel);
	if (virtual_hw_surface->format->BitsPerPixel != 16)
	{
		printf("Error in SDL_Rotate_270, Wrong virtual_hw_surface pixel format: %d bpb, expected: 16 bpb\n", virtual_hw_surface->format->BitsPerPixel);
		return;
	}
	if (hw_surface->format->BitsPerPixel != 16)
	{
		printf("Error in SDL_Rotate_270, Wrong hw_surface pixel format: %d bpb, expected: 16 bpb\n", hw_surface->format->BitsPerPixel);
		return;
	}

	/// --- Checking if same dimensions ---
	if (hw_surface->w != virtual_hw_surface->w || hw_surface->h != virtual_hw_surface->h)
	{
		printf("Error in SDL_Rotate_270, hw_surface (%dx%d) and virtual_hw_surface (%dx%d) have different dimensions\n",
		       hw_surface->w, hw_surface->h, virtual_hw_surface->w, virtual_hw_surface->h);
		return;
	}

	/// --- Pixel copy and rotation (270) ---
	uint16_t *cur_p_src, *cur_p_dst;
	for (i = 0; i < virtual_hw_surface->h; i++)
	{
		for (j = 0; j < virtual_hw_surface->w; j++)
		{
			cur_p_src = source_pixels + i * virtual_hw_surface->w + j;
			cur_p_dst = dest_pixels + (hw_surface->h - 1 - j) * hw_surface->w + i;
			*cur_p_dst = *cur_p_src;
		}
	}
}

void vid_flip(){
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

	/* Clear screen if necessary */
	static ENUM_ASPECT_RATIOS_TYPES prev_aspect_ratio = NB_ASPECT_RATIOS_TYPES;
	if(prev_aspect_ratio != aspect_ratio){
		memset(hw_screen->pixels, 0, hw_screen->w*hw_screen->h*hw_screen->format->BytesPerPixel);
		prev_aspect_ratio = aspect_ratio;
	}

	/* Blit scaled based on defined aspect ratio */
	switch(aspect_ratio){
		case ASPECT_RATIOS_TYPE_STRETCHED:
		upscale_160x144_to_240x240_bilinearish(gb_screen, hw_screen);
		/*flip_NNOptimized_AllowOutOfScreen(gb_screen, hw_screen,
	        RES_HW_SCREEN_HORIZONTAL, RES_HW_SCREEN_VERTICAL);*/
		break;

		case ASPECT_RATIOS_TYPE_SCALED:
		upscale_160x144_to_240x216_bilinearish(gb_screen, hw_screen);
		break;

		default:
		printf("ERROR in %s, wrong aspect ratio: %d\n", aspect_ratio);
		aspect_ratio = ASPECT_RATIOS_TYPE_STRETCHED;
		break;
	}

	if (fb.enabled) vid_flip();
}
