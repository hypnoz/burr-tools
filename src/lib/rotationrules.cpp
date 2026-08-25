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
#include "rotationrules.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <set>

namespace {

struct cellLess {
  bool operator()(const rotationRules_c::cell_t & a, const rotationRules_c::cell_t & b) const {
    if (a.x != b.x) return a.x < b.x;
    if (a.y != b.y) return a.y < b.y;
    return a.z < b.z;
  }
};

typedef std::set<rotationRules_c::cell_t, cellLess> cellSet;

/* Voxel (i,j,k) occupies [i,i+1]×[j,j+1]×[k,k+1]. A unit cube centred at C
 * overlaps lattice voxels in each axis from floor(C-0.5) to floor(C+0.5-eps). */
static void addOverlappedCells(double cx, double cy, double cz, cellSet & out) {
  const double eps = 1e-9;
  int x0 = (int)floor(cx - 0.5 + eps);
  int x1 = (int)floor(cx + 0.5 - eps);
  int y0 = (int)floor(cy - 0.5 + eps);
  int y1 = (int)floor(cy + 0.5 - eps);
  int z0 = (int)floor(cz - 0.5 + eps);
  int z1 = (int)floor(cz + 0.5 - eps);

  for (int x = x0; x <= x1; x++)
    for (int y = y0; y <= y1; y++)
      for (int z = z0; z <= z1; z++)
        out.insert(rotationRules_c::cell_t(x, y, z));
}

/* Rotate in-plane coords (u,v) by angle θ toward a ±90° turn.
 * sense 0 = +90°, sense 1 = -90°. */
static void rotateInPlane(double u, double v, double theta, unsigned int sense,
                          double * uOut, double * vOut) {
  double c = cos(theta);
  double s = sin(theta);
  if (sense == 0) {
    *uOut = u * c - v * s;
    *vOut = u * s + v * c;
  } else {
    *uOut = u * c + v * s;
    *vOut = -u * s + v * c;
  }
}

/* Continuous centre of a voxel after rotating by theta about pivot centre. */
static void rotatedCenter(double dx, double dy, double dz,
                          unsigned int axis, unsigned int sense, double theta,
                          double * ox, double * oy, double * oz) {
  if (axis == 0) {
    double ny, nz;
    rotateInPlane(dy, dz, theta, sense, &ny, &nz);
    *ox = dx; *oy = ny; *oz = nz;
  } else if (axis == 1) {
    if (sense == 0) {
      *ox = dx * cos(theta) + dz * sin(theta);
      *oy = dy;
      *oz = -dx * sin(theta) + dz * cos(theta);
    } else {
      *ox = dx * cos(theta) - dz * sin(theta);
      *oy = dy;
      *oz = dx * sin(theta) + dz * cos(theta);
    }
  } else {
    double nx, ny;
    rotateInPlane(dx, dy, theta, sense, &nx, &ny);
    *ox = nx; *oy = ny; *oz = dz;
  }
}

/**
 * Sample the continuous 90° path of each moving voxel and reject if any
 * sample overlaps another piece. This only adds rejections; it never
 * overrides a failed axis-cross check.
 */
static bool arcClear(const cellSet & occ,
                     const std::vector<rotationRules_c::cell_t> & startCells,
                     const rotationRules_c::cell_t & pivot,
                     unsigned int axis,
                     unsigned int sense) {

  const double pcx = pivot.x + 0.5;
  const double pcy = pivot.y + 0.5;
  const double pcz = pivot.z + 0.5;

  static const int STEPS = 8;
  const double halfPi = 1.5707963267948966;

  for (unsigned int ci = 0; ci < startCells.size(); ci++) {
    const rotationRules_c::cell_t & cell = startCells[ci];
    double dx = (cell.x + 0.5) - pcx;
    double dy = (cell.y + 0.5) - pcy;
    double dz = (cell.z + 0.5) - pcz;

    if (axis == 0 && dy == 0 && dz == 0) continue;
    if (axis == 1 && dx == 0 && dz == 0) continue;
    if (axis == 2 && dx == 0 && dy == 0) continue;

    for (int s = 1; s < STEPS; s++) {
      double theta = halfPi * (double)s / (double)STEPS;
      double ox, oy, oz;
      rotatedCenter(dx, dy, dz, axis, sense, theta, &ox, &oy, &oz);

      cellSet hit;
      addOverlappedCells(pcx + ox, pcy + oy, pcz + oz, hit);

      for (cellSet::const_iterator it = hit.begin(); it != hit.end(); ++it)
        if (occ.find(*it) != occ.end()) {
          if (getenv("BT_ROT_DEBUG"))
            fprintf(stderr,
                    "ROT_ARC_HIT moving=(%d,%d,%d) hits_static=(%d,%d,%d) "
                    "at sample mid-path (centre~%.2f,%.2f,%.2f)\n",
                    cell.x, cell.y, cell.z,
                    it->x, it->y, it->z,
                    pcx + ox, pcy + oy, pcz + oz);
          return false;
        }
    }
  }

  return true;
}

static bool occupiedAt(const cellSet & occ, int x, int y, int z) {
  return occ.find(rotationRules_c::cell_t(x, y, z)) != occ.end();
}

static int axialCoord(const rotationRules_c::cell_t & c, unsigned int axis) {
  if (axis == 0) return c.x;
  if (axis == 1) return c.y;
  return c.z;
}

/* Moving voxel lies on the pivot column (same in-plane coords as pivot). */
static bool onPivotColumn(const rotationRules_c::cell_t & c,
                          const rotationRules_c::cell_t & pivot,
                          unsigned int axis) {
  if (axis == 0)
    return c.y == pivot.y && c.z == pivot.z;
  if (axis == 1)
    return c.x == pivot.x && c.z == pivot.z;
  return c.x == pivot.x && c.y == pivot.y;
}

/* True if any moving voxel on layer `a` is face-adjacent to an other-piece voxel. */
static bool layerTouchesOther(const cellSet & occ,
                              const std::vector<rotationRules_c::cell_t> & startCells,
                              unsigned int axis,
                              int a) {
  static const int d[6][3] = {
    { -1, 0, 0 }, { 1, 0, 0 },
    { 0, -1, 0 }, { 0, 1, 0 },
    { 0, 0, -1 }, { 0, 0, 1 }
  };

  for (unsigned int i = 0; i < startCells.size(); i++) {
    const rotationRules_c::cell_t & c = startCells[i];
    if (axialCoord(c, axis) != a)
      continue;
    for (int k = 0; k < 6; k++) {
      if (occupiedAt(occ, c.x + d[k][0], c.y + d[k][1], c.z + d[k][2]))
        return true;
    }
  }
  return false;
}

/* Record ±U/±V face neighbours of one moving cell for axis-cross (in-plane only). */
static void axisCrossScanCell(const rotationRules_c::cell_t & c,
                              unsigned int axis,
                              const cellSet & occ,
                              const cellSet & start,
                              bool * negU, bool * posU, bool * negV, bool * posV,
                              cellSet * slotOccupied,
                              cellSet * restricted) {

  rotationRules_c::cell_t nu, pu, nv, pv;

  if (axis == 0) {
    nu = rotationRules_c::cell_t(c.x, c.y - 1, c.z);
    pu = rotationRules_c::cell_t(c.x, c.y + 1, c.z);
    nv = rotationRules_c::cell_t(c.x, c.y, c.z - 1);
    pv = rotationRules_c::cell_t(c.x, c.y, c.z + 1);
  } else if (axis == 1) {
    nu = rotationRules_c::cell_t(c.x - 1, c.y, c.z);
    pu = rotationRules_c::cell_t(c.x + 1, c.y, c.z);
    nv = rotationRules_c::cell_t(c.x, c.y, c.z - 1);
    pv = rotationRules_c::cell_t(c.x, c.y, c.z + 1);
  } else {
    nu = rotationRules_c::cell_t(c.x - 1, c.y, c.z);
    pu = rotationRules_c::cell_t(c.x + 1, c.y, c.z);
    nv = rotationRules_c::cell_t(c.x, c.y - 1, c.z);
    pv = rotationRules_c::cell_t(c.x, c.y + 1, c.z);
  }

  const rotationRules_c::cell_t slots[4] = { nu, pu, nv, pv };
  bool * flags[4] = { negU, posU, negV, posV };

  for (int i = 0; i < 4; i++) {
    if (start.find(slots[i]) != start.end())
      continue;
    if (occ.find(slots[i]) != occ.end()) {
      *flags[i] = true;
      if (slotOccupied)
        slotOccupied->insert(slots[i]);
    } else if (restricted) {
      restricted->insert(slots[i]);
    }
  }
}

/**
 * Static ±U/±V neighbours face-adjacent to pivot-column moving voxels,
 * aggregated across layers along the rotation axis that face-touch the
 * other piece. Opposite sides on the same layer are allowed; opposition
 * across two different touching layers is rejected.
 */
static bool axisCrossClear(const cellSet & occ,
                           const std::vector<rotationRules_c::cell_t> & startCells,
                           const rotationRules_c::cell_t & pivot,
                           unsigned int axis) {

  if (startCells.empty())
    return true;

  cellSet start;
  for (unsigned int i = 0; i < startCells.size(); i++)
    start.insert(startCells[i]);

  int aMin = axialCoord(startCells[0], axis);
  int aMax = aMin;
  for (unsigned int i = 1; i < startCells.size(); i++) {
    int a = axialCoord(startCells[i], axis);
    if (a < aMin) aMin = a;
    if (a > aMax) aMax = a;
  }

  bool seenNegU = false, seenPosU = false;
  bool seenNegV = false, seenPosV = false;

  for (int a = aMin; a <= aMax; a++) {
    if (!layerTouchesOther(occ, startCells, axis, a))
      continue;

    bool layerNegU = false, layerPosU = false;
    bool layerNegV = false, layerPosV = false;

    for (unsigned int i = 0; i < startCells.size(); i++) {
      const rotationRules_c::cell_t & c = startCells[i];
      if (axialCoord(c, axis) != a)
        continue;
      if (!onPivotColumn(c, pivot, axis))
        continue;
      axisCrossScanCell(c, axis, occ, start,
                        &layerNegU, &layerPosU, &layerNegV, &layerPosV, 0, 0);
    }

    if ((layerPosU && seenNegU) || (layerNegU && seenPosU) ||
        (layerPosV && seenNegV) || (layerNegV && seenPosV)) {
      if (getenv("BT_ROT_DEBUG")) {
        const char * uName = (axis == 0) ? "Y" : "X";
        const char * vName = (axis == 0) ? "Z" : ((axis == 1) ? "Z" : "Y");
        fprintf(stderr,
                "ROT_AXIS_CROSS detail pivot=(%d,%d,%d) axis=%u "
                "touching-layer=%d cross-layer sides: -%s=%d +%s=%d -%s=%d +%s=%d "
                "(layer -%s=%d +%s=%d -%s=%d +%s=%d)\n",
                pivot.x, pivot.y, pivot.z, axis, a,
                uName, (int)seenNegU, uName, (int)seenPosU,
                vName, (int)seenNegV, vName, (int)seenPosV,
                uName, (int)layerNegU, uName, (int)layerPosU,
                vName, (int)layerNegV, vName, (int)layerPosV);
      }
      return false;
    }

    seenNegU |= layerNegU;
    seenPosU |= layerPosU;
    seenNegV |= layerNegV;
    seenPosV |= layerPosV;
  }

  return true;
}

}

