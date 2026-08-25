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
#include "solution.h"
#include "voxel.h"

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
      assm = puzzle.getPuzzle().getGridType()->findAssembler(puzzle);

      errState = assm->createMatrix(parameters & PAR_KEEP_MIRROR, parameters & PAR_KEEP_ROTATIONS, parameters & PAR_COMPLETE_ROTATIONS);
      if (errState != assembler_c::ERR_NONE) {

        errParam = assm->getErrorsParam();

        action.store(ACT_ERROR, std::memory_order_relaxed);

        delete assm;
        assm = 0;
        return;
      }

      if (parameters & PAR_REDUCE) {

        if (!stopPressed.load(std::memory_order_relaxed))
          action.store(ACT_REDUCE, std::memory_order_relaxed);

        assm->reduce();
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
      assm->assemble(this);
      flushDisassemblyQueue();
      puzzle.addTime(time(0)-startTime);

      if (assm->getFinished() >= 1) {
        action.store(ACT_FINISHED, std::memory_order_relaxed);
        puzzle.finishedSolving();
      } else
        action.store(ACT_PAUSING, std::memory_order_relaxed);

    } else {
      flushDisassemblyQueue();
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
solutionLimit(10),
solutionDrop(1),
stopPressed(false),
return_after_prep(false),
disassm(0),
assm(0),
disasmWorkerStop(false),
disasmPending(0)
{

  if (par & PAR_DISASSM)
    disassm = new disassembler_0_c(puz, (par & PAR_CHECK_ROTATIONS) != 0);

  /* Persist solutions under <solutionsWithRotations> so older BurrTools skip them */
  if (par & PAR_CHECK_ROTATIONS)
    puzzle.setSolutionsWithRotations(true);
}

solveThread_c::~solveThread_c(void) {

  stop();
  joinThread();
  stopDisasmWorker();

  if (disassm) {
    delete disassm;
    disassm = 0;
  }
}

void solveThread_c::waitUntilFinished(void) {
  joinThread();
}

void solveThread_c::startDisasmWorker(void) {

#ifndef NO_THREADING
  if (!(parameters & PAR_DISASSM))
    return;

  disasmWorkerStop.store(false, std::memory_order_relaxed);
  disasmWorker = std::thread([this]() { this->disasmWorkerRun(); });
#endif
}

void solveThread_c::stopDisasmWorker(void) {

  if (!(parameters & PAR_DISASSM))
    return;

  disasmWorkerStop.store(true, std::memory_order_release);
  disasmQueueCv.notify_all();

#ifndef NO_THREADING
  if (disasmWorker.joinable())
    disasmWorker.join();
#endif

  std::lock_guard<std::mutex> lock(disasmQueueMutex);
  while (!disasmQueue.empty()) {
    delete disasmQueue.front().assembly;
    disasmQueue.pop();
  }
  disasmPending.store(0, std::memory_order_relaxed);
}

void solveThread_c::disasmWorkerRun(void) {

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

    processDisassembly(task, solutionAction);

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
    return disasmQueue.empty() && disasmPending.load(std::memory_order_acquire) == 0;
  });
#endif
}

unsigned int solveThread_c::findInsertIndexByMoves(unsigned int lev) const {

  unsigned int lo = 0;
  unsigned int hi = puzzle.getNumberOfSavedSolutions();

  while (lo < hi) {
    unsigned int mid = lo + (hi - lo) / 2;
    const disassembly_c * s2 = puzzle.getSavedSolution(mid)->getDisassembly();

    if (s2 && s2->sumMoves() > lev)
      hi = mid;
    else
      lo = mid + 1;
  }

  return lo;
}

void solveThread_c::processDisassembly(const disasmTask_c & task, int _solutionAction) {

  assembly_c * a = task.assembly;

  if (a->placementCount() <= 1) {
    puzzle.addSolution(a, task.assemblyNumber);
    puzzle.incNumSolutions();
    return;
  }

  separation_c * s = disassm->disassemble(a);

  if (!s) {
    delete a;
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
            puzzle.removeSolution(0);
        }
        break;
      case SRT_LEVEL:
        {
          for (unsigned int i = 0; i < puzzle.getNumberOfSavedSolutions(); i++) {

            const disassembly_c * s2 = puzzle.getSavedSolution(i)->getDisassemblyInfo();

            if (s2 && (s2->compare(s) > 0)) {
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
            puzzle.removeSolution(0);
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
      processDisassembly(task, _solutionAction);
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

  action.store(ACT_WAIT_TO_STOP, std::memory_order_relaxed);

  if (puzzle.getAssembler())
    puzzle.getAssembler()->stop();

  stopPressed.store(true, std::memory_order_release);
}

bool solveThread_c::start(bool stop_after_prep) {

  stopPressed.store(false, std::memory_order_relaxed);
  return_after_prep = stop_after_prep;
  startTime = time(0);

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
