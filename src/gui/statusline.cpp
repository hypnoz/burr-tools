/* BurrTools
 *
 * BurrTools is the legal property of its developers, whose
 * names are listed in the COPYRIGHT file, which is included
 * within the source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */
#include "statusline.h"

LStatusLine::LStatusLine(int x, int y, int w, int h) : layouter_c(x, y, w, h), colorModeIndex(0) {

  weight(1, 0);
  box(FL_FLAT_BOX);
  color(FL_BACKGROUND_COLOR);

  text = new LFl_Box(0, 0, 1, 1);
  text->box(FL_NO_BOX);
  text->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  text->weight(1, 0);
  text->pitch(4);

  clear_visible_focus();

  end();
}

void LStatusLine::setText(const char * t) {

  text->copy_label(t);
}

void LStatusLine::setColorModeIndex(int i) {
  colorModeIndex = i;
}

voxelFrame_c::colorMode LStatusLine::getColorMode(void) const {

  switch (colorModeIndex) {
    case 0: return voxelFrame_c::pieceColor;
    case 1: return voxelFrame_c::paletteColor;
    case 2: return voxelFrame_c::anaglyphColor;
    case 3: return voxelFrame_c::anaglyphColorL;
    default: return voxelFrame_c::pieceColor;
  }
}
