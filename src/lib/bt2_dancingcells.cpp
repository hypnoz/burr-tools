/* BurrTools
 *
 * Knuth dancing cells search used only by BurrTools 2 (assembler_bt2_c).
 */
#include "bt2_dancingcells.h"

#include <algorithm>
#include <limits>

namespace {
const float kBranchCoeff = 0.13f;
}

bt2Cells_c::bt2Cells_c(void) :
  nPrimary(0),
  nItems(0),
  setupDone(false),
  active(0),
  activeFrames(0),
  cbUser(0),
  cbFn(0),
  iterations(0)
{
}

bt2Cells_c::bt2Cells_c(const bt2Cells_c & src) :
  nPrimary(src.nPrimary),
  nItems(src.nItems),
  setupDone(src.setupDone),
  options(src.options),
  nodes(src.nodes),
  itemBase(src.itemBase),
  itemSize(src.itemSize),
  SET(src.SET),
  itemOrder(src.itemOrder),
  itemIndex(src.itemIndex),
  active(src.active),
  pendingItems(),
  pendingRows(),
  SOL(src.SOL),
  solRows(src.solRows),
  frames(src.frames),
  activeFrames(src.activeFrames),
  cbUser(0),
  cbFn(0),
  iterations(0)
{
}

bt2Cells_c & bt2Cells_c::operator=(const bt2Cells_c & src) {
  if (this == &src)
    return *this;
  nPrimary = src.nPrimary;
  nItems = src.nItems;
  setupDone = src.setupDone;
  options = src.options;
  nodes = src.nodes;
  itemBase = src.itemBase;
  itemSize = src.itemSize;
  SET = src.SET;
  itemOrder = src.itemOrder;
  itemIndex = src.itemIndex;
  active = src.active;
  pendingItems.clear();
  pendingRows.clear();
  SOL = src.SOL;
  solRows = src.solRows;
  frames = src.frames;
  activeFrames = src.activeFrames;
  iterations = 0;
  return *this;
}

bt2Cells_c::~bt2Cells_c(void) {
}

void bt2Cells_c::clear(void) {
  nPrimary = 0;
  nItems = 0;
  setupDone = false;
  options.clear();
  nodes.clear();
  itemBase.clear();
  itemSize.clear();
  SET.clear();
  itemOrder.clear();
  itemIndex.clear();
  active = 0;
  pendingItems.clear();
  pendingRows.clear();
  SOL.clear();
  solRows.clear();
  frames.clear();
  activeFrames = 0;
  iterations = 0;
}

void bt2Cells_c::prepare(unsigned int prim, unsigned int second) {
  clear();
  nPrimary = prim;
  nItems = prim + second;
}

void bt2Cells_c::addOption(unsigned int rowId, const std::vector<unsigned int> & items) {
  pendingRows.push_back(rowId);
  pendingItems.push_back(items);
}

void bt2Cells_c::finishSetup(void) {
  if (nItems == 0) {
    setupDone = true;
    frames.assign(1, Frame());
    activeFrames = 1;
    return;
  }

  std::vector<unsigned int> count(nItems, 0);
  for (unsigned int o = 0; o < pendingItems.size(); o++) {
    for (unsigned int k = 0; k < pendingItems[o].size(); k++) {
      unsigned int it = pendingItems[o][k];
      if (it < nItems)
        count[it]++;
    }
  }

  itemBase.assign(nItems, 0);
  itemSize.assign(nItems, 0);
  unsigned int pos = 0;
  for (unsigned int i = 0; i < nItems; i++) {
    itemBase[i] = pos;
    pos += count[i];
  }
  SET.assign(pos, 0);

  nodes.clear();
  options.clear();
  options.reserve(pendingItems.size());

  std::vector<unsigned int> fill(nItems, 0);
  for (unsigned int o = 0; o < pendingItems.size(); o++) {
    Option opt;
    opt.rowId = pendingRows[o];
    opt.start = (unsigned int)nodes.size();
    opt.len = 0;
    for (unsigned int k = 0; k < pendingItems[o].size(); k++) {
      unsigned int it = pendingItems[o][k];
      if (it >= nItems)
        continue;
      Node n;
      n.item = it;
      n.loc = itemBase[it] + fill[it];
      SET[n.loc] = o;
      fill[it]++;
      nodes.push_back(n);
      opt.len++;
    }
    options.push_back(opt);
  }

  for (unsigned int i = 0; i < nItems; i++)
    itemSize[i] = fill[i];

  itemOrder.resize(nItems);
  itemIndex.resize(nItems);
  for (unsigned int i = 0; i < nItems; i++) {
    itemOrder[i] = i;
    itemIndex[i] = i;
  }
  active = nItems;

  /* Secondary items with no options can sit inactive. */
  for (unsigned int i = nItems; i-- > 0; ) {
    if (!isPrimary(i) && itemSize[i] == 0)
      deactivate(i);
  }

  pendingItems.clear();
  pendingRows.clear();

  SOL.clear();
  solRows.clear();
  frames.assign(1, Frame());
  activeFrames = 1;
  setupDone = true;
}

