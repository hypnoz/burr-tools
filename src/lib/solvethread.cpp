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
#include "solvethread.h"

#include "disassembly.h"
#include "problem.h"
#include "puzzle.h"
#include "assembly.h"
#include "disassembler_0.h"
#include "disassembler_factory.h"
#include "bt2_assemble.h"
#include "solution.h"
#include "voxel.h"

#include <chrono>

namespace {

enum {
  SOL_COUNT_ASM,
  SOL_SAVE_ASM,
  SOL_COUNT_DISASM,
  SOL_DISASM,
};

int solutionActionFromParameters(int parameters) {
  int action = 0;
  if (!(parameters & solveThread_c::PAR_JUST_COUNT)) action += 1;
  if (parameters & solveThread_c::PAR_DISASSM) action += 2;
  return action;
}

struct disasmDurationGuard_c {
  std::atomic<unsigned int> * count;
  std::atomic<unsigned long long> * totalMs;
  std::chrono::steady_clock::time_point t0;

  disasmDurationGuard_c(std::atomic<unsigned int> * c, std::atomic<unsigned long long> * t)
    : count(c), totalMs(t), t0(std::chrono::steady_clock::now()) {}

  ~disasmDurationGuard_c() {
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    if (ms < 0)
      ms = 0;
    totalMs->fetch_add((unsigned long long)ms, std::memory_order_relaxed);
    count->fetch_add(1, std::memory_order_relaxed);
  }
};

unsigned int chooseDisasmWorkerCount(bool rotationsEnabled) {
#ifdef NO_THREADING
  return 1;
#else
  /* 90° rotation search is memory-bandwidth heavy. Extra workers contend
   * with the assembler and with each other, and on rotation puzzles that
   * made wall-clock time worse than a single worker. */
  if (rotationsEnabled)
    return 1;

  unsigned int hw = std::thread::hardware_concurrency();
  if (hw < 1)
    hw = 1;
  if (hw <= 2)
    return 1;
  unsigned int n = hw - 2;
  /* Leave headroom for GUI + assembler. Cap concurrent BFS fronts so a
   * large machine does not spawn dozens of searches at once. */
  if (n > 16)
    n = 16;
  return n;
#endif
}

unsigned long long elapsedMs(std::chrono::steady_clock::time_point t0) {
  long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
  if (ms < 0)
    return 0;
  return (unsigned long long)ms;
}

} // namespace

