/* BurrTools
 *
 * BurrTools 2 assembly driver: first-level DLX tree-split.
 * Feature status and comparison: bt2_solver.h, bt_classic_solver.h
 */
#ifndef __BT2_ASSEMBLE_H__
#define __BT2_ASSEMBLE_H__

#include "bt2_solver.h"

class assembler_c;
class assembler_cb;

/** hardware_concurrency()-2, min 1, cap 16. 1 if threading is disabled. */
unsigned int bt2ChooseAssemblerWorkers(void);

/**
 * Search the prepared assembler with first-level DLX tree-split when possible.
 * Falls back to a single assemble() for assembler_1, NO_THREADING, clone
 * failure, or workerCount <= 1.
 * Returns the number of assembler threads that actually ran.
 */
unsigned int bt2Assemble(assembler_c * assm, assembler_cb * callback,
                         unsigned int workerCount);

#endif
