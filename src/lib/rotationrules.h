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

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#ifndef __ROTATION_RULES_H__
#define __ROTATION_RULES_H__

#include <vector>

/**
 * Clearance / validity rules for 90° piece rotations during disassembly.
 *
 * - End position must not overlap other pieces
 * - Along the rotation axis, static (other-piece) neighbours on each in-plane
 *   axis are checked across *all* layers: if one opposite slot is occupied on
 *   any layer, the other opposite slot may not be occupied on any layer
 *   (e.g. +X on layer 1 forbids -X on every layer). Same for Y (or the other
 *   in-plane pair). Example layer grid 0..15: rotating on-axis voxel at 7 with
 *   static at 3 and 11 is invalid even if those statics sit on different layers.
 * - Arc sweep: sample the continuous 90° path so a voxel cannot clip through
 *   another piece mid-turn even when start and end are clear
 *
 * Additional named rules can be added here after physical validation.
 */
class rotationRules_c {

public:

  struct cell_t {
    int x, y, z;
    cell_t() : x(0), y(0), z(0) {}
    cell_t(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}
  };

  rotationRules_c(void) {}
  virtual ~rotationRules_c(void) {}

  /**
   * Return true if this rotation is allowed.
   *
   * @param occupied   world cells occupied by pieces that are not moving
   * @param startCells world cells of the moving piece before rotation
   * @param endCells   world cells of the moving piece after rotation
   * @param pivot      world cell used as rotation centre (on the piece)
   * @param axis       0=X, 1=Y, 2=Z
   * @param sense      0 = +90°, 1 = -90°
   */
  virtual bool allowRotation(const std::vector<cell_t> & occupied,
                             const std::vector<cell_t> & startCells,
                             const std::vector<cell_t> & endCells,
                             const cell_t & pivot,
                             unsigned int axis,
                             unsigned int sense) const;

private:

  // no copying and assigning
  rotationRules_c(const rotationRules_c&);
  void operator=(const rotationRules_c&);
};

#endif
