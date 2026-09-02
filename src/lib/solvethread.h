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
#ifndef __SOLVETHREAD_H__
#define __SOLVETHREAD_H__

#include "assembler.h"
#include "disassembler.h"
#include "bt_assert.h"
#include "thread.h"
#include "solvertype.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <time.h>
#include <vector>

class problem_c;
class assembly_c;
class separation_c;

struct disasmTask_c {
  assembly_c * assembly;
  unsigned long assemblyNumber;
  unsigned long solutionNumber;
};

/** Snapshot of solver timing and counts for the Debug statistics pane. */
struct solveStats_c {

  enum Status {
    ST_IDLE,
    ST_RUNNING,
    ST_PAUSED,
    ST_FINISHED,
    ST_ERROR
  };

  bool hasData;
  Status status;
  solverType_e solverType;
  bool disassemblyEnabled;
  bool rotationsEnabled;
  unsigned int hardwareThreads;
  unsigned int assemblerThreads;
  unsigned int disasmWorkers;
  unsigned long assembliesFound;
  unsigned long solutionsFound;
  unsigned long inseparable;
  unsigned long dlxIterations;
  float assemblyProgress;
  unsigned int pending;
  unsigned int peakPending;
  unsigned int disasmCompleted;
  unsigned long long elapsedMs;
  unsigned long long prepareMs;
  unsigned long long reduceMs;
  unsigned long long assemblyMs;
  unsigned long long disasmWorkMs;
  unsigned long long linearSearchMs;
  unsigned long long rotationSearchMs;
  unsigned long long drainMs;
  float avgDisasmSeconds;
};

class solveThread_c : public assembler_cb, public thread_c {

  public:

    enum {
      ACT_PREPARATION,
      ACT_REDUCE,
      ACT_ASSEMBLING,
      ACT_DISASSEMBLING,
      ACT_PAUSING,
      ACT_FINISHED,
      ACT_ERROR,
      ACT_ASSERT,
      ACT_WAIT_TO_STOP
    };

  private:
    /* what is currently happening in the assembler thread */
    std::atomic<unsigned int> action;

  public:
    /* return the current activity */
    unsigned int currentAction(void) { return action.load(std::memory_order_relaxed); }

    /* some activities might have a parameter, return that */
    unsigned int currentActionParameter(void);

    /** block until the solver thread has finished (success, pause, or error) */
    void waitUntilFinished(void);

  private:

    assembler_c::errState errState;
    int errParam;

  public:

    assembler_c::errState getErrorState(void) {
      bt_assert(action.load(std::memory_order_relaxed) == ACT_ERROR);
      return errState;
    }
    int getErrorParam(void) {
      bt_assert(action.load(std::memory_order_relaxed) == ACT_ERROR);
      return errParam;
    }

  private:

    time_t startTime;

  public:

    /* how much time has passed since calling start */
    unsigned long getTime(void) { return time(0) - startTime; }

    bool disassemblyEnabled(void) const { return (parameters & PAR_DISASSM) != 0; }
    unsigned int getDisassemblyPending(void) const { return disasmPending.load(std::memory_order_relaxed); }
    unsigned int getDisassemblyCompleted(void) const { return disasmCompleted.load(std::memory_order_relaxed); }
    unsigned int getDisassemblyWorkerCount(void) const { return disasmWorkerCount.load(std::memory_order_relaxed); }
    float getAverageDisassemblySeconds(void) const {
      unsigned int n = disasmCompleted.load(std::memory_order_relaxed);
      if (n == 0)
        return 0;
      return (float)disasmMsTotal.load(std::memory_order_relaxed) / 1000.0f / (float)n;
    }

    /**
     * Overall solve progress in [0, 1].
     * assemblyFraction is the covering-search (DLX) fraction from the assembler.
     * When disassembly is enabled this also includes take-apart work so the
     * value does not jump to 100% while the disassembly queue is still draining.
     * While a take-apart is running, the value also creeps forward on a
     * time schedule (capped at 95%) so the bar keeps moving.
     */
    float getProgress(float assemblyFraction) const;

    solveStats_c getStats(void) const;

  private:

    problem_c & puzzle;
    int parameters;

  public:

    static const int PAR_REDUCE =             0x01;  // do a reduction after preparation
    static const int PAR_KEEP_MIRROR =        0x02;  // keep mirror solutions
    static const int PAR_KEEP_ROTATIONS =     0x04;  // keep rotated solutions
    static const int PAR_DROP_DISASSEMBLIES = 0x08;  // remove disassembly instructions after analysis
    static const int PAR_DISASSM =            0x10;  // do the disassembly analysis
    static const int PAR_JUST_COUNT =         0x20;  // just count the solutions, don't save them
    static const int PAR_COMPLETE_ROTATIONS = 0x40;  // do a thorough rotation check
    static const int PAR_CHECK_ROTATIONS =    0x80;  // try 90° piece rotations during disassembly

