#include "debugstatspanel.h"

#include "../lib/solvertype.h"

#include <FL/Fl.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

const int kTitleSize = 18;
const int kBodySize = 16;
const int kTabStop = 40;

void formatDuration(char *buf, size_t len, unsigned long long ms) {
  if (ms < 1000) {
    snprintf(buf, len, "%llu ms", (unsigned long long)ms);
    return;
  }
  double s = (double)ms / 1000.0;
  if (s < 60)
    snprintf(buf, len, "%.1f s", s);
  else if (s < 3600)
    snprintf(buf, len, "%.1f min", s / 60.0);
  else
    snprintf(buf, len, "%.1f h", s / 3600.0);
}

const char *statusTitle(solveStats_c::Status st) {
  switch (st) {
    case solveStats_c::ST_RUNNING: return "Running";
    case solveStats_c::ST_PAUSED: return "Paused";
    case solveStats_c::ST_FINISHED: return "Finished";
    case solveStats_c::ST_ERROR: return "Error";
    default: return "Idle";
  }
}

double pct(unsigned long long part, unsigned long long total) {
  if (total == 0)
    return 0;
  return 100.0 * (double)part / (double)total;
}

void appendBullet(std::string &out, const std::string &line) {
  if (!out.empty())
    out += "\n";
  out += "•  ";
  out += line;
}

std::string formatPct(unsigned long long part, unsigned long long total) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.0f%%", pct(part, total));
  return buf;
}

std::string formatDur(unsigned long long ms) {
  char buf[64];
  formatDuration(buf, sizeof(buf), ms);
  return buf;
}

void appendKv(std::string &out, const char *label, const char *value, bool indent) {
  if (indent)
    out += "      ";
  out += label;
  out += "\t";
  out += value ? value : "";
  out += "\n";
}

void appendHeader(std::string &out, const char *title) {
  out += "\n";
  out += title;
  out += "\n";
}

