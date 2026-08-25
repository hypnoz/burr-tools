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
#ifndef __ROTATION_MOVES_0_H__
#define __ROTATION_MOVES_0_H__

#include "rotationrules.h"

#include <vector>

class problem_c;
class disassemblerNode_c;
class movementCache_c;
class symmetries_c;

/**
 * Direction codes used on disassemblerNode_c for rotation edges.
 * dir = ROTATION_DIR_BASE + axis*2 + sense  (axis 0..2, sense 0=+90 1=-90)
 */
static const unsigned int ROTATION_DIR_BASE = 100;

inline bool isRotationDirection(unsigned int dir) {
  return dir >= ROTATION_DIR_BASE;
}

/**
 * Generate valid 90° rotation moves for brick grids.
 *
 * Single-piece and compound (multi-piece rigid) rotations share the same pivot,
 * axis, and sense. Compound moves treat the selected pieces as one body for
 * clearance checks.
 */
class rotationMoves_0_c {

  private:

    const problem_c & problem;
    movementCache_c * cache;
    const symmetries_c * sym;
    rotationRules_c rules;

    /* iterator state for find-style enumeration */
    disassemblerNode_c * searchnode;
    const std::vector<unsigned int> * pieces;
    unsigned int nextsubset;
    int nextpivot;
    unsigned int nextaxis;
    unsigned int nextsense;
    bool active;

    std::vector<rotationRules_c::cell_t> pivotCells;

    void collectWorldCells(unsigned int pieceIdx, std::vector<rotationRules_c::cell_t> & out) const;
    void rebuildPivotCells(unsigned int subsetMask);
    disassemblerNode_c * tryCurrentCandidate(void);

    static void rotateVector(int * x, int * y, int * z, unsigned int axis, unsigned int sense);
    static unsigned char rotationTransformId(unsigned int axis, unsigned int sense);
    static unsigned int nextSubsetMask(unsigned int mask, unsigned int n);

  public:

    rotationMoves_0_c(const problem_c & puz, movementCache_c * cache_);
    ~rotationMoves_0_c(void) {}

    void init_find(disassemblerNode_c * nd, const std::vector<unsigned int> & pcs);
    disassemblerNode_c * find(void);

  private:

    // no copying and assigning
    rotationMoves_0_c(const rotationMoves_0_c&);
    void operator=(const rotationMoves_0_c&);
};

#endif