void solveThread_c::run(void){

  try {

    /* first check, if there is an assembler available with the
     * problem, if there is one take that
     */
    if (puzzle.getAssembler()) {
      assm = puzzle.getAssembler();
      assm->applySolutionFilterFlags(parameters & PAR_KEEP_MIRROR, parameters & PAR_KEEP_ROTATIONS, parameters & PAR_COMPLETE_ROTATIONS);
    }
    else {

      /* otherwise we have to create a new one
       */
      action.store(ACT_PREPARATION, std::memory_order_relaxed);
      statsPhase = PHASE_PREPARE;
      phaseOrigin = std::chrono::steady_clock::now();
      assm = puzzle.getPuzzle().getGridType()->findAssembler(puzzle, false, solverType);

      errState = assm->createMatrix(parameters & PAR_KEEP_MIRROR, parameters & PAR_KEEP_ROTATIONS, parameters & PAR_COMPLETE_ROTATIONS);
      prepareMs.store(elapsedMs(phaseOrigin), std::memory_order_relaxed);
      statsPhase = PHASE_NONE;
      if (errState != assembler_c::ERR_NONE) {

        errParam = assm->getErrorsParam();

        action.store(ACT_ERROR, std::memory_order_relaxed);

        delete assm;
        assm = 0;
        return;
      }

      if (stopPressed.load(std::memory_order_relaxed)) {
        delete assm;
        assm = 0;
        action.store(ACT_PAUSING, std::memory_order_relaxed);
        return;
      }

      if (parameters & PAR_REDUCE) {

        if (!stopPressed.load(std::memory_order_relaxed))
          action.store(ACT_REDUCE, std::memory_order_relaxed);

        statsPhase = PHASE_REDUCE;
        phaseOrigin = std::chrono::steady_clock::now();
        if (!stopPressed.load(std::memory_order_relaxed))
          assm->reduce();
        reduceMs.store(elapsedMs(phaseOrigin), std::memory_order_relaxed);
        statsPhase = PHASE_NONE;
      }

      if (stopPressed.load(std::memory_order_relaxed)) {
        action.store(ACT_PAUSING, std::memory_order_relaxed);
        return;
      }

      /* set the assembler to the problem as soon as it is finished
       * with initialisation, NOT EARLIER as the function
       * also restores the assembler state to a state that might
       * be saved within the problem
       */
      errState = puzzle.setAssembler(assm);
      if (errState != assembler_c::ERR_NONE) {
        action.store(ACT_ERROR, std::memory_order_relaxed);
        delete assm;
        assm = 0;
        return;
      }
    }

    if (return_after_prep) {
      action.store(ACT_PAUSING, std::memory_order_relaxed);
      return;
    }

    if (!stopPressed.load(std::memory_order_relaxed)) {

      for (unsigned int i = 0; i < puzzle.getPuzzle().getNumberOfShapes(); i++)
        puzzle.getPuzzle().getShape(i)->initHotspot();

      action.store(ACT_ASSEMBLING, std::memory_order_relaxed);
      statsPhase = PHASE_ASSEMBLE;
      phaseOrigin = std::chrono::steady_clock::now();
      if (solverType == SOLVER_BT2) {
        assemblerThreadCount = bt2ChooseAssemblerWorkers();
        assemblerThreadCount = bt2Assemble(assm, this, assemblerThreadCount);
      } else {
        assemblerThreadCount = 1;
        assm->assemble(this);
      }
      assemblyMs.store(elapsedMs(phaseOrigin), std::memory_order_relaxed);
      statsPhase = PHASE_NONE;

      if (!stopPressed.load(std::memory_order_relaxed)) {
        statsPhase = PHASE_DRAIN;
        phaseOrigin = std::chrono::steady_clock::now();
        flushDisassemblyQueue();
        drainMs.store(elapsedMs(phaseOrigin), std::memory_order_relaxed);
        statsPhase = PHASE_NONE;
      }

      puzzle.addTime(time(0)-startTime);

      if (stopPressed.load(std::memory_order_relaxed))
        action.store(ACT_PAUSING, std::memory_order_relaxed);
      else if (assm->getFinished() >= 1) {
        action.store(ACT_FINISHED, std::memory_order_relaxed);
        puzzle.finishedSolving();
      } else
        action.store(ACT_PAUSING, std::memory_order_relaxed);

    } else {
      action.store(ACT_PAUSING, std::memory_order_relaxed);
      puzzle.addTime(time(0)-startTime);
    }

  }

  catch (assert_exception & a) {

    ae = a;
    action.store(ACT_ASSERT, std::memory_order_relaxed);
    if (puzzle.getAssembler())
      puzzle.removeAllSolutions();
  }
}

solveThread_c::solveThread_c(problem_c & puz, int par) :
action(ACT_PREPARATION),
puzzle(puz),
parameters(par),
sortMethod(SRT_COMPLETE_MOVES),
solverType(SOLVER_CLASSIC),
solutionLimit(10),
solutionDrop(1),
stopPressed(false),
return_after_prep(false),
assm(0),
assemblerThreadCount(1),
disasmWorkerStop(false),
disasmWorkerCount(0),
disasmPending(0),
disasmCompleted(0),
disasmMsTotal(0),
disasmPeakPending(0),
disasmInseparable(0),
prepareMs(0),
reduceMs(0),
assemblyMs(0),
drainMs(0),
statsPhase(PHASE_NONE),
disasmCreepActive(false),
disasmCreepShown(0)
{

  /* Persist solutions under <solutionsWithRotations> so older BurrTools skip them */
  if (par & PAR_CHECK_ROTATIONS)
    puzzle.setSolutionsWithRotations(true);
}

solveThread_c::~solveThread_c(void) {

  stop();
  joinThread();
  stopDisasmWorker();

  for (unsigned int i = 0; i < disassemblers.size(); i++)
    delete disassemblers[i];
  disassemblers.clear();
}

void solveThread_c::waitUntilFinished(void) {
  joinThread();
}