void bt2Cells_c::setSolutionCallback(void * user, solution_fn fn) {
  cbUser = user;
  cbFn = fn;
}

void bt2Cells_c::deactivate(unsigned int item) {
  unsigned int idx = itemIndex[item];
  if (idx >= active)
    return;
  unsigned int lastIdx = active - 1;
  unsigned int last = itemOrder[lastIdx];
  itemOrder[idx] = last;
  itemOrder[lastIdx] = item;
  itemIndex[last] = idx;
  itemIndex[item] = lastIdx;
  active--;
}

void bt2Cells_c::storeSizes(Save & s) const {
  s.size = itemSize;
  s.active = active;
}

void bt2Cells_c::restoreSizes(const Save & s) {
  itemSize = s.size;
  active = s.active;
}

bool bt2Cells_c::hideFromItem(unsigned int opt, unsigned int item) {
  if (!isActiveItem(item))
    return true;

  unsigned int sz = itemSize[item];
  if (sz == 0)
    return !isPrimary(item);

  unsigned int loc = 0;
  bool found = false;
  unsigned int start = itemBase[item];
  unsigned int end = options[opt].start + options[opt].len;
  for (unsigned int n = options[opt].start; n < end; n++) {
    if (nodes[n].item == item) {
      loc = nodes[n].loc;
      found = true;
      break;
    }
  }
  if (!found)
    return true;

  unsigned int lastLoc = start + sz - 1;
  if (loc < start || loc > lastLoc)
    return true;

  if (loc != lastLoc) {
    unsigned int other = SET[lastLoc];
    SET[loc] = other;
    SET[lastLoc] = opt;
    for (unsigned int n = options[other].start; n < options[other].start + options[other].len; n++) {
      if (nodes[n].item == item) {
        nodes[n].loc = loc;
        break;
      }
    }
    for (unsigned int n = options[opt].start; n < end; n++) {
      if (nodes[n].item == item) {
        nodes[n].loc = lastLoc;
        break;
      }
    }
  }

  itemSize[item] = sz - 1;
  if (isPrimary(item) && itemSize[item] == 0)
    return false;
  return true;
}

bool bt2Cells_c::discardOption(unsigned int opt) {
  unsigned int start = options[opt].start;
  unsigned int end = start + options[opt].len;
  for (unsigned int n = start; n < end; n++) {
    if (!hideFromItem(opt, nodes[n].item))
      return false;
  }
  return true;
}

bool bt2Cells_c::selectOption(unsigned int opt) {
  unsigned int start = options[opt].start;
  unsigned int end = start + options[opt].len;
  for (unsigned int n = start; n < end; n++) {
    unsigned int item = nodes[n].item;
    if (!isActiveItem(item))
      continue;

    std::vector<unsigned int> others;
    unsigned int base = itemBase[item];
    unsigned int sz = itemSize[item];
    others.reserve(sz);
    for (unsigned int k = 0; k < sz; k++) {
      unsigned int other = SET[base + k];
      if (other != opt)
        others.push_back(other);
    }
    for (unsigned int k = 0; k < others.size(); k++) {
      if (!discardOption(others[k]))
        return false;
    }
    deactivate(item);
  }
  SOL.push_back(opt);
  return true;
}

bool bt2Cells_c::findBranch(std::vector<unsigned int> & force, unsigned int & chosen, bool & contradiction) {
  force.clear();
  chosen = 0;
  contradiction = false;
  unsigned int minSize = std::numeric_limits<unsigned int>::max();
  bool have = false;

  for (unsigned int idx = 0; idx < active; idx++) {
    unsigned int item = itemOrder[idx];
    if (!isPrimary(item))
      continue;
    unsigned int s = itemSize[item];
    if (s == 0) {
      contradiction = true;
      return false;
    }
    if (s == 1)
      force.push_back(item);
    else if (s < minSize) {
      minSize = s;
      chosen = item;
      have = true;
    }
  }

  if (!force.empty())
    return true;
  if (!have) {
    /* No active primary items: solution (or nothing to do). */
    return false;
  }
  return true;
}

bool bt2Cells_c::fastTrack(const std::vector<unsigned int> & force) {
  for (unsigned int i = 0; i < force.size(); i++) {
    unsigned int item = force[i];
    if (!isActiveItem(item))
      continue;
    std::vector<unsigned int> opts;
    unsigned int sz = itemSize[item];
    unsigned int base = itemBase[item];
    opts.reserve(sz);
    for (unsigned int k = 0; k < sz; k++)
      opts.push_back(SET[base + k]);
    for (unsigned int k = 0; k < opts.size(); k++) {
      if (!selectOption(opts[k]))
        return false;
      if (k + 1 < opts.size() && !isActiveItem(item))
        return false;
    }
  }
  return true;
}

void bt2Cells_c::pushFrame(void) {
  if (activeFrames == frames.size())
    frames.push_back(Frame());
  activeFrames++;
  frames[activeFrames - 1].state = 0;
  frames[activeFrames - 1].solMark = 0;
  frames[activeFrames - 1].x = 0;
}