bool rotationRules_c::allowRotation(const std::vector<cell_t> & occupied,
                                    const std::vector<cell_t> & startCells,
                                    const std::vector<cell_t> & endCells,
                                    const cell_t & pivot,
                                    unsigned int axis,
                                    unsigned int sense) const {

  cellSet occ;
  for (unsigned int i = 0; i < occupied.size(); i++)
    occ.insert(occupied[i]);

  {
    const char * spec = getenv("BT_ROT_DUMP");
    int px, py, pz, a, s;
    if (spec && sscanf(spec, "%d,%d,%d,%u,%u", &px, &py, &pz, &a, &s) == 5 &&
        pivot.x == px && pivot.y == py && pivot.z == pz &&
        axis == (unsigned)a && sense == (unsigned)s) {
      static bool dumped = false;
      if (!dumped) {
        dumped = true;
        fprintf(stderr, "ROT_DUMP moving cells (%zu):\n", startCells.size());
        for (unsigned int i = 0; i < startCells.size(); i++)
          fprintf(stderr, "  M %d %d %d\n", startCells[i].x, startCells[i].y, startCells[i].z);
        fprintf(stderr, "ROT_DUMP static/other cells (%zu):\n", occupied.size());
        for (unsigned int i = 0; i < occupied.size(); i++)
          fprintf(stderr, "  S %d %d %d\n", occupied[i].x, occupied[i].y, occupied[i].z);
      }
    }
  }

  /* End-position: final voxels must not overlap other pieces */
  for (unsigned int i = 0; i < endCells.size(); i++)
    if (occ.find(endCells[i]) != occ.end()) {
      if (getenv("BT_ROT_DEBUG"))
        fprintf(stderr,
                "ROT_REJECT end-overlap pivot=(%d,%d,%d) axis=%u sense=%u end=(%d,%d,%d)\n",
                pivot.x, pivot.y, pivot.z, axis, sense,
                endCells[i].x, endCells[i].y, endCells[i].z);
      return false;
    }

  /* Opposite static neighbours forbidden across layers that touch the other piece */
  if (!axisCrossClear(occ, startCells, pivot, axis)) {
    if (getenv("BT_ROT_DEBUG"))
      fprintf(stderr,
              "ROT_REJECT axis-cross pivot=(%d,%d,%d) axis=%u sense=%u "
              "(opposite ±in-plane static neighbours face-adjacent to moving piece on layers touching other piece)\n",
              pivot.x, pivot.y, pivot.z, axis, sense);
    return false;
  }

  /* Continuous path must not clip other pieces mid-turn */
  if (!arcClear(occ, startCells, pivot, axis, sense)) {
    if (getenv("BT_ROT_DEBUG"))
      fprintf(stderr,
              "ROT_REJECT arc-sweep pivot=(%d,%d,%d) axis=%u sense=%u "
              "(mid-path unit cube overlaps other piece)\n",
              pivot.x, pivot.y, pivot.z, axis, sense);
    return false;
  }

  if (getenv("BT_ROT_DEBUG"))
    fprintf(stderr,
            "ROT_ALLOW pivot=(%d,%d,%d) axis=%u sense=%u\n",
            pivot.x, pivot.y, pivot.z, axis, sense);

  return true;
}