std::string buildAnalysis(const solveStats_c & stats) {
  std::string note;

  if (stats.status == solveStats_c::ST_RUNNING)
    appendBullet(note, "The solver is still running. Figures below update while the search is in progress.");
  else if (stats.status == solveStats_c::ST_PAUSED)
    appendBullet(note, "The solver was paused. These figures are from the paused run.");
  else if (stats.status == solveStats_c::ST_FINISHED)
    appendBullet(note, "The solver finished. These figures are from the completed run.");
  else if (stats.status == solveStats_c::ST_ERROR)
    appendBullet(note, "The solver stopped with an error. Partial figures from that run are shown.");

  const unsigned long long wall = stats.elapsedMs ? stats.elapsedMs : 1;
  const unsigned long long phaseSum = stats.prepareMs + stats.reduceMs + stats.assemblyMs + stats.drainMs;

  {
    std::string line = "Share of elapsed wall time (" + formatDur(stats.elapsedMs) + "): ";
    line += "prepare " + formatPct(stats.prepareMs, wall) + " (" + formatDur(stats.prepareMs) + ")";
    line += ", reduce / optimize " + formatPct(stats.reduceMs, wall) + " (" + formatDur(stats.reduceMs) + ")";
    line += ", assembly search " + formatPct(stats.assemblyMs, wall) + " (" + formatDur(stats.assemblyMs) + ")";
    if (stats.disassemblyEnabled)
      line += ", queue drain after assembly " + formatPct(stats.drainMs, wall) + " (" + formatDur(stats.drainMs) + ")";
    if (phaseSum + 50 < stats.elapsedMs && stats.elapsedMs > 0)
      line += ". Other overhead is the remaining wall time.";
    appendBullet(note, line);
  }

  {
    std::string line = "DLX iterations: ";
    char nbuf[64];
    snprintf(nbuf, sizeof(nbuf), "%lu", stats.dlxIterations);
    line += nbuf;
    line += ". Each iteration is one step of Knuth’s dancing-links covering search: it tries to place or backtrack a piece in the assembly. ";
    if (stats.assembliesFound == 0 && stats.dlxIterations > 0) {
      line += "Iterations are happening but no assembly has been found yet, so time is still in the covering search (or in prepare/reduce).";
    } else if (stats.assemblyMs >= stats.prepareMs && stats.assemblyMs >= stats.reduceMs &&
               stats.assemblyMs >= stats.drainMs &&
               (!stats.disassemblyEnabled || stats.peakPending <= (stats.disasmWorkers ? stats.disasmWorkers : 1) + 1)) {
      line += "Assembly search used a large share of wall time while the take-apart queue stayed small, so DLX was a bottleneck.";
    } else if (stats.dlxIterations > 0 && stats.assembliesFound > 0 &&
               stats.dlxIterations / (stats.assembliesFound ? stats.assembliesFound : 1) > 10000) {
      line += "Many iterations were needed per assembly found, so the covering tree is bushy; DLX work is significant even if it was not the only limiter.";
    } else {
      line += "DLX does not look like the only limiter; compare the time shares above and the disassembly notes below.";
    }
    appendBullet(note, line);
  }

  unsigned int workers = stats.disasmWorkers ? stats.disasmWorkers : 1;
  bool queueIdle = stats.disassemblyEnabled && stats.peakPending <= workers + 1;
  bool queueBacked = stats.disassemblyEnabled &&
      (stats.peakPending > workers * 4 || stats.pending > workers * 2 ||
       stats.drainMs > stats.assemblyMs / 2);

  if (!stats.disassemblyEnabled) {
    appendBullet(note, "Disassembly was off, so only assembly (DLX) search was measured. Take-apart time is not part of this run.");
  } else {
    std::string line = "Disassembly CPU time (sum of workers) was ";
    line += formatDur(stats.disasmWorkMs);
    line += " across ";
    char wbuf[32];
    snprintf(wbuf, sizeof(wbuf), "%u", workers);
    line += wbuf;
    line += workers == 1 ? " worker." : " workers.";

    unsigned long long moveSearch = stats.linearSearchMs + stats.rotationSearchMs;
    if (moveSearch > 0) {
      line += " Of move search, linear slides were ";
      line += formatPct(stats.linearSearchMs, moveSearch);
      line += " (" + formatDur(stats.linearSearchMs) + ")";
      if (stats.rotationsEnabled) {
        line += " and 90° rotations were ";
        line += formatPct(stats.rotationSearchMs, moveSearch);
        line += " (" + formatDur(stats.rotationSearchMs) + ").";
      } else {
        line += ". Check Rotations was off, so rotation search was not used.";
      }
    } else if (stats.rotationsEnabled) {
      line += " Linear and rotation move-search timers had not accumulated yet (or the run was too short).";
    }
    appendBullet(note, line);

    if (queueBacked) {
      appendBullet(note, "Bottleneck: take-apart search lagged behind assembly (the queue peaked or drain after assembly was large). DLX was waiting on disassembly rather than the other way around.");
    } else if (queueIdle && stats.assembliesFound > 0) {
      appendBullet(note, "Bottleneck: disassembly workers stayed idle most of the time, so assembly search (DLX) limited the run.");
    } else if (stats.assembliesFound == 0) {
      appendBullet(note, "Bottleneck: no assemblies found yet, so time is still in preparation, reduction, or covering search.");
    } else {
      appendBullet(note, "Bottleneck: assembly and disassembly shared the run. The larger of “Time finding assemblies” and “Disassembly work time” is the main cost.");
    }

    if (stats.rotationsEnabled && stats.rotationSearchMs > stats.linearSearchMs &&
        stats.rotationSearchMs * 2 > stats.linearSearchMs + stats.rotationSearchMs) {
      appendBullet(note, "Rotation move search used more time than sliding moves. That is expected with Check Rotations, and it is a likely place for future speedups.");
    }
  }

  if (stats.assembliesFound > 0 && stats.solutionsFound * 5 < stats.assembliesFound &&
      stats.inseparable > 0) {
    appendBullet(note, "Many assemblies could not be taken apart (valid solutions are much fewer than assemblies considered), so disassembly work was spent rejecting inseparable placements.");
  }

  appendBullet(note, [&]() {
    std::string line = "Worth considering for future performance work: ";
    std::vector<std::string> items;
    if (!stats.disassemblyEnabled || (queueIdle && stats.assembliesFound > 0) ||
        (stats.assemblyMs >= stats.drainMs && stats.assemblyMs >= stats.disasmWorkMs / (workers ? workers : 1))) {
      items.push_back("parallel assembly / tree-split of the DLX covering search, because assembly time was a large share");
    }
    if (stats.disassemblyEnabled && queueBacked) {
      items.push_back("faster take-apart search or more disassembly workers (Check Rotations currently keeps a single worker)");
    }
    if (stats.rotationsEnabled && stats.rotationSearchMs >= stats.linearSearchMs) {
      items.push_back("speeding up 90° rotation move generation, which dominated move search");
    }
    if (stats.reduceMs > stats.assemblyMs && stats.reduceMs > stats.elapsedMs / 5) {
      items.push_back("the reduce / optimize phase, which used a large share of this run");
    }
    if (items.empty()) {
      items.push_back("profile the larger of assembly search and disassembly; that is the first place extra CPU would help");
    }
    for (size_t i = 0; i < items.size(); i++) {
      if (i)
        line += "; ";
      line += items[i];
    }
    line += ".";
    return line;
  }());

  return note;
}