void bt2Cells_c::popFrame(void) {
  if (activeFrames > 0)
    activeFrames--;
}

void bt2Cells_c::convertSolution(void) {
  solRows.resize(SOL.size());
  for (unsigned int i = 0; i < SOL.size(); i++)
    solRows[i] = options[SOL[i]].rowId;
}

void bt2Cells_c::emitSolution(void) {
  convertSolution();
  if (cbFn)
    cbFn(cbUser, solRows.empty() ? 0 : &solRows[0], (unsigned int)solRows.size());
}

void bt2Cells_c::forwardToNextBranch(void) {
  popFrame();
  while (activeFrames > 0) {
    Frame & f = frames[activeFrames - 1];
    switch (f.state) {
      case 0:
        return;
      case 2:
        SOL.resize(f.solMark);
        popFrame();
        break;
      case 1:
        popFrame();
        break;
      case 3:
        if (!SOL.empty())
          SOL.pop_back();
        restoreSizes(f.save);
        discardOption(f.x);
        f.state = 1;
        pushFrame();
        return;
      default:
        popFrame();
        break;
    }
  }
}

bt2Cells_c * bt2Cells_c::split(void) {
  if (!setupDone || finished())
    return 0;

  bool haveState3 = false;
  for (unsigned int i = 0; i < activeFrames; i++) {
    if (frames[i].state == 3) {
      haveState3 = true;
      break;
    }
  }

  if (haveState3) {
    bt2Cells_c * other = new bt2Cells_c(*this);
    forwardToNextBranch();
    if (other->finished()) {
      delete other;
      return 0;
    }
    return other;
  }

  /* At the choose-frame: peel the first option of the MRV item. */
  if (activeFrames == 0)
    return 0;

  std::vector<unsigned int> force;
  unsigned int chosen = 0;
  bool contra = false;
  if (!findBranch(force, chosen, contra) || contra || !force.empty())
    return 0;
  if (itemSize[chosen] < 2)
    return 0;

  unsigned int opt = firstOptionOf(chosen);
  bt2Cells_c * other = new bt2Cells_c(*this);
  other->storeSizes(other->frames[other->activeFrames - 1].save);
  other->frames[other->activeFrames - 1].x = opt;
  if (!other->selectOption(opt)) {
    delete other;
    return 0;
  }
  other->frames[other->activeFrames - 1].state = 3;
  other->pushFrame();

  discardOption(opt);
  return other;
}

unsigned int bt2Cells_c::remainingWork(void) const {
  if (!setupDone || finished())
    return 0;
  unsigned int best = 0;
  for (unsigned int idx = 0; idx < active; idx++) {
    unsigned int item = itemOrder[idx];
    if (!isPrimary(item))
      continue;
    if (itemSize[item] > best)
      best = itemSize[item];
  }
  return best;
}

float bt2Cells_c::progress(void) const {
  if (!setupDone)
    return 0;
  if (finished())
    return 1.0f;
  float res = 1.0f;
  for (int i = (int)activeFrames - 1; i >= 0; --i) {
    switch (frames[i].state) {
      case 0: res = 0.0f; break;
      case 1: res = (1.0f - kBranchCoeff) * res + kBranchCoeff; break;
      case 2: break;
      case 3: res *= kBranchCoeff; break;
      default: break;
    }
  }
  return res;
}

void bt2Cells_c::solve(unsigned int maxIterations, std::atomic<bool> * abort) {
  if (!setupDone)
    return;

  unsigned int left = maxIterations;
  const bool limited = maxIterations != 0;
  if (limited)
    left++;

  while (activeFrames > 0) {
    if (abort && abort->load(std::memory_order_acquire))
      return;

    Frame & f = frames[activeFrames - 1];
    switch (f.state) {
      case 0: {
        if (limited) {
          if (left == 1)
            return;
          left--;
        }
        iterations++;

        std::vector<unsigned int> force;
        unsigned int chosen = 0;
        bool contra = false;
        bool branched = findBranch(force, chosen, contra);

        if (contra) {
          popFrame();
          break;
        }

        if (!force.empty()) {
          f.solMark = (unsigned int)SOL.size();
          if (fastTrack(force)) {
            f.state = 2;
            pushFrame();
          } else {
            SOL.resize(f.solMark);
            popFrame();
          }
          break;
        }

        if (!branched) {
          emitSolution();
          popFrame();
          break;
        }

        f.x = firstOptionOf(chosen);
        storeSizes(f.save);
        if (selectOption(f.x)) {
          f.state = 3;
          pushFrame();
        } else {
          restoreSizes(f.save);
          discardOption(f.x);
          f.state = 1;
          pushFrame();
        }
        break;
      }
      case 2:
        SOL.resize(f.solMark);
        popFrame();
        break;
      case 1:
        popFrame();
        break;
      case 3:
        if (!SOL.empty())
          SOL.pop_back();
        restoreSizes(f.save);
        discardOption(f.x);
        f.state = 1;
        pushFrame();
        break;
      default:
        popFrame();
        break;
    }
  }
}
