

#ifndef __LOADER_H__
#define __LOADER_H__


typedef struct loader_s
{
	char *rom;
	char *base;
	char *sram;
	char *state;
	int ramloaded;
} loader_t;


extern loader_t loader;
extern int saveslot;


int rom_load();
int sram_load();
int sram_save();

void loader_init(char *s);
void state_save(int n);
void state_file_save(char *filepath);
void state_load(int n);
void state_file_load(char *filepath);
int check_savefile(int n, char *savefile);

#endif


