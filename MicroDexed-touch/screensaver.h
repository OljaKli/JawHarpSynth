#ifndef _SCREENSAVER_H
#define _SCREENSAVER_H

enum ScreenSaver {
  RANDOM            = 0,
  QIX               = 1,
  CUBE              = 2,
  SWARM             = 3,
  TERRAIN           = 4,
  GAMEOFLIFE        = 5,
  CM5_I             = 6,
  CM5_II            = 7,
  DISABLED          = 8,
  NUM_SCREENSAVERS  = 9
};

void boot_animation();
void InitializeCube();
void cube_screensaver();
void qix_screensaver();
#endif

