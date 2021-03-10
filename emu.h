#ifndef EMU_H
#define EMU_H

void emu_run();
void emu_reset();

extern char *prog_name;
extern char *load_state_file;
extern int load_state_slot;
extern char *quick_save_file_extension;
extern char *mRomName;
extern char *mRomPath;
extern char *quick_save_file;
extern int mQuickSaveAndPoweroff;
extern char *cfg_file_default;
extern char *cfg_file_rom;
extern char *cfg_file_default_name;
extern char *cfg_file_extension;

#endif
