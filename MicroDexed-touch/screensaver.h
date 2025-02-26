#ifndef _SCREENSAVER_H
#define _SCREENSAVER_H

enum ScreenSaver {
  RANDOM            = 0,
  QIX               = 1,
  CUBE              = 2,
  SWARM             = 3,
  TERRAIN           = 4,
  DISABLED          = 5,
  NUM_SCREENSAVERS  = 6
};

void boot_animation();
void InitializeCube();
void cube_screensaver();
void qix_screensaver();

#endif

