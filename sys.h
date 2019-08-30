#ifndef VID_H
#define VID_H

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_image.h>


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
    X(ASPECT_RATIOS_TYPE_MANUAL, "MANUAL ZOOM") \
    X(ASPECT_RATIOS_TYPE_STRECHED, "STRECHED") \
    X(ASPECT_RATIOS_TYPE_CROPPED, "CROPPED") \
    X(ASPECT_RATIOS_TYPE_SCALED, "SCALED") \
    X(NB_ASPECT_RATIOS_TYPES, "")

////------ Enumeration of the different aspect ratios ------
#undef X
#define X(a, b) a,
typedef enum {ASPECT_RATIOS} ENUM_ASPECT_RATIOS_TYPES;



////------ Defines to be shared -------
#define STEP_CHANGE_VOLUME          10
#define STEP_CHANGE_BRIGHTNESS      10

////------ Menu commands -------
#define SHELL_CMD_VOLUME_GET        "/root/shell_cmds/volume_get.sh"
#define SHELL_CMD_VOLUME_SET        "/root/shell_cmds/volume_set.sh"
#define SHELL_CMD_BRIGHTNESS_GET    "/root/shell_cmds/brightness_get.sh"
#define SHELL_CMD_BRIGHTNESS_SET    "/root/shell_cmds/brightness_set.sh"
#define SHELL_CMD_POWERDOWN         "shutdown -h now"

////------ Global variables -------
extern int volume_percentage;
extern int brightness_percentage;

/*extern const char *aspect_ratio_name[];
extern int aspect_ratio;
extern int aspect_ratio_factor_percent;
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


/* FIXME these have different prototype for obsolete ( == M$ ) platforms */
#include <sys/time.h>
int sys_elapsed(struct timeval *prev);
void sys_initpath();

#endif
