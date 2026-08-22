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
#include "rotationmoves_0.h"

#include "bt_assert.h"
#include "disassemblernode.h"
#include "movementcache.h"
#include "problem.h"
#include "puzzle.h"
#include "gridtype.h"
#include "symmetries.h"
#include "voxel.h"

/* Cube orientation indices for ±90° about X/Y/Z (see tabs_0/rotmatrix.inc) */
static const unsigned char ROT_X_P90 = 1;
static const unsigned char ROT_X_M90 = 3;
static const unsigned char ROT_Y_P90 = 12;
static const unsigned char ROT_Y_M90 = 4;
static const unsigned char ROT_Z_P90 = 16;
static const unsigned char ROT_Z_M90 = 20;

rotationMoves_0_c::rotationMoves_0_c(const problem_c & puz, movementCache_c * cache_) :
  problem(puz),
  cache(cache_),
  sym(puz.getPuzzle().getGridType()->getSymmetries()),
  searchnode(0),
  pieces(0),
  nextpiece(0),
  nextpivot(0),
  nextaxis(0),
  nextsense(0),
  active(false)
{
}

void rotationMoves_0_c::rotateVector(int * x, int * y, int * z, unsigned int axis, unsigned int sense) {

  int ox = *x, oy = *y, oz = *z;

  if (axis == 0) {
    if (sense == 0) { *x = ox; *y = -oz; *z = oy; }   /* +90 X */
    else            { *x = ox; *y = oz;  *z = -oy; }  /* -90 X */
  } else if (axis == 1) {
    if (sense == 0) { *x = oz;  *y = oy; *z = -ox; }  /* +90 Y */
    else            { *x = -oz; *y = oy; *z = ox; }   /* -90 Y */
  } else {
    if (sense == 0) { *x = -oy; *y = ox; *z = oz; }   /* +90 Z */
    else            { *x = oy;  *y = -ox; *z = oz; }  /* -90 Z */
  }
}

unsigned char rotationMoves_0_c::rotationTransformId(unsigned int axis, unsigned int sense) {

  static const unsigned char ids[3][2] = {
    { ROT_X_P90, ROT_X_M90 },
    { ROT_Y_P90, ROT_Y_M90 },
    { ROT_Z_P90, ROT_Z_M90 }
  };
  bt_assert(axis < 3 && sense < 2);
  return ids[axis][sense];
}

void rotationMoves_0_c::collectWorldCells(unsigned int pieceIdx, std::vector<rotationRules_c::cell_t> & out) const {

  out.clear();

  unsigned int pieceId = (*pieces)[pieceIdx];
  unsigned int shapeId = cache->getShapeOfPiece(pieceId);
  unsigned char trans = (unsigned char)searchnode->getTrans(pieceIdx);
  const voxel_c * sh = cache->getTransformedShape(shapeId, trans);

  int px = searchnode->getX(pieceIdx);
  int py = searchnode->getY(pieceIdx);
  int pz = searchnode->getZ(pieceIdx);
  int hx = (int)sh->getHx();
  int hy = (int)sh->getHy();
  int hz = (int)sh->getHz();

  for (unsigned int z = 0; z < sh->getZ(); z++)
    for (unsigned int y = 0; y < sh->getY(); y++)
      for (unsigned int x = 0; x < sh->getX(); x++)
        if (sh->isFilled(x, y, z))
          out.push_back(rotationRules_c::cell_t(px - hx + (int)x, py - hy + (int)y, pz - hz + (int)z));
}

void rotationMoves_0_c::rebuildOccupied(unsigned int movingPieceIdx) {

  occupied.clear();
  for (unsigned int i = 0; i < pieces->size(); i++) {
    if (i == movingPieceIdx) continue;
    if (searchnode->is_piece_removed(i)) continue;

    std::vector<rotationRules_c::cell_t> cells;
    collectWorldCells(i, cells);
    for (unsigned int c = 0; c < cells.size(); c++)
      occupied.push_back(cells[c]);
  }
}

