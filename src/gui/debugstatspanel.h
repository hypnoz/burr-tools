#ifndef __DEBUG_STATS_PANEL_H__
#define __DEBUG_STATS_PANEL_H__

#include "Layouter.h"
#include "../lib/solvethread.h"

#include <string>

class LFl_Box;
class debugStatsBody_c;

class debugStatsPanel_c : public layouter_c {

  LFl_Box *pageTitle;
  debugStatsBody_c *body;

  std::string lastText;
  bool freezeLive;
  bool idleShown;

public:

  debugStatsPanel_c(int x, int y, int w, int h);

  void takeFocus(void);
  void showStats(const solveStats_c & stats);
};

#endif
