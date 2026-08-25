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
 * - Along the rotation axis, on each layer where the moving piece face-touches
 *   another piece, consider only pivot-column moving voxels (same in-plane
 *   coords as the pivot). Collect static ±U/±V face neighbours of those cells.
 *   If one touching layer has static on -U and a different touching layer has
 *   static on +U (same for V), reject. Same-layer opposites are ignored.
 *   Layers with no face contact to the other piece are skipped.
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

  /**
   * Collect cells useful for Debug Rotations visualisation of one candidate.
   *
   * @param outBlocking    static cells that currently violate arc-sweep or
   *                       participate in an axis-cross opposition (hard conflicts)
   * @param outClearance   mid-path lattice cells that are not part of the moving
   *                       piece at start — empty cells here would block if filled
 * @param outRestricted  empty ±in-plane slots face-adjacent to a moving voxel
 *                       on a layer that touches the other piece
   */
  void collectDebugConflictCells(const std::vector<cell_t> & occupied,
                                 const std::vector<cell_t> & startCells,
                                 const cell_t & pivot,
                                 unsigned int axis,
                                 unsigned int sense,
                                 std::vector<cell_t> & outBlocking,
                                 std::vector<cell_t> & outClearance,
                                 std::vector<cell_t> & outRestricted) const;

private:

  // no copying and assigning
  rotationRules_c(const rotationRules_c&);
  void operator=(const rotationRules_c&);
};

#endif