void rotationMoves_0_c::rebuildPieceCells(unsigned int pieceIdx) {

  collectWorldCells(pieceIdx, startCells);
  pivotCells = startCells;
}

disassemblerNode_c * rotationMoves_0_c::tryCurrentCandidate(void) {

  if ((unsigned int)nextpivot >= pivotCells.size())
    return 0;

  rotationRules_c::cell_t pivot = pivotCells[nextpivot];

  std::vector<rotationRules_c::cell_t> endCells;
  endCells.reserve(startCells.size());

  for (unsigned int i = 0; i < startCells.size(); i++) {
    int dx = startCells[i].x - pivot.x;
    int dy = startCells[i].y - pivot.y;
    int dz = startCells[i].z - pivot.z;
    rotateVector(&dx, &dy, &dz, nextaxis, nextsense);
    endCells.push_back(rotationRules_c::cell_t(pivot.x + dx, pivot.y + dy, pivot.z + dz));
  }

  if (!rules.allowRotation(occupied, startCells, endCells, pivot, nextaxis, nextsense))
    return 0;

  /* New orientation: apply world rotation after the piece's current orientation */
  unsigned char oldTrans = (unsigned char)searchnode->getTrans(nextpiece);
  unsigned char rotId = rotationTransformId(nextaxis, nextsense);
  unsigned char newTrans = sym->transAdd(oldTrans, rotId);

  /* Hotspot (placement) moves with the world rotation around the pivot */
  int px = searchnode->getX(nextpiece);
  int py = searchnode->getY(nextpiece);
  int pz = searchnode->getZ(nextpiece);
  int hx = px - pivot.x;
  int hy = py - pivot.y;
  int hz = pz - pivot.z;
  rotateVector(&hx, &hy, &hz, nextaxis, nextsense);
  int newPx = pivot.x + hx;
  int newPy = pivot.y + hy;
  int newPz = pivot.z + hz;

  /* Skip no-ops (symmetric pieces that look identical after rotation) */
  if (newPx == px && newPy == py && newPz == pz && newTrans == oldTrans)
    return 0;

  unsigned int dir = ROTATION_DIR_BASE + nextaxis * 2 + nextsense;
  disassemblerNode_c * n = new disassemblerNode_c(pieces->size(), searchnode, (int)dir, 1);
  n->setRotationInfo((unsigned int)nextpiece, pivot.x, pivot.y, pivot.z);

  for (unsigned int i = 0; i < pieces->size(); i++) {
    if ((int)i == nextpiece)
      n->set(i, newPx, newPy, newPz, newTrans);
    else
      n->set(i,
             searchnode->getX(i),
             searchnode->getY(i),
             searchnode->getZ(i),
             searchnode->getTrans(i));
  }

  return n;
}

void rotationMoves_0_c::init_find(disassemblerNode_c * nd, const std::vector<unsigned int> & pcs) {

  searchnode = nd;
  pieces = &pcs;
  nextpiece = 0;
  nextpivot = 0;
  nextaxis = 0;
  nextsense = 0;
  active = true;

  if (pieces->size() == 0) {
    active = false;
    return;
  }

  rebuildOccupied(0);
  rebuildPieceCells(0);
}

disassemblerNode_c * rotationMoves_0_c::find(void) {

  if (!active)
    return 0;

  while (true) {

    disassemblerNode_c * n = tryCurrentCandidate();

    /* advance iterators */
    nextsense++;
    if (nextsense >= 2) {
      nextsense = 0;
      nextaxis++;
      if (nextaxis >= 3) {
        nextaxis = 0;
        nextpivot++;
        if ((unsigned int)nextpivot >= pivotCells.size()) {
          nextpivot = 0;
          nextpiece++;
          if ((unsigned int)nextpiece >= pieces->size()) {
            active = false;
            if (n) return n;
            return 0;
          }
          rebuildOccupied(nextpiece);
          rebuildPieceCells(nextpiece);
        }
      }
    }

    if (n)
      return n;
  }
}
