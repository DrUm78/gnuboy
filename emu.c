


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "defs.h"
#include "regs.h"
#include "hw.h"
#include "loader.h"
#include "cpu.h"
#include "mem.h"
#include "lcd.h"
#include "rc.h"
#include "rtc.h"
#include "sys.h"
#include "configfile_fk.h"
#include "sound.h"
#include "cpu.h"


static int framelen = 16743;
static int framecount;

char *prog_name;
char *load_state_file = NULL;
int load_state_slot = -1;
char *quick_save_file_extension = "quicksave";
char *mRomName = NULL;
char *mRomPath = NULL;
char *quick_save_file = NULL;
char *cfg_file_default = NULL;
char *cfg_file_rom = NULL;
char *cfg_file_default_name = "default_config";
char *cfg_file_extension = "fkcfg";
int mQuickSaveAndPoweroff=0;


rcvar_t emu_exports[] =
{
	RCV_INT("framelen", &framelen),
	RCV_INT("framecount", &framecount),
	RCV_END
};




/* Quick save and turn off the console */
void quick_save_and_poweroff()
{
	FILE *fp;

	printf("Save Instant Play file\n");

	Uint32 start = SDL_GetTicks();

	/* Send command to cancel any previously scheduled powerdown */
	fp = popen(SHELL_CMD_CANCEL_SCHED_POWERDOWN, "r");
	if (fp == NULL)
	{
	        /* Countdown is still ticking, so better do nothing
	           than start writing and get interrupted!
		*/
		printf("Failed to cancel scheduled shutdown\n");
		exit(0);
	}
	pclose(fp);

	printf("============== Cancel time %d\n", SDL_GetTicks() - start);
	/* Save  */
	state_file_save(quick_save_file);

	/* Perform Instant Play save and shutdown */
	execlp(SHELL_CMD_INSTANT_PLAY, SHELL_CMD_INSTANT_PLAY,
	       prog_name, "--loadStateFile", quick_save_file, mRomName, NULL);

	/* Should not be reached */
	printf("Failed to perform Instant Play save and shutdown\n");

	/* Exit Emulator */
	exit(0);
}




void emu_init()
{
	
}


/*
 * emu_reset is called to initialize the state of the emulated
 * system. It should set cpu registers, hardware registers, etc. to
 * their appropriate values at powerup time.
 */

void emu_reset()
{
	hw_reset();
	lcd_reset();
	cpu_reset();
	mbc_reset();
	sound_reset();
}





void emu_step()
{
	cpu_emulate(cpu.lcdc);
}



/* This mess needs to be moved to another module; it's just here to
 * make things work in the mean time. */

void *sys_timer();

void emu_run()
{
	void *timer = sys_timer();
	int delay;

	vid_begin();
	lcd_begin();

	/* Load slot */
	if (load_state_slot != -1)
	{
		printf("LOADING FROM SLOT %d...\n", load_state_slot+1);
		state_load(load_state_slot);
		printf("LOADED FROM SLOT %d\n", load_state_slot+1);
		load_state_slot = -1;
	}
	/* Load file */
	else if (load_state_file != NULL)
	{
		printf("LOADING FROM FILE %s...\n", load_state_file);
		state_file_load(load_state_file);
		printf("LOADED FROM SLOT %s\n", load_state_file);
		load_state_file = NULL;
	}
	/* Load quick save file */
	else if(access( quick_save_file, F_OK ) != -1)
	{
		printf("Found quick save file: %s\n", quick_save_file);

		int resume = launch_resume_menu_loop();
		if (resume == RESUME_YES)
		{
			printf("Resume game from quick save file: %s\n", quick_save_file);
			state_file_load(quick_save_file);
		}
		else {
			printf("Reset game\n");

			/* Remove quicksave file if present */
			if (remove(quick_save_file) == 0)
			{
				printf("Deleted successfully: %s\n", quick_save_file);
			}
			else {
				printf("Unable to delete the file: %s\n", quick_save_file);
			}
		}
	}

	/* Main emulation loop */
	for (;;)
	{
		cpu_emulate(2280);
		while (R_LY > 0 && R_LY < 144)
			emu_step();
		
		vid_end();
		rtc_tick();
		sound_mix();
		if (!pcm_submit())
		{
			delay = framelen - sys_elapsed(timer);
			sys_sleep(delay);
			sys_elapsed(timer);
		}

		/* Quick save and poweroff */
		if(mQuickSaveAndPoweroff){
			quick_save_and_poweroff();
			mQuickSaveAndPoweroff = 0;
		}

		doevents();

		vid_begin();
		if (framecount) { if (!--framecount) die("finished\n"); }
		
		if (!(R_LCDC & 0x80))
			cpu_emulate(32832);
		
		while (R_LY > 0) /* wait for next frame */
			emu_step();
	}
}