void rotationRules_c::collectDebugConflictCells(
    const std::vector<cell_t> & occupied,
    const std::vector<cell_t> & startCells,
    const cell_t & pivot,
    unsigned int axis,
    unsigned int sense,
    std::vector<cell_t> & outBlocking,
    std::vector<cell_t> & outClearance,
    std::vector<cell_t> & outRestricted) const {

  outBlocking.clear();
  outClearance.clear();
  outRestricted.clear();

  cellSet occ;
  for (unsigned int i = 0; i < occupied.size(); i++)
    occ.insert(occupied[i]);

  cellSet start;
  for (unsigned int i = 0; i < startCells.size(); i++)
    start.insert(startCells[i]);

  cellSet blocking;
  cellSet clearance;
  cellSet restricted;

  /* Arc-sweep samples: every overlapped lattice cell that is not a start
   * voxel is a clearance cell; those also in occ are hard blockers. */
  {
    const double pcx = pivot.x + 0.5;
    const double pcy = pivot.y + 0.5;
    const double pcz = pivot.z + 0.5;
    static const int STEPS = 8;
    const double halfPi = 1.5707963267948966;

    for (unsigned int ci = 0; ci < startCells.size(); ci++) {
      const cell_t & cell = startCells[ci];
      double dx = (cell.x + 0.5) - pcx;
      double dy = (cell.y + 0.5) - pcy;
      double dz = (cell.z + 0.5) - pcz;

      if (axis == 0 && dy == 0 && dz == 0) continue;
      if (axis == 1 && dx == 0 && dz == 0) continue;
      if (axis == 2 && dx == 0 && dy == 0) continue;

      for (int s = 1; s < STEPS; s++) {
        double theta = halfPi * (double)s / (double)STEPS;
        double ox, oy, oz;
        rotatedCenter(dx, dy, dz, axis, sense, theta, &ox, &oy, &oz);

        cellSet hit;
        addOverlappedCells(pcx + ox, pcy + oy, pcz + oz, hit);
        for (cellSet::const_iterator it = hit.begin(); it != hit.end(); ++it) {
          if (start.find(*it) != start.end())
            continue;
          clearance.insert(*it);
          if (occ.find(*it) != occ.end())
            blocking.insert(*it);
        }
      }
    }
  }

  /* Axis-cross: ±in-plane slots face-adjacent to pivot-column moving voxels on
   * touching layers. Cross-layer opposition marks hard blockers. */
  if (!startCells.empty()) {
    int aMin = axialCoord(startCells[0], axis);
    int aMax = aMin;
    for (unsigned int i = 1; i < startCells.size(); i++) {
      int a = axialCoord(startCells[i], axis);
      if (a < aMin) aMin = a;
      if (a > aMax) aMax = a;
    }

    bool seenNegU = false, seenPosU = false;
    bool seenNegV = false, seenPosV = false;
    cellSet slotOccupied;

    for (int a = aMin; a <= aMax; a++) {
      if (!layerTouchesOther(occ, startCells, axis, a))
        continue;

      bool layerNegU = false, layerPosU = false;
      bool layerNegV = false, layerPosV = false;

      for (unsigned int i = 0; i < startCells.size(); i++) {
        const cell_t & c = startCells[i];
        if (axialCoord(c, axis) != a)
          continue;
        if (!onPivotColumn(c, pivot, axis))
          continue;
        axisCrossScanCell(c, axis, occ, start,
                          &layerNegU, &layerPosU, &layerNegV, &layerPosV,
                          &slotOccupied, &restricted);
      }

      bool cross = (layerPosU && seenNegU) || (layerNegU && seenPosU) ||
                   (layerPosV && seenNegV) || (layerNegV && seenPosV);
      if (cross) {
        for (cellSet::const_iterator it = slotOccupied.begin(); it != slotOccupied.end(); ++it)
          blocking.insert(*it);
      }

      seenNegU |= layerNegU;
      seenPosU |= layerPosU;
      seenNegV |= layerNegV;
      seenPosV |= layerPosV;
    }

    for (cellSet::const_iterator it = slotOccupied.begin(); it != slotOccupied.end(); ++it)
      clearance.insert(*it);
  }

  for (cellSet::const_iterator it = blocking.begin(); it != blocking.end(); ++it)
    outBlocking.push_back(*it);
  for (cellSet::const_iterator it = clearance.begin(); it != clearance.end(); ++it)
    if (blocking.find(*it) == blocking.end())
      outClearance.push_back(*it);
  for (cellSet::const_iterator it = restricted.begin(); it != restricted.end(); ++it)
    if (blocking.find(*it) == blocking.end() && clearance.find(*it) == clearance.end())
      outRestricted.push_back(*it);
}