void solveThread_c::startDisasmWorker(void) {

  if (!(parameters & PAR_DISASSM))
    return;

  const bool checkRotations = (parameters & PAR_CHECK_ROTATIONS) != 0;
  unsigned int n = chooseDisasmWorkerCount(checkRotations);
  disasmWorkerCount.store(n, std::memory_order_relaxed);

  for (unsigned int i = 0; i < n; i++)
    disassemblers.push_back(createDisassembler(puzzle, checkRotations, solverType));

#ifndef NO_THREADING
  disasmWorkerStop.store(false, std::memory_order_relaxed);
  disasmWorkers.reserve(n);
  for (unsigned int i = 0; i < n; i++) {
    disassembler_c * d = disassemblers[i];
    disasmWorkers.emplace_back([this, d]() { this->disasmWorkerRun(d); });
  }
#endif
}

void solveThread_c::stopDisasmWorker(void) {
  cancelDisassemblyWork();
}

void solveThread_c::cancelDisassemblyWork(void) {

  if (!(parameters & PAR_DISASSM))
    return;

  disasmWorkerStop.store(true, std::memory_order_release);

  for (unsigned int i = 0; i < disassemblers.size(); i++)
    if (disassemblers[i])
      disassemblers[i]->stop();

  disasmQueueCv.notify_all();

#ifndef NO_THREADING
  for (unsigned int i = 0; i < disasmWorkers.size(); i++)
    if (disasmWorkers[i].joinable())
      disasmWorkers[i].join();
  disasmWorkers.clear();
#endif

  std::lock_guard<std::mutex> lock(disasmQueueMutex);
  while (!disasmQueue.empty()) {
    delete disasmQueue.front().assembly;
    disasmQueue.pop();
  }
  disasmPending.store(0, std::memory_order_relaxed);
}

void solveThread_c::disasmWorkerRun(disassembler_c * workerDisassm) {

  const int solutionAction = solutionActionFromParameters(parameters);

  while (true) {
    disasmTask_c task;

    {
      std::unique_lock<std::mutex> lock(disasmQueueMutex);
      disasmQueueCv.wait(lock, [this]() {
        return !disasmQueue.empty() || disasmWorkerStop.load(std::memory_order_acquire);
      });

      if (disasmQueue.empty()) {
        if (disasmWorkerStop.load(std::memory_order_acquire))
          break;
        continue;
      }

      task = disasmQueue.front();
      disasmQueue.pop();
    }

    if (disasmWorkerStop.load(std::memory_order_acquire)) {
      delete task.assembly;
      if (disasmPending.fetch_sub(1, std::memory_order_acq_rel) == 1)
        disasmQueueCv.notify_all();
      continue;
    }

    processDisassembly(task, solutionAction, workerDisassm);

    if (disasmPending.fetch_sub(1, std::memory_order_acq_rel) == 1)
      disasmQueueCv.notify_all();
  }
}

void solveThread_c::enqueueDisassembly(assembly_c * a) {

  disasmTask_c task;
  task.assembly = a;
  task.assemblyNumber = puzzle.getNumAssemblies();
  task.solutionNumber = puzzle.getNumSolutions();

  disasmPending.fetch_add(1, std::memory_order_relaxed);
  {
    unsigned int p = disasmPending.load(std::memory_order_relaxed);
    unsigned int peak = disasmPeakPending.load(std::memory_order_relaxed);
    while (p > peak && !disasmPeakPending.compare_exchange_weak(peak, p, std::memory_order_relaxed))
      ;
  }
  {
    std::lock_guard<std::mutex> lock(disasmQueueMutex);
    disasmQueue.push(task);
  }
  disasmQueueCv.notify_one();
}

void solveThread_c::flushDisassemblyQueue(void) {

  if (!(parameters & PAR_DISASSM))
    return;

#ifdef NO_THREADING
  return;
#else
  std::unique_lock<std::mutex> lock(disasmQueueMutex);
  disasmQueueCv.wait(lock, [this]() {
    return stopPressed.load(std::memory_order_acquire) ||
           disasmWorkerStop.load(std::memory_order_acquire) ||
           (disasmQueue.empty() &&
            disasmPending.load(std::memory_order_acquire) == 0);
  });
#endif
}