void appendIdleRows(std::string &out) {
  appendKv(out, "Solver:", "Idle", false);
  appendKv(out, "Solver Type:", "—", false);

  appendHeader(out, "Assembly");
  appendKv(out, "Assemblies considered:", "—", true);
  appendKv(out, "Valid (disassemblable):", "—", true);
  appendKv(out, "Inseparable assemblies:", "—", true);
  appendKv(out, "DLX iterations:", "—", true);
  appendKv(out, "Assembly search progress:", "—", true);
  appendKv(out, "Time finding assemblies:", "—", true);
  appendKv(out, "Prepare time:", "—", true);
  appendKv(out, "Reduce / optimize time:", "—", true);
  appendKv(out, "Assembler threads:", "—", true);

  appendHeader(out, "Disassembly");
  appendKv(out, "Host CPU threads:", "—", true);
  appendKv(out, "Disassembly workers:", "—", true);
  appendKv(out, "Queue pending:", "—", true);
  appendKv(out, "Peak queue pending:", "—", true);
  appendKv(out, "Disassemblies completed:", "—", true);
  appendKv(out, "Average time per disassembly:", "—", true);
  appendKv(out, "Disassembly work time (sum):", "—", true);
  appendKv(out, "Linear move search time:", "—", true);
  appendKv(out, "Rotation move search time:", "—", true);
  appendKv(out, "Queue drain after assembly:", "—", true);
  appendKv(out, "Elapsed wall time:", "—", true);

  appendHeader(out, "Analysis");
  out += "•  No solve has been run yet. Start a search on the Solver tab; this pane keeps the latest run after it finishes or is paused.\n";
}

std::string formatReport(const solveStats_c & stats) {
  std::string out;
  char buf[128];

  if (!stats.hasData) {
    appendIdleRows(out);
    return out;
  }

  appendKv(out, "Solver:", statusTitle(stats.status), false);
  appendKv(out, "Solver Type:", solverTypeLabel(stats.solverType), false);

  appendHeader(out, "Assembly");
  snprintf(buf, sizeof(buf), "%lu", stats.assembliesFound);
  appendKv(out, "Assemblies considered:", buf, true);
  snprintf(buf, sizeof(buf), "%lu", stats.solutionsFound);
  appendKv(out, "Valid (disassemblable):", buf, true);
  snprintf(buf, sizeof(buf), "%lu", stats.inseparable);
  appendKv(out, "Inseparable assemblies:", buf, true);
  snprintf(buf, sizeof(buf), "%lu", stats.dlxIterations);
  appendKv(out, "DLX iterations:", buf, true);
  if (stats.assemblyProgress >= 0)
    snprintf(buf, sizeof(buf), "%.1f %%", stats.assemblyProgress * 100.0f);
  else
    snprintf(buf, sizeof(buf), "unknown");
  appendKv(out, "Assembly search progress:", buf, true);
  formatDuration(buf, sizeof(buf), stats.assemblyMs);
  appendKv(out, "Time finding assemblies:", buf, true);
  formatDuration(buf, sizeof(buf), stats.prepareMs);
  appendKv(out, "Prepare time:", buf, true);
  formatDuration(buf, sizeof(buf), stats.reduceMs);
  appendKv(out, "Reduce / optimize time:", buf, true);
  snprintf(buf, sizeof(buf), "%u", stats.assemblerThreads);
  appendKv(out, "Assembler threads:", buf, true);

  appendHeader(out, "Disassembly");
  snprintf(buf, sizeof(buf), "%u", stats.hardwareThreads);
  appendKv(out, "Host CPU threads:", buf, true);

  if (!stats.disassemblyEnabled) {
    appendKv(out, "Disassembly workers:", "off", true);
    appendKv(out, "Queue pending:", "—", true);
    appendKv(out, "Peak queue pending:", "—", true);
    appendKv(out, "Disassemblies completed:", "—", true);
    appendKv(out, "Average time per disassembly:", "—", true);
    appendKv(out, "Disassembly work time (sum):", "—", true);
    appendKv(out, "Linear move search time:", "—", true);
    appendKv(out, "Rotation move search time:", "off", true);
    appendKv(out, "Queue drain after assembly:", "—", true);
  } else {
    snprintf(buf, sizeof(buf), "%u", stats.disasmWorkers);
    appendKv(out, "Disassembly workers:", buf, true);
    snprintf(buf, sizeof(buf), "%u", stats.pending);
    appendKv(out, "Queue pending:", buf, true);
    snprintf(buf, sizeof(buf), "%u", stats.peakPending);
    appendKv(out, "Peak queue pending:", buf, true);
    snprintf(buf, sizeof(buf), "%u", stats.disasmCompleted);
    appendKv(out, "Disassemblies completed:", buf, true);
    snprintf(buf, sizeof(buf), "%.3f s", stats.avgDisasmSeconds);
    appendKv(out, "Average time per disassembly:", buf, true);
    formatDuration(buf, sizeof(buf), stats.disasmWorkMs);
    appendKv(out, "Disassembly work time (sum):", buf, true);
    formatDuration(buf, sizeof(buf), stats.linearSearchMs);
    appendKv(out, "Linear move search time:", buf, true);
    if (stats.rotationsEnabled) {
      formatDuration(buf, sizeof(buf), stats.rotationSearchMs);
      appendKv(out, "Rotation move search time:", buf, true);
    } else {
      appendKv(out, "Rotation move search time:", "off", true);
    }
    formatDuration(buf, sizeof(buf), stats.drainMs);
    appendKv(out, "Queue drain after assembly:", buf, true);
  }

  formatDuration(buf, sizeof(buf), stats.elapsedMs);
  appendKv(out, "Elapsed wall time:", buf, true);

  appendHeader(out, "Analysis");
  out += buildAnalysis(stats);
  if (out.empty() || out[out.size() - 1] != '\n')
    out += "\n";
  return out;
}

} // namespace

