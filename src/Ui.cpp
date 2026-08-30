// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#include "Ui.h"

#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "Config.h"
#include "Display.h"
#include "Pack.h"
#include "Platform.h"
#include "RunTask.h"
#include "Runner.h"      // base64_encode_int, for showing values as IRCIS writes them
#include "Program.h"
#include "Sinks.h"
#include "Store.h"
#include "Theme.h"
#include "WebView.h"

namespace ui {
namespace {

// ---------------------------------------------------------------------------
// small widget helpers
// ---------------------------------------------------------------------------

struct Btn {
  int x, y, w, h;
  const char* label;
  uint16_t fg = theme::text;
  uint16_t bg = theme::panel;
};

bool hit(const Btn& b, int x, int y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

void drawBtn(const Btn& b, bool on = false, bool enabled = true) {
  // A disabled button loses its fill as well as its text colour, so a green
  // "LOAD+RUN" does not still read as available when there is nothing to load.
  uint16_t bg = !enabled ? theme::panel : (on ? b.fg : b.bg);
  uint16_t fg = !enabled ? theme::dim : (on ? theme::bg : b.fg);
  gfx.fillRoundRect(b.x, b.y, b.w, b.h, 3, bg);
  gfx.drawRoundRect(b.x, b.y, b.w, b.h, 3, enabled ? theme::line : theme::panel);
  gfx.setFont(&fonts::Font0);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.setTextColor(fg, bg);
  gfx.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2);
  gfx.setTextDatum(textdatum_t::top_left);
}

void label(int x, int y, const char* s, uint16_t fg = theme::text, uint16_t bg = theme::bg) {
  gfx.setFont(&fonts::Font0);
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.setTextColor(fg, bg);
  gfx.drawString(s, x, y);
}

// ---------------------------------------------------------------------------
// Content typeface
//
// The program's own text -- output, derived keys, parameters, help -- is set in
// the same family as the ZOOM grid, so it all reads as one thing. Chrome (the
// status bar, the tab bar, buttons) stays on the compact built-in font, where
// tight boxes matter more than typography. Only the 480 panel has the width
// for this; the 320 panel keeps the built-in font throughout.
static constexpr int kContentW    = 11;   // FreeMono9pt advance
static constexpr int kContentH    = 19;
static constexpr int kContentBigW = 14;   // FreeMono12pt advance
static constexpr int kContentBigH = 24;

// Anchored to the edges so both panels place them sensibly. On 320 these
// evaluate to the original coordinates.
constexpr int kModalBtnY = kScreenH - 32;
constexpr int kModalBtnW = (kScreenW - 30) / 4;

inline int modalBtnX(int i) { return 6 + i * (kModalBtnW + 6); }

void useContentFont(bool big = false) {
  gfx.setFont(big ? (const lgfx::IFont*)&fonts::FreeMono12pt7b
                  : (const lgfx::IFont*)&fonts::FreeMono9pt7b);
  gfx.setTextSize(1);
}

// Content text, positioned by the TOP of its line. GFX fonts draw from a
// baseline, so this centres within the line box rather than guessing an offset.
void clabel(int x, int yTop, const char* s, uint16_t fg = theme::text,
            uint16_t bg = theme::bg, bool big = false) {
  useContentFont(big);
  gfx.setTextDatum(textdatum_t::middle_left);
  gfx.setTextColor(fg, bg);
  gfx.drawString(s, x, yTop + (big ? kContentBigH : kContentH) / 2);
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.setTextSize(1);
}

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

enum class Tab : uint8_t { Run, Out, Prog, Edit, Keys, Save, Sys, COUNT };
const char* kTabNames[] = { "RUN", "OUT", "PROG", "EDIT", "SETS", "SAVE", "SYS" };

// Which tabs exist depends on whether the pack has been unlocked. Locked,
// this is an ordinary IRCIS interpreter: you pick a program, edit it and run
// it. The parameter editor, the saved sets and the presets are not merely
// disabled -- they are absent, along with the packed program itself.
// Six tabs either way, so the bar geometry never changes: w = 80, centres at
// 40, 120, 200, 280, 360, 440. EDIT and SAVE mean different things in the two
// modes -- a program editor and SD program files while locked, the packed
// program's parameters and presets once unlocked. PROG and SETS swap places.
const Tab kTabsLocked[]   = { Tab::Run, Tab::Out, Tab::Edit, Tab::Prog,
                              Tab::Save, Tab::Sys };
const Tab kTabsUnlocked[] = { Tab::Run, Tab::Out, Tab::Edit, Tab::Keys,
                              Tab::Save, Tab::Sys };

int tabCount() {
  return Store::unlocked() ? (int)(sizeof(kTabsUnlocked) / sizeof(Tab))
                           : (int)(sizeof(kTabsLocked) / sizeof(Tab));
}
Tab tabAt(int i) {
  const Tab* t = Store::unlocked() ? kTabsUnlocked : kTabsLocked;
  if (i < 0) i = 0;
  if (i >= tabCount()) i = tabCount() - 1;
  return t[i];
}
// -1 when the tab is not currently on the bar.
int tabSlot(Tab want) {
  for (int i = 0; i < tabCount(); ++i) if (tabAt(i) == want) return i;
  return -1;
}

enum class Modal : uint8_t { None, Picker, Cell, Confirm, Message, Debug,
                             Info, Ircis, Device, Wifi, Size,
                             Splash };

Tab   g_tab = Tab::Run;
Modal g_modal = Modal::None;
bool  g_dirty = true;          // full repaint of the current screen
// Set alongside g_dirty when a tap changed nothing below the header --
// transport, speed. A full repaint redraws the grid one cell at a time, and
// for a large program that is hundreds of them, which on the board shows up
// as the program crawling back onto the screen before the button responds.
bool  g_headerOnly = false;
// Set alongside g_dirty when only the area between the header and the tab bar
// changed -- scrolling a list, paging an output. Repainting the chrome as well
// is what makes scrolling look like the screen blinking.
bool  g_bodyOnly = false;
// Narrower still: only the readout band under the program on RUN.
bool  g_bandOnly = false;
// Whether the device was open last time the lock state was looked at, so the
// reveal is announced once rather than on every repaint.
bool  g_wasUnlocked = false;

// The RUN tab repaints at most this often while a program is moving. At RAPID
// the step count changes over a thousand times a second, and repainting on
// every change makes the runners strobe and leaves little for anything else.
// A paused, finished or restarted run always gets its frame.
constexpr uint32_t kRunPaintMs = 60;
uint32_t g_lastRunPaintMs = 0;

// Everything the band under the program is made of, boiled down to one number.
// Redrawing it when none of that has changed repainted "No output yet." and
// the scroll arrows sixteen times a second for no reason. 0 means "unknown",
// which forces the next draw.
uint32_t g_bandSig = 0;
// The same idea for the header. Only the step counter changes while a program
// runs; the transport, the speed and the ZOOM toggle do not, and redrawing
// them sixteen times a second is what makes them shimmer.
uint32_t g_headerSig = 0;

prog::Program g_edit;          // the working grid; always what the machine runs


void message(const std::string& title, const std::string& body);
void confirm(const std::string& title, const std::string& body,
             std::function<void()> yes);
// Write the loaded program to the card under the name the editor shows.
void saveCurrentProgram();
// The card file this program came from, or was last saved to. Saving over
// that one is what you meant; saving over any other is worth a question.
std::string g_progFile;

// A run has finished and its output has not been looked at yet.
bool g_outputUnseen = false;

int  g_editPage = 0;
uint32_t g_outVersion = 0xFFFFFFFF;

// cell inspector
int g_cellRow = 0, g_cellCol = 0;

// picker modal
std::string g_pickerSet;
std::string g_pickerValue;
std::string g_pickerTitle;
std::string g_pickerHint;
std::size_t g_pickerMax = 5;
// Index at which the second group starts. Characters at or after it are the
// IRCIS symbols -- everything that is not a base64 digit -- drawn as a
// separate block.
std::size_t g_pickerSplit = 0;
std::string g_pickerOriginal;   // what the field held when it was opened
std::function<void(const std::string&)> g_pickerCommit;
// Where closing the picker goes back to. The character picker is opened from
// the inspector, and dropping to RUN afterwards loses your place in the grid;
// most other pickers are opened from a tab and have nowhere to return to.
Modal g_pickerBack = Modal::None;

// confirm / message modal
std::string g_msgTitle, g_msgBody;
std::function<void()> g_confirmYes;

// runner cells drawn last frame, so they can be restored without a full repaint
struct DrawnCell { int row, col; };
std::vector<DrawnCell> g_prevRunners;

// ---------------------------------------------------------------------------
// grid rendering
// ---------------------------------------------------------------------------

// Two ways of looking at the 11 x 81 program: WIDE fits 80 of the 81 columns
// across the panel at the built-in font, ZOOM trades columns for legibility.
// The values are what gets written to NVS, so they are pinned rather than
// sequential -- 0 was a third view for the 320 px panel that no longer exists.
enum class View : uint8_t { Wide = 1, Zoom = 2 };
View g_view = View::Wide;
inline bool wideView() { return g_view == View::Wide; }

// ZOOM shows 34 columns and 11 rows. A program that fits inside that has
// nothing to gain from the small font, so it is shown large and the WIDE/ZOOM
// toggle is not offered at all.
bool zoomOnly();
// WIDE shows 80 of the 81 columns; this shifts the window by one so the last
// column can be seen.
int  g_wideShift = 0;
int  g_scrollCol = 0;         // ZOOM: leftmost visible column
// Both views show a band 11 rows deep. A loaded program can be up to 28 rows,
// so this is the topmost visible row.
// How tall the grid band is depends on what sits under it. With no readout
// the program gets everything down to the scroll-button row; with one, it
// stops at eleven rows and the readout takes the rest.
constexpr int kVisRows = 11;
constexpr int kBandLines = 4;            // readout lines, when there is one

inline bool runViewNone() { return Store::runView() == 2; }

// The row carrying the scroll arrows, and the grid height above it.
int shiftRowH() { return kContentH + 2; }
int gridRows() {
  if (!runViewNone()) return kVisRows;
  int avail = (kTabY - 6) - kWideY - shiftRowH() - 3;
  int n = avail / kWideCellH;
  return n < 1 ? 1 : n;
}

// How many rows the current view actually shows. ZOOM always draws kVisRows of
// its own taller cells; only WIDE sizes itself to what is left under the
// program. Everything that scrolls or follows has to ask this rather than
// gridRows(), or it works to the wrong window in ZOOM.
int gridRowsShown();
int maxGridRow();
int gridBandH() { return gridRows() * kWideCellH; }
int shiftRowY() { return kWideY + gridBandH() + 3; }

// Centre the program when it is narrower than the window: a 25-column program
// sat against the left edge with half the screen empty beside it.
int wideX() {
  int c = g_edit.cols();
  int shown = c < kWideCols ? c : kWideCols;
  int x = (kScreenW - shown * kWideCellW) / 2;
  return x > 0 ? x : 0;
}

int  g_gridRow = 0;
// How many wrapped lines back from the end of the output we are looking.
// Zero follows the tail.
int  g_outLine = 0;
// First visible runner in the readout. Up to kMaxRunners can be alive at once
// and only kBandLines of them fit, so the list scrolls the same way the output
// above it does.
int  g_runnerTop = 0;

// Editor cursor. Declared here because the header draws it, and the header is
// composed well before the editor itself.
int g_curRow = 0, g_curCol = 0;

// Zoomed out, the cell is the RUN tab's WIDE cell and nearly any program fits
// on screen at once. There is no room for arrows around a 6 px cell, so there
// are none: tap the character you want the cursor on. Zoomed in you get the
// spaced cells and the arrows back.
bool g_edZoom = true;


bool zoomOnly() {
  return g_edit.cols() <= kZoomCols && g_edit.rows() <= kVisRows;
}

// A program that fits entirely leaves room underneath for the runner list,
// which ZOOM otherwise gives up in exchange for the whole body.
constexpr int kZoomListH = 4 * kContentH + 6;
int zoomBottom() { return zoomOnly() ? kTabY - kZoomListH : kTabY; }

// Centre on the PROGRAM when it is narrower or shorter than the window, so a
// small program sits in the middle of the screen rather than up in a corner.
int zoomOriginX() {
  int w = g_edit.cols() < kZoomCols ? g_edit.cols() : kZoomCols;
  int x = (kScreenW - w * kZoomCellW) / 2;
  return x > 0 ? x : 0;
}
int zoomOriginY() {
  int h = g_edit.rows() < kVisRows ? g_edit.rows() : kVisRows;
  int y = kHeaderH + (zoomBottom() - kHeaderH - h * kZoomCellH) / 2;
  return y > kHeaderH ? y : kHeaderH;
}
int zoomHeight() {
  int h = g_edit.rows() < kVisRows ? g_edit.rows() : kVisRows;
  return h * kZoomCellH;
}

// Pick the view for whatever program has just been loaded: forced for one that
// fits, otherwise back to whatever the user last chose.
void syncViewToProgram() {
  g_view = zoomOnly() ? View::Zoom
         : ((Store::gridView() == (int)View::Zoom) ? View::Zoom : View::Wide);
  g_scrollCol = 0;
  g_gridRow = 0;
  g_wideShift = 0;
}
bool g_follow = true;         // ZOOM: keep the active runner in view

int cellW() { return g_view == View::Zoom ? kZoomCellW : kWideCellW; }
int cellH() { return g_view == View::Zoom ? kZoomCellH : kWideCellH; }

// Pan by a pixel delta; content follows the finger. Separated out so the
// arithmetic is testable without synthesising touch events.
void panByPixels(int dx);

// The detail pane's typeface. On the 480 panel this is a proper monospace at a
// sensible aspect; on the 320 panel there is only room for the built-in font
// doubled.
// The grid font for the current view. WIDE and ZOOM draw cell by cell rather
// than a row at a time: a GFX font advances by its own metric, not by our cell
// width, so drawing a row as one string put the glyphs somewhere other than
// the grid -- which is why the runner highlights sat beside their letters
// instead of on them. Per-cell placement makes the cell width authoritative,
// and lets the letter spacing be chosen freely.
inline void applyGridFont() {
  if (g_view == View::Zoom) {
    gfx.setFont(&fonts::FreeMono12pt7b);   // regular, not bold
    gfx.setTextSize(1);
  }
  else {
    gfx.setFont(&fonts::Font0);            // full 5x7 glyphs at a 5 px pitch
    gfx.setTextSize(1);
  }
}

// Centre the glyph in its cell whenever the cell is taller than the font box.
// When they are the same height, centring would round the glyph up into the row
// above and smear the rows together, so it is top-aligned instead. This also
// keeps what you see aligned with what you tap.
inline void placeGlyph(const char* ch, int x, int y) {
  if (gfx.fontHeight() < cellH()) {
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.drawString(ch, x + cellW() / 2, y + cellH() / 2);
  }
  else {
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.drawString(ch, x, y);
  }
}

void setScroll(int col) {
  // Clamped to the LOADED program: a fixed overhang would let a narrow one be
  // dragged clean off the side of the screen.
  int max = g_edit.cols() - kZoomCols;
  if (max < 0) max = 0;
  if (col < 0) col = 0;
  if (col > max) col = max;
  g_scrollCol = col;
}

void panByPixels(int dx) {
  if (dx == 0) return;
  if (g_edit.cols() <= kZoomCols) return;    // nothing off screen to reach
  setScroll(g_scrollCol - dx / kZoomCellW);
}

// Vertical drag moves the row window. Only does anything when the loaded
// program is taller than the window.
void panByRows(int dy) {
  if (dy == 0) return;
  int max = maxGridRow();
  if (max <= 0) return;
  int r = g_gridRow - dy / cellH();
  if (r < 0) r = 0;
  if (r > max) r = max;
  g_gridRow = r;
}

// Returns false when the cell is not currently on screen, which in ZOOM is the
// common case -- callers use that to skip drawing.
bool cellPos(int row, int col, int& x, int& y) {
  if (row < 0 || row >= g_edit.rows() || col < 0 || col >= g_edit.cols()) return false;
  if (g_view == View::Zoom) {
    if (col < g_scrollCol || col >= g_scrollCol + kZoomCols) return false;
    int zr = row - g_gridRow;
    if (zr < 0 || zr >= kVisRows) return false;
    x = zoomOriginX() + (col - g_scrollCol) * kZoomCellW;
    y = zoomOriginY() + zr * kZoomCellH;
    return true;
  }
  int c = col - g_wideShift;
  if (c < 0 || c >= kWideCols) return false;    // outside the column window
  int r = row - g_gridRow;
  if (r < 0 || r >= gridRows()) return false;   // outside the row window
  x = wideX() + c * kWideCellW;
  y = kWideY + r * kWideCellH;
  return true;
}

bool cellAt(int px, int py, int& row, int& col) {
  if (g_view == View::Zoom) {
    if (py < zoomOriginY() || py >= zoomOriginY() + zoomHeight()) return false;
    int w = g_edit.cols() < kZoomCols ? g_edit.cols() : kZoomCols;
    if (px < zoomOriginX() || px >= zoomOriginX() + w * kZoomCellW) return false;
    row = g_gridRow + (py - zoomOriginY()) / kZoomCellH;
    col = g_scrollCol + (px - zoomOriginX()) / kZoomCellW;
    return row >= 0 && row < g_edit.rows() && col < g_edit.cols();
  }
  if (py < kWideY || py >= kWideY + kWideH) return false;
  if (py >= kWideY + gridBandH()) return false;
  int shown = g_edit.cols() < kWideCols ? g_edit.cols() : kWideCols;
  if (px < wideX() || px >= wideX() + shown * kWideCellW) return false;
  row = g_gridRow + (py - kWideY) / kWideCellH;
  col = (px - wideX()) / kWideCellW + g_wideShift;
  return row >= 0 && row < g_edit.rows() && col < g_edit.cols();
}

int slotAtCell(int row, int col) {
  // A parameter is a position in the PACKED program. In any other one those
  // coordinates are ordinary cells, so nothing should be highlighted as a
  // parameter or named as one in the character inspector.
  if (!g_edit.isPacked()) return -1;
  for (int i = 0; i < prog::slotCount(); ++i) {
    const prog::Slot& s = prog::slot(i);
    if (s.row == row && col >= s.col && col < s.col + s.len) return i;
  }
  return -1;
}

void drawCell(int row, int col, uint16_t fg, uint16_t bg) {
  int x, y;
  if (!cellPos(row, col, x, y)) return;
  char ch[2] = { g_edit.cell(row, col), 0 };
  gfx.fillRect(x, y, cellW(), cellH(), bg);
  gfx.setTextColor(fg, bg);
  applyGridFont();
  placeGlyph(ch, x, y);
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.setTextSize(1);
}

#if defined(SK_HOST)
unsigned long g_gridPaints = 0;   // instrumentation, host only
#endif

void drawGrid() {
#if defined(SK_HOST)
  ++g_gridPaints;
#endif
  gfx.setTextDatum(textdatum_t::top_left);

  // Both views place every glyph on the grid individually, so the cell
  // width stays authoritative and letter spacing can be chosen freely.
  applyGridFont();
  int from = (g_view == View::Zoom) ? g_scrollCol : g_wideShift;
  int to   = (g_view == View::Zoom) ? g_scrollCol + kZoomCols : g_wideShift + kWideCols;
  if (to > g_edit.cols()) to = g_edit.cols();
  char ch[2] = { 0, 0 };
  int rowTo = g_gridRow + gridRowsShown();
  if (rowTo > g_edit.rows()) rowTo = g_edit.rows();
  for (int r = g_gridRow; r < rowTo; ++r) {
    for (int c = from; c < to; ++c) {
      ch[0] = g_edit.cell(r, c);
      int x, y;
      if (!cellPos(r, c, x, y)) continue;
      // Every cell is painted, blanks included: drawing only the non-blank
      // ones leaves the previous row's glyph-box overflow behind.
      gfx.fillRect(x, y, cellW(), cellH(), theme::bg);
      uint16_t fg = ch[0] == '.' ? theme::blank
                  : g_edit.cellModified(r, c) ? theme::edited
                  : (slotAtCell(r, c) >= 0 ? theme::accent : theme::text);
      // The entry marker is only meaningful when the entry point can move.
      if (Store::startEditable() && r == run::startRow() && c == run::startCol())
        gfx.drawRect(x, y, cellW(), cellH(), theme::edited);
      gfx.setTextColor(fg, theme::bg);
      placeGlyph(ch, x, y);
    }
  }
  gfx.setTextSize(1);
  gfx.setTextDatum(textdatum_t::top_left);

  // The entry point's heading, drawn AFTER the grid: it sits in the margin of
  // a neighbouring cell, and that cell's own background fill would erase it.
  if (Store::startEditable()) {
    int sx, sy;
    if (cellPos(run::startRow(), run::startCol(), sx, sy)) {
      const int w = cellW(), h = cellH();
      const int mx = sx + w / 2, my = sy + h / 2;
      const uint16_t col = theme::edited;
      switch (run::startDir()) {
        case 'N': gfx.fillTriangle(mx - 3, sy - 1, mx + 3, sy - 1, mx, sy - 5, col); break;
        case 'S': gfx.fillTriangle(mx - 3, sy + h + 1, mx + 3, sy + h + 1, mx, sy + h + 5, col); break;
        case 'W': gfx.fillTriangle(sx - 1, my - 3, sx - 1, my + 3, sx - 5, my, col); break;
        default:  gfx.fillTriangle(sx + w + 1, my - 3, sx + w + 1, my + 3, sx + w + 5, my, col); break;
      }
    }
  }

  g_prevRunners.clear();
}


// The colour a character carries in the grid: quiet grey for padding, amber
// for a parameter, cyan once edited.
uint16_t charColour(int row, int col) {
  if (g_edit.cell(row, col) == '.') return theme::blank;
  if (g_edit.cellModified(row, col)) return theme::edited;
  return slotAtCell(row, col) >= 0 ? theme::accent : theme::text;
}

void restoreCell(int row, int col) {
  int slot = slotAtCell(row, col);
  bool mod = g_edit.cellModified(row, col);
  uint16_t fg = g_edit.cell(row, col) == '.' ? theme::blank
              : mod ? theme::edited : (slot >= 0 ? theme::accent : theme::text);
  drawCell(row, col, fg, theme::bg);
}

// Scale an RGB565 colour toward black, for the fading tails.
// Mix two RGB565 colours, num/den of the way from `to` back towards `from`.
// Fading a trail towards the BACKGROUND rather than towards black is what
// makes it work in both palettes: on white, dimming towards black would make
// the oldest cell the most prominent one.
uint16_t blend565(uint16_t from, uint16_t to, int num, int den) {
  if (den <= 0) return from;
  if (num < 0) num = 0;
  if (num > den) num = den;
  int fr = (from >> 11) & 0x1F, fg = (from >> 5) & 0x3F, fb = from & 0x1F;
  int tr = (to   >> 11) & 0x1F, tg = (to   >> 5) & 0x3F, tb = to   & 0x1F;
  int r = tr + (fr - tr) * num / den;
  int g = tg + (fg - tg) * num / den;
  int b = tb + (fb - tb) * num / den;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

uint16_t dim565(uint16_t c, int num, int den) {
  int r = ((c >> 11) & 0x1F) * num / den;
  int g = ((c >> 5) & 0x3F) * num / den;
  int b = (c & 0x1F) * num / den;
  return (uint16_t)((r << 11) | (g << 5) | b);
}


void drawRunners(const run::Snapshot& snap) {
  for (const auto& p : g_prevRunners) restoreCell(p.row, p.col);
  g_prevRunners.clear();

  // Tails only make sense while you can follow them. At FAST/FULL a runner
  // crosses hundreds of cells between frames, so the tail is just noise --
  // and redrawing it would cost more than the run itself.
  run::Speed sp = run::speed();
  bool trails = (sp == run::Speed::Slow);

  if (trails) {
    // Trails only exist at SLOW, where you can see each step land, so a long
    // one is just clutter: five or six cells is enough to read the direction
    // of travel.
    int maxAge = (g_view == View::Zoom) ? 6 : 5;
    if (maxAge > run::kTrailView) maxAge = run::kTrailView;
    for (int i = 0; i < run::kMaxRunners; ++i) {
      const run::RunnerView& r = snap.runners[i];
      if (!r.alive) continue;
      uint16_t base = theme::runner[r.id % 6];
      int from = r.trailLen - 1;
      if (from >= maxAge) from = maxAge - 1;
      for (int age = from; age >= 0; --age) {
        int ty = r.trailY[age], tx = r.trailX[age];
        if (ty >= g_edit.rows() || tx >= g_edit.cols()) continue;
        int x, y;
        if (!cellPos(ty, tx, x, y)) continue;
        // Tint the CHARACTER rather than filling the cell behind it. Filling
        // hid what the runner had just walked over, which is the one thing you
        // are watching for. Oldest fades into the background, newest is close
        // to the runner's own colour.
        uint16_t c = blend565(base, theme::bg, maxAge - age, maxAge + 1);
        char ch[2] = { g_edit.cell(ty, tx), 0 };
        gfx.fillRect(x, y, cellW(), cellH(), theme::bg);
        applyGridFont();
        gfx.setTextColor(c, theme::bg);
        placeGlyph(ch, x, y);
        gfx.setTextDatum(textdatum_t::top_left);
        gfx.setTextSize(1);
        g_prevRunners.push_back({ty, tx});
      }
    }
  }

  for (int i = 0; i < run::kMaxRunners; ++i) {
    const run::RunnerView& r = snap.runners[i];
    if (!r.alive || r.y >= g_edit.rows() || r.x >= g_edit.cols()) continue;
    uint16_t col = theme::runner[r.id % 6];
    int x, y;
    if (!cellPos(r.y, r.x, x, y)) continue;
    // A paused runner keeps its own colour -- the readout below already says
    // it is pausing, and greying it made it look like it had died.
    uint16_t head = col;
    drawCell(r.y, r.x, theme::bg, head);
    g_prevRunners.push_back({r.y, r.x});
  }
}

// ---------------------------------------------------------------------------
// chrome
// ---------------------------------------------------------------------------

const char* speedName(run::Speed s) {
  switch (s) {
    case run::Speed::Slow:  return "SLOW";
    case run::Speed::Fast:  return "FAST";
    case run::Speed::Rapid: return "RAPID";
    case run::Speed::Full:  return "FULL";
  }
  return "?";
}

// Transport, right-anchored, in a fixed 140 px strip so the speed and view
// buttons to its left never move:  |<   <   play/pause   >   >|
//
// The two single-step buttons are optional (SYS > STEP BUTTONS). With five
// buttons in the strip each is 26 px wide, which is easy to mis-hit on a
// resistive panel; with three they are 44 px, and restart / play / run-to-end
// are the ones you reach for at speed.
constexpr int kTransportX = kScreenW - 142;
inline bool steps() { return Store::stepButtons(); }
Btn btnEnd()   { return steps() ? Btn{ kScreenW -  28, 2, 26, 18, "", theme::text,   theme::panel }
                                : Btn{ kScreenW -  46, 2, 44, 18, "", theme::text,   theme::panel }; }
Btn btnFwd()   { return { kScreenW -  56, 2, 26, 18, "", theme::text,   theme::panel }; }
Btn btnRun()   { return steps() ? Btn{ kScreenW -  86, 2, 28, 18, "", theme::accent, theme::panel }
                                : Btn{ kScreenW -  94, 2, 44, 18, "", theme::accent, theme::panel }; }
Btn btnBack()  { return { kScreenW - 114, 2, 26, 18, "", theme::text,   theme::panel }; }
Btn btnStart() { return { kTransportX, 2, steps() ? 26 : 44, 18, "", theme::text, theme::panel }; }
Btn btnSpeed()  { return { kScreenW - 178, 2, 34, 18, "FAST", theme::good, theme::panel }; }
Btn btnView()   { return { kScreenW - 232, 2, 52, 18, "ZOOM", theme::edited, theme::panel }; }

// Transport symbols, drawn rather than lettered.
enum class Glyph : uint8_t { Start, Back, Play, Pause, Fwd, End };

void drawGlyph(const Btn& b, Glyph g, bool on) {
  int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
  uint16_t c = on ? theme::bg : b.fg;
  switch (g) {
    case Glyph::Start:
      gfx.fillRect(cx - 6, cy - 5, 2, 10, c);
      gfx.fillTriangle(cx + 5, cy - 5, cx + 5, cy + 5, cx - 3, cy, c);
      break;
    case Glyph::Back:
      gfx.fillTriangle(cx + 4, cy - 5, cx + 4, cy + 5, cx - 4, cy, c);
      break;
    case Glyph::Play:
      gfx.fillTriangle(cx - 4, cy - 6, cx - 4, cy + 6, cx + 6, cy, c);
      break;
    case Glyph::Pause:
      gfx.fillRect(cx - 5, cy - 5, 4, 10, c);
      gfx.fillRect(cx + 1, cy - 5, 4, 10, c);
      break;
    case Glyph::Fwd:
      gfx.fillTriangle(cx - 4, cy - 5, cx - 4, cy + 5, cx + 4, cy, c);
      break;
    case Glyph::End:
      gfx.fillTriangle(cx - 5, cy - 5, cx - 5, cy + 5, cx + 3, cy, c);
      gfx.fillRect(cx + 4, cy - 5, 2, 10, c);
      break;
  }
}

// The editor's status bar carries two controls: the program's name, and its
// size. Both open a dialog. They live up here because the body is entirely
// spoken for -- grid above, keyboard below.
// The editor's status bar, left to right after the title: the program's name,
// its size, the zoom toggle, SAVE, and then the cursor readout at the right
// edge. Saving belongs where the editing and the renaming happen, not two tabs
// away, so the name field gives up the width for it.
Btn btnEdName() { return { 118, 2, 116, 18, "", theme::text, theme::panel }; }
Btn btnEdSize() { return { 238, 2,  62, 18, "", theme::text, theme::panel }; }
Btn btnEdZoom() { return { 304, 2,  50, 18, "ZOOM", theme::edited, theme::panel }; }
Btn btnEdSave() { return { 358, 2,  50, 18, "SAVE", theme::good,  theme::panel }; }
bool editorTab() { return g_tab == Tab::Edit && !Store::unlocked(); }

// Everything in the header EXCEPT the step counter.
uint32_t headerSignature(const run::Snapshot& snap) {
  uint32_t h = 2166136261u;
  auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix((uint32_t)g_tab);
  mix((uint32_t)g_view);
  mix(zoomOnly() ? 1u : 0u);
  mix((uint32_t)run::speed());
  mix(snap.running ? 1u : 0u);
  mix(snap.finished ? 1u : 0u);
  mix(snap.step > 0 ? 1u : 0u);
  mix(Store::unlocked() ? 1u : 0u);
  mix(g_edit.modifiedCells());
  return h ? h : 1u;
}

// Just the counter, on its own strip between the title and the ZOOM button.
void drawHeaderStep(const run::Snapshot& snap) {
  char buf[32];
  snprintf(buf, sizeof(buf), "step %u", (unsigned)snap.step);
  const int right = btnView().x - 8;
  const int left  = 156;
  gfx.fillRect(left, 1, right - left, kHeaderH - 2, theme::panel);
  gfx.setFont(&fonts::Font0);
  gfx.setTextDatum(textdatum_t::middle_right);
  gfx.setTextColor(snap.finished ? theme::good : theme::text, theme::panel);
  gfx.drawString(buf, right, kHeaderH / 2);
  gfx.setTextDatum(textdatum_t::top_left);
}

void drawHeader(const run::Snapshot& snap) {
  gfx.fillRect(0, 0, kScreenW, kHeaderH, theme::panel);
  gfx.drawFastHLine(0, kHeaderH - 1, kScreenW, theme::line);

  char buf[64];
  if (g_tab == Tab::Run) {
    snprintf(buf, sizeof(buf), "step %u", (unsigned)snap.step);
    gfx.setFont(&fonts::Font2);
    gfx.setTextDatum(textdatum_t::middle_left);
    gfx.setTextColor(theme::accent, theme::panel);
    // The RUN page is titled with the program on it. The transport starts at
    // x 248, so a long name is cut rather than drawn under it.
    std::string title = g_edit.programName();
    if (title.size() > 20) title = title.substr(0, 20);
    gfx.drawString(title.c_str(), 4, kHeaderH / 2);
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.setFont(&fonts::Font0);
    gfx.setTextDatum(textdatum_t::middle_right);
    gfx.setTextColor(snap.finished ? theme::good : theme::text, theme::panel);
    gfx.drawString(buf, btnView().x - 8, kHeaderH / 2);
    gfx.setTextDatum(textdatum_t::top_left);

    // One toggle: filled when the zoomed view is showing.
    if (!zoomOnly()) drawBtn(btnView(), g_view == View::Zoom);
    Btn sp = btnSpeed(); sp.label = speedName(run::speed());
    drawBtn(sp);

    drawBtn(btnStart());  drawGlyph(btnStart(), Glyph::Start, false);
    if (steps()) {
      drawBtn(btnBack(), false, snap.step > 0);
      drawGlyph(btnBack(), Glyph::Back, false);
    }
    Btn rb = btnRun();
    drawBtn(rb, snap.running);
    drawGlyph(rb, snap.running ? Glyph::Pause : Glyph::Play, snap.running);
    if (steps()) {
      drawBtn(btnFwd(), false, !snap.finished);
      drawGlyph(btnFwd(), Glyph::Fwd, false);
    }
    drawBtn(btnEnd(), false, !snap.finished);
    drawGlyph(btnEnd(), Glyph::End, false);
  }
  else {
    const char* title = "";
    switch (g_tab) {
      case Tab::Edit: title = Store::unlocked() ? pack::str(pack::kStrEditTitle) : "EDIT PROGRAM"; break;
      case Tab::Keys: title = "PARAMETER SETS"; break;
      case Tab::Out:  title = "OUTPUT"; break;
      case Tab::Save: title = Store::unlocked() ? "PRESETS" : "SAVED PROGRAMS"; break;
      case Tab::Prog: title = "PROGRAMS"; break;
      case Tab::Sys:  title = "SYSTEM"; break;
      default: break;
    }
    gfx.setFont(&fonts::Font2);
    gfx.setTextDatum(textdatum_t::middle_left);
    gfx.setTextColor(theme::accent, theme::panel);
    gfx.drawString(title, 4, kHeaderH / 2);
    gfx.setTextDatum(textdatum_t::top_left);
    if (editorTab()) {
      // Name and size are buttons; the cursor position is just a readout.
      Btn nb = btnEdName();
      std::string nm = g_edit.programName();
      if (nm.size() > 14) nm = nm.substr(0, 14);
      nb.label = nm.c_str();
      drawBtn(nb);
      char sz[24];
      snprintf(sz, sizeof(sz), "%d x %d", g_edit.rows(), g_edit.cols());
      Btn sb = btnEdSize();
      sb.label = sz;
      drawBtn(sb);
      drawBtn(btnEdZoom(), g_edZoom);
      drawBtn(btnEdSave(), false, plat::sdPresent());
      snprintf(buf, sizeof(buf), "r%d c%d", g_curRow, g_curCol);
    }
    else if (g_tab == Tab::Out) buf[0] = 0;   // OUT shows the run, not the edit state
    else snprintf(buf, sizeof(buf), "%d edited cell%s", g_edit.modifiedCells(),
                  g_edit.modifiedCells() == 1 ? "" : "s");
    gfx.setFont(&fonts::Font0);
    gfx.setTextDatum(textdatum_t::middle_right);
    gfx.setTextColor(editorTab() ? theme::text
                     : (g_edit.modifiedCells() ? theme::edited : theme::dim), theme::panel);
    gfx.drawString(buf, kScreenW - 4, kHeaderH / 2);
    gfx.setTextDatum(textdatum_t::top_left);
  }
}

void drawTabs() {
  int n = tabCount();
  int w = kScreenW / n;
  for (int i = 0; i < n; ++i) {
    Tab t = tabAt(i);
    bool on = g_tab == t;
    // The width rarely divides evenly; give the last tab the leftover pixels.
    int tw = (i == n - 1) ? kScreenW - i * w : w;
    bool flag = (t == Tab::Out) && g_outputUnseen && !on;
    gfx.fillRect(i * w, kTabY, tw, kTabH, on ? theme::accent : theme::panel);
    gfx.drawRect(i * w, kTabY, tw, kTabH, flag ? theme::good : theme::line);
    if (flag) gfx.fillCircle(i * w + tw - 8, kTabY + 8, 3, theme::good);
    gfx.setFont(&fonts::Font2);
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setTextColor(on ? theme::bg : theme::text, on ? theme::accent : theme::panel);
    // Tapping the tab you are already on is the play/pause shortcut, so while
    // a run is going the RUN tab says what the tap would do. Only while it is
    // the current tab: from anywhere else the same tap navigates instead.
    const char* label = (t == Tab::Run && on && run::snapshot().running)
                        ? "PAUSE" : kTabNames[(int)t];
    gfx.drawString(label, i * w + tw / 2, kTabY + kTabH / 2);
  }
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.setFont(&fonts::Font0);
}

// ---------------------------------------------------------------------------
// screens
// ---------------------------------------------------------------------------

// Live per-runner state under the program: where each one is, which way it
// is heading, and the character it is reading this step -- the same fields
// IRCIS puts in its own debug trace. Redrawn every step, not just on a full
// repaint, so it tracks the run.
// The scroll row sits directly under the grid, wherever that ends.
#define kWideShiftY shiftRowY()
int gridRowsShown() { return g_view == View::Zoom ? kVisRows : gridRows(); }
int maxGridRow()    { int m = g_edit.rows() - gridRowsShown(); return m > 0 ? m : 0; }

int runnerListY() { return kTabY - kBandLines * kContentH - 2; }
#define kRunnerListY runnerListY()

Btn btnWideLeft()  { return { kScreenW - 62, kWideShiftY, 26, kContentH + 2, "<" }; }
Btn btnWideRight() { return { kScreenW - 32, kWideShiftY, 26, kContentH + 2, ">" }; }
// Only drawn when the loaded program is taller than the 11-row window.
Btn btnRowUp()     { return { kScreenW - 122, kWideShiftY, 26, kContentH + 2, "^" }; }
Btn btnRowDown()   { return { kScreenW - 92,  kWideShiftY, 26, kContentH + 2, "v" }; }
// The output's own pair, on the left of the same row, so the two kinds of
// scrolling stay visibly separate: the program's on the right, the text's here.
Btn btnOutUp()     { return { 4,  kWideShiftY, 26, kContentH + 2, "^" }; }
Btn btnOutDown()   { return { 34, kWideShiftY, 26, kContentH + 2, "v" }; }


// Cheap FNV-1a over the things drawRunnerList actually shows.
uint32_t bandSignature(const run::Snapshot& snap) {
  uint32_t h = 2166136261u;
  auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix((uint32_t)Store::runView() + 1u);
  mix((uint32_t)g_wideShift);
  mix((uint32_t)g_gridRow);
  mix((uint32_t)g_outLine);
  mix((uint32_t)g_runnerTop);
  mix((uint32_t)g_edit.cols());
  mix((uint32_t)g_edit.rows());
  mix(snap.step == 0 ? 1u : 0u);
  if (Store::runView() == 1) {
    mix((uint32_t)run::output().size());
  }
  else {
    for (int i = 0; i < run::kMaxRunners && i < 4; ++i) {
      const run::RunnerView& r = snap.runners[i];
      mix((uint32_t)(r.used ? 1 : 0));
      mix((uint32_t)(r.alive ? 1 : 0));
      mix((uint32_t)r.y);
      mix((uint32_t)r.x);
      mix((uint32_t)r.dir);
      mix((uint32_t)r.diedStep);
      mix((uint32_t)(r.paused ? 1 : 0));
    }
  }
  return h ? h : 1u;          // never 0: that is the "unknown" marker
}

#if defined(SK_HOST)
unsigned long g_bandPaints = 0;   // instrumentation, host only
#endif

void drawRunnerList(const run::Snapshot& snap) {
#if defined(SK_HOST)
  ++g_bandPaints;
#endif
  gfx.fillRect(0, kWideShiftY, kScreenW, kTabY - kWideShiftY, theme::bg);
  // A program that fits across the screen has nothing to scroll to, so the
  // arrows are absent rather than permanently greyed out.
  if (g_edit.cols() > (wideView() ? kWideCols : kZoomCols)) {
    drawBtn(btnWideLeft(), false, g_wideShift > 0);
    drawBtn(btnWideRight(), false, g_wideShift + kWideCols < g_edit.cols());
  }
  if (maxGridRow() > 0) {
    drawBtn(btnRowUp(),   false, g_gridRow > 0);
    drawBtn(btnRowDown(), false, g_gridRow < maxGridRow());
  }
  // Nothing under the program: the scroll row is all there is, and the grid
  // above it has already taken the space.
  if (runViewNone()) return;

  int y = kRunnerListY;

  // The other readout: what the program has printed so far, growing as it
  // runs, the way you would watch the output file on a desktop build.
  if (Store::runView() == 1) {
    const std::string out = run::output();
    if (out.empty()) {
      g_outLine = 0;
      clabel(12, y, snap.step == 0 ? "Press play to begin IRCIS."
                                   : "No output yet.",
             snap.step == 0 ? theme::accent : theme::dim);
      return;
    }
    // Wrap to the screen, then keep the last four lines: the tail is where
    // anything new appears.
    const std::size_t wide = (kScreenW - 16) / kContentW;
    std::vector<std::string> lines;
    std::string cur;
    for (char c : out) {
      if (c == '\n') { lines.push_back(cur); cur.clear(); continue; }
      cur.push_back(c);
      if (cur.size() >= wide) { lines.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) lines.push_back(cur);
    // Four lines fit. g_outLine is how far back from the end we are looking:
    // zero follows the tail as it grows, anything else holds still so you can
    // read what has already gone past.
    const std::size_t rows = kBandLines;
    const std::size_t total = lines.size();
    std::size_t maxBack = total > rows ? total - rows : 0;
    if ((std::size_t)g_outLine > maxBack) g_outLine = (int)maxBack;
    std::size_t end = total - (std::size_t)g_outLine;
    std::size_t from = end > rows ? end - rows : 0;

    if (maxBack > 0) {
      drawBtn(btnOutUp(),   false, (std::size_t)g_outLine < maxBack);
      drawBtn(btnOutDown(), false, g_outLine > 0);
    }
    // Say when there is more above, so a long output does not read as a short
    // one that begins mid-word.
    if (from > 0) { clabel(12, y, "...", theme::dim); y += kContentH; ++from; }
    for (std::size_t i = from; i < end; ++i) {
      // While following the tail the newest line is the bright one.
      bool last = (i + 1 == total) && g_outLine == 0;
      clabel(12, y, lines[i].c_str(), last ? theme::text : theme::dim);
      y += kContentH;
    }
    return;
  }

  if (snap.step == 0) {
    clabel(12, y, "Press play to begin IRCIS.", theme::accent);
    char st[64];
    snprintf(st, sizeof(st), "entry: row %d  col %d  heading %c",
             run::startRow(), run::startCol(), run::startDir());
    clabel(12, y + kContentH, st, theme::dim);
    return;
  }
  // Which runners there are to show, before working out which of them fit.
  int used[run::kMaxRunners];
  int nUsed = 0;
  for (int i = 0; i < run::kMaxRunners; ++i)
    if (snap.runners[i].used) used[nUsed++] = i;

  const int rows = nUsed > kBandLines ? kBandLines - 1 : kBandLines;
  const int maxTop = nUsed > rows ? nUsed - rows : 0;
  if (g_runnerTop > maxTop) g_runnerTop = maxTop;
  if (g_runnerTop < 0) g_runnerTop = 0;
  if (maxTop > 0) {
    drawBtn(btnOutUp(),   false, g_runnerTop > 0);
    drawBtn(btnOutDown(), false, g_runnerTop < maxTop);
  }

  for (int k = g_runnerTop; k < nUsed && k < g_runnerTop + rows; ++k) {
    const run::RunnerView& r = snap.runners[used[k]];
    gfx.fillRect(4, y + kContentH / 2 - 2, 5, 5, theme::runner[r.id % 6]);
    char buf[72];
    if (!r.alive) {
      snprintf(buf, sizeof(buf), "R%d  died at step %u", r.id, (unsigned)r.diedStep);
      clabel(12, y, buf, theme::dim);
    }
    else {
      // The character is printed in its own colour rather than in quotes --
      // an apostrophe inside quotes reads as nothing at all.
      snprintf(buf, sizeof(buf), "R%d  r%-2d c%-2d %c  reads ", r.id, r.y, r.x, r.dir);
      clabel(12, y, buf, theme::text);
      char one[2] = { g_edit.cell(r.y, r.x), 0 };
      int cx = 12 + (int)strlen(buf) * kContentW;
      clabel(cx, y, one, charColour(r.y, r.x));
      if (r.paused) clabel(cx + 2 * kContentW, y, "paused", theme::dim);
    }
    y += kContentH;
  }
  if (maxTop > 0) {
    char where[64];
    snprintf(where, sizeof(where), "runners %d-%d of %d",
             g_runnerTop + 1,
             g_runnerTop + rows < nUsed ? g_runnerTop + rows : nUsed, nUsed);
    clabel(12, y, where, theme::dim);
  }
}

void drawRun(const run::Snapshot& snap) {
  gfx.fillRect(0, kBodyY, kScreenW, kBodyH, theme::bg);
  drawGrid();
  drawRunners(snap);

  if (g_view == View::Zoom && !zoomOnly()) return;   // the grid is the whole screen

  drawRunnerList(snap);
}

struct EditRow { int y, h; };
constexpr int kEditRowH = kContentH + 8;
constexpr int kEditRowsPerPage = kBodyH / kEditRowH;
// Page 1 is exactly the parameters the puzzle asks you to set.
inline int editPageFirst(int page) { return page == 0 ? 0 : prog::primarySlots(); }
inline int editPageCount(int page) {
  int n = page == 0 ? prog::primarySlots() : prog::slotCount() - prog::primarySlots();
  int room = kEditRowsPerPage - 1;
  return n < room ? n : room;
}

Btn btnEditPrev() { return { 4, kTabY - 22, 44, 18, "PREV" }; }
Btn btnEditNext() { return { 52, kTabY - 22, 44, 18, "NEXT" }; }
Btn btnEditRevert() { return { 246, kTabY - 22, 70, 18, "REVERT ALL", theme::warn } ; }

// Defined further down with the other modals.
void openPicker(const std::string& title, const std::string& hint,
                const std::string& set, const std::string& initial,
                std::size_t maxLen, std::function<void(const std::string&)> commit,
                std::size_t split = 0);

// The characters IRCIS accepts. Shared by the program editor's keyboard and
// the character picker, so the two can never drift apart.
// There are four picker keyboards, and only four. Each is the one before it
// with more characters, and the layout never moves: the base64 block is always
// 8 x 8, the IRCIS symbols are always the two columns beside it, and the text
// extras go in rows underneath. A key you have learned the position of stays
// where it was.
//
//   1  kKbDigits   digits, for J alone
//   2  kKbBase64   the 64 base64 digits, for parameter slots
//   3  kKbProgram  + the 16 symbols IRCIS understands: every character a
//                  program may contain. EDIT CHARACTER uses this.
//   4  kKbText     + the rest of printable ASCII, for free text -- program
//                  names, WiFi credentials, preset names.
//
// The EDIT PROGRAM page has a keyboard of its own with a different job: it is
// the typing surface, not a modal, and it carries exactly kKbProgram's set.
const char* kCellBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
// Sixteen symbols exactly, so they fit two columns beside the base64 block.
// No space key ('.' already writes the blank IRCIS reads) and no ':' -- it is
// not an IRCIS command. A program may still contain one, in which case IRCIS
// simply cannot process it; REVERT puts it back if that cell is ever changed.
const char* kCellSymbols = "!\"#$%&'*-.<>?@^|";
// Printable ASCII that is neither a base64 digit nor an IRCIS command: the
// fifteen characters keyboard 4 adds. No character appears on two keyboards
// in two different places.
const char* kTextExtras = " (),:;=[\\]_`{}~";

// \x01 is a blank key, so "1234567890" in three columns reads
// 123 / 456 / 789 / [ ]0[ ] and the zero lands under the eight.
const char* kKbDigits = "123456789\x01" "0";
const std::string kKbBase64  = kCellBase64;
const std::string kKbProgram = kKbBase64 + kCellSymbols;
const std::string kKbText    = kKbProgram + kTextExtras;
// Where the base64 block ends: the split every keyboard past the first uses,
// so the symbols always begin in the same place.
const std::size_t kKbSplit = kKbBase64.size();

// ---------------------------------------------------------------------------
// EDIT while locked: a text editor for the loaded IRCIS program.
//
// The body splits into a grid window on top and a keyboard underneath. The
// grid cell is 6 x 15, the same as the RUN tab's WIDE view, so 80 of the 96
// possible columns are visible at once and the two views agree about what a
// cell looks like.
// ---------------------------------------------------------------------------

constexpr int kEdGridY = kBodyY + 4;

// Keyboard: five rows of 16 characters, then a row of wider function keys.
// Five rows of sixteen: the 64 base64 digits then the 16 symbols. There is no
// function row -- the cursor carries its own arrows, '.' is the blank so a DEL
// key would be a duplicate, and size and name are buttons in the status bar.
constexpr int kEdKeyRows = 5;
constexpr int kEdKeyCols = 16;
constexpr int kEdKeyW    = kScreenW / kEdKeyCols;     // 30
constexpr int kEdKeyH    = 20;
constexpr int kEdKeyY    = kTabY - kEdKeyRows * kEdKeyH;

int g_edRow = 0, g_edCol = 0;      // top-left of the visible window

// The grid band runs from under the header down to the keyboard.
//
// The cell is deliberately far larger than the glyph, exactly as the character
// inspector's is: the cursor's arrows are drawn at the outer edge of the
// NEIGHBOURING cell, so that cell's own whitespace is what keeps them off the
// characters either side. Packing tighter would mean drawing arrows on top of
// letters, which is what this replaces. The cost is that fewer columns show at
// once and the view scrolls sooner -- RUN is where you look at a whole program.
constexpr int kEdCellW = 30;
constexpr int kEdCellH = 32;

int edBandH() { return kEdKeyY - kEdGridY - 4; }
int edCellW() { return g_edZoom ? kEdCellW : kWideCellW; }
int edCellH() { return g_edZoom ? kEdCellH : kWideCellH; }
int edCols()  { int n = kScreenW / edCellW(); return n > prog::kMaxCols ? prog::kMaxCols : n; }
int edRows()  { return edBandH() / edCellH(); }

// Centre on the program when it is smaller than the band.
int edGridX() {
  int w = g_edit.cols() < edCols() ? g_edit.cols() : edCols();
  int x = (kScreenW - w * edCellW()) / 2;
  return x > 0 ? x : 0;
}
int edGridY() {
  int h = g_edit.rows() < edRows() ? g_edit.rows() : edRows();
  int y = kEdGridY + (edBandH() - h * edCellH()) / 2;
  return y > kEdGridY ? y : kEdGridY;
}

// Keep the cursor on screen after any movement.
void edFollow() {
  const int vr = edRows(), vc = edCols();
  // One cell of margin when the arrows are drawn, so the neighbouring cell an
  // arrow lives in is itself on screen. Zoomed out there are no arrows, so the
  // cursor may sit on the very edge.
  const int m = g_edZoom ? 1 : 0;
  if (g_curRow - m < g_edRow) g_edRow = g_curRow - m;
  if (g_curRow + m >= g_edRow + vr) g_edRow = g_curRow + m + 1 - vr;
  if (g_curCol - m < g_edCol) g_edCol = g_curCol - m;
  if (g_curCol + m >= g_edCol + vc) g_edCol = g_curCol + m + 1 - vc;
  int mr = g_edit.rows() - vr; if (mr < 0) mr = 0;
  int mc = g_edit.cols() - vc; if (mc < 0) mc = 0;
  if (g_edRow > mr) g_edRow = mr;
  if (g_edCol > mc) g_edCol = mc;
  if (g_edRow < 0) g_edRow = 0;
  if (g_edCol < 0) g_edCol = 0;
}

// index 0..79 -> the character that key types
char edKeyChar(int i) {
  if (i < 64) return kCellBase64[i];
  if (i < 80) return kCellSymbols[i - 64];
  return 0;
}


// The cursor's own arrows, drawn against the cell it sits on. Unlike the
// character inspector -- where the cursor is pinned to the centre and the grid
// slides underneath -- here the cursor moves and the program stays put, which
// is much easier to follow when the whole program is on screen. A cell can be
// as small as 6 x 15, so the arrows get a minimum size you can actually hit
// and are allowed to overlap their neighbours.
// Defined with the character inspector, which draws the same arrows.
void drawChevron(const Btn& b, int dx, int dy);

// The cell next door, exactly as the character inspector does it. The chevron
// is drawn at that cell's far edge, in the whitespace around its glyph.
Btn edArrow(int dx, int dy) {
  return { edGridX() + (g_curCol - g_edCol + dx) * kEdCellW,
           edGridY() + (g_curRow - g_edRow + dy) * kEdCellH,
           kEdCellW, kEdCellH, "" };
}

void drawProgEditGrid() {
  gfx.fillRect(0, kEdGridY, kScreenW, edBandH() + 4, theme::bg);
  const int cw = edCellW(), chh = edCellH();
  const int ox = edGridX(), oy = edGridY();
  gfx.setTextSize(1);
  int rowTo = g_edRow + edRows(); if (rowTo > g_edit.rows()) rowTo = g_edit.rows();
  int colTo = g_edCol + edCols(); if (colTo > g_edit.cols()) colTo = g_edit.cols();
  char ch[2] = { 0, 0 };
  for (int r = g_edRow; r < rowTo; ++r) {
    for (int c = g_edCol; c < colTo; ++c) {
      int x = ox + (c - g_edCol) * cw;
      int y = oy + (r - g_edRow) * chh;
      bool cur = (r == g_curRow && c == g_curCol);
      uint16_t bg = cur ? theme::accent : theme::bg;
      gfx.fillRect(x, y, cw, chh, bg);
      ch[0] = g_edit.cell(r, c);
      uint16_t fg = cur ? theme::bg : (ch[0] == '.' ? theme::blank : theme::text);
      gfx.setTextColor(fg, bg);
      if (g_edZoom) {
        gfx.setFont(&fonts::FreeMono12pt7b);
        gfx.setTextDatum(textdatum_t::middle_center);
        gfx.drawString(ch, x + cw / 2, y + chh / 2);
      }
      else {
        gfx.setFont(&fonts::Font0);
        gfx.setTextDatum(textdatum_t::top_left);
        gfx.drawString(ch, x, y + 4);
      }
    }
  }
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.setFont(&fonts::Font0);
}

void drawProgEdit() {
  gfx.fillRect(0, kBodyY, kScreenW, kBodyH, theme::bg);
  edFollow();
  drawProgEditGrid();

  gfx.setTextDatum(textdatum_t::middle_center);
  for (int r = 0; r < kEdKeyRows; ++r) {
    for (int c = 0; c < kEdKeyCols; ++c) {
      int i = r * kEdKeyCols + c;
      char k = edKeyChar(i);
      if (!k) continue;
      int x = c * kEdKeyW, y = kEdKeyY + r * kEdKeyH;
      // Same scheme as the character picker: a key that is not a base64
      // digit gets a lighter face. Seven base64 letters are ALSO commands
      // (v V + / r R p), so those keep the base64 face and only the glyph
      // changes colour -- the key is still a digit, it just does two jobs.
      bool symbol  = i >= 64;
      bool command = symbol || std::strchr("vV+/rRp", k) != nullptr;
      uint16_t kbg = symbol ? theme::line : theme::panel;
      gfx.fillRect(x + 1, y + 1, kEdKeyW - 2, kEdKeyH - 2, kbg);
      gfx.drawRect(x + 1, y + 1, kEdKeyW - 2, kEdKeyH - 2, theme::line);
      char lbl[2] = { k, 0 };
      gfx.setFont(&fonts::Font0);
      gfx.setTextSize(2);
      gfx.setTextColor(command ? theme::accent : theme::text, kbg);
      // +1: Font0's glyph box is 16 px in a 20 px key, and middle_center
      // rounds the odd pixel upwards -- capitals ended up touching the top
      // edge with two clear beneath them.
      gfx.drawString(lbl, x + kEdKeyW / 2, y + kEdKeyH / 2 + 1);
    }
  }
  gfx.setTextSize(1);

  // Arrows last, so they sit over the grid rather than under it.
  if (g_edZoom) {
    if (g_curRow > 0)                  drawChevron(edArrow(0, -1), 0, -1);
    if (g_curRow < g_edit.rows() - 1)  drawChevron(edArrow(0,  1), 0,  1);
    if (g_curCol > 0)                  drawChevron(edArrow(-1, 0), -1, 0);
    if (g_curCol < g_edit.cols() - 1)  drawChevron(edArrow( 1, 0),  1, 0);
  }

  gfx.setTextDatum(textdatum_t::top_left);
  gfx.setFont(&fonts::Font0);
}

void openSizeDialog();

// Rows x columns for a new or resized program. Kept as a stepper rather than a
// keypad: the range is small, the bounds are hard, and a stepper cannot be
// used to enter something out of range in the first place.
int  g_sizeRows = 11, g_sizeCols = 40;
bool g_sizeIsNew = true;         // false when resizing the loaded program

constexpr int kSzY = 60;
constexpr int kSzH = 150;
Btn btnSzRowsDn() { return { 150, kSzY + 40, 40, 28, "-" }; }
Btn btnSzRowsUp() { return { 250, kSzY + 40, 40, 28, "+" }; }
Btn btnSzColsDn() { return { 150, kSzY + 76, 40, 28, "-" }; }
Btn btnSzColsUp() { return { 250, kSzY + 76, 40, 28, "+" }; }
Btn btnSzCancel() { return { 40,  kSzY + kSzH - 36, 120, 28, "CANCEL", theme::bad }; }
Btn btnSzOk()     { return { kScreenW - 160, kSzY + kSzH - 36, 120, 28, "OK",
                             theme::bg, theme::good }; }

void drawSize() {
  gfx.fillRect(20, kSzY, kScreenW - 40, kSzH, theme::panel);
  gfx.drawRect(20, kSzY, kScreenW - 40, kSzH, theme::accent);
  gfx.setFont(&fonts::Font2);
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.setTextColor(theme::accent, theme::panel);
  gfx.drawString(g_sizeIsNew ? "NEW PROGRAM" : "RESIZE", 30, kSzY + 6);

  char buf[32];
  clabel(40, kSzY + 46, "rows", theme::text, theme::panel);
  snprintf(buf, sizeof(buf), "%d", g_sizeRows);
  gfx.setFont(&fonts::Font2);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.setTextColor(theme::text, theme::panel);
  gfx.drawString(buf, 220, kSzY + 54);
  gfx.setTextDatum(textdatum_t::top_left);
  clabel(40, kSzY + 82, "cols", theme::text, theme::panel);
  snprintf(buf, sizeof(buf), "%d", g_sizeCols);
  gfx.setFont(&fonts::Font2);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.setTextColor(theme::text, theme::panel);
  gfx.drawString(buf, 220, kSzY + 90);
  gfx.setTextDatum(textdatum_t::top_left);

  drawBtn(btnSzRowsDn()); drawBtn(btnSzRowsUp());
  drawBtn(btnSzColsDn()); drawBtn(btnSzColsUp());
  drawBtn(btnSzCancel()); drawBtn(btnSzOk());
  gfx.setFont(&fonts::Font0);
}

void handleSizeTouch(int x, int y) {
  auto clampR = [] { if (g_sizeRows < 1) g_sizeRows = 1;
                     if (g_sizeRows > prog::kMaxRows) g_sizeRows = prog::kMaxRows; };
  auto clampC = [] { if (g_sizeCols < 1) g_sizeCols = 1;
                     if (g_sizeCols > prog::kMaxCols) g_sizeCols = prog::kMaxCols; };
  if (hit(btnSzRowsDn(), x, y)) { --g_sizeRows; clampR(); g_dirty = true; return; }
  if (hit(btnSzRowsUp(), x, y)) { ++g_sizeRows; clampR(); g_dirty = true; return; }
  if (hit(btnSzColsDn(), x, y)) { --g_sizeCols; clampC(); g_dirty = true; return; }
  if (hit(btnSzColsUp(), x, y)) { ++g_sizeCols; clampC(); g_dirty = true; return; }
  if (hit(btnSzCancel(), x, y)) { g_modal = Modal::None; g_dirty = true; return; }
  if (hit(btnSzOk(), x, y)) {
    if (g_sizeIsNew) { g_edit.newProgram(g_sizeRows, g_sizeCols); g_progFile.clear(); }
    else if (!g_edit.resize(g_sizeRows, g_sizeCols)) {
      g_modal = Modal::None;
      message("Cannot resize", pack::str(pack::kStrResizeBody));
      return;
    }
    run::load(g_edit);
    markLoaded();
    g_curRow = g_curCol = 0;
    g_edRow  = g_edCol  = 0;
    syncViewToProgram();
    g_modal = Modal::None;
    g_tab = Tab::Edit;
    g_dirty = true;
  }
}

void openSizeDialog() {
  g_sizeIsNew = false;
  g_sizeRows = g_edit.rows();
  g_sizeCols = g_edit.cols();
  g_modal = Modal::Size;
  g_dirty = true;
}

// Back to a plain IRCIS interpreter. Shared by the SYS tile and by the
// console's `lock`, so the two cannot drift apart.
void relock() {
  Store::setUnlocked(false);
  g_wasUnlocked = false;
  // Day is the palette for reading an IRCIS program; unlocking swaps to
  // night, so locking swaps back.
  Store::setDayMode(true);
  theme::setDay(true);
  // The packed program is no longer listed, so do not leave it loaded.
  g_edit.loadProgram(1);
  run::load(g_edit);
  markLoaded();
  syncViewToProgram();
  g_curRow = g_curCol = 0;
  g_edRow = g_edCol = 0;
  g_tab = Tab::Run;
  g_dirty = true;
}

void openRenameDialog() {
  openPicker("Program name", "", kKbText, g_edit.programName(), 24,
             [](const std::string& v) { g_edit.setProgramName(v); g_dirty = true; },
             kKbSplit);
}

void handleProgEditTouch(int x, int y) {
  // The status bar's two controls.
  if (hit(btnEdName(), x, y)) { openRenameDialog(); return; }
  if (hit(btnEdSize(), x, y)) { openSizeDialog();   return; }
  if (hit(btnEdZoom(), x, y)) { g_edZoom = !g_edZoom; g_dirty = true; return; }
  if (hit(btnEdSave(), x, y)) { saveCurrentProgram(); return; }
  // The cursor's arrows sit on top of the grid, so they are tested first.
  if (g_edZoom) {
    if (g_curRow > 0 && hit(edArrow(0, -1), x, y))                { --g_curRow; g_dirty = true; return; }
    if (g_curRow < g_edit.rows() - 1 && hit(edArrow(0, 1), x, y)) { ++g_curRow; g_dirty = true; return; }
    if (g_curCol > 0 && hit(edArrow(-1, 0), x, y))                { --g_curCol; g_dirty = true; return; }
    if (g_curCol < g_edit.cols() - 1 && hit(edArrow(1, 0), x, y)) { ++g_curCol; g_dirty = true; return; }
  }
  // the grid: tap a cell to put the cursor there
  if (y >= edGridY() && y < edGridY() + edRows() * edCellH() && x >= edGridX()) {
    int r = g_edRow + (y - edGridY()) / edCellH();
    int c = g_edCol + (x - edGridX()) / edCellW();
    if (r >= 0 && r < g_edit.rows() && c >= 0 && c < g_edit.cols()) {
      g_curRow = r; g_curCol = c; g_dirty = true;
    }
    return;
  }
  // character keys: type and advance, wrapping to the next row
  if (y >= kEdKeyY && y < kEdKeyY + kEdKeyRows * kEdKeyH) {
    int r = (y - kEdKeyY) / kEdKeyH;
    int c = x / kEdKeyW;
    char k = edKeyChar(r * kEdKeyCols + c);
    if (!k) return;
    if (g_edit.setCell(g_curRow, g_curCol, k)) {
      markEdited();
      if (g_curCol < g_edit.cols() - 1) ++g_curCol;
      else if (g_curRow < g_edit.rows() - 1) { g_curCol = 0; ++g_curRow; }
    }
    g_dirty = true;
  }
}

void drawEdit() {
  gfx.fillRect(0, kBodyY, kScreenW, kBodyH, theme::bg);
  int first = editPageFirst(g_editPage);
  for (int i = 0; i < editPageCount(g_editPage); ++i) {
    int idx = first + i;
    if (idx >= prog::slotCount()) break;
    const prog::Slot& s = prog::slot(idx);
    int y = kBodyY + 2 + i * kEditRowH;
    bool mod = g_edit.slotModified(idx);
    gfx.fillRoundRect(2, y, kScreenW - 4, kEditRowH - 2, 2, theme::panel);
    clabel(6, y + (kEditRowH - 2 - kContentH) / 2, s.label.c_str(),
           mod ? theme::edited : theme::text, theme::panel);

    std::string val = g_edit.slotValue(idx);
    useContentFont();
    gfx.setTextDatum(textdatum_t::middle_right);
    gfx.setTextColor(mod ? theme::edited : theme::accent, theme::panel);
    gfx.drawString(val.c_str(), kScreenW - 34, y + kEditRowH / 2 - 1);
    gfx.setTextDatum(textdatum_t::top_left);

    if (mod) {
      Btn rb{ kScreenW - 30, y + 2, 26, kEditRowH - 6, "UNDO", theme::warn, theme::panel };
      drawBtn(rb);
    }
  }
  char page[32];
  snprintf(page, sizeof(page), "page %d/2", g_editPage + 1);
  label(102, kTabY - 17, page, theme::dim);
  drawBtn(btnEditPrev(), false, g_editPage > 0);
  drawBtn(btnEditNext(), false, g_editPage == 0);
  drawBtn(btnEditRevert(), false, g_edit.modifiedCells() > 0);
}

// ---------------------------------------------------------------------------
// SETS: pick a combination of parameters and write it into the grid
// ---------------------------------------------------------------------------

// The columns, what each one writes and the entries it starts with are all
// described by the pack. Anything else is added by the user with ADD and kept
// in the board's own memory.
int setKinds() { return pack::setGroupCount(); }
int builtinCount(int kind) { return (int)pack::setGroup(kind).builtin.size(); }

std::string setEntry(int kind, int i) {
  const pack::SetGroup& g = pack::setGroup(kind);
  if (i < (int)g.builtin.size()) return g.builtin[i];
  return Store::customSet(kind, i - (int)g.builtin.size());
}
int setEntryCount(int kind) { return builtinCount(kind) + Store::customSetCount(kind); }
bool setIsCustom(int kind, int i) { return i >= builtinCount(kind); }

// A single-parameter column is shown without its padding, so what the user
// sees is what they would type.
std::string setSlotValue(const prog::Program& p, const pack::SetGroup& g, std::size_t k) {
  std::string v = p.slotValue(prog::slotIndex(g.slots[k].c_str()));
  if (g.slots.size() == 1)
    while (!v.empty() && v.back() == '.') v.pop_back();
  return v;
}

// Split a space-separated entry into its fields.
std::vector<std::string> setFields(const std::string& v) {
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < v.size()) {
    std::size_t j = v.find(' ', i);
    if (j == std::string::npos) j = v.size();
    if (j > i) out.push_back(v.substr(i, j - i));
    i = j + 1;
  }
  return out;
}

constexpr int kSetRowH = kContentH + 6;
// Sized to what each column actually holds, so all four fit across.
int setColW(int kind) { return pack::setGroup(kind).cells * kContentW + 6; }
int setColX(int kind) {
  int x = 4;
  for (int k = 0; k < kind; ++k) x += setColW(k) + 4;
  return x;
}
Btn btnSetRow(int kind, int i) {
  return { setColX(kind), kBodyY + kContentH + 2 + i * kSetRowH, setColW(kind), kSetRowH - 3, "" };
}
int setMaxRows() {
  return (kTabY - kContentH - 12 - (kBodyY + kContentH + 2)) / kSetRowH;
}
// Directly under that column's own entries, rather than on a shared row at the
// bottom: which column an ADD belongs to should not need working out.
Btn btnSetAdd(int kind) {
  int n = setEntryCount(kind);
  const int cap = setMaxRows();
  if (n > cap) n = cap;
  return { setColX(kind), kBodyY + kContentH + 2 + n * kSetRowH,
           setColW(kind), kContentH + 4, "ADD" };
}

// Does this entry match what is in the grid right now?
bool setActive(int kind, int i) {
  const pack::SetGroup& g = pack::setGroup(kind);
  std::vector<std::string> f = setFields(setEntry(kind, i));
  if (g.isCount) return f.size() == 1 && g_edit.countValue() == atoi(f[0].c_str());
  if (f.size() != g.slots.size()) return false;
  for (std::size_t k = 0; k < g.slots.size(); ++k)
    if (setSlotValue(g_edit, g, k) != f[k]) return false;
  return true;
}

void applySet(int kind, int i) {
  const pack::SetGroup& g = pack::setGroup(kind);
  std::vector<std::string> f = setFields(setEntry(kind, i));
  if (g.isCount) {
    if (f.size() == 1) g_edit.setCountValue(atoi(f[0].c_str()));
  }
  else if (f.size() == g.slots.size()) {
    for (std::size_t k = 0; k < g.slots.size(); ++k)
      g_edit.setSlotValue(prog::slotIndex(g.slots[k].c_str()), f[k]);
  }
  markEdited();
}

// "ADD CURRENT" stores whatever is in the grid now, so a set is built by
// editing the parameters and then keeping them.
std::string currentSetValue(int kind) {
  const pack::SetGroup& g = pack::setGroup(kind);
  if (g.isCount) return std::to_string(g_edit.countValue());
  std::string v;
  for (std::size_t k = 0; k < g.slots.size(); ++k) {
    if (k) v += ' ';
    v += setSlotValue(g_edit, g, k);
  }
  return v;
}

// Whether what is in the grid now is already one of this column's entries.
// ADD is greyed out when it is, rather than offering to add a duplicate and
// then refusing.
bool currentSetKept(int kind) {
  const std::string v = currentSetValue(kind);
  for (int i = 0; i < setEntryCount(kind); ++i)
    if (setEntry(kind, i) == v) return true;
  return false;
}

void addCurrentSet(int kind) {
  const std::string v = currentSetValue(kind);
  for (int i = 0; i < setEntryCount(kind); ++i)
    if (setEntry(kind, i) == v) return;          // ADD is already greyed out
  if (!Store::addCustomSet(kind, v)) message("Full", "Nine custom entries is the limit.");
  g_dirty = true;
}

void drawKeys() {
  gfx.fillRect(0, kBodyY, kScreenW, kBodyH, theme::bg);

  int maxRows = setMaxRows();
  for (int kind = 0; kind < setKinds(); ++kind) {
    clabel(setColX(kind), kBodyY + 1, pack::setGroup(kind).head.c_str(), theme::dim);
    int n = setEntryCount(kind);
    for (int i = 0; i < n && i < maxRows; ++i) {
      Btn b = btnSetRow(kind, i);
      bool on = setActive(kind, i);
      bool custom = setIsCustom(kind, i);
      gfx.fillRoundRect(b.x, b.y, b.w, b.h, 3, on ? theme::good : theme::panel);
      if (custom && !on) gfx.drawRoundRect(b.x, b.y, b.w, b.h, 3, theme::edited);
      clabel(b.x + 5, b.y + (b.h - kContentH) / 2, setEntry(kind, i).c_str(),
             on ? theme::bg : (custom ? theme::edited : theme::text),
             on ? theme::good : theme::panel);
    }
    drawBtn(btnSetAdd(kind), false, !currentSetKept(kind));
  }
}

void handleKeysTouch(int x, int y) {
  for (int kind = 0; kind < setKinds(); ++kind) {
    if (hit(btnSetAdd(kind), x, y)) {
      if (!currentSetKept(kind)) addCurrentSet(kind);
      return;
    }
    for (int i = 0; i < setEntryCount(kind); ++i) {
      if (hit(btnSetRow(kind, i), x, y)) { applySet(kind, i); return; }
    }
  }
}

// A long press on a custom entry offers to delete it.
void handleKeysLongPress(int x, int y) {
  for (int kind = 0; kind < setKinds(); ++kind) {
    for (int i = 0; i < setEntryCount(kind); ++i) {
      if (!hit(btnSetRow(kind, i), x, y)) continue;
      if (!setIsCustom(kind, i)) return;          // built-ins cannot be removed
      int idx = i - builtinCount(kind);
      std::string label = setEntry(kind, i);
      confirm("Delete this entry?", label,
              [kind, idx] { Store::deleteCustomSet(kind, idx); });
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// OUT: what the run produced, and the settings that produced it
// ---------------------------------------------------------------------------

int g_outLines = 8;
int g_outTotal = 1;
// First visible line on the OUT tab. The device shows the whole output a
// screen at a time rather than sending you to the serial console for the rest.
int g_outTop = 0;

constexpr int kOutHeaderH = 3 * kContentH + 4;
constexpr int kOutBtnW = 74;
constexpr int kOutBtnH = kContentH + 2;
Btn btnOutSd()     { return { kScreenW - kOutBtnW - 4, kBodyY + 1, kOutBtnW, kOutBtnH, "SAVE SD" }; }
Btn btnOutColour() { return { kScreenW - kOutBtnW - 4, kBodyY + 3 + kOutBtnH, kOutBtnW, kOutBtnH, "COLOUR", theme::good }; }
// Only drawn when the output is longer than the page, on a footer row of
// their own so they cannot land on top of a line of output.
constexpr int kOutFootH = kContentH + 4;
Btn btnOutPgUp()   { return { kScreenW - 62, kTabY - kOutFootH, 28, kContentH + 2, "^" }; }
Btn btnOutPgDn()   { return { kScreenW - 32, kTabY - kOutFootH, 28, kContentH + 2, "v" }; }

struct LineSpan { uint32_t start; uint16_t len; };

std::vector<LineSpan> wrapSpans(const std::string& text, int cols) {
  std::vector<LineSpan> spans;
  uint32_t start = 0;
  uint16_t len = 0;
  for (uint32_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') { spans.push_back({start, len}); start = i + 1; len = 0; continue; }
    ++len;
    if ((int)len >= cols) { spans.push_back({start, len}); start = i + 1; len = 0; }
  }
  if (len) spans.push_back({start, len});
  return spans;
}

// Chunk colour: red if the value came out negative, orange at six characters,
// green at five or fewer.
uint16_t chunkColour(const std::string& text, const run::Chunk& c) {
  if (c.len == 0) return theme::text;
  if (text[c.start] == '-') return theme::bad;
  return c.len >= 6 ? theme::warn : theme::good;
}

// Scratch for reading back the grid the interpreter is running. File scope
// deliberately: 3 KB of it will not fit on the loop task's stack.
prog::Program g_ranGrid;

void drawOutHeader() {
  run::loadedGridInto(g_ranGrid);
  if (!g_ranGrid.isPacked() || setKinds() == 0) {
    // The parameter columns only mean anything for a packed program. For any
    // other one, name the program and say how the run went -- which is what
    // the dialog that used to appear over this page was for.
    char what[64];
    snprintf(what, sizeof(what), "%s   %d x %d",
             g_ranGrid.programName(), g_ranGrid.rows(), g_ranGrid.cols());
    clabel(4, kBodyY + 1, what, theme::accent);

    const run::Snapshot snap = run::snapshot();
    if (snap.step > 0) {
      char how[80];
      snprintf(how, sizeof(how), "%u steps   %u runner%s   %u died   %u ms",
               (unsigned)snap.step,
               (unsigned)snap.runnersCreated, snap.runnersCreated == 1 ? "" : "s",
               (unsigned)snap.deaths, (unsigned)snap.elapsedMs);
      clabel(4, kBodyY + 1 + kContentH, how, theme::dim);
    }
    gfx.drawFastHLine(0, kBodyY + kOutHeaderH - 2, kScreenW, theme::line);
    return;
  }

  // One row per parameter group, packing a group onto the current row when it
  // still fits beside what is already there. Narrow groups end up sharing a
  // row, which is what keeps the header down to three lines.
  const int avail = kScreenW - kOutBtnW - 12;
  int x = 4, y = kBodyY + 1;
  for (int kind = 0; kind < setKinds(); ++kind) {
    const pack::SetGroup& g = pack::setGroup(kind);
    std::string v;
    bool changed = false;
    for (std::size_t k = 0; k < g.slots.size(); ++k)
      if (g_ranGrid.slotModified(prog::slotIndex(g.slots[k].c_str()))) changed = true;
    if (g.isCount) {
      v = std::to_string(g_ranGrid.countValue());
    }
    else {
      for (std::size_t k = 0; k < g.slots.size(); ++k) {
        if (k) v += ' ';
        v += setSlotValue(g_ranGrid, g, k);
      }
    }
    const int w = (int)(g.head.size() + 1 + v.size()) * kContentW;
    if (x > 4 && x + w > avail) { x = 4; y += kContentH; }
    clabel(x, y, g.head.c_str(), theme::accent);
    clabel(x + (int)(g.head.size() + 1) * kContentW, y, v.c_str(),
           changed ? theme::edited : theme::text);
    x += w + 2 * kContentW;
  }

  gfx.drawFastHLine(0, kBodyY + kOutHeaderH - 2, kScreenW, theme::line);
}

void drawOut() {
  gfx.fillRect(0, kBodyY, kScreenW, kBodyH, theme::bg);
  drawOutHeader();

  drawBtn(btnOutSd(), false, plat::sdPresent());
  // Colouring by chunk length and sign describes a packed program's output.
  // For any other program the output is just text.
  if (g_ranGrid.isPacked()) drawBtn(btnOutColour(), Store::outputColour());

  int top = kBodyY + kOutHeaderH;
  int cw = kContentBigW, ch = kContentBigH;
  int cols = (kScreenW - 8) / cw;

  std::string text = run::output();
  std::vector<LineSpan> spans = wrapSpans(text, cols);
  g_outTotal = (int)spans.size();
  if (spans.empty()) {
    g_outLines = (kTabY - 2 - top) / ch;
    clabel(4, top, "(no output yet)", theme::dim, theme::bg, true);
    return;
  }

  // The footer row costs a line, so only give it up when the output actually
  // needs scrolling: measure without it, and measure again if it does.
  auto linesFor = [&](int reserve) {
    int n = (kTabY - 2 - reserve - top) / ch;
    return n < 1 ? 1 : n;
  };
  g_outLines = linesFor(0);
  const bool scrolls = g_outTotal > g_outLines;
  if (scrolls) g_outLines = linesFor(kOutFootH);

  std::vector<run::Chunk> chunks =
      (g_ranGrid.isPacked() && Store::outputColour()) ? run::chunks() : std::vector<run::Chunk>();

  // Clamp here rather than at the tap: the output grows while a run is going,
  // so what was the last page a moment ago may not be any more.
  const int maxTop = g_outTotal > g_outLines ? g_outTotal - g_outLines : 0;
  if (g_outTop > maxTop) g_outTop = maxTop;
  if (g_outTop < 0) g_outTop = 0;

  for (int i = 0; i < g_outLines && g_outTop + i < g_outTotal; ++i) {
    const LineSpan& ln = spans[g_outTop + i];
    int y = top + i * ch;
    if (chunks.empty()) {
      clabel(4, y, text.substr(ln.start, ln.len).c_str(), theme::text, theme::bg, true);
      continue;
    }
    // Walk the chunks overlapping this line and draw each piece in its colour.
    uint32_t pos = ln.start, endPos = ln.start + ln.len;
    while (pos < endPos) {
      const run::Chunk* hit = nullptr;
      for (const auto& c : chunks)
        if (pos >= c.start && pos < (uint32_t)c.start + c.len) { hit = &c; break; }
      uint32_t stop = endPos;
      uint16_t col = theme::text;
      if (hit) {
        stop = (uint32_t)hit->start + hit->len;
        if (stop > endPos) stop = endPos;
        col = chunkColour(text, *hit);
      }
      else {
        for (const auto& c : chunks)
          if ((uint32_t)c.start > pos && (uint32_t)c.start < stop) stop = c.start;
      }
      clabel(4 + (int)(pos - ln.start) * cw, y, text.substr(pos, stop - pos).c_str(),
             col, theme::bg, true);
      pos = stop;
    }
  }

  if (maxTop > 0) {
    drawBtn(btnOutPgUp(), false, g_outTop > 0);
    drawBtn(btnOutPgDn(), false, g_outTop < maxTop);
    char where[64];
    const int last = g_outTop + g_outLines < g_outTotal ? g_outTop + g_outLines : g_outTotal;
    snprintf(where, sizeof(where), "lines %d-%d of %d", g_outTop + 1, last, g_outTotal);
    clabel(4, kTabY - kOutFootH + 2, where, theme::dim);
  }
}

constexpr int kSaveRowH = kContentH + 8;
Btn btnSaveSlot(int i)  { return { 4, kBodyY + 4 + i * kSaveRowH, kScreenW - 130, kSaveRowH - 4, "" }; }
Btn btnSaveWrite(int i) { return { kScreenW - 122, kBodyY + 4 + i * kSaveRowH, 56, kSaveRowH - 4, "SAVE", theme::good }; }
Btn btnSaveDel(int i)   { return { kScreenW -  62, kBodyY + 4 + i * kSaveRowH, 56, kSaveRowH - 4, "DEL", theme::bad }; }

// ---------------------------------------------------------------------------
// SAVE while locked: program files on the SD card, one row per line, so they
// can be moved on and off with any computer.
// ---------------------------------------------------------------------------

std::vector<std::string> g_progFiles;
bool g_progFilesRead = false;

// Split a program file into rows, padded to the longest, bounded by the
// model's limits. False when there is nothing usable in it.
bool splitProgramText(const std::string& text,
                      std::vector<std::string>& rows, std::size_t& wide) {
  rows.clear();
  std::string cur;
  for (char c : text) {
    if (c == '\n') { rows.push_back(cur); cur.clear(); }
    else if (c != '\r') cur.push_back(c);
  }
  if (!cur.empty()) rows.push_back(cur);
  while (!rows.empty() && rows.back().empty()) rows.pop_back();
  if (rows.empty()) return false;
  wide = 0;
  for (const std::string& r : rows) wide = r.size() > wide ? r.size() : wide;
  if (wide == 0) return false;
  return (int)rows.size() <= prog::kMaxRows && (int)wide <= prog::kMaxCols;
}

void afterProgramChange() {
  run::load(g_edit);
  markLoaded();
  g_curRow = g_curCol = 0;
  g_edRow = g_edCol = 0;
  syncViewToProgram();
}

// A different program: it brings its own identity and its own blank baseline.
bool loadProgramText(const std::string& text) {
  std::vector<std::string> rows;
  std::size_t w = 0;
  if (!splitProgramText(text, rows, w)) return false;
  g_edit.newProgram((int)rows.size(), (int)w);
  g_progFile.clear();
  for (std::size_t r = 0; r < rows.size(); ++r)
    for (std::size_t c = 0; c < w; ++c)
      g_edit.setCell((int)r, (int)c, c < rows[r].size() ? rows[r][c] : '.');
  // The text it arrived as is its baseline, so it starts with no edits and
  // REVERT means "back to the file", not "back to blank".
  g_edit.adoptBaseline();
  afterProgramChange();
  return true;
}

// The SAME program, edited elsewhere -- the web editor. Writing the cells into
// the loaded program keeps its baseline, so only the characters that actually
// differ come back marked as edited. Loading it as a new program instead would
// give it a blank baseline and light the whole grid up as changed, which is
// what it used to do. A different shape cannot be an edit, so that still
// becomes a new program.
bool applyProgramText(const std::string& text) {
  std::vector<std::string> rows;
  std::size_t w = 0;
  if (!splitProgramText(text, rows, w)) return false;
  if ((int)rows.size() != g_edit.rows() || (int)w != g_edit.cols())
    return loadProgramText(text);
  for (std::size_t r = 0; r < rows.size(); ++r)
    for (std::size_t c = 0; c < w; ++c)
      g_edit.setCell((int)r, (int)c, c < rows[r].size() ? rows[r][c] : '.');
  afterProgramChange();
  return true;
}

// Whatever of a program's name a filesystem will take.
std::string fileNameFor(const char* name) {
  std::string out;
  for (const char* n = name; *n && out.size() < 24; ++n)
    if (std::isalnum((unsigned char)*n) || *n == '-' || *n == '_') out.push_back(*n);
  return out;
}

void refreshProgFiles();
void promptSaveAs();

// SAVE writes the loaded program to the card under the name in the status bar,
// which is editable right beside it, so what it writes is never a surprise.
void saveCurrentProgram() {
  if (!plat::sdPresent()) {
    message("No card", "Insert an SD card to save programs.");
    return;
  }
  const std::string name = fileNameFor(g_edit.programName());
  if (name.empty()) { promptSaveAs(); return; }
  auto write = [name] {
    if (!plat::progWrite(name, g_edit.text())) {
      message("Save failed", "Could not write to the card.");
      return;
    }
    g_edit.setProgramName(name);
    g_progFile = name;
    refreshProgFiles();
    message("Saved", name + ".txt");
  };
  // Overwriting the file this program came from is what you meant; overwriting
  // somebody else's is worth one question.
  std::string existing;
  if (name != g_progFile && plat::progRead(name, existing))
    confirm("Replace " + name + ".txt?", "Another program of that name is "
            "already on the card.", write);
  else
    write();
}

void refreshProgFiles() {
  g_progFiles.clear();
  g_progFilesRead = plat::progList(g_progFiles);
}

constexpr int kFileRowH = 26;
constexpr int kFileRows = 7;
Btn fileTile(int i) {
  return { 4, kBodyY + 4 + i * kFileRowH, kScreenW - 40, 22, "", theme::text, theme::panel };
}
Btn fileDel(int i) {
  return { kScreenW - 32, kBodyY + 4 + i * kFileRowH, 28, 22, "X", theme::bad, theme::panel };
}
Btn btnFileSaveAs() {
  return { 4, kTabY - 30, 150, 26, "SAVE AS...", theme::bg, theme::good };
}

void drawFiles() {
  gfx.fillRect(0, kBodyY, kScreenW, kBodyH, theme::bg);
  if (!plat::sdPresent()) {
    clabel(6, kBodyY + 10, "No SD card.", theme::warn);
    return;
  }
  int n = (int)g_progFiles.size();
  if (n > kFileRows) n = kFileRows;
  for (int i = 0; i < n; ++i) {
    Btn b = fileTile(i);
    gfx.fillRoundRect(b.x, b.y, b.w, b.h, 3, theme::panel);
    gfx.drawRoundRect(b.x, b.y, b.w, b.h, 3, theme::line);
    clabel(10, b.y + (b.h - kContentH) / 2 + 1, g_progFiles[i].c_str(), theme::text, theme::panel);
    drawBtn(fileDel(i));
  }
  if (g_progFiles.empty())
    clabel(6, kBodyY + 10, "No saved programs yet.", theme::dim);
  else if ((int)g_progFiles.size() > kFileRows) {
    char more[48];
    snprintf(more, sizeof(more), "+%d more on the card", (int)g_progFiles.size() - kFileRows);
    clabel(6, kBodyY + 4 + kFileRows * kFileRowH, more, theme::dim);
  }
  drawBtn(btnFileSaveAs());
}

void promptSaveAs() {
  openPicker("Save program as", "", kKbText, fileNameFor(g_edit.programName()), 24,
             [](const std::string& v) {
               if (v.empty()) return;
               if (!plat::progWrite(v, g_edit.text())) {
                 message("Save failed", "Could not write to the card.");
                 return;
               }
               // The program is now that file; keep the two names together.
               g_edit.setProgramName(v);
               g_progFile = v;
               refreshProgFiles();
               message("Saved", v + ".txt");
             },
             kKbSplit);
}

void handleFilesTouch(int x, int y) {
  if (!plat::sdPresent()) return;
  if (hit(btnFileSaveAs(), x, y)) { promptSaveAs(); return; }
  int n = (int)g_progFiles.size();
  if (n > kFileRows) n = kFileRows;
  for (int i = 0; i < n; ++i) {
    if (hit(fileDel(i), x, y)) {
      std::string name = g_progFiles[i];
      confirm("Delete program?", name, [name] {
        plat::progDelete(name);
        refreshProgFiles();
      });
      return;
    }
    if (hit(fileTile(i), x, y)) {
      std::string text;
      if (!plat::progRead(g_progFiles[i], text)) {
        message("Load failed", "Could not read that file.");
        return;
      }
      if (!loadProgramText(text)) {
        message("Not a program", "That file is empty, or too big for the grid.");
        return;
      }
      g_edit.setProgramName(g_progFiles[i]);
      g_progFile = g_progFiles[i];
      g_tab = Tab::Run;
      g_dirty = true;
      return;
    }
  }
}

void drawSave() {
  gfx.fillRect(0, kBodyY, kScreenW, kBodyH, theme::bg);
  for (int i = 0; i < Store::kMaxPresets; ++i) {
    Store::PresetInfo info = Store::presetInfo(i);
    Btn b = btnSaveSlot(i);
    gfx.fillRoundRect(b.x, b.y, b.w, b.h, 3, theme::panel);
    gfx.drawRoundRect(b.x, b.y, b.w, b.h, 3, theme::line);
    char buf[64];
    if (info.used)
      snprintf(buf, sizeof(buf), "%d  %s  (%d cells)", i + 1, info.name.c_str(), info.changedCells);
    else
      snprintf(buf, sizeof(buf), "%d  -- empty --", i + 1);
    clabel(b.x + 6, b.y + (b.h - kContentH) / 2, buf,
           info.used ? theme::text : theme::dim, theme::panel);
    drawBtn(btnSaveWrite(i));
    drawBtn(btnSaveDel(i), false, info.used);
  }
}

// Two columns, one concern per tile. Everything WiFi lives behind one dialog
// rather than three tiles.
constexpr int kSysRowH = 26;
Btn sysTile(int row, int col, const char* label, uint16_t fg = theme::text) {
  int w = (kScreenW - 12) / 2;
  return { 4 + col * (w + 4), kBodyY + 4 + row * kSysRowH, w, 22, label, fg, theme::panel };
}
// Two columns, grouped: connectivity and appearance, then diagnostics, then
// what the RUN page does, then the About pages, then the destructive pair.
// Two of the tiles only exist once unlocked, and the rows below them close up
// when they are absent.
Btn btnSysWifi()    { return sysTile(0, 0, "WIFI"); }
Btn btnSysTheme()   { return sysTile(0, 1, "THEME: NIGHT"); }
Btn btnSysSd()      { return sysTile(1, 0, "SD LOG: OFF"); }
Btn btnSysDebug()   { return sysTile(1, 1, "DEBUG"); }
Btn btnSysDump()    { return sysTile(2, 0, "DUMP GRID"); }
Btn btnSysCal()     { return sysTile(2, 1, "RECALIBRATE TOUCH"); }
// Named for what it actually controls rather than "advanced".
Btn btnSysStart()   { return sysTile(3, 0, "START POINT: FIXED"); }
// What the RUN page shows under the program.
Btn btnSysRunView() { return sysTile(3, 1, "RUN VIEW: RUNNERS"); }
// Whether the transport carries its two single-step buttons. There is an odd
// number of tiles either way, so one half-row is empty; it sits here, between
// the settings and the About pages, where it reads as the break between them.
Btn btnSysSteps() { return sysTile(4, 0, "STEP BUTTONS: OFF"); }
// Row 5 onwards depends on the mode.
Btn btnSysInfo()  { return sysTile(5, 0, pack::str(pack::kStrInfoTile), theme::accent); }
Btn btnSysIrcis() { return Store::unlocked() ? sysTile(5, 1, "ABOUT IRCIS", theme::accent)
                                            : sysTile(5, 0, "ABOUT IRCIS", theme::accent); }
Btn btnSysRead()  { return Store::unlocked() ? sysTile(6, 0, "ABOUT THIS DEVICE", theme::accent)
                                            : sysTile(5, 1, "ABOUT THIS DEVICE", theme::accent); }
// Puts the device back to looking like a plain IRCIS interpreter without
// throwing away anything else. Re-entering means setting the WiFi credentials
// again. Only exists once unlocked, for obvious reasons.
Btn btnSysExit()  { return sysTile(6, 1, pack::str(pack::kStrExitTile), theme::warn); }
Btn btnSysReset() { return Store::unlocked() ? sysTile(7, 0, "RESET ALL DATA", theme::bad)
                                            : sysTile(6, 0, "RESET ALL DATA", theme::bad); }
Btn btnSysOff()   { return Store::unlocked() ? sysTile(7, 1, "OFF", theme::warn)
                                            : sysTile(6, 1, "OFF", theme::warn); }

// ---------------------------------------------------------------------------
// PROG: pick a program to load. While locked this lists only the bundled IRCIS
// examples; unlocked, the packed program joins them at the top.
// ---------------------------------------------------------------------------

// The first program the list will show. The packed one is index 0, so locking
// simply starts the list one further along.
int firstProgram() { return Store::unlocked() ? 0 : 1; }

constexpr int kProgRowH = 26;
Btn progTile(int i) {
  return { 4, kBodyY + 4 + i * kProgRowH, kScreenW - 8, 22, "", theme::text, theme::panel };
}

void drawProg() {
  gfx.fillRect(0, kBodyY, kScreenW, kBodyH, theme::bg);
  int shown = 0;
  for (int i = firstProgram(); i < prog::programCount(); ++i, ++shown) {
    const prog::ProgramDef& d = prog::programAt(i);
    bool on = (g_edit.programIndex() == i);
    Btn b = progTile(shown);
    b.fg = d.packed ? theme::accent : theme::text;
    gfx.fillRoundRect(b.x, b.y, b.w, b.h, 3, on ? theme::accent : theme::panel);
    gfx.drawRoundRect(b.x, b.y, b.w, b.h, 3, theme::line);
    uint16_t fg = on ? theme::bg : b.fg;
    uint16_t bg = on ? theme::accent : theme::panel;
    clabel(10, b.y + (b.h - kContentH) / 2 + 1, d.name, fg, bg);
    char dim[32];
    snprintf(dim, sizeof(dim), "%d x %d", d.rows_n, d.cols_n);
    gfx.setFont(&fonts::Font0);
    gfx.setTextDatum(textdatum_t::middle_right);
    gfx.setTextColor(on ? theme::bg : theme::dim, bg);
    gfx.drawString(dim, kScreenW - 14, b.y + b.h / 2);
    gfx.setTextDatum(textdatum_t::top_left);
  }
  // A blank grid of your own, at whatever size you choose.
  Btn nb = progTile(shown);
  gfx.fillRoundRect(nb.x, nb.y, nb.w, nb.h, 3, theme::panel);
  gfx.drawRoundRect(nb.x, nb.y, nb.w, nb.h, 3, theme::good);
  clabel(10, nb.y + (nb.h - kContentH) / 2 + 1, "New program...", theme::good, theme::panel);
  gfx.setFont(&fonts::Font0);
  gfx.setTextDatum(textdatum_t::middle_right);
  gfx.setTextColor(theme::dim, theme::panel);
  gfx.drawString("choose a size", kScreenW - 14, nb.y + nb.h / 2);
  gfx.setTextDatum(textdatum_t::top_left);

}

void handleProgTouch(int x, int y) {
  int shown = 0;
  int total = prog::programCount() - firstProgram();
  if (hit(progTile(total), x, y)) {      // the NEW PROGRAM row, always last
    g_sizeIsNew = true;
    g_sizeRows = 11;
    g_sizeCols = 40;
    g_modal = Modal::Size;
    g_dirty = true;
    return;
  }
  for (int i = firstProgram(); i < prog::programCount(); ++i, ++shown) {
    if (hit(progTile(shown), x, y)) {
      if (g_edit.programIndex() != i) {
        g_edit.loadProgram(i);
        g_progFile.clear();
        run::load(g_edit);
        markLoaded();
        syncViewToProgram();
        g_follow = true;
      }
      g_tab = Tab::Run;
      g_dirty = true;
      return;
    }
  }
}

void drawSys() {
  gfx.fillRect(0, kBodyY, kScreenW, kBodyH, theme::bg);

  Btn w = btnSysWifi();
  std::string wl = std::string("WIFI: ") + (web::running() ? web::ipAddress() : "off");
  w.label = wl.c_str();
  drawBtn(w, web::running());

  // A tile is filled when it is set to something OTHER than the default, so
  // the SYS page reads as a list of what has been changed rather than a wall
  // of highlights. Day and Output are the defaults.
  Btn th = btnSysTheme();
  std::string tl = std::string("THEME: ") + (Store::dayMode() ? "DAY" : "NIGHT");
  th.label = tl.c_str();
  drawBtn(th, !Store::dayMode());

  // With no card there is nowhere to log, so the tile reads OFF and is
  // greyed rather than offering a switch that cannot do anything.
  const bool card = plat::sdPresent();
  Btn sd = btnSysSd();
  std::string sl = std::string("SD LOG: ")
                 + ((card && Store::sdLoggingEnabled()) ? "ON" : "OFF");
  sd.label = sl.c_str();
  drawBtn(sd, card && Store::sdLoggingEnabled(), card);

  drawBtn(btnSysDebug());
  drawBtn(btnSysCal());
  drawBtn(btnSysDump());

  Btn rv = btnSysRunView();
  const int rvv = Store::runView();
  std::string rl = std::string("RUN VIEW: ")
                 + (rvv == 0 ? "RUNNERS" : rvv == 1 ? "OUTPUT" : "NONE");
  rv.label = rl.c_str();
  drawBtn(rv, rvv != 2);          // NONE is the default, so it reads plain

  Btn sb = btnSysSteps();
  std::string sbl = std::string("STEP BUTTONS: ")
                  + (Store::stepButtons() ? "ON" : "OFF");
  sb.label = sbl.c_str();
  drawBtn(sb, Store::stepButtons());

  Btn st = btnSysStart();
  std::string stl = std::string("START POINT: ")
                  + (Store::startEditable() ? "FREE" : "FIXED");
  st.label = stl.c_str();
  drawBtn(st, Store::startEditable());

  if (Store::unlocked()) { drawBtn(btnSysInfo()); drawBtn(btnSysExit()); }
  drawBtn(btnSysIrcis());
  drawBtn(btnSysRead());

  drawBtn(btnSysReset());
  drawBtn(btnSysOff());
}

// ---------------------------------------------------------------------------
// modals: the on-screen keyboard and the character inspector
// ---------------------------------------------------------------------------

struct PickerGeom { int cols, rows, kw, kh, x0, y0; int sideCols; };

// When the symbol group is small enough to sit in two columns beside the
// base64 block (16 symbols in 2 x 8), everything fits in eight rows instead of
// eleven -- which makes every key noticeably taller.
constexpr int kPickMainCols = 8;
constexpr int kPickSideCols = 2;
constexpr int kPickSideMax  = kPickSideCols * kPickMainCols;   // 16

PickerGeom pickerGeom() {
  PickerGeom g;
  int n = (int)g_pickerSet.size();
  int sym = g_pickerSplit ? n - (int)g_pickerSplit : 0;
  bool side = g_pickerSplit && sym > 0;

  // The text keyboard is the program keyboard with more characters, so it
  // keeps the same 8 + 2 block and puts the extras in rows underneath rather
  // than reflowing everything: the keys you use most stay where they were.
  int extras = (side && sym > kPickSideMax) ? sym - kPickSideMax : 0;
  if (side && sym > kPickSideMax) { extras = sym - kPickSideMax; side = true; }

  if (n <= 12)          { g.cols = 3; g.sideCols = 0; g.rows = (n + 2) / 3; }
  else if (side)        { g.cols = kPickMainCols + kPickSideCols; g.sideCols = kPickSideCols;
                          g.rows = ((int)g_pickerSplit + kPickMainCols - 1) / kPickMainCols
                                 + (extras + g.cols - 1) / g.cols; }
  else                  { g.cols = kPickMainCols; g.sideCols = 0;
                          g.rows = (n + kPickMainCols - 1) / kPickMainCols; }

  g.kw = (kScreenW - 12) / g.cols;
  int avail = kModalBtnY - 8 - (10 + kContentBigH + kContentH);
  g.kh = avail / (g.rows > 0 ? g.rows : 1);
  if (g.kh > 34) g.kh = 34;
  g.x0 = (kScreenW - g.cols * g.kw) / 2;
  g.y0 = 10 + kContentBigH + kContentH;
  return g;
}

// Where key i sits. With a side group the symbols fill the right-hand columns.
bool pickerCell(int i, const PickerGeom& g, int& r, int& c) {
  const int mainRows = ((int)g_pickerSplit + kPickMainCols - 1) / kPickMainCols;
  if (g.sideCols && (std::size_t)i >= g_pickerSplit) {
    int k = i - (int)g_pickerSplit;
    if (k < kPickSideMax) {                 // the two side columns
      r = k / g.sideCols;
      c = kPickMainCols + (k % g.sideCols);
      return r < g.rows;
    }
    k -= kPickSideMax;                      // extras, in full rows underneath
    r = mainRows + k / g.cols;
    c = k % g.cols;
    return r < g.rows;
  }
  int cols = g.sideCols ? kPickMainCols : g.cols;
  r = i / cols;
  c = i % cols;
  return true;
}

// The first button is labelled for what it will do: CLEAR while the field
// still holds the original, REVERT once it has been changed.
Btn btnPickClear()  { return { modalBtnX(0), kModalBtnY, kModalBtnW, 26, "CLEAR", theme::warn }; }
Btn btnPickBack()   { return { modalBtnX(1), kModalBtnY, kModalBtnW, 26, "DEL", theme::warn }; }
Btn btnPickCancel() { return { modalBtnX(2), kModalBtnY, kModalBtnW, 26, "CANCEL", theme::bad }; }
Btn btnPickOk()     { return { modalBtnX(3), kModalBtnY, kModalBtnW, 26, "OK", theme::bg, theme::good }; }

void drawPicker() {
  gfx.fillScreen(theme::bg);
  clabel(6, 4, g_pickerTitle.c_str(), theme::accent);

  std::string shown = g_pickerValue.empty() ? std::string("_") : g_pickerValue;
  useContentFont(true);
  gfx.setTextDatum(textdatum_t::middle_right);
  gfx.setTextColor(theme::edited, theme::bg);
  gfx.drawString(shown.c_str(), kScreenW - 6, 4 + kContentBigH / 2);
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.setTextSize(1);

  if (g_pickerMax == 1 && g_pickerValue != g_pickerOriginal) {
    // Right-aligned under the character, the way the inspector shows it.
    char was[32];
    snprintf(was, sizeof(was), "was %s", g_pickerOriginal.c_str());
    useContentFont();
    gfx.setTextDatum(textdatum_t::middle_right);
    gfx.setTextColor(theme::warn, theme::bg);
    gfx.drawString(was, kScreenW - 6, 8 + kContentBigH + kContentH / 2);
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.setTextSize(1);
  }
  else {
    clabel(6, 6 + kContentBigH, g_pickerHint.c_str(), theme::dim);
  }

  PickerGeom g = pickerGeom();
  for (int i = 0; i < (int)g_pickerSet.size(); ++i) {
    if (g_pickerSet[i] == '\x01') continue;          // spacer, not a key
    int r, c;
    if (!pickerCell(i, g, r, c)) continue;
    int x = g.x0 + c * g.kw, y = g.y0 + r * g.kh;
    bool symbol = g_pickerSplit && (std::size_t)i >= g_pickerSplit;
    gfx.fillRect(x + 1, y + 1, g.kw - 2, g.kh - 2, symbol ? theme::line : theme::panel);
    useContentFont();
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setTextColor(symbol ? theme::accent : theme::text, symbol ? theme::line : theme::panel);
    char ch[2] = { g_pickerSet[i] == ' ' ? '_' : g_pickerSet[i], 0 };
    gfx.drawString(ch, x + g.kw / 2, y + g.kh / 2);
    gfx.setTextDatum(textdatum_t::top_left);
  }
  gfx.setTextSize(1);

  Btn clr = btnPickClear();
  clr.label = (g_pickerValue != g_pickerOriginal) ? "REVERT" : "CLEAR";
  drawBtn(clr);
  drawBtn(btnPickBack());
  drawBtn(btnPickCancel());
  drawBtn(btnPickOk());
}

void openPicker(const std::string& title, const std::string& hint,
                const std::string& set, const std::string& initial,
                std::size_t maxLen, std::function<void(const std::string&)> commit,
                std::size_t split) {
  g_pickerSplit = split;
  g_pickerTitle = title;
  g_pickerHint = hint;
  g_pickerSet = set;
  g_pickerValue = initial;
  g_pickerOriginal = initial;
  g_pickerMax = maxLen;
  g_pickerCommit = commit;
  g_modal = Modal::Picker;
  g_dirty = true;
}

// ---------------------------------------------------------------------------
// CHARACTER INSPECTOR
//
// A zoomed window on the grid centred on the selected character, with the
// navigation over it: the four chevrons are the cells next to the selection,
// so moving scrolls the grid behind rather than a cursor across it.
// ---------------------------------------------------------------------------

constexpr int kInsCellW = 40;
constexpr int kInsCellH = 32;
constexpr int kInsCols  = kScreenW / kInsCellW;
constexpr int kInsRows  = 6;
constexpr int kInsX     = (kScreenW - kInsCols * kInsCellW) / 2;
// The focus is deliberately left of centre and high in the window: what you
// want to see is where the runner is GOING, which is mostly to the right and
// below, so the space is better spent there than behind it.
constexpr int kInsMidC  = 4;
constexpr int kInsMidR  = 1;

// One header line shorter than it was, which buys a sixth grid row.
int insTop() { return 3 * kContentH + 8; }
int insCellX(int c) { return kInsX + c * kInsCellW; }
int insCellY(int r) { return insTop() + r * kInsCellH; }

// Top-left of the window, in grid coordinates. It sits at a fixed offset from
// the cursor until an edge of the program comes into view, and is then pinned
// to that edge -- so the panel always shows as much of the program as it can,
// and it is the cursor that moves inside the window instead.
int insOrigin(int cursor, int extent, int window, int mid) {
  const int max = extent - window;
  if (max <= 0) return 0;                 // the whole of it fits
  int o = cursor - mid;
  if (o > max) o = max;
  if (o < 0) o = 0;
  return o;
}
int insOriginRow() { return insOrigin(g_cellRow, g_edit.rows(), kInsRows, kInsMidR); }
int insOriginCol() { return insOrigin(g_cellCol, g_edit.cols(), kInsCols, kInsMidC); }
// Where the cursor sits inside the window.
int insCurR() { return g_cellRow - insOriginRow(); }
int insCurC() { return g_cellCol - insOriginCol(); }

Btn btnCellUp()    { return { insCellX(insCurC()),     insCellY(insCurR() - 1), kInsCellW, kInsCellH, "" }; }
Btn btnCellDown()  { return { insCellX(insCurC()),     insCellY(insCurR() + 1), kInsCellW, kInsCellH, "" }; }
Btn btnCellLeft()  { return { insCellX(insCurC() - 1), insCellY(insCurR()),     kInsCellW, kInsCellH, "" }; }
Btn btnCellRight() { return { insCellX(insCurC() + 1), insCellY(insCurR()),     kInsCellW, kInsCellH, "" }; }
Btn btnCellSet()   { return { modalBtnX(0), kModalBtnY, kModalBtnW, 26, "EDIT CHAR", theme::bg, theme::good }; }
Btn btnCellRevert(){ return { modalBtnX(1), kModalBtnY, kModalBtnW, 26, "REVERT", theme::warn }; }
Btn btnCellStart() { return { modalBtnX(2), kModalBtnY, kModalBtnW, 26, "START", theme::edited }; }
Btn btnCellClose() { return { modalBtnX(3), kModalBtnY, kModalBtnW, 26, "CLOSE", theme::bad }; }

// Small chevrons pushed to the outer edge of their cell, so the character in
// the middle stays readable behind them.
void drawChevron(const Btn& b, int dx, int dy) {
  int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
  uint16_t c = theme::accent;
  if (dy < 0)      { int t = b.y + 3;       gfx.fillTriangle(cx - 5, t + 5, cx + 5, t + 5, cx, t, c); }
  else if (dy > 0) { int t = b.y + b.h - 3; gfx.fillTriangle(cx - 5, t - 5, cx + 5, t - 5, cx, t, c); }
  else if (dx < 0) { int l = b.x + 3;       gfx.fillTriangle(l + 5, cy - 5, l + 5, cy + 5, l, cy, c); }
  else             { int r = b.x + b.w - 3; gfx.fillTriangle(r - 5, cy - 5, r - 5, cy + 5, r, cy, c); }
}

void drawCellModal() {
  gfx.fillScreen(theme::bg);
  gfx.setFont(&fonts::Font2);
  gfx.setTextColor(theme::accent, theme::bg);
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.drawString("CHARACTER INSPECTOR", 6, 4);

  char buf[80];
  int y = 28;
  char cur = g_edit.cell(g_cellRow, g_cellCol);
  char orig = g_edit.baselineCell(g_cellRow, g_cellCol);
  int slot = slotAtCell(g_cellRow, g_cellCol);

  // The character itself sits top right, the way the EDIT CHARACTER page
  // shows it, with what it used to be underneath.
  char one[2] = { cur, 0 };
  useContentFont(true);
  gfx.setTextDatum(textdatum_t::middle_right);
  gfx.setTextColor(theme::edited, theme::bg);
  gfx.drawString(one, kScreenW - 8, 6 + kContentBigH / 2);
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.setTextSize(1);
  if (cur != orig) {
    char was[24];
    snprintf(was, sizeof(was), "was %c", orig);
    useContentFont();
    gfx.setTextDatum(textdatum_t::middle_right);
    gfx.setTextColor(theme::warn, theme::bg);
    gfx.drawString(was, kScreenW - 8, 8 + kContentBigH + kContentH / 2);
    gfx.setTextDatum(textdatum_t::top_left);
  }

  snprintf(buf, sizeof(buf), "row %d  col %d", g_cellRow, g_cellCol);
  clabel(6, y, buf, theme::text);
  y += kContentH;
  if (slot >= 0) clabel(6, y, prog::slot(slot).label.c_str(), theme::accent);


  const int oR = insOriginRow(), oC = insOriginCol();
  for (int r = 0; r < kInsRows; ++r) {
    for (int c = 0; c < kInsCols; ++c) {
      int gr = oR + r;
      int gc = oC + c;
      if (gr < 0 || gr >= g_edit.rows() || gc < 0 || gc >= g_edit.cols()) continue;
      int x = insCellX(c), yy = insCellY(r);
      bool centre = (gr == g_cellRow && gc == g_cellCol);

      char ch[2] = { g_edit.cell(gr, gc), 0 };
      bool mod = g_edit.cellModified(gr, gc);
      int sl = slotAtCell(gr, gc);
      // Quiet grey for ordinary cells; a subdued version of their own colour
      // for parameters and edits, so the structure still reads.
      uint16_t fg = mod ? dim565(theme::edited, 1, 2)
                  : sl >= 0 ? dim565(theme::accent, 1, 2)
                  : theme::blank;
      uint16_t bg = theme::bg;
      if (centre) {
        gfx.fillRoundRect(x + 2, yy + 2, kInsCellW - 4, kInsCellH - 4, 4, theme::panel);
        gfx.drawRoundRect(x + 2, yy + 2, kInsCellW - 4, kInsCellH - 4, 4, theme::accent);
        bg = theme::panel;
        fg = mod ? theme::edited : (sl >= 0 ? theme::accent : theme::text);
      }
      useContentFont(true);
      gfx.setTextDatum(textdatum_t::middle_center);
      gfx.setTextColor(fg, bg);
      gfx.drawString(ch, x + kInsCellW / 2, yy + kInsCellH / 2);
      gfx.setTextDatum(textdatum_t::top_left);
    }
  }
  gfx.setTextSize(1);

  if (g_cellRow > 0)             drawChevron(btnCellUp(), 0, -1);
  if (g_cellRow < g_edit.rows() - 1) drawChevron(btnCellDown(), 0, 1);
  if (g_cellCol > 0)             drawChevron(btnCellLeft(), -1, 0);
  if (g_cellCol < g_edit.cols() - 1) drawChevron(btnCellRight(), 1, 0);

  drawBtn(btnCellSet());
  drawBtn(btnCellRevert(), false, cur != orig);
  if (Store::startEditable()) {
    // Pressing START again cycles the direction, so one button sets both.
    Btn stb = btnCellStart();
    bool isStart = (run::startRow() == g_cellRow && run::startCol() == g_cellCol);
    char lbl[16];
    snprintf(lbl, sizeof(lbl), "START %c", isStart ? run::startDir() : 'E');
    stb.label = lbl;
    drawBtn(stb, isStart);
  }
  drawBtn(btnCellClose());
}

// ---------------------------------------------------------------------------
// Paged dialogs: DEBUG (keys / system) and ABOUT (the puzzle)
// ---------------------------------------------------------------------------

int g_dialogPage = 0;

constexpr int kDlgY = 26;
int dlgH() { return kTabY - kDlgY - 8; }
Btn btnDlgPrev()  { return { 20, kDlgY + dlgH() - 32, 70, 26, "<" }; }
Btn btnDlgNext()  { return { 94, kDlgY + dlgH() - 32, 70, 26, ">" }; }
Btn btnDlgClose() { return { kScreenW - 92, kDlgY + dlgH() - 32, 72, 26, "CLOSE", theme::bad }; }

int dlgFrame(const char* title, const char* page, int pages) {
  int w = kScreenW - 24, h = dlgH();
  gfx.fillRect(12, kDlgY, w, h, theme::panel);
  gfx.drawRect(12, kDlgY, w, h, theme::accent);
  gfx.setFont(&fonts::Font2);
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.setTextColor(theme::accent, theme::panel);
  gfx.drawString(title, 20, kDlgY + 5);
  // A one-page dialog has nothing to page through, so it gets neither a
  // counter nor a pair of dead arrows.
  char tag[48];
  if (pages > 1) snprintf(tag, sizeof(tag), "%s  %d/%d", page, g_dialogPage + 1, pages);
  else           snprintf(tag, sizeof(tag), "%s", page);
  gfx.setFont(&fonts::Font0);
  gfx.setTextDatum(textdatum_t::top_right);
  gfx.setTextColor(theme::dim, theme::panel);
  gfx.drawString(tag, kScreenW - 20, kDlgY + 9);
  gfx.setTextDatum(textdatum_t::top_left);
  if (pages > 1) {
    drawBtn(btnDlgPrev(), false, g_dialogPage > 0);
    drawBtn(btnDlgNext(), false, g_dialogPage + 1 < pages);
  }
  drawBtn(btnDlgClose());
  return kDlgY + 26;
}

void drawDebugKeys(int y) {
  std::vector<run::GlobalVar> vars = run::globals();
  auto find = [&vars](const char* n) -> const run::GlobalVar* {
    for (const auto& v : vars) if (!strcmp(v.name, n)) return &v;
    return nullptr;
  };
  if (vars.empty()) { clabel(20, y, pack::str(pack::kStrDebugEmpty), theme::dim, theme::panel); return; }
  const char* names[7] = { "K", "L", "M", "N", "R", "P", "Q" };
  const char* role[7];
  for (int i = 0; i < 7; ++i) role[i] = pack::str(pack::kStrGvRole0 + i);
  for (int i = 0; i < 7; ++i) {
    const run::GlobalVar* v = find(names[i]);
    if (!v) continue;
    if (i == 4) y += 4;
    char buf[64];
    snprintf(buf, sizeof(buf), "&%s %11d %-8s %s", names[i], v->value,
             v->isInt ? ircis::base64_encode_int(v->value).c_str() : "(char)", role[i]);
    clabel(20, y, buf, i < 4 ? theme::accent : theme::text, theme::panel);
    y += kContentH;
  }
}

void drawDebugSystem(int y) {
  run::Snapshot snap = run::snapshot();
  char buf[80];
  unsigned freeNow = (unsigned)plat::freeHeap();
  unsigned biggest = (unsigned)plat::maxAllocHeap();
  bool frag = (freeNow > 8192 && biggest < freeNow / 2);
  snprintf(buf, sizeof(buf), "free heap      %u", freeNow);
  clabel(20, y, buf, theme::dim, theme::panel); y += kContentH;
  snprintf(buf, sizeof(buf), "largest block  %u%s", biggest, frag ? "  FRAGMENTED" : "");
  clabel(20, y, buf, frag ? theme::warn : theme::dim, theme::panel); y += kContentH;
  snprintf(buf, sizeof(buf), "run time       %u ms", (unsigned)snap.elapsedMs);
  clabel(20, y, buf, theme::dim, theme::panel); y += kContentH;
  snprintf(buf, sizeof(buf), "steps          %u", (unsigned)snap.step);
  clabel(20, y, buf, theme::dim, theme::panel); y += kContentH;
  if (snap.elapsedMs > 0) {
    snprintf(buf, sizeof(buf), "rate           %u steps/sec",
             (unsigned)((uint64_t)snap.step * 1000u / snap.elapsedMs));
    clabel(20, y, buf, theme::good, theme::panel); y += kContentH;
  }
  snprintf(buf, sizeof(buf), "runners made   %u", (unsigned)snap.runnersCreated);
  clabel(20, y, buf, theme::dim, theme::panel); y += kContentH;
  snprintf(buf, sizeof(buf), "out-of-bounds %lu   stack UB %lu", snap.oobReads, snap.ubReads);
  clabel(20, y, buf, (snap.oobReads || snap.ubReads) ? theme::bad : theme::good, theme::panel);
}

void drawDebug() {
  // The globals page reads named variables out of the machine, which only a
  // packed program can label. It is not shown at all in plain mode.
  const bool keysPage = Store::unlocked();
  const int pages = keysPage ? 2 : 1;
  if (!keysPage) g_dialogPage = 0;
  int y = dlgFrame("DEBUG", keysPage && g_dialogPage == 0 ? "keys" : "system", pages);
  if (keysPage && g_dialogPage == 0) drawDebugKeys(y);
  else                               drawDebugSystem(y);
}


const char* const kIrcis1[] = {
  "IRCIS - 'I Run Chars I See',",
  "by Arjun Nair (batman-nair).",
  "",
  "The program is a 2D grid, one",
  "instruction per cell. A runner walks",
  "it in a straight line until told to",
  "turn, and can split into several",
  "runners at once. Inspired by Befunge.",
  "",
  "  github.com/batman-nair/IRCIS",
};
const char* const kIrcis2[] = {
  "MOVEMENT",
  "",
  "  >   walk east      <   walk west",
  "  ^   walk north     v   walk south",
  "",
  "  *   split: a second runner carries",
  "      on in the direction this one",
  "      was already going",
  "  !   end this runner",
};
const char* const kIrcis3[] = {
  "VALUES",
  "",
  "  '   begins a literal, ending at the",
  "      next blank. All digits reads as",
  "      decimal; any other base64",
  "      character makes it base64.",
  "        '42.    pushes 42",
  "        'fU.    pushes base64 fU",
  "  \"   toggles string mode: what lies",
  "      between is pushed as text",
};
const char* const kIrcis4[] = {
  "THE STACK AND VARIABLES",
  "",
  "  @X  push global X    &X  pop to X",
  "  @x  push local x     &x  pop to x",
  "",
  "      Case is what chooses: capitals",
  "      are global, lower case local.",
  "",
  "  @2  copy the 2nd entry from the top",
  "  &2  discard the top 2 entries",
};
const char* const kIrcis5[] = {
  "ARITHMETIC",
  "",
  "  Each pops two values and pushes the",
  "  result. They take the ' prefix too,",
  "  which is what tells + from a digit.",
  "",
  "    '+  add     '-  subtract",
  "    '*  multiply    '/  divide",
  "    '%  modulo      '^  power",
  "    '&  and   '|  or   'V  xor",
};
const char* const kIrcis6[] = {
  "OUTPUT, CHANCE AND TIME",
  "",
  "  #   pop and print",
  "  %   pop and print as base64",
  "  $   end the line",
  "",
  "  r   push a random 0 or 1",
  "  R   pop n, push a random 0..n",
  "  p   pop n and pause for n",
};
const char* const kIrcis7[] = {
  "CONDITIONS AND BLANKS",
  "",
  "  ?   if the top of the stack is true",
  "      carry straight on, else turn to",
  "      whichever side has a non-blank",
  "      cell.",
  "",
  "  .   and space are blanks. Any other",
  "      character with no meaning is",
  "      simply walked over.",
};

const char* const kDevice1Locked[] = {
  "pIRCIS - the p is for pocket - runs",
  "IRCIS programs on a 4.0 inch touch",
  "display: choose one, edit it, and",
  "watch the runners move through it.",
  "",
  "The interpreter is a port of IRCIS",
  "itself, checked against the original",
  "build step for step.",
  "",
  "  github.com/jamesleaver/pIRCIS",
};
// The tab tour and the editing rules. A device with the pack open describes
// more tabs than one without, so the unlocked forms come from the pack.
const char* const kDevice2Locked[] = {
  "RUN   the program running.",
  "      |< step back    > play",
  "      >| run to the end",
  "      Tap a character to edit it.",
  "      ZOOM appears when a program",
  "      is too wide to show at once.",
  "",
  "OUT   whatever the program printed.",
};
const char* const kDevice3Locked[] = {
  "EDIT  a text editor. Tap a cell or",
  "      use the cursor's arrows, then",
  "      type. '.' is the blank, so it",
  "      doubles as delete.",
  "",
  "PROG  the bundled examples, and a",
  "      new program at any size.",
  "",
  "SAVE  programs as .txt on the card.",
};
const char* const kDevice4Locked[] = {
  "Every edit re-runs immediately.",
  "",
  "An all-digit literal is decimal to",
  "IRCIS; anything else is base64, so",
  "'42 is forty-two and 'fU is not.",
  "",
  "The name and the size in the status",
  "bar are buttons: one renames the",
  "program, the other reshapes it.",
};

const char* const kDevice5Locked[] = {
  "SYS > START POINT chooses where",
  "execution begins and which way it",
  "heads.",
  "",
  "Set it FREE and tapping a character",
  "on RUN starts the program there;",
  "tapping it again turns it, and a",
  "chevron shows the way. FIXED starts",
  "at the top left, heading east.",
};

// A page of text in a paged dialog. `highlight` picks out one line -- an
// address, a URL -- in the accent colour; -1 for none.
struct TextPage { const char* const* lines; int n; const char* tag; int highlight; };
#define SK_PAGE(a, tag, hl) { a, (int)(sizeof(a) / sizeof(char*)), tag, hl }

const TextPage kIrcisPages[] = {
  SK_PAGE(kIrcis1, "the language", 9), SK_PAGE(kIrcis2, "movement",  -1),
  SK_PAGE(kIrcis3, "values",      -1), SK_PAGE(kIrcis4, "the stack", -1),
  SK_PAGE(kIrcis5, "arithmetic",  -1), SK_PAGE(kIrcis6, "output",    -1),
  SK_PAGE(kIrcis7, "conditions",  -1),
};
const TextPage kDeviceLockedPages[] = {
  SK_PAGE(kDevice1Locked, "what it is",  9), SK_PAGE(kDevice2Locked, "the tabs",   -1),
  SK_PAGE(kDevice3Locked, "the tabs",   -1), SK_PAGE(kDevice4Locked, "editing",    -1),
  SK_PAGE(kDevice5Locked, "start point", -1),
};
#undef SK_PAGE

constexpr int kIrcisCount  = (int)(sizeof(kIrcisPages) / sizeof(TextPage));
constexpr int kDeviceCount = (int)(sizeof(kDeviceLockedPages) / sizeof(TextPage));

// How many pages a dialog has. The packed groups are described by the pack, so
// adding one there cannot leave the ">" button refusing to reach it.
int devicePageCount() {
  const int n = pack::pageCount(pack::kGroupDevice);
  return n > 0 ? n : kDeviceCount;
}

// Anything that is plainly a web address. Colouring these by rule rather than
// by a per-page index means a link added later is coloured without anyone
// having to remember to say so.
bool looksLikeLink(const char* s) {
  return std::strstr(s, "://") != nullptr || std::strstr(s, "github.com") != nullptr
      || std::strstr(s, "x.com/") != nullptr;
}

void drawOnePage(const char* title, const TextPage& pg, int count) {
  int y = dlgFrame(title, pg.tag, count);
  for (int i = 0; i < pg.n; ++i) {
    // A line starting with a digit is a heading -- an id, a date -- and sits
    // above its indented text in the dim colour.
    bool head = (pg.lines[i][0] >= '0' && pg.lines[i][0] <= '9');
    uint16_t col = (looksLikeLink(pg.lines[i]) || i == pg.highlight) ? theme::accent
                 : (head ? theme::dim : theme::text);
    clabel(20, y, pg.lines[i], col, theme::panel);
    y += kContentH;
  }
}

void drawTextPages(const char* title, const TextPage* pages, int count) {
  if (g_dialogPage >= count) g_dialogPage = count - 1;
  drawOnePage(title, pages[g_dialogPage], count);
}

// A page held by the pack, borrowed as a TextPage. The strings belong to the
// pack and stay put while it is open, so their pointers can go straight to the
// drawing code.
constexpr int kMaxPageLines = 24;
bool drawPackPages(const char* title, uint8_t group) {
  const int count = pack::pageCount(group);
  if (count <= 0) return false;
  if (g_dialogPage >= count) g_dialogPage = count - 1;
  const pack::Page& src = pack::page(group, g_dialogPage);
  static const char* lines[kMaxPageLines];
  int n = (int)src.lines.size();
  if (n > kMaxPageLines) n = kMaxPageLines;
  for (int i = 0; i < n; ++i) lines[i] = src.lines[i].c_str();
  TextPage pg = { lines, n, src.tag.c_str(), src.highlight };
  drawOnePage(title, pg, count);
  return true;
}

void drawInfo()   { drawPackPages(pack::str(pack::kStrInfoTitle), pack::kGroupInfo); }
void drawIrcis()  { drawTextPages("IRCIS", kIrcisPages, kIrcisCount); }
// Shown once at power-on. Credit where it is due: this is a port of somebody
// else's language, and the first thing the device says should say so.
const char* const kSplashLocked[] = {
  "pIRCIS: choose a program, edit",
  "it, and watch the runners walk it a",
  "step at a time.",
  "",
  "IRCIS - 'I Run Chars I See' - is the",
  "work of Arjun Nair (batman-nair). The",
  "interpreter here is a port of his work,",
  "which can be found here:",
  "",
  "  github.com/batman-nair/IRCIS",
};

void drawSplash() {
  if (Store::unlocked() && pack::pageCount(pack::kGroupSplash) > 0) {
    g_dialogPage = 0;
    drawPackPages(pack::str(pack::kStrSplashTitle), pack::kGroupSplash);
    return;
  }
  const int n = (int)(sizeof(kSplashLocked) / sizeof(char*));
  int y = dlgFrame("WELCOME TO pIRCIS", "", 1);
  for (int i = 0; i < n; ++i) {
    clabel(20, y, kSplashLocked[i], looksLikeLink(kSplashLocked[i]) ? theme::accent : theme::text,
           theme::panel);
    y += kContentH;
  }
}

void drawDevice() {
  if (Store::unlocked() && drawPackPages("THIS DEVICE", pack::kGroupDevice)) return;
  drawTextPages("THIS DEVICE", kDeviceLockedPages, kDeviceCount);
}

// These buttons sit INSIDE the dialog panel, so they cannot use modalBtnX():
// that lays out across the full screen width (6 .. kScreenW-6) and would hang
// over both edges of a box that only spans 12 .. kScreenW-12. Inset by 8 px
// from the panel's inner edge instead, and divide what is left four ways.

// Shown once when a run completes, so a finished output is not left sitting
// unnoticed on a tab you are not looking at.
// Everything WiFi in one place, rather than three tiles on SYS.
Btn btnWifiSsid()  { return { 20, kDlgY + 34,  kScreenW - 40, 24, "SSID" }; }
Btn btnWifiPass()  { return { 20, kDlgY + 62,  kScreenW - 40, 24, "PASSWORD" }; }
Btn btnWifiConn()  { return { 20, kDlgY + 90,  (kScreenW - 46) / 2, 24, "CONNECT", theme::good }; }
Btn btnWifiWeb()   { return { 26 + (kScreenW - 46) / 2, kDlgY + 90, (kScreenW - 46) / 2, 24, "WEB VIEW" }; }
Btn btnWifiClose() { return { kScreenW - 92, kDlgY + 124, 72, 26, "CLOSE", theme::bad }; }

// The rule itself lives in Store::setWifi -- the credentials are tried
// against the pack. All this does is notice the moment it opens, so the tabs
// can appear and say why.
void announceUnlock();
// Set when the message that follows should go on to the info pages.
bool g_msgThenInfo = false;

bool checkUnlock() {
  bool now = Store::unlocked();
  bool changed = now && !g_wasUnlocked;
  g_wasUnlocked = now;
  return changed;
}

void drawWifi() {
  int w = kScreenW - 24, h = 162;
  gfx.fillRect(12, kDlgY, w, h, theme::panel);
  gfx.drawRect(12, kDlgY, w, h, theme::accent);
  gfx.setFont(&fonts::Font2);
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.setTextColor(theme::accent, theme::panel);
  gfx.drawString("WIFI", 20, kDlgY + 5);

  Btn ss = btnWifiSsid();
  std::string sv = Store::wifiSsid();
  std::string sl = std::string("SSID: ") + (sv.empty() ? "(unset)" : sv.substr(0, 24));
  ss.label = sl.c_str();
  drawBtn(ss);

  Btn pw = btnWifiPass();
  std::string pl = std::string("PASSWORD: ") + (Store::wifiPass().empty() ? "(unset)" : "set");
  pw.label = pl.c_str();
  drawBtn(pw);

  Btn cn = btnWifiConn();
  std::string cl = web::running() ? std::string("DISCONNECT") : std::string("CONNECT");
  cn.label = cl.c_str();
  bool radio = web::available();
  drawBtn(cn, web::running(), radio && !Store::wifiSsid().empty());

  drawBtn(btnWifiWeb(), false, radio && web::running());
  if (!radio)
    clabel(20, kDlgY + 120, "No radio here -- credentials still save.",
           theme::dim, theme::panel);
  if (web::running()) {
    std::string url = std::string("http://") + web::ipAddress() + "/";
    clabel(20, kDlgY + 120, url.c_str(), theme::good, theme::panel);
  }
  drawBtn(btnWifiClose());
}


void drawMessage() {
  gfx.fillRect(20, 60, kScreenW - 40, 120, theme::panel);
  gfx.drawRect(20, 60, kScreenW - 40, 120, theme::accent);
  gfx.setFont(&fonts::Font2);
  gfx.setTextDatum(textdatum_t::top_center);
  gfx.setTextColor(theme::accent, theme::panel);
  gfx.drawString(g_msgTitle.c_str(), kScreenW / 2, 70);
  gfx.setFont(&fonts::Font0);
  gfx.setTextColor(theme::text, theme::panel);
  // Wrap on word boundaries. Breaking mid-word reads as a rendering fault
  // rather than a long message, which is exactly the wrong impression for a
  // dialog whose whole job is to be believed.
  constexpr std::size_t kWrap = 44;
  int y = 96;
  std::size_t i = 0;
  while (i < g_msgBody.size()) {
    std::size_t take = g_msgBody.size() - i;
    if (take > kWrap) {
      take = g_msgBody.rfind(' ', i + kWrap);
      // No space to break on: fall back to a hard cut rather than loop.
      take = (take == std::string::npos || take <= i) ? kWrap : take - i;
    }
    gfx.drawString(g_msgBody.substr(i, take).c_str(), kScreenW / 2, y);
    y += 10;
    i += take;
    while (i < g_msgBody.size() && g_msgBody[i] == ' ') ++i;
  }
  gfx.setTextDatum(textdatum_t::top_left);
  if (g_modal == Modal::Confirm) {
    drawBtn(Btn{ kScreenW / 2 - 120, 148, 100, 24, "CANCEL", theme::bad });
    drawBtn(Btn{ kScreenW / 2 + 20,  148, 100, 24, "CONFIRM", theme::bg, theme::good });
  }
  else {
    drawBtn(Btn{ kScreenW / 2 - 50, 148, 100, 24, "OK", theme::bg, theme::good });
  }
}

void message(const std::string& title, const std::string& body) {
  g_msgTitle = title; g_msgBody = body; g_modal = Modal::Message; g_dirty = true;
}

void confirm(const std::string& title, const std::string& body, std::function<void()> yes) {
  g_msgTitle = title; g_msgBody = body; g_confirmYes = yes;
  g_modal = Modal::Confirm; g_dirty = true;
}

// Called after either WiFi credential is saved. Silent unless this is the
// moment the device opens up.
void announceUnlock() {
  if (!checkUnlock()) return;
  g_dirty = true;
  // The day palette is for reading an IRCIS program in sunlight. Entering
  // this mode switches to night, and the setting is stored so it stays there.
  Store::setDayMode(false);
  theme::setDay(false);
  // The output is the point of the exercise here, and its 38 chunks read
  // better uncoloured until you ask for the colouring.
  Store::setRunView(1);
  Store::setOutputColour(false);
  g_outLine = 0;
  g_runnerTop = 0;

  // PROG disappears in this mode, so put the packed program up rather than
  // leaving whatever example was loaded with no way to change it.
  g_edit.loadProgram(prog::kPackedIndex);
  run::load(g_edit);
  markLoaded();
  syncViewToProgram();
  g_tab = Tab::Run;
  // The splash says what just happened better than a dialog can, and CLOSE
  // carries on into the info pages rather than leaving them to be found on
  // the SYS page.
  g_msgThenInfo = true;
  g_dialogPage = 0;
  g_modal = Modal::Splash;
}

// ---------------------------------------------------------------------------
// actions
// ---------------------------------------------------------------------------

void openSlotEditor(int idx) {
  const prog::Slot& s = prog::slot(idx);
  std::string cur = g_edit.slotValue(idx);

  if (s.kind == prog::SlotKind::Count) {
    char hint[80];
    snprintf(hint, sizeof(hint), "0-99");
    // "1234567890" in three columns lays out as 123 / 456 / 789 / 0.
    // \x01 is a blank key, so the row reads [ ][0][ ] and the zero lands
    // under the eight.
    openPicker(s.label, hint, kKbDigits, std::to_string(g_edit.countValue()), 2,
               [](const std::string& v) {   // setCountValue finds the slot itself
                 int j = v.empty() ? 0 : atoi(v.c_str());
                 if (!g_edit.setCountValue(j)) { message("Rejected", "J must be 0-99."); return; }
                 markEdited();
               });
    return;
  }

  // Every slot uses the same base64 keypad: an all-digit value entered there
  // is a decimal literal to IRCIS, and anything else is base64, so both kinds
  // of seed can be typed on one keyboard.
  // Trailing '.' is padding, not part of the value.
  std::string shown = cur;
  while (!shown.empty() && shown.back() == '.') shown.pop_back();

  char hint[110];
  snprintf(hint, sizeof(hint), "%d cells", (int)s.len);
  openPicker(s.label.c_str(), hint, kKbBase64, shown, s.len,
             [idx](const std::string& v) {
               if (!g_edit.setSlotValue(idx, v)) {
                 message("Rejected", "Value does not fit this slot.");
                 return;
               }
               markEdited();
             });
}

void handleTabs(int x, int y) {
  int n = tabCount();
  int w = kScreenW / n;
  int i = x / w;
  if (i >= n) i = n - 1;              // the last tab owns the leftover pixels
  if (i >= 0) {
    // Tapping the tab you are already on is a shortcut for the play button --
    // the run controls are at the top of the screen, the tab bar at the bottom.
    if (tabAt(i) == Tab::Run && g_tab == Tab::Run) {
      if (run::snapshot().running) run::cmdPause();
      else                         run::cmdRun();
      g_dirty = true;
      return;
    }
    g_tab = tabAt(i);
    // The card is only read on entry, not on every repaint.
    if (g_tab == Tab::Save && !Store::unlocked()) refreshProgFiles();
    g_dirty = true;
  }
}

void handleRunTouch(int x, int y) {
  if (y < kHeaderH) {
    if (!zoomOnly() && hit(btnView(), x, y)) {
      // ZOOM in, or back out to WIDE.
      int next = (g_view == View::Zoom) ? (int)View::Wide : (int)View::Zoom;
      Store::setGridView(next);
      g_view = (View)next;
      g_follow = true;                // a fresh zoom starts following again
      g_prevRunners.clear();          // stale coordinates from the other layout
      g_dirty = true;
      return;
    }
    if (hit(btnStart(), x, y))      { run::load(g_edit);  g_follow = true; g_dirty = true; }
    else if (steps() && hit(btnBack(), x, y)) { run::cmdStepBack(); g_dirty = true; }
    else if (steps() && hit(btnFwd(), x, y))  { run::cmdStep(1); }
    else if (hit(btnEnd(), x, y))   { run::cmdRunToEnd(); g_dirty = true; }
    else if (hit(btnRun(), x, y))   {
      run::Snapshot s = run::snapshot();
      if (s.running) run::cmdPause();
      else {
        g_follow = true;
        run::cmdRun();      // restarts by itself if the run had finished
      }
      g_dirty = true; g_headerOnly = true;
    }
    else if (hit(btnSpeed(), x, y)) {
      int n = ((int)run::speed() + 1) % 4;
      run::setSpeed((run::Speed)n);
      Store::setRunSpeed(n);
      g_dirty = true; g_headerOnly = true;
    }
    return;
  }
  if (maxGridRow() > 0) {
    if (hit(btnRowUp(), x, y))   { if (g_gridRow > 0) { --g_gridRow; g_bodyOnly = true; g_dirty = true; } return; }
    if (hit(btnRowDown(), x, y)) { if (g_gridRow < maxGridRow()) { ++g_gridRow; g_bodyOnly = true; g_dirty = true; } return; }
  }
  if (Store::runView() == 1) {
    if (hit(btnOutUp(), x, y))   { ++g_outLine; g_bandOnly = true; g_dirty = true; return; }
    if (hit(btnOutDown(), x, y)) { if (g_outLine > 0) --g_outLine; g_bandOnly = true; g_dirty = true; return; }
  }
  else if (Store::runView() == 0) {
    // The same pair of buttons, driving the runner list instead. Clamping is
    // left to the drawing code, which is the only thing that knows how many
    // runners there are and how many of them fit.
    if (hit(btnOutUp(), x, y))   { if (g_runnerTop > 0) --g_runnerTop; g_bandOnly = true; g_dirty = true; return; }
    if (hit(btnOutDown(), x, y)) { ++g_runnerTop; g_bandOnly = true; g_dirty = true; return; }
  }
  if (wideView() && g_edit.cols() > kWideCols) {
    if (hit(btnWideLeft(), x, y))  { if (g_wideShift > 0) { --g_wideShift; g_bodyOnly = true; g_dirty = true; } return; }
    if (hit(btnWideRight(), x, y)) {
      if (g_wideShift + kWideCols < g_edit.cols()) { ++g_wideShift; g_bodyOnly = true; g_dirty = true; }
      return;
    }
  }
  int r, c;
  if (cellAt(x, y, r, c)) {
    int slot = slotAtCell(r, c);
    if (slot >= 0) { openSlotEditor(slot); return; }   // straight to the parameter
    if (!Store::unlocked()) {
      // The character inspector belongs to a packed program -- it names
      // parameters and shows what a cell used to be. Moving the entry point
      // was the one thing it did that a plain program still needs, so that
      // lives here instead:
      // tap a character to start there, tap it again to turn it. The chevron
      // beside the cell says which way.
      if (!Store::startEditable()) return;
      if (r == run::startRow() && c == run::startCol()) {
        const char* order = "ESWN";              // clockwise from east
        const char* at = std::strchr(order, run::startDir());
        char next = at && at[1] ? at[1] : order[0];
        run::setStart(c, r, next);
        Store::setStartPoint(c, r, next);
      }
      else {
        run::setStart(c, r, run::startDir());
        Store::setStartPoint(c, r, run::startDir());
      }
      run::load(g_edit);
      markLoaded();
      g_dirty = true;
      return;
    }
    g_cellRow = r; g_cellCol = c;
    g_modal = Modal::Cell;
    g_dirty = true;
  }
}

// Keep the active runner on screen. Only re-centres when it reaches the edge
// margin, so the view is not repainted on every step.
void followRunner(const run::Snapshot& snap) {
  if (g_view != View::Zoom || !g_follow || snap.runnerCount == 0) return;
  int col = -1, row = -1, best = 999;
  for (int i = 0; i < snap.runnerCount; ++i)
    if (snap.runners[i].id < best) {
      best = snap.runners[i].id;
      col = snap.runners[i].x;
      row = snap.runners[i].y;
    }
  if (col < 0) return;

  // Re-centre only when the runner reaches a margin, so the panel is not
  // repainted every step. Both axes work the same way: a program taller than
  // the window needs following down as much as one wider than it needs
  // following across.
  const int margin = 4;
  if (col < g_scrollCol + margin || col >= g_scrollCol + kZoomCols - margin) {
    // setScroll clamps to the loaded program's width, which is the only thing
    // that knows how far right there is to go.
    const int before = g_scrollCol;
    setScroll(col - kZoomCols / 2);
    if (g_scrollCol != before) g_dirty = true;
  }

  const int maxRow = maxGridRow();
  if (maxRow > 0 && row >= 0) {
    const int visible = gridRowsShown();
    // A one-row margin: the vertical window is only a handful of rows deep, so
    // the horizontal margin would leave almost nothing in view.
    const int vMargin = visible > 4 ? 1 : 0;
    if (row < g_gridRow + vMargin || row >= g_gridRow + visible - vMargin) {
      int want = row - visible / 2;
      if (want < 0) want = 0;
      if (want > maxRow) want = maxRow;
      if (want != g_gridRow) { g_gridRow = want; g_dirty = true; }
    }
  }
}

void handleEditTouch(int x, int y) {
  if (hit(btnEditPrev(), x, y))   { if (g_editPage > 0) { --g_editPage; g_dirty = true; } return; }
  if (hit(btnEditNext(), x, y))   { if (g_editPage == 0) { g_editPage = 1; g_dirty = true; } return; }
  if (hit(btnEditRevert(), x, y)) {
    confirm("Revert everything?",
            pack::str(pack::kStrRevertBody),
            [] { g_edit.revertAll(); markEdited(); });
    return;
  }
  int first = editPageFirst(g_editPage);
  for (int i = 0; i < editPageCount(g_editPage); ++i) {
    int idx = first + i;
    if (idx >= prog::slotCount()) break;
    int ry = kBodyY + 2 + i * kEditRowH;
    if (y >= ry && y < ry + kEditRowH - 2) {
      if (x >= kScreenW - 30 && g_edit.slotModified(idx)) {
        g_edit.revertSlot(idx);
        markEdited();
      }
      else openSlotEditor(idx);
      return;
    }
  }
}

void handleOutTouch(int x, int y) {
  if (g_ranGrid.isPacked() && hit(btnOutColour(), x, y)) {
    Store::setOutputColour(!Store::outputColour());
    g_dirty = true;
    return;
  }
  if (hit(btnOutSd(), x, y)) {
    if (!plat::sdPresent()) { message("No card", "Insert an SD card."); return; }
    std::string path;
    run::loadedGridInto(g_ranGrid);
    if (sinks::saveRunToSd(run::output(), g_ranGrid, path))
      message("Saved", path);
    else
      message("SD failed", "Could not write to the card.");
    return;
  }
  // A page at a time, leaving one line of overlap so nothing is skipped over.
  const int page = g_outLines > 1 ? g_outLines - 1 : 1;
  if (hit(btnOutPgUp(), x, y)) {
    if (g_outTop > 0) { g_outTop -= page; if (g_outTop < 0) g_outTop = 0; g_bodyOnly = true; g_dirty = true; }
    return;
  }
  if (hit(btnOutPgDn(), x, y)) {
    if (g_outTop + g_outLines < g_outTotal) { g_outTop += page; g_bodyOnly = true; g_dirty = true; }
    return;
  }
}

void handleSaveTouch(int x, int y) {
  for (int i = 0; i < Store::kMaxPresets; ++i) {
    if (hit(btnSaveWrite(i), x, y)) {
      openPicker("Preset name", "", kKbText, "", 12,
                 [i](const std::string& name) {
                   if (Store::savePreset(i, name.empty() ? "unnamed" : name, g_edit))
                     message("Saved", "Preset " + std::to_string(i + 1) + " stored in NVS.");
                   else
                     message("Too large", "More edits than a preset can hold.");
                 },
                 kKbSplit);
      return;
    }
    if (hit(btnSaveDel(i), x, y)) {
      Store::PresetInfo info = Store::presetInfo(i);
      if (!info.used) return;
      confirm("Delete preset?", info.name, [i] { Store::deletePreset(i); });
      return;
    }
    if (hit(btnSaveSlot(i), x, y)) {
      Store::PresetInfo info = Store::presetInfo(i);
      if (!info.used) return;
      if (Store::loadPreset(i, g_edit)) { markEdited(); g_dirty = true; }
      return;
    }
  }
}

void handleSysTouch(int x, int y) {
  if (hit(btnSysWifi(), x, y))       { g_modal = Modal::Wifi; g_dirty = true; }
  else if (hit(btnSysSd(), x, y)) {
    if (plat::sdPresent()) { Store::setSdLogging(!Store::sdLoggingEnabled()); g_dirty = true; }
  }
  else if (hit(btnSysDebug(), x, y)) { g_modal = Modal::Debug; g_dialogPage = 0; g_dirty = true; }
  else if (hit(btnSysRunView(), x, y)) {
    Store::setRunView((Store::runView() + 1) % 3);
    g_outLine = 0;
    g_dirty = true;
  }
  else if (hit(btnSysSteps(), x, y)) {
    Store::setStepButtons(!Store::stepButtons());
    g_dirty = true;
  }
  else if (hit(btnSysTheme(), x, y)) {
    bool day = !Store::dayMode();
    Store::setDayMode(day);
    theme::setDay(day);
    g_dirty = true;
  }
  else if (hit(btnSysOff(), x, y)) {
    confirm("Turn off?", "The screen goes dark. Press the board's reset "
                         "button to start it again.", [] { plat::powerOff(); });
  }
  else if (Store::unlocked() && hit(btnSysExit(), x, y)) {
    confirm(pack::str(pack::kStrExitTitle),
            "The device goes back to being a plain IRCIS interpreter. "
            "Set the WiFi credentials again to return.", [] {
      relock();
    });
  }
  else if (Store::unlocked() && hit(btnSysInfo(), x, y))
                                     { g_modal = Modal::Info; g_dialogPage = 0; g_dirty = true; }
  else if (hit(btnSysIrcis(), x, y)) { g_modal = Modal::Ircis;  g_dialogPage = 0; g_dirty = true; }
  else if (hit(btnSysRead(), x, y))  { g_modal = Modal::Device; g_dialogPage = 0; g_dirty = true; }
  else if (hit(btnSysStart(), x, y)) {
    const bool free = !Store::startEditable();
    Store::setStartEditable(free);
    if (!free) {
      // FIXED means the program starts where IRCIS would start it.
      run::setStart(0, 0, 'E');
      Store::setStartPoint(0, 0, 'E');
      run::load(g_edit);
      markLoaded();
    }
    g_dirty = true;
  }
  else if (hit(btnSysCal(), x, y)) {
    confirm("Recalibrate touch?", "You will be asked to tap the four corners.",
            [] { Store::clearTouchCalibration(); gfx.beginTouch(true); });
  }
  else if (hit(btnSysDump(), x, y)) {
    plat::logf("--- %s ---\n", g_edit.programName());
    plat::log(g_edit.text().c_str());
    plat::logln(g_edit.isPacked() ? pack::str(pack::kStrDumpDiff)
                                  : "--- edits from the program as loaded ---");
    for (const prog::Diff& d : g_edit.diff())
      plat::logf("  row %2d col %2d  '%c' -> '%c'\n", d.row, d.col,
                 g_edit.baselineCell(d.row, d.col), d.ch);
    if (web::running())
      message("Dumped", "Console, and http://" + web::ipAddress() + "/edit");
    else
      message("Dumped", "Grid and edits written to the console.");
  }
  else if (hit(btnSysReset(), x, y)) {
    // Plain mode has no presets or saved sets. Both readings are true: the
    // built-in programs live in flash and survive either way.
    confirm("Erase everything?",
            Store::unlocked()
              ? std::string(pack::str(pack::kStrResetBody))
              : std::string("WiFi and calibration go, and the device returns to "
                            "how it shipped. The programs are in flash and are "
                            "untouched."),
            [] {
              Store::factoryReset();
              // factoryReset() clears the unlock too, so the packed program
              // is no longer listed -- do not leave it loaded with no way back.
              // The device should come back exactly as it ships: day palette,
              // an example loaded, and the welcome dialog it shows at power-on.
              theme::setDay(Store::dayMode());
              g_wasUnlocked = false;
              g_edit.loadProgram(prog::kFirstExample);
              run::setStart(0, 0, 'E');
              syncViewToProgram();
              g_curRow = g_curCol = 0;
              g_edRow = g_edCol = 0;
              g_outLine = 0;
              g_tab = Tab::Run;
              markEdited();
              g_dialogPage = 0;
              g_modal = Modal::Splash;
            });
  }
}

void handleWifiTouch(int x, int y) {
  if (hit(btnWifiClose(), x, y)) { g_modal = Modal::None; g_dirty = true; return; }
  // Storing credentials is just writing to NVS and works with no radio at all.
  // Only joining a network needs one, so the guard sits on those two buttons
  // rather than across the whole dialog.
  if (hit(btnWifiSsid(), x, y)) {
    openPicker("WiFi SSID", "", kKbText,
               Store::wifiSsid(), 31,
               [](const std::string& v) { Store::setWifi(v, Store::wifiPass());
                                         g_modal = Modal::Wifi; announceUnlock(); },
               kKbSplit);
    return;
  }
  if (hit(btnWifiPass(), x, y)) {
    openPicker("WiFi password", "", kKbText, "", 63,
               [](const std::string& v) { Store::setWifi(Store::wifiSsid(), v);
                                         g_modal = Modal::Wifi; announceUnlock(); },
               kKbSplit);
    return;
  }
  if (!web::available()) return;
  if (hit(btnWifiConn(), x, y)) {
    if (web::running()) web::stop();
    else if (!web::begin()) message("WiFi failed", "Could not join " + Store::wifiSsid());
    g_dirty = true;
    return;
  }
  if (hit(btnWifiWeb(), x, y) && web::running())
    message("Web view", std::string("http://") + web::ipAddress() + "/");
}

void handlePickerTouch(int x, int y) {
  if (hit(btnPickCancel(), x, y)) {
    g_modal = g_pickerBack;
    g_pickerBack = Modal::None;
    g_dirty = true;
    return;
  }
  if (hit(btnPickOk(), x, y)) {
    auto commit = g_pickerCommit;
    std::string v = g_pickerValue;
    g_modal = g_pickerBack;
    g_pickerBack = Modal::None;
    g_dirty = true;
    if (commit) commit(v);          // a commit may still send you elsewhere
    return;
  }
  if (hit(btnPickClear(), x, y)) {
    if (g_pickerValue != g_pickerOriginal) g_pickerValue = g_pickerOriginal;
    else                                   g_pickerValue.clear();
    g_dirty = true;
    return;
  }
  if (hit(btnPickBack(), x, y)) {
    if (!g_pickerValue.empty()) g_pickerValue.pop_back();
    g_dirty = true;
    return;
  }
  PickerGeom g = pickerGeom();
  if (x < g.x0 || y < g.y0) return;
  int c = (x - g.x0) / g.kw, r = (y - g.y0) / g.kh;
  if (c < 0 || c >= g.cols || r < 0 || r >= g.rows) return;
  int i;
  if (g.sideCols && c >= kPickMainCols)
    i = (int)g_pickerSplit + r * g.sideCols + (c - kPickMainCols);
  else
    i = r * (g.sideCols ? kPickMainCols : g.cols) + c;
  if (i < 0 || i >= (int)g_pickerSet.size()) return;
  if (g_pickerSet[i] == '\x01') return;          // spacer
  // A single-character field replaces on every press -- there is nothing to
  // clear first.
  char picked = g_pickerSet[i];
  if (g_pickerMax == 1) g_pickerValue = std::string(1, picked);
  else if (g_pickerValue.size() < g_pickerMax) g_pickerValue.push_back(picked);
  else return;
  g_dirty = true;
}

void handleCellTouch(int x, int y) {
  if (hit(btnCellUp(), x, y))    { if (g_cellRow > 0) --g_cellRow; g_dirty = true; return; }
  if (hit(btnCellDown(), x, y))  { if (g_cellRow < g_edit.rows() - 1) ++g_cellRow; g_dirty = true; return; }
  if (hit(btnCellLeft(), x, y))  { if (g_cellCol > 0) --g_cellCol; g_dirty = true; return; }
  if (hit(btnCellRight(), x, y)) { if (g_cellCol < g_edit.cols() - 1) ++g_cellCol; g_dirty = true; return; }

  // Any other character in the window jumps straight to it.
  if (y >= insTop() && y < insTop() + kInsRows * kInsCellH &&
      x >= kInsX && x < kInsX + kInsCols * kInsCellW) {
    int r = g_cellRow + ((y - insTop()) / kInsCellH - kInsMidR);
    int c = g_cellCol + ((x - kInsX) / kInsCellW - kInsMidC);
    if (r >= 0 && r < g_edit.rows() && c >= 0 && c < g_edit.cols()) {
      g_cellRow = r; g_cellCol = c; g_dirty = true;
    }
    return;
  }

  if (hit(btnCellSet(), x, y)) {
    int r = g_cellRow, c = g_cellCol;
    std::string set = std::string(kCellBase64) + kCellSymbols;
    openPicker("EDIT CHARACTER", "", kKbProgram,
               std::string(1, g_edit.cell(r, c)), 1,
               [r, c](const std::string& v) {
                 if (v.empty()) return;
                 if (g_edit.cell(r, c) == v[0]) return;    // no change, no edit
                 g_edit.setCell(r, c, v[0]);
                 markEdited();
               },
               kKbSplit);
    g_pickerBack = Modal::Cell;      // back to the inspector, not out to RUN
    return;
  }
  if (hit(btnCellRevert(), x, y)) {
    // The button is drawn disabled when the cell already matches; honour that,
    // or a tap here would raise "edits pending" without changing anything.
    char orig = g_edit.baselineCell(g_cellRow, g_cellCol);
    if (g_edit.cell(g_cellRow, g_cellCol) != orig) {
      g_edit.setCell(g_cellRow, g_cellCol, orig);
      markEdited();
    }
    return;
  }
  if (Store::startEditable() && hit(btnCellStart(), x, y)) {
    const char* order = "ESWN";
    char dir = 'E';
    if (run::startRow() == g_cellRow && run::startCol() == g_cellCol) {
      const char* at = strchr(order, run::startDir());
      dir = (at && at[1]) ? at[1] : 'E';
    }
    run::setStart(g_cellCol, g_cellRow, dir);
    Store::setStartPoint(g_cellCol, g_cellRow, dir);
    markEdited();                       // re-runs from the new entry point
    return;
  }
  if (hit(btnCellClose(), x, y)) { g_modal = Modal::None; g_dirty = true; }
}

void handleMessageTouch(int x, int y) {
  if (y < 148 || y >= 172) return;
  if (g_modal == Modal::Confirm) {
    if (x >= kScreenW / 2 + 20 && x < kScreenW / 2 + 120) {
      auto f = g_confirmYes; g_modal = Modal::None; g_dirty = true; if (f) f(); return;
    }
    if (x >= kScreenW / 2 - 120 && x < kScreenW / 2 - 20) { g_modal = Modal::None; g_dirty = true; }
    return;
  }
  if (x >= kScreenW / 2 - 50 && x < kScreenW / 2 + 50) {
    if (g_msgThenInfo) {
      g_msgThenInfo = false;
      g_modal = Modal::Info;        // straight into the info pages
      g_dialogPage = 0;
    }
    else g_modal = Modal::None;
    g_dirty = true;
  }
}

// ---------------------------------------------------------------------------
// touch plumbing
// ---------------------------------------------------------------------------

bool     g_wasTouched = false;
uint32_t g_lastTouchMs = 0;
// Drag state. Only the ZOOM grid area defers its tap to release; everywhere
// else still acts on press, so the rest of the UI feels exactly as before.
bool g_dragArmed = false;
bool g_dragged = false;
bool g_deferTap = false;      // act on release, so a long press can be told apart
uint32_t g_pressMs = 0;
int  g_pressX = 0, g_pressY = 0, g_dragLastX = 0, g_dragLastY = 0;
constexpr uint32_t kLongPressMs = 600;

void onTap(int x, int y) {
  switch (g_modal) {
    case Modal::Picker:  handlePickerTouch(x, y); return;
    case Modal::Cell:    handleCellTouch(x, y); return;
    case Modal::Wifi: handleWifiTouch(x, y); return;
    case Modal::Size: handleSizeTouch(x, y); return;
    // Any tap anywhere dismisses the welcome. When it is the one shown on
    // unlocking, it carries on into the info pages.
    case Modal::Splash:
      if (g_msgThenInfo) { g_msgThenInfo = false; g_modal = Modal::Info; g_dialogPage = 0; }
      else               g_modal = Modal::None;
      g_dirty = true;
      return;
    case Modal::Debug:
    case Modal::Info:
    case Modal::Ircis:
    case Modal::Device: {
      int pages = g_modal == Modal::Info   ? pack::pageCount(pack::kGroupInfo)
                : g_modal == Modal::Ircis  ? kIrcisCount
                : g_modal == Modal::Device ? devicePageCount()
                : (Store::unlocked() ? 2 : 1);   // DEBUG loses its keys page
      if (hit(btnDlgClose(), x, y)) { g_modal = Modal::None; g_dirty = true; }
      else if (hit(btnDlgPrev(), x, y) && g_dialogPage > 0) { --g_dialogPage; g_dirty = true; }
      else if (hit(btnDlgNext(), x, y) && g_dialogPage + 1 < pages) { ++g_dialogPage; g_dirty = true; }
      return;
    }
    case Modal::Confirm:
    case Modal::Message: handleMessageTouch(x, y); return;
    default: break;
  }
  if (y >= kTabY) {
    handleTabs(x, y);
    if (g_tab == Tab::Out) g_outputUnseen = false;
    return;
  }
  switch (g_tab) {
    case Tab::Run:  handleRunTouch(x, y); break;
    case Tab::Edit:
      if (Store::unlocked()) handleEditTouch(x, y);
      else                   handleProgEditTouch(x, y);
      break;
    case Tab::Keys: handleKeysTouch(x, y); break;
    case Tab::Out:  handleOutTouch(x, y); break;
    case Tab::Save:
      if (Store::unlocked()) handleSaveTouch(x, y);
      else                   handleFilesTouch(x, y);
      break;
    case Tab::Prog: handleProgTouch(x, y); break;
    case Tab::Sys:  handleSysTouch(x, y); break;
    default: break;
  }
}

// Everything between the header and the tab bar. Each tab's drawer clears its
// own area first, so this is safe to call on its own.
void drawBody(const run::Snapshot& snap) {
  switch (g_tab) {
    case Tab::Run:  drawRun(snap); break;
    case Tab::Edit:
      if (Store::unlocked()) drawEdit();
      else                   drawProgEdit();
      break;
    case Tab::Keys: drawKeys(); break;
    case Tab::Out:  drawOut(); break;
    case Tab::Save:
      if (Store::unlocked()) drawSave();
      else                   drawFiles();
      break;
    case Tab::Prog: drawProg(); break;
    case Tab::Sys:  drawSys(); break;
    default: break;
  }
}

void drawAll(const run::Snapshot& snap) {
  if (g_modal == Modal::Picker) { drawPicker(); return; }
  if (g_modal == Modal::Cell)   { drawCellModal(); return; }
  if (g_modal == Modal::Debug)  { drawDebug(); return; }
  if (g_modal == Modal::Info)   { drawInfo(); return; }
  if (g_modal == Modal::Wifi)   { drawWifi(); return; }
  if (g_modal == Modal::Splash) {
    // The base screen first: the splash is also raised on unlocking and after
    // a reset, when the header and the tab bar underneath it have both just
    // changed and would otherwise show the state they were in before.
    gfx.fillScreen(theme::bg);
    drawHeader(snap);
    drawBody(snap);
    drawTabs();
    drawSplash();
    return;
  }
  if (g_modal == Modal::Size)   { drawSize();   return; }
  if (g_modal == Modal::Ircis)  { drawIrcis();  return; }
  if (g_modal == Modal::Device) { drawDevice(); return; }
  gfx.fillScreen(theme::bg);
  drawHeader(snap);
  drawBody(snap);
  drawTabs();
  if (g_modal == Modal::Confirm || g_modal == Modal::Message) drawMessage();
}

} // namespace

prog::Program& editGrid() { return g_edit; }
// Every edit is pushed into the interpreter immediately and the run restarts.
// There is no "pending" state to forget about -- what is on screen is always
// what will run.
void markEdited() { run::load(g_edit); g_dirty = true; }
void markLoaded() { g_dirty = true; }
void repaint() { g_dirty = true; }
void injectTap(int x, int y) { onTap(x, y); }
// Scratch harness: draw the same row in every candidate font so the choice
// for the detail pane is made by looking at it.
void fontSampler() {
  gfx.fillScreen(theme::bg);
  gfx.setTextDatum(textdatum_t::top_left);
  std::string row;
  for (int c = 0; c < 70; ++c) row.push_back(g_edit.cell(0, c));

  struct Sample { const char* name; const lgfx::IFont* font; int size; int adv; };
  const Sample samples[] = {
    { "Font0 2x        adv 12",  &fonts::Font0,            2, 12 },
    { "AsciiFont8x16   adv  8",  &fonts::AsciiFont8x16,    1,  8 },
    { "FreeMono9pt     adv 11",  &fonts::FreeMono9pt7b,    1, 11 },
    { "FreeMonoBold9pt adv 11",  &fonts::FreeMonoBold9pt7b,1, 11 },
    { "FreeMono12pt    adv 14",  &fonts::FreeMono12pt7b,   1, 14 },
  };

  int y = 4;
  for (const Sample& s : samples) {
    gfx.setFont(&fonts::Font0);
    gfx.setTextSize(1);
    gfx.setTextColor(theme::accent, theme::bg);
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "%s   %d cols", s.name, kScreenW / s.adv);
    gfx.drawString(hdr, 2, y);

    gfx.setFont(s.font);
    gfx.setTextSize(s.size);
    gfx.setTextColor(theme::text, theme::bg);
    gfx.drawString(row.substr(0, kScreenW / s.adv).c_str(), 2, y + 11);
    y += 60;
  }
  gfx.setTextSize(1);
  gfx.setFont(&fonts::Font0);
}

void injectHold(int x, int y) { handleKeysLongPress(x, y); }
void injectDrag(int dx) {
  if (g_view != View::Zoom) return;
  panByPixels(dx);
  g_follow = false;
  g_dirty = true;
}

// Vertical counterpart. Works in both views, because both show an 11-row
// window and any program taller than that needs to scroll.
void injectDragV(int dy) {
  panByRows(dy);
  g_follow = false;
  g_dirty = true;
}
void showMessage(const std::string& title, const std::string& body) { message(title, body); }
void notifyUnlocked() { announceUnlock(); }
#if defined(SK_HOST)
unsigned long gridPaints() { return g_gridPaints; }
unsigned long bandPaints() { return g_bandPaints; }
#else
unsigned long gridPaints() { return 0; }
unsigned long bandPaints() { return 0; }
#endif
bool loadProgramTextPublic(const std::string& t) { return loadProgramText(t); }
bool applyProgramTextPublic(const std::string& t) { return applyProgramText(t); }
void lockDevice() { relock(); }

void begin() {
  theme::setDay(Store::dayMode());   // before a single pixel is drawn
  int sc, sr; char sd;
  Store::startPoint(sc, sr, sd);
  // A stored entry point only applies while it is allowed to move; otherwise
  // the program starts where IRCIS would start it, whatever was saved.
  if (!Store::startEditable()) { sc = 0; sr = 0; sd = 'E'; }
  run::setStart(sc, sr, sd);
  run::setSpeed((run::Speed)Store::runSpeed());
  // Locked, the packed program is not in the list, so open on an example.
  g_edit = prog::Program();
  if (!Store::unlocked()) g_edit.loadProgram(1);
  g_wasUnlocked = Store::unlocked();   // so an already-open device stays quiet
  g_gridRow = 0;
  if (tabSlot(g_tab) < 0) g_tab = Tab::Run;

  // run::begin() has already loaded its default. Hand it whatever we actually
  // selected, or the interpreter runs one program while the screen draws
  // another -- which is exactly what happened when the two could differ.
  // Anything that is not an explicit ZOOM -- including the retired 320 px
  // split view still sitting in NVS from an older build -- opens as WIDE,
  // unless the program is small enough to be shown large.
  syncViewToProgram();
  run::load(g_edit);
  markLoaded();

  g_modal = Modal::Splash;
  g_dirty = true;
}

void tick() {
  int32_t tx, ty;
  bool touched = gfx.getTouch(&tx, &ty);
  uint32_t now = plat::millis();

  if (touched && !g_wasTouched) {
    g_pressX = tx; g_pressY = ty; g_dragLastX = tx; g_dragLastY = ty; g_dragged = false;
    g_pressMs = now;
    g_dragArmed = (g_modal == Modal::None && g_tab == Tab::Run && g_view == View::Zoom &&
                   ty >= zoomOriginY() && ty < zoomOriginY() + zoomHeight());
    // The SETS list also waits for release, so holding an entry can offer to
    // delete it instead of selecting it.
    bool setsList = (g_modal == Modal::None && g_tab == Tab::Keys &&
                     ty >= kBodyY && ty < kTabY);
    g_deferTap = g_dragArmed || setsList;
    if (!g_deferTap && now - g_lastTouchMs > 120) {
      g_lastTouchMs = now;
      onTap(tx, ty);
    }
  }
  else if (touched && g_wasTouched && g_dragArmed) {
    int dx = tx - g_dragLastX;
    int dy = ty - g_dragLastY;
    bool moved = false;
    if (dx >= kZoomCellW || dx <= -kZoomCellW) {
      panByPixels(dx);
      g_dragLastX = tx;
      moved = true;
    }
    int step = cellH();
    if (dy >= step || dy <= -step) {
      panByRows(dy);
      g_dragLastY = ty;
      moved = true;
    }
    if (moved) {
      g_dragged = true;
      g_follow = false;                           // manual control takes over
      g_dirty = true;
    }
  }
  else if (!touched && g_wasTouched && g_deferTap) {
    if (!g_dragged && now - g_lastTouchMs > 120) {
      g_lastTouchMs = now;
      // Press coordinates, not release: resistive touch gets noisy as the
      // pressure drops.
      if (!g_dragArmed && now - g_pressMs >= kLongPressMs)
        handleKeysLongPress(g_pressX, g_pressY);
      else
        onTap(g_pressX, g_pressY);
    }
    g_dragArmed = false;
    g_deferTap = false;
  }
  g_wasTouched = touched;

  static uint32_t lastDraw = 0;
  static uint32_t lastStep = 0xFFFFFFFF;
  static bool lastRunning = false;
  static bool lastFinished = false;
  run::Snapshot snap = run::snapshot();
  if (snap.running != lastRunning) { lastRunning = snap.running; g_dirty = true; }
  if (snap.finished && !lastFinished) {
    lastFinished = true;
    if (!run::output().empty()) {
      // Straight to the output, which is what a dialog about the output was
      // standing in front of. If something else is open, leave it alone and
      // let the tab's own flag say there is something to look at.
      if (g_modal == Modal::None) {
        g_tab = Tab::Out;
        g_outTop = 0;
        g_outputUnseen = false;
      }
      else g_outputUnseen = true;
      g_dirty = true;
    }
  }
  else if (!snap.finished) lastFinished = false;

  if (g_dirty) {
    // A header-only repaint leaves the grid alone: it is the same grid, and
    // redrawing it is the whole cost.
    if (g_headerOnly && g_tab == Tab::Run && g_modal == Modal::None) {
      drawHeader(snap); g_headerSig = headerSignature(snap);
    }
    else if (g_bandOnly && g_tab == Tab::Run && g_modal == Modal::None) {
      drawRunnerList(snap); g_bandSig = bandSignature(snap);
    }
    else if (g_bodyOnly && g_modal == Modal::None) {
      drawBody(snap); g_bandSig = 0;
    }
    else { drawAll(snap); g_bandSig = 0; g_headerSig = 0; }
    g_dirty = false;
    g_headerOnly = false;
    g_bodyOnly = false;
    g_bandOnly = false;
    lastDraw = now;
    g_lastRunPaintMs = now;
    lastStep = snap.step;
    g_outVersion = run::outputVersion();
    return;
  }

  if (now - lastDraw < 33) return;
  lastDraw = now;

  if (g_modal != Modal::None) return;

  if (g_tab == Tab::Run) {
    if (snap.step != lastStep) {
      followRunner(snap);
      if (g_dirty) {
        drawAll(snap); g_dirty = false; g_headerOnly = false;
        g_bandSig = 0; g_headerSig = 0;
        g_lastRunPaintMs = now; lastStep = snap.step; return;
      }
      // A restart winds the step count back, and the old trails have to go.
      const bool restarted = snap.step < lastStep;
      const bool due = restarted || !snap.running ||
                       (uint32_t)(now - g_lastRunPaintMs) >= kRunPaintMs;
      if (!due) return;                  // let it get on with running
      if (restarted) { drawAll(snap); g_bandSig = 0; g_headerSig = 0; }
      else {
        // The buttons only need repainting when one of them would look
        // different; the rest of the time it is just the counter.
        uint32_t hs = headerSignature(snap);
        if (hs != g_headerSig) { drawHeader(snap); g_headerSig = hs; }
        else                   drawHeaderStep(snap);
        drawRunners(snap);
        // The band is drawn in WIDE, and in ZOOM when the program leaves room
        // for it. Refresh it on the same terms, or the output readout would
        // sit stale under a small program.
        if (wideView() || zoomOnly()) {
          // Only when it would actually look different.
          uint32_t sig = bandSignature(snap);
          if (sig != g_bandSig) { drawRunnerList(snap); g_bandSig = sig; }
        }
      }
      g_lastRunPaintMs = now;
      lastStep = snap.step;
    }
  }
  else if (g_tab == Tab::Out) {
    uint32_t v = run::outputVersion();
    if (v != g_outVersion) { g_outVersion = v; drawOut(); }
  }
  else if (g_tab == Tab::Keys) {
    // The keys appear partway through the run; refresh while it is moving.
    if (snap.step != lastStep) { drawKeys(); lastStep = snap.step; }
  }
}

} // namespace ui