unsigned int solveThread_c::findInsertIndexByMoves(unsigned int lev) const {

  unsigned int lo = 0;
  unsigned int hi = puzzle.getNumberOfSavedSolutions();

  while (lo < hi) {
    unsigned int mid = lo + (hi - lo) / 2;
    const disassembly_c * s2 = puzzle.getSavedSolution(mid)->getDisassemblyInfo();

    if (s2 && s2->sumMoves() < lev)
      hi = mid;
    else
      lo = mid + 1;
  }

  return lo;
}

unsigned int solveThread_c::findInsertIndexByRotations(unsigned int lev) const {

  unsigned int lo = 0;
  unsigned int hi = puzzle.getNumberOfSavedSolutions();

  while (lo < hi) {
    unsigned int mid = lo + (hi - lo) / 2;
    const disassembly_c * s2 = puzzle.getSavedSolution(mid)->getDisassemblyInfo();

    if (s2 && s2->sumRotations() < lev)
      hi = mid;
    else
      lo = mid + 1;
  }

  return lo;
}

void solveThread_c::processDisassembly(const disasmTask_c & task, int _solutionAction, disassembler_c * workerDisassm) {

  disasmDurationGuard_c duration(&disasmCompleted, &disasmMsTotal);

  assembly_c * a = task.assembly;

  if (a->placementCount() <= 1) {
    puzzle.addSolution(a, task.assemblyNumber);
    puzzle.incNumSolutions();
    return;
  }

  separation_c * s = workerDisassm->disassemble(a);

  if (!s) {
    delete a;
    disasmInseparable.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  if (_solutionAction != SOL_DISASM) {
    delete s;
    delete a;
    puzzle.incNumSolutions();
    return;
  }

  {
    problem_c::SolutionsLock solutionsLock(puzzle);

    bool ins = false;

    switch(sortMethod) {
      case SRT_COMPLETE_MOVES:
        {
          unsigned int lev = s->sumMoves();
          unsigned int insertPos = findInsertIndexByMoves(lev);

          if (insertPos < puzzle.getNumberOfSavedSolutions()) {
            if (parameters & PAR_DROP_DISASSEMBLIES) {
              puzzle.addSolution(a, new separationInfo_c(s), task.assemblyNumber, task.solutionNumber, insertPos);
              delete s;
            } else
              puzzle.addSolution(a, s, task.assemblyNumber, task.solutionNumber, insertPos);
            ins = true;
          }

          if (!ins) {
            if (parameters & PAR_DROP_DISASSEMBLIES) {
              puzzle.addSolution(a, new separationInfo_c(s), task.assemblyNumber, task.solutionNumber);
              delete s;
            } else
              puzzle.addSolution(a, s, task.assemblyNumber, task.solutionNumber);
          }

          if (solutionLimit && (puzzle.getNumberOfSavedSolutions() > solutionLimit))
            puzzle.removeSolution(puzzle.getNumberOfSavedSolutions()-1);
        }
        break;
      case SRT_ROTATIONS:
        {
          unsigned int lev = s->sumRotations();
          unsigned int insertPos = findInsertIndexByRotations(lev);

          if (insertPos < puzzle.getNumberOfSavedSolutions()) {
            if (parameters & PAR_DROP_DISASSEMBLIES) {
              puzzle.addSolution(a, new separationInfo_c(s), task.assemblyNumber, task.solutionNumber, insertPos);
              delete s;
            } else
              puzzle.addSolution(a, s, task.assemblyNumber, task.solutionNumber, insertPos);
            ins = true;
          }

          if (!ins) {
            if (parameters & PAR_DROP_DISASSEMBLIES) {
              puzzle.addSolution(a, new separationInfo_c(s), task.assemblyNumber, task.solutionNumber);
              delete s;
            } else
              puzzle.addSolution(a, s, task.assemblyNumber, task.solutionNumber);
          }

          if (solutionLimit && (puzzle.getNumberOfSavedSolutions() > solutionLimit))
            puzzle.removeSolution(puzzle.getNumberOfSavedSolutions()-1);
        }
        break;
      case SRT_LEVEL:
        {
          for (unsigned int i = 0; i < puzzle.getNumberOfSavedSolutions(); i++) {

            const disassembly_c * s2 = puzzle.getSavedSolution(i)->getDisassemblyInfo();

            if (s2 && (s2->compare(s) < 0)) {
              if (parameters & PAR_DROP_DISASSEMBLIES) {
                puzzle.addSolution(a, new separationInfo_c(s), task.assemblyNumber, task.solutionNumber, i);
                delete s;
              } else
                puzzle.addSolution(a, s, task.assemblyNumber, task.solutionNumber, i);
              ins = true;
              break;
            }
          }

          if (!ins)  {
            if (parameters & PAR_DROP_DISASSEMBLIES) {
              puzzle.addSolution(a, new separationInfo_c(s), task.assemblyNumber, task.solutionNumber);
              delete s;
            } else
              puzzle.addSolution(a, s, task.assemblyNumber, task.solutionNumber);
          }

          if (solutionLimit && (puzzle.getNumberOfSavedSolutions() > solutionLimit))
            puzzle.removeSolution(puzzle.getNumberOfSavedSolutions()-1);
        }
        break;
      case SRT_UNSORT:
        if (task.solutionNumber % (solutionDrop * dropMultiplicator) == 0) {
          if (parameters & PAR_DROP_DISASSEMBLIES) {
            puzzle.addSolution(a, new separationInfo_c(s), task.assemblyNumber, task.solutionNumber);
            delete s;
          } else
            puzzle.addSolution(a, s, task.assemblyNumber, task.solutionNumber);
        } else {
          delete a;
          delete s;
        }
        break;
    }
  }

  puzzle.incNumSolutions();
}

void solveThread_c::trimSavedSolutions(int _solutionAction) {

  if (!solutionLimit)
    return;

  problem_c::SolutionsLock solutionsLock(puzzle);

  if (puzzle.getNumberOfSavedSolutions() > solutionLimit) {
    unsigned int idx = (_solutionAction == SOL_SAVE_ASM) ? puzzle.getNumAssemblies()-1
                                                         : puzzle.getNumSolutions()-1;

    idx = (idx % (solutionLimit * solutionDrop * dropMultiplicator)) / (solutionDrop * dropMultiplicator);

    if (idx == solutionLimit-1)
      dropMultiplicator *= 2;

    puzzle.removeSolution(idx+1);
  }
}

bool solveThread_c::assembly(assembly_c * a) {

  std::lock_guard<std::mutex> lock(assemblyCallbackMutex);

  if (stopPressed.load(std::memory_order_acquire)) {
    delete a;
    return true;
  }

  const int _solutionAction = solutionActionFromParameters(parameters);

  switch(_solutionAction) {
  case SOL_COUNT_ASM:
    delete a;
    break;
  case SOL_SAVE_ASM:

    if (puzzle.getNumAssemblies() % (solutionDrop*dropMultiplicator) == 0)
      puzzle.addSolution(a);
    else
      delete a;

    break;

  case SOL_DISASM:
  case SOL_COUNT_DISASM:
    {
      if (a->placementCount() <= 1) {
        puzzle.addSolution(a);
        puzzle.incNumSolutions();
        break;
      }

#ifdef NO_THREADING
      disasmTask_c task;
      task.assembly = a;
      task.assemblyNumber = puzzle.getNumAssemblies();
      task.solutionNumber = puzzle.getNumSolutions();
      processDisassembly(task, _solutionAction, disassemblers[0]);
#else
      enqueueDisassembly(a);
#endif
    }
    break;
  }

  puzzle.incNumAssemblies();
  trimSavedSolutions(_solutionAction);

  return true;
}

void solveThread_c::stop(void) {

  unsigned int act = action.load(std::memory_order_relaxed);

  if ((act != ACT_ASSEMBLING) &&
      (act != ACT_REDUCE) &&
      (act != ACT_DISASSEMBLING) &&
      (act != ACT_PREPARATION)
     )
    return;

  stopPressed.store(true, std::memory_order_release);
  action.store(ACT_WAIT_TO_STOP, std::memory_order_relaxed);

  if (puzzle.getAssembler())
    puzzle.getAssembler()->stop();

  cancelDisassemblyWork();
}

bool solveThread_c::start(bool stop_after_prep) {

  stopPressed.store(false, std::memory_order_relaxed);
  return_after_prep = stop_after_prep;
  startTime = time(0);
  statsOrigin = std::chrono::steady_clock::now();

  dropMultiplicator = 1;

  unsigned int a;

  if ((parameters & (PAR_JUST_COUNT | PAR_DISASSM)) == 0) {

    if (!puzzle.numAssembliesKnown())
      a = 0;
    else
      a = puzzle.getNumAssemblies();
  } else {
    if (!puzzle.numSolutionsKnown())
      a = 0;
    else
      a = puzzle.getNumSolutions();
  }

  while (a+solutionDrop > 2 * solutionLimit * solutionDrop) {
    dropMultiplicator *= 2;
    a = (a+1) / 2;
  }

  if (parameters & PAR_DISASSM)
    startDisasmWorker();

  return thread_c::start();
}

unsigned int solveThread_c::currentActionParameter(void) {

  switch(action.load(std::memory_order_relaxed)) {
  case ACT_REDUCE:
  case ACT_PREPARATION:
    if (assm)
      return assm->getReducePiece();
    else
      return 0;

  default:
    return 0;
  }
}

namespace {

/* Seconds between creep steps while take-apart is running.
 * Classic / BurrTools 2: 1% steps on the one-left vs many-left schedule.
 * Andrew Crowell: 5% per second (speed is unknown). Halt at 95% for all types. */
double disasmCreepInterval(int pct, bool oneLeft, solverType_e type, int *step) {
  if (pct >= 95) {
    *step = 0;
    return 1e9;
  }
  if (type == SOLVER_CROWELL) {
    *step = 5;
    return 1.0;
  }
  *step = 1;
  if (oneLeft) {
    if (pct < 80) return 1.0;
    if (pct < 90) return 2.0;
    if (pct < 95) return 5.0;
  } else {
    if (pct < 80) return 3.0;
    if (pct < 90) return 5.0;
    if (pct < 95) return 15.0;
  }
  *step = 0;
  return 1e9;
}

} // namespace

float solveThread_c::getProgress(float assemblyFraction) const {

  if (assemblyFraction < 0)
    assemblyFraction = 0;
  else if (assemblyFraction > 1)
    assemblyFraction = 1;

  if (!(parameters & PAR_DISASSM))
    return assemblyFraction;

  const unsigned int pending = disasmPending.load(std::memory_order_relaxed);
  const unsigned int completed = disasmCompleted.load(std::memory_order_relaxed);
  const unsigned long assemblies = puzzle.numAssembliesKnown() ? puzzle.getNumAssemblies() : 0;

  float futureAsm = 0;
  if (assemblyFraction > 0.0001f && assemblyFraction < 0.999f && assemblies > 0)
    futureAsm = (float)assemblies * (1.0f - assemblyFraction) / assemblyFraction;

  const float disasmDone = (float)completed;
  const float disasmLeft = (float)pending + futureAsm;
  const float disasmTotal = disasmDone + disasmLeft;

  float disasmFrac;
  if (disasmTotal < 1.0f) {
    /* No take-apart work seen yet. Covering complete with zero assemblies
     * means there is nothing to disassemble. */
    disasmFrac = (assemblyFraction >= 0.999f) ? 1.0f : 0.0f;
  } else {
    disasmFrac = disasmDone / disasmTotal;
    if (disasmFrac > 1.0f)
      disasmFrac = 1.0f;
  }

  /* When workers keep up, disasmFrac tracks assemblyFraction and any mix
   * still equals covering progress. When covering finishes first, the bar
   * continues with the queue. Rotations make take-apart much slower, so
   * weight that side more. */
  const bool rotations = (parameters & PAR_CHECK_ROTATIONS) != 0;
  const float asmWeight = rotations ? 0.2f : 0.5f;
  float progress = asmWeight * assemblyFraction + (1.0f - asmWeight) * disasmFrac;

  if ((pending > 0 || assemblyFraction < 0.999f) && progress > 0.999f)
    progress = 0.999f;

  if (progress < 0)
    progress = 0;

  /* While at least one assembly is still being taken apart, creep the bar
   * forward so it does not sit frozen. Real progress always wins if it
   * jumps ahead. Cap at 95% until the solve actually finishes. */
  if (pending == 0) {
    disasmCreepActive = false;
    return progress;
  }

  const bool oneLeft = (disasmLeft <= 1.001f);
  const auto now = std::chrono::steady_clock::now();

  if (!disasmCreepActive) {
    disasmCreepActive = true;
    disasmCreepShown = progress;
    disasmCreepTick = now;
    return progress;
  }

  if (progress > disasmCreepShown) {
    disasmCreepShown = progress;
    disasmCreepTick = now;
  }

  int pct = (int)(disasmCreepShown * 100.0f + 1e-4f);
  if (pct < 0) pct = 0;
  if (pct > 95) pct = 95;

  double elapsed = std::chrono::duration<double>(now - disasmCreepTick).count();
  while (pct < 95) {
    int step = 1;
    const double iv = disasmCreepInterval(pct, oneLeft, solverType, &step);
    if (step <= 0 || elapsed + 1e-9 < iv)
      break;
    elapsed -= iv;
    pct += step;
    if (pct > 95)
      pct = 95;
  }

  disasmCreepTick = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(elapsed));
  disasmCreepShown = (float)pct / 100.0f;

  if (disasmCreepShown < progress)
    disasmCreepShown = progress;

  return disasmCreepShown;
}

