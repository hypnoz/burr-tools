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
        if (occ.find(*it) != occ.end())
          return false;
    }
  }

  return true;
}

static bool occupiedAt(const cellSet & occ, int x, int y, int z) {
  return occ.find(rotationRules_c::cell_t(x, y, z)) != occ.end();
}

/**
 * Static neighbours on each in-plane axis, aggregated across all layers
 * along the rotation axis (span of the moving piece).
 *
 * If any layer has a static voxel on one side of an in-plane axis (e.g. +X),
 * no layer may have a static voxel on the opposite side (e.g. -X). Same for
 * the other in-plane pair (Y for a Z rotation). Same-layer 3/7/11 patterns
 * are included as the special case where both opposites appear on one layer.
 */
static bool axisCrossClear(const cellSet & occ,
                           const std::vector<rotationRules_c::cell_t> & startCells,
                           const rotationRules_c::cell_t & pivot,
                           unsigned int axis) {

  if (startCells.empty())
    return true;

  int aMin = 0, aMax = 0;
  if (axis == 0) {
    aMin = aMax = startCells[0].x;
    for (unsigned int i = 1; i < startCells.size(); i++) {
      if (startCells[i].x < aMin) aMin = startCells[i].x;
      if (startCells[i].x > aMax) aMax = startCells[i].x;
    }
  } else if (axis == 1) {
    aMin = aMax = startCells[0].y;
    for (unsigned int i = 1; i < startCells.size(); i++) {
      if (startCells[i].y < aMin) aMin = startCells[i].y;
      if (startCells[i].y > aMax) aMax = startCells[i].y;
    }
  } else {
    aMin = aMax = startCells[0].z;
    for (unsigned int i = 1; i < startCells.size(); i++) {
      if (startCells[i].z < aMin) aMin = startCells[i].z;
      if (startCells[i].z > aMax) aMax = startCells[i].z;
    }
  }

  bool hasNegU = false, hasPosU = false;
  bool hasNegV = false, hasPosV = false;

  for (int a = aMin; a <= aMax; a++) {
    if (axis == 0) {
      /* Axis // X, layer = x: in-plane Y and Z at (a, pivot.y, pivot.z) */
      if (occupiedAt(occ, a, pivot.y - 1, pivot.z)) hasNegU = true;
      if (occupiedAt(occ, a, pivot.y + 1, pivot.z)) hasPosU = true;
      if (occupiedAt(occ, a, pivot.y, pivot.z - 1)) hasNegV = true;
      if (occupiedAt(occ, a, pivot.y, pivot.z + 1)) hasPosV = true;
    } else if (axis == 1) {
      /* Axis // Y, layer = y: in-plane X and Z */
      if (occupiedAt(occ, pivot.x - 1, a, pivot.z)) hasNegU = true;
      if (occupiedAt(occ, pivot.x + 1, a, pivot.z)) hasPosU = true;
      if (occupiedAt(occ, pivot.x, a, pivot.z - 1)) hasNegV = true;
      if (occupiedAt(occ, pivot.x, a, pivot.z + 1)) hasPosV = true;
    } else {
      /* Axis // Z, layer = z: in-plane X and Y */
      if (occupiedAt(occ, pivot.x - 1, pivot.y, a)) hasNegU = true;
      if (occupiedAt(occ, pivot.x + 1, pivot.y, a)) hasPosU = true;
      if (occupiedAt(occ, pivot.x, pivot.y - 1, a)) hasNegV = true;
      if (occupiedAt(occ, pivot.x, pivot.y + 1, a)) hasPosV = true;
    }

    if ((hasNegU && hasPosU) || (hasNegV && hasPosV))
      return false;
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

  /* End-position: final voxels must not overlap other pieces */
  for (unsigned int i = 0; i < endCells.size(); i++)
    if (occ.find(endCells[i]) != occ.end())
      return false;

  /* Opposite static neighbours forbidden across all layers on the axis */
  if (!axisCrossClear(occ, startCells, pivot, axis))
    return false;

  /* Continuous path must not clip other pieces mid-turn */
  if (!arcClear(occ, startCells, pivot, axis, sense))
    return false;

  return true;
}