class debugStatsBody_c : public Fl_Text_Display, public layoutable_c {

  Fl_Text_Buffer *buf;

public:

  debugStatsBody_c(int x, int y)
    : Fl_Text_Display(0, 0, 100, 100), layoutable_c(x, y, 1, 1),
      buf(new Fl_Text_Buffer()) {
    buffer(buf);
    buf->tab_distance(kTabStop);
    wrap_mode(WRAP_AT_BOUNDS, 0);
    scrollbar_align(FL_ALIGN_RIGHT);
    box(FL_DOWN_BOX);
    textfont(FL_HELVETICA);
    textsize(kBodySize);
    color(FL_BACKGROUND_COLOR);
    textcolor(FL_FOREGROUND_COLOR);
    cursor_color(FL_BACKGROUND_COLOR);
    hide_cursor();
    weight(1, 1);
    setShrinkMinSize(80, 40);
  }

  ~debugStatsBody_c() {
    buffer(0);
    delete buf;
  }

  Fl_Text_Buffer *textBuffer() { return buf; }

  void setText(const char *s) {
    int row = scroll_row();
    int col = scroll_col();
    buf->text(s ? s : "");
    scroll(row, col);
  }

  virtual void getMinSize(int *width, int *height) const {
    *width = 80;
    *height = 60;
  }
};

debugStatsPanel_c::debugStatsPanel_c(int x, int y, int w, int h)
  : layouter_c(x, y, w, h), freezeLive(false), idleShown(false)
{
  pitch(16);
  clip_children(1);
  clear_visible_focus();

  pageTitle = new LFl_Box("Solver Debug Statistics", 0, 0, 1, 1);
  pageTitle->labelfont(FL_HELVETICA_BOLD);
  pageTitle->labelsize(kTitleSize);
  pageTitle->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  pageTitle->setMinimumSize(0, 32);
  pageTitle->weight(1, 0);

  body = new debugStatsBody_c(0, 1);

  setShrinkMinSize(120, 0);

  solveStats_c empty = {};
  showStats(empty);
}

void debugStatsPanel_c::takeFocus(void) {
  if (body)
    Fl::focus(body);
}

void debugStatsPanel_c::showStats(const solveStats_c & stats) {

  if (!stats.hasData) {
    if (idleShown)
      return;
    idleShown = true;
    freezeLive = false;
  } else {
    idleShown = false;
    if (freezeLive && stats.status != solveStats_c::ST_RUNNING)
      return;
  }

  std::string text = formatReport(stats);
  if (text == lastText)
    return;

  /* Keep a live highlight so copy still works while numbers are updating. */
  if (body && body->textBuffer() && body->textBuffer()->selected())
    return;

  lastText = text;
  if (body)
    body->setText(text.c_str());

  if (stats.hasData)
    freezeLive = (stats.status != solveStats_c::ST_RUNNING);
}