solveStats_c solveThread_c::getStats(void) const {

  solveStats_c s = {};
  s.hasData = true;
  s.solverType = solverType;
  s.disassemblyEnabled = (parameters & PAR_DISASSM) != 0;
  s.rotationsEnabled = (parameters & PAR_CHECK_ROTATIONS) != 0;
  s.hardwareThreads = std::thread::hardware_concurrency();
  if (s.hardwareThreads < 1)
    s.hardwareThreads = 1;
  s.assemblerThreads = assemblerThreadCount;
  if (s.assemblerThreads < 1)
    s.assemblerThreads = 1;
  s.disasmWorkers = disasmWorkerCount.load(std::memory_order_relaxed);
  s.assembliesFound = puzzle.numAssembliesKnown() ? puzzle.getNumAssemblies() : 0;
  s.solutionsFound = puzzle.numSolutionsKnown() ? puzzle.getNumSolutions() : 0;
  s.inseparable = disasmInseparable.load(std::memory_order_relaxed);
  s.pending = disasmPending.load(std::memory_order_relaxed);
  s.peakPending = disasmPeakPending.load(std::memory_order_relaxed);
  s.disasmCompleted = disasmCompleted.load(std::memory_order_relaxed);
  s.disasmWorkMs = disasmMsTotal.load(std::memory_order_relaxed);
  s.avgDisasmSeconds = getAverageDisassemblySeconds();

  if (assm) {
    s.dlxIterations = assm->getIterations();
    s.assemblyProgress = assm->getFinished();
  }

  unsigned long long extra = 0;
  if (statsPhase != PHASE_NONE)
    extra = elapsedMs(phaseOrigin);

  s.prepareMs = prepareMs.load(std::memory_order_relaxed);
  s.reduceMs = reduceMs.load(std::memory_order_relaxed);
  s.assemblyMs = assemblyMs.load(std::memory_order_relaxed);
  s.drainMs = drainMs.load(std::memory_order_relaxed);
  if (statsPhase == PHASE_PREPARE) s.prepareMs += extra;
  else if (statsPhase == PHASE_REDUCE) s.reduceMs += extra;
  else if (statsPhase == PHASE_ASSEMBLE) s.assemblyMs += extra;
  else if (statsPhase == PHASE_DRAIN) s.drainMs += extra;

  s.elapsedMs = elapsedMs(statsOrigin);

  unsigned long long rotUs = 0;
  unsigned long long linUs = 0;
  for (unsigned int i = 0; i < disassemblers.size(); i++)
    if (disassemblers[i]) {
      rotUs += disassemblers[i]->getRotationSearchUs();
      linUs += disassemblers[i]->getLinearSearchUs();
    }
  s.rotationSearchMs = rotUs / 1000;
  s.linearSearchMs = linUs / 1000;

  unsigned int act = action.load(std::memory_order_relaxed);
  switch (act) {
    case ACT_FINISHED:
      s.status = solveStats_c::ST_FINISHED;
      break;
    case ACT_PAUSING:
      s.status = solveStats_c::ST_PAUSED;
      break;
    case ACT_ERROR:
    case ACT_ASSERT:
      s.status = solveStats_c::ST_ERROR;
      break;
    default:
      s.status = solveStats_c::ST_RUNNING;
      break;
  }

  return s;
}