    // create all the necessary data structures to start the thread later on
    solveThread_c(problem_c & puz, int par);
    const problem_c & getProblem(void) const { return puzzle; }

  private:

    int sortMethod;
    solverType_e solverType;

  public:

    enum {
      SRT_UNSORT,
      SRT_COMPLETE_MOVES,
      SRT_LEVEL,
      SRT_ROTATIONS
    };

    void setSortMethod(int sort) { sortMethod = sort; }

    void setSolverType(solverType_e type) { solverType = type; }
    solverType_e getSolverType(void) const { return solverType; }

  private:

    /* don't save more than this number of solutions 0 means no limit */
    unsigned int solutionLimit;

    /* save only every x-th solution, the others are dropped */
    unsigned int solutionDrop;

    /* this is used to increase the drop with time, when the limit is reached
     * and only every 2nd valid solution is taken
     */
    unsigned int dropMultiplicator;

  public:

    void setSolutionLimits(unsigned int limit, unsigned int drop = 1) {
      solutionLimit = limit;
      solutionDrop = drop;
    }

  private:

    assert_exception ae;

  public:

    const assert_exception & getAssertException(void) {
      return ae;
    }

  private:

    std::atomic<bool> stopPressed;
    bool return_after_prep;  // sometimes it is useful to only prepare and return,
                             // if this flag is set, the program will return

    std::vector<disassembler_c *> disassemblers;
    assembler_c * assm;
    unsigned int assemblerThreadCount;

    std::mutex assemblyCallbackMutex;

    /* asynchronous disassembly pipeline (when PAR_DISASSM is set) */
    std::vector<std::thread> disasmWorkers;
    std::mutex disasmQueueMutex;
    std::condition_variable disasmQueueCv;
    std::queue<disasmTask_c> disasmQueue;
    std::atomic<bool> disasmWorkerStop;
    std::atomic<unsigned int> disasmWorkerCount;
    std::atomic<unsigned int> disasmPending;
    std::atomic<unsigned int> disasmCompleted;
    std::atomic<unsigned long long> disasmMsTotal;
    std::atomic<unsigned int> disasmPeakPending;
    std::atomic<unsigned long> disasmInseparable;
    std::atomic<unsigned long long> prepareMs;
    std::atomic<unsigned long long> reduceMs;
    std::atomic<unsigned long long> assemblyMs;
    std::atomic<unsigned long long> drainMs;
    std::chrono::steady_clock::time_point statsOrigin;
    std::chrono::steady_clock::time_point phaseOrigin;
    enum { PHASE_NONE, PHASE_PREPARE, PHASE_REDUCE, PHASE_ASSEMBLE, PHASE_DRAIN } statsPhase;

    /* GUI-thread-only state for the disassembly progress-bar creep. */
    mutable bool disasmCreepActive;
    mutable float disasmCreepShown;
    mutable std::chrono::steady_clock::time_point disasmCreepTick;

    void startDisasmWorker(void);
    void stopDisasmWorker(void);
    void cancelDisassemblyWork(void);
    void disasmWorkerRun(disassembler_c * workerDisassm);
    void enqueueDisassembly(assembly_c * a);
    void flushDisassemblyQueue(void);
    void processDisassembly(const disasmTask_c & task, int solutionAction, disassembler_c * workerDisassm);
    unsigned int findInsertIndexByMoves(unsigned int lev) const;
    unsigned int findInsertIndexByRotations(unsigned int lev) const;
    void trimSavedSolutions(int solutionAction);

public:

  // stop and exit
  virtual ~solveThread_c(void);

private:

  // the call-back
  bool assembly(assembly_c* a);

public:

  // let the thread start
  // returns true, if everything went well, false otherwise
  bool start(bool stop_after_prep = false);

  // try to stop the thread at the next possible position
  void stop(void);

  bool stopped(void) const {
    unsigned int act = action.load(std::memory_order_relaxed);
    return ((act == ACT_PAUSING) ||
            (act == ACT_FINISHED) ||
            (act == ACT_ERROR)
           );
  }

  void run(void);

private:

  // no copying and assigning
  solveThread_c(const solveThread_c&);
  void operator=(const solveThread_c&);
};

#endif
