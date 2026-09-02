/* BurrTools
 *
 * AI / maintainer notes for the BurrTools 2 solver (SOLVER_BT2).
 *
 * Read this file before changing assembler_bt2.*, bt2_dancingcells.*,
 * bt2_assemble.*, or SOLVER_BT2 wiring. Classic take-apart
 * (disassembler_0_c, rotationMoves_0_c) must stay a complete search.
 * Classic assembly (assembler_0_c / assembler_1_c) must stay untouched.
 * Andrew Crowell heuristics stay in crowell_solver.h.
 *
 * Comparison of BurrTools Classic vs Andrew Crowell vs BurrTools 2:
 * bt_classic_solver.h
 *
 * Reference: ../burrtoolsng_andreas/
 *   DancingCellSolver (Knuth Algorithm F/C), MCCSolver::split(),
 *   MultiThreadedSolver work stealing. No SQLite / MCCTree persist here.
 *
 * ---------------------------------------------------------------------------
 * HOW THE ENGINE IS SELECTED
 * ---------------------------------------------------------------------------
 *
 * GUI: Solver tab → "Solver Type" → BurrTools 2.
 * CLI: ./build/burrTxt -dRr --solver "BurrTools 2" <file>
 *      aliases: bt2, burrtools2
 *
 * Take-apart: createDisassembler(..., SOLVER_BT2) → disassembler_0_c
 *             (same class as Classic; movementAnalysator uses rotationMoves_0_c).
 * Assembly:   gridType_c::findAssembler(..., SOLVER_BT2) → assembler_bt2_c
 *             (own files). Range / multi-copy puzzles fall back to assembler_1_c
 *             serial, same as Classic.
 *
 * ---------------------------------------------------------------------------
 * FILE SPLIT (do not share search code with Classic / Crowell)
 * ---------------------------------------------------------------------------
 *
 * assembler_0_c / assembler_1_c   Classic + Crowell DLX only
 * assembler_bt2_c                 BT2 matrix prepare/reduce (copied from
 *                                 assembler_0) + dancing-cells search
 * bt2_dancingcells.*              Knuth dancing cells + split()
 * bt2_assemble.cpp                worker peel + time-slice + steal
 *
 * ---------------------------------------------------------------------------
 * FEATURE STATUS
 * ---------------------------------------------------------------------------
 *
 * === 1. Isolate BT2 from Classic take-apart =================================
 * IMPLEMENTED. Factory returns disassembler_0_c. Do not add tree-split inside
 * disassembler_0_c.
 *
 * === 2. Isolate BT2 assembly from Classic assembler_0_c =====================
 * IMPLEMENTED. Classic no longer has clonePrepared / root-branch filter.
 * BT2 must not edit assembler_0.cpp.
 *
 * === 3. Dancing cells search (Knuth Algorithm C / F subset) =================
 * IMPLEMENTED in bt2_dancingcells.cpp. After reduce, assembler_bt2_c converts
 * the remaining DLX matrix to SET/ITEM arrays and searches with swap-to-end
 * hide (cache-friendly vs linked-list DLX). Forced SIZE==1 items are
 * fast-tracked. Variable voxels are secondary items.
 *
 * === 4. MCC split at arbitrary depth =======================================
 * IMPLEMENTED as bt2Cells_c::split() / assembler_bt2_c::splitSearch().
 * State-3: clone keeps the current selected option, source skips it
 * (forwardToNextBranch). State-0: peel the first MRV option onto the clone.
 *
 * === 5. Time-sliced solve and split-largest-leftover =======================
 * IMPLEMENTED in bt2_assemble.cpp. Fill the pool by peeling, then run
 * iteration slices in parallel. Idle workers steal from remainingSearchWork().
 *
 * === 6. MCCTree persist / restore / SQLite =================================
 * NOT IMPLEMENTED (would need a new save format and a database). In-session
 * pause uses abort; XML resume of a split tree is not supported. Treat Stop
 * during BT2 assembly as abort.
 *
 * === 7. assembler_1_c (ranges, multi-copies) ===============================
 * NOT IMPLEMENTED. findAssembler falls back to serial assembler_1_c.
 *
 * === 8. Write-queue batching ===============================================
 * NOT IMPLEMENTED. Workers still callback assembler_cb::assembly() with the
 * existing mutex in solveThread / burrTxt.
 *
 * === 9. Same take-apart as Classic =========================================
 * IMPLEMENTED. Rotation-heavy puzzles with few assemblies still spend almost
 * all time in take-apart; dancing cells will not change that wall time.
 *
 * ---------------------------------------------------------------------------
 * FILES TO EDIT WHEN EXTENDING BT2
 * ---------------------------------------------------------------------------
 *
 * assembler_bt2.cpp / .h    matrix + cells conversion + assembler_c API
 * bt2_dancingcells.cpp / .h search, split, progress
 * bt2_assemble.cpp          workers, slices, steal
 * gridtype.cpp              SOLVER_BT2 → assembler_bt2_c
 * solvethread.cpp           SOLVER_BT2 → bt2Assemble
 * burrTxt.cpp               --solver "BurrTools 2"
 *
 * Do not put dancing cells or split into assembler_0_c.
 */
#ifndef __BT2_SOLVER_H__
#define __BT2_SOLVER_H__

#endif
