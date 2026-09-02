/* BurrTools
 *
 * Knuth dancing cells (TAOCP 7.2.2.3 Algorithm C / F subset) for BurrTools 2.
 * Exact cover with optional secondary items. C++11. Not used by Classic or Crowell.
 *
 * Feature notes: bt2_solver.h
 */
#ifndef __BT2_DANCING_CELLS_H__
#define __BT2_DANCING_CELLS_H__

#include <atomic>
#include <vector>

class bt2Cells_c {

public:
  typedef void (*solution_fn)(void * user, const unsigned int * rowIds, unsigned int n);

  bt2Cells_c(void);
  bt2Cells_c(const bt2Cells_c & src);
  ~bt2Cells_c(void);

  void clear(void);

  void prepare(unsigned int nPrimary, unsigned int nSecondary);
  void addOption(unsigned int rowId, const std::vector<unsigned int> & items);
  void finishSetup(void);
  bool ready(void) const { return setupDone; }

  void setSolutionCallback(void * user, solution_fn fn);

  /* 0 iterations = run until finished or abort. */
  void solve(unsigned int maxIterations, std::atomic<bool> * abort);

  bool finished(void) const { return activeFrames == 0; }
  float progress(void) const;
  unsigned int remainingWork(void) const;
  unsigned long getIterations(void) const { return iterations; }
  unsigned int depth(void) const { return activeFrames; }

  /* Clone current remaining matrix/stack, then advance this instance past the
   * current branch (Andreas MCCSolver::split + forwardToNextBranch). */
  bt2Cells_c * split(void);

  const std::vector<unsigned int> & currentRowIds(void) const { return solRows; }

  bt2Cells_c & operator=(const bt2Cells_c & src);

private:
  struct Option {
    unsigned int rowId;
    unsigned int start;
    unsigned int len;
  };

  struct Node {
    unsigned int item;
    unsigned int loc;
  };

  struct Save {
    std::vector<unsigned int> size;
    unsigned int active;
  };

  struct Frame {
    int state;
    unsigned int solMark;
    unsigned int x;
    Save save;
    Frame(void) : state(0), solMark(0), x(0) {}
  };

  unsigned int nPrimary;
  unsigned int nItems;
  bool setupDone;

  std::vector<Option> options;
  std::vector<Node> nodes;
  std::vector<unsigned int> itemBase;
  std::vector<unsigned int> itemSize;
  std::vector<unsigned int> SET;
  std::vector<unsigned int> itemOrder;
  std::vector<unsigned int> itemIndex;
  unsigned int active;

  std::vector<std::vector<unsigned int> > pendingItems;
  std::vector<unsigned int> pendingRows;

  std::vector<unsigned int> SOL;
  std::vector<unsigned int> solRows;

  std::vector<Frame> frames;
  unsigned int activeFrames;

  void * cbUser;
  solution_fn cbFn;
  unsigned long iterations;

  bool isPrimary(unsigned int item) const { return item < nPrimary; }
  bool isActiveItem(unsigned int item) const { return itemIndex[item] < active; }

  void deactivate(unsigned int item);
  void storeSizes(Save & s) const;
  void restoreSizes(const Save & s);

  bool hideFromItem(unsigned int opt, unsigned int item);
  bool discardOption(unsigned int opt);
  bool selectOption(unsigned int opt);

  bool findBranch(std::vector<unsigned int> & force, unsigned int & chosen, bool & contradiction);
  bool fastTrack(const std::vector<unsigned int> & force);

  void pushFrame(void);
  void popFrame(void);
  void convertSolution(void);
  void emitSolution(void);
  void forwardToNextBranch(void);

  unsigned int firstOptionOf(unsigned int item) const {
    return SET[itemBase[item]];
  }
};

#endif
