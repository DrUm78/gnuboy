#ifndef VID_H
#define VID_H

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_image.h>


/// -------------- DEFINES --------------
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define ABS(x) (((x) < 0) ? (-x) : (x))

#define GB_SCREEN_WIDTH 160
#define GB_SCREEN_HEIGHT 144
#define RES_HW_SCREEN_HORIZONTAL  240
#define RES_HW_SCREEN_VERTICAL    240
#define SCREEN_HORIZONTAL_SIZE      RES_HW_SCREEN_HORIZONTAL
#define SCREEN_VERTICAL_SIZE        RES_HW_SCREEN_VERTICAL

typedef enum{
    MENU_TYPE_VOLUME,
    MENU_TYPE_BRIGHTNESS,
    MENU_TYPE_SAVE,
    MENU_TYPE_LOAD,
    MENU_TYPE_ASPECT_RATIO,
    MENU_TYPE_EXIT,
    MENU_TYPE_POWERDOWN,
    NB_MENU_TYPES,
} ENUM_MENU_TYPE;


///------ Definition of the different aspect ratios
#define ASPECT_RATIOS \
    X(ASPECT_RATIOS_TYPE_STRETCHED, "STRETCHED") \
    X(ASPECT_RATIOS_TYPE_SCALED, "SCALED") \
    X(NB_ASPECT_RATIOS_TYPES, "")

////------ Enumeration of the different aspect ratios ------
#undef X
#define X(a, b) a,
typedef enum {ASPECT_RATIOS} ENUM_ASPECT_RATIOS_TYPES;

///------ Definition of the different resume options
#define RESUME_OPTIONS \
    X(RESUME_YES, "RESUME GAME") \
    X(RESUME_NO, "NEW GAME") \
    X(NB_RESUME_OPTIONS, "")

////------ Enumeration of the different resume options ------
#undef X
#define X(a, b) a,
typedef enum {RESUME_OPTIONS} ENUM_RESUME_OPTIONS;


////------ Defines to be shared -------
#define STEP_CHANGE_VOLUME          10
#define STEP_CHANGE_BRIGHTNESS      10
#define NOTIF_SECONDS_DISP          2

////------ Menu commands -------
#define SHELL_CMD_VOLUME_GET                "volume_get"
#define SHELL_CMD_VOLUME_SET                "volume_set"
#define SHELL_CMD_BRIGHTNESS_GET            "brightness_get"
#define SHELL_CMD_BRIGHTNESS_SET            "brightness_set"
#define SHELL_CMD_POWERDOWN                 "shutdown_funkey"
#define SHELL_CMD_SCHEDULE_POWERDOWN        "sched_shutdown"
#define SHELL_CMD_NOTIF                     "notif_set"
#define SHELL_CMD_WRITE_QUICK_LOAD_CMD      "write_args_quick_load_file"

////------ Global variables -------
extern int volume_percentage;
extern int brightness_percentage;
extern int stop_menu_loop;

extern const char *aspect_ratio_name[];
extern int aspect_ratio;
/*extern int aspect_ratio_factor_percent;
extern int aspect_ratio_factor_step;*/


/* stuff implemented by the different sys/ backends */

void vid_begin();
void vid_end();
void vid_flip();
void vid_init();
void vid_preinit();
void vid_close();
void vid_setpal(int i, int r, int g, int b);
void vid_settitle(char *title);
SDL_Surface * vid_getwindow();

void pcm_init();
int pcm_submit();
void pcm_close();

void ev_poll();

void sys_checkdir(char *path, int wr);
void sys_sleep(int us);
void sys_sanitize(char *s);

void joy_init();
void joy_poll();
void joy_close();

void kb_init();
void kb_poll();
void kb_close();



void init_menu_SDL();
void deinit_menu_SDL();
void init_menu_zones();
void init_menu_system_values();
void run_menu_loop();
int launch_resume_menu_loop();


/* FIXME these have different prototype for obsolete ( == M$ ) platforms */
#include <sys/time.h>
int sys_elapsed(struct timeval *prev);
void sys_initpath();

#endif
