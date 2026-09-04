// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#include <lgfx/utility/lgfx_qrcode.h>
#include "Ui.h"

#include <algorithm>
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
#include "Version.h"
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

// `big` sets the label in Font2, the face the tab bar and the status-bar
// title use. Compact controls stay on the small face; pages of labelled
// settings read better in the larger one.
void drawBtn(const Btn& b, bool on = false, bool enabled = true, bool big = false) {
  // A disabled button loses its fill as well as its text colour, so a green
  // "LOAD+RUN" does not still read as available when there is nothing to load.
  uint16_t bg = !enabled ? theme::panel : (on ? b.fg : b.bg);
  uint16_t fg = !enabled ? theme::dim : (on ? theme::bg : b.fg);
  gfx.fillRoundRect(b.x, b.y, b.w, b.h, 3, bg);
  gfx.drawRoundRect(b.x, b.y, b.w, b.h, 3, enabled ? theme::line : theme::panel);
  gfx.setFont(big ? (const lgfx::IFont*)&fonts::Font2
                  : (const lgfx::IFont*)&fonts::Font0);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.setTextColor(fg, bg);
  gfx.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2);
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.setFont(&fonts::Font0);
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

// Small-font text wrapped to the panel width. A death reason runs to 56
// characters, which is wider than the panel at the content font, and clabel
// does not wrap -- the tail would simply be cut off at the screen edge.
// Returns the y below the last line, and stops short of 'bottom'. With
// draw = false it measures instead, so a caller can refuse to start something
// that will not fit whole rather than lose the tail of it.
int wrapped(int x, int y, const char* s, uint16_t fg, int lineH, int bottom,
            bool draw = true) {
  gfx.setFont(&fonts::Font0);
  gfx.setTextSize(1);
  gfx.setTextColor(fg, theme::panel);
  gfx.setTextDatum(textdatum_t::top_left);
  const int avail = kScreenW - x - 12;
  char line[160] = {0}, word[64], trial[224];
  bool any = false;
  const char* p = s;
  while (*p && y + lineH <= bottom) {
    int w = 0;
    while (*p == ' ') ++p;
    while (*p && *p != ' ' && w < (int)sizeof(word) - 1) word[w++] = *p++;
    word[w] = 0;
    if (!w) break;
    snprintf(trial, sizeof(trial), any ? "%s %s" : "%s%s", line, word);
    // A single word wider than the line has nowhere to break, so let it run on
    // rather than loop forever; no interpreter message contains one.
    if (!any || gfx.textWidth(trial) <= avail) {
      snprintf(line, sizeof(line), "%s", trial);
      any = true;
    } else {
      if (draw) gfx.drawString(line, x, y);
      y += lineH;
      snprintf(line, sizeof(line), "%s", word);
    }
  }
  if (any && y + lineH <= bottom) { if (draw) gfx.drawString(line, x, y); y += lineH; }
  return y;
}

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

enum class Tab : uint8_t { Run, Out, Prog, Edit, Keys, Save, Sys, COUNT };
const char* kTabNames[] = { "RUN", "OUT", "PROG", "EDIT", "SETS", "SAVE", "SYS" };
// Defined with the editor keyboards, below: which one a tap on EDIT brings up.
const char* edCurKb();

// Which tabs exist depends on whether the pack has been unlocked. Locked,
// this is an ordinary IRCIS interpreter: you pick a program, edit it and run
// it. The parameter editor, the saved sets and the presets are not merely
// disabled -- they are absent, along with the packed program itself.
// Six tabs either way, so the bar geometry never changes: w = 80, centres at
// 40, 120, 200, 280, 360, 440. EDIT and SAVE mean different things in the two
// modes -- a program editor and SD program files while locked, the packed
// program's parameters and presets once unlocked. PROG and SETS swap places.
// Plain mode has five tabs, not six: PROGRAMS lists the built-ins and your
// saved files together, so there is nothing left for a SAVE tab to hold. The
// unlocked build keeps six -- its SAVE tab is parameter presets, which is a
// different thing that happens to have shared a name.
const Tab kTabsLocked[]   = { Tab::Run, Tab::Out, Tab::Edit, Tab::Prog,
                              Tab::Sys };
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
                             Splash, Shortcuts, Learn };

Tab   g_tab = Tab::Run;

// Whether the on-screen keyboards are wanted. With a real keyboard attached
// and chosen they are not, and neither is the tab that cycles them.
inline bool onScreenKeys() { return !(Store::hardwareKeys() && plat::haveKeyboard()); }
// Whether to put the shortcut letter into a label. Only worth the clutter for
// somebody actually working from the keys.
inline bool keyHints() { return Store::hardwareKeys() && plat::haveKeyboard(); }
Modal g_modal = Modal::None;
bool  g_dirty = true;          // Narrower still: the handful of cells a keystroke or a cursor move actually
// The view-tag reader and its record of the last tag applied, defined with
// the editor's flush; the boot and lock paths further up need them too.
void applyViewTags(const std::string& text);
std::string tagIn(const std::string& text);
extern std::string g_appliedTag;
// What the next frame has to repaint, as bits that compose: a tap that
// changed the cursor and the readout asks for the grid and the header and
// gets exactly those. PaintAll is the whole screen, and it is sticky: once
// something has asked for it, no partial request left standing from earlier
// in the tick can talk it back down to a partial one -- which is how a
// full repaint used to lose the keyboard's ring or the tab bar.
enum : uint16_t {
  // full repaint of the current screen
  // Set alongside g_dirty when a tap changed nothing below the header --
  // transport, speed. A full repaint redraws the grid one cell at a time, and
  // for a large program that is hundreds of them, which on the board shows up
  // as the program crawling back onto the screen before the button responds.
  PaintHeader   = 1 << 0,
  // Set alongside g_dirty when only the area between the header and the tab bar
  // changed -- scrolling a list, paging an output. Repainting the chrome as well
  // is what makes scrolling look like the screen blinking.
  PaintBody     = 1 << 1,
  // Narrower still: only the readout band under the program on RUN.
  PaintBand     = 1 << 2,
  // The tab bar, drawn on top of whichever of the three above was asked for.
  // The RUN tab carries the play/pause glyph and the EDIT tab names the next
  // keyboard, so both change without the grid changing.
  PaintTabs     = 1 << 3,
  // Narrower again: the editor's grid, without its keyboard. Moving the cursor
  // or typing a character changes two cells and the readout; the thirty-three
  // keys underneath are identical either way.
  PaintEdGrid   = 1 << 4,
  // The program on RUN and the arrows under it, without the readout below them.
  // Scrolling the grid does not change what a program has printed, and clearing
  // the output to redraw it identically is just a flicker.
  PaintRunGrid  = 1 << 5,
  // The editor's keyboard, without the grid above it. Cycling between the IRCIS
  // page and the letter pages changes thirty keys and nothing else.
  PaintEdKeys   = 1 << 6,
  // And the two things on the editor's status bar that change as you type --
  // the cursor readout and whether undo and redo are live. Repainting the whole
  // bar for a keystroke made it flicker on every press.
  PaintEdHead   = 1 << 7,
  // The part of the dialog that is up which changes as you use it: the
  // value being typed into the picker, the inspector's cell and buttons,
  // the WiFi dialog's buttons, a paged dialog's page. Each dialog knows how
  // to draw only that; the frame around it stays as it was painted.
  PaintModal    = 1 << 8,
  // One tile on SYS, named by g_sysTile. A toggle used to repaint the page.
  PaintSysTile  = 1 << 9,
  // One row of the parameter page, named by g_editRow, and the REVERT ALL
  // button whose state follows it.
  PaintEditRow  = 1 << 10,
  PaintAll      = 1 << 15,
};
uint16_t g_paint = 0;
void wantAll()   { g_paint |= PaintAll;   g_dirty = true; }
void wantModal() { g_paint |= PaintModal; g_dirty = true; }
int g_sysTile = -1;
int g_editRow = -1;

// touched. Up to three -- the character replaced, the cell the cursor left,
// the cell it arrived on. If the window scrolls under the cursor the whole
// grid has to go anyway, and edFollow says so.
struct EdCell { int16_t row, col; };
EdCell g_edCells[3];
int    g_edCellCount = 0;
// Set whenever something has painted over the tab bar -- a full repaint, a
// modal closing, a palette change. drawTabs() skips a tab whose appearance is
// unchanged, which is only safe while what is on the panel is still there.
bool  g_tabsStale = true;
// Set by markEdited: something has asked the machine to rebuild, so a repaint
// is already on its way and whoever asked need not queue another.
bool  g_rebuildPending = false;
// Set by the reset button: the machine is going back to the top, but the grid
// it runs on is the same grid. Every other rebuild -- an edit, a different
// program -- changes the characters and needs the whole body.
bool  g_resetSameGrid = false;
// Whether the device was open last time the lock state was looked at, so the
// unlock is announced once rather than on every repaint.
bool  g_wasUnlocked = false;

// The RUN tab repaints at most this often while a program is moving. At QUICK
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
// The file this program came from, or was last saved to, and which of the two
// stores it lives in. Saving over that one is what you meant; saving over any
// other is worth a question.
std::string g_progFile;
// Which folder PROG is showing. Empty is the top of the list, where the
// folders live. One level and no deeper, so there is always exactly one way
// back up and no breadcrumb to draw.
std::string g_progDir;
// The folders across both stores, and how many programs are in each.
std::vector<std::string> g_progFolders;
std::vector<int>         g_progFolderCount;

// A stored name is "Counting/odds" or just "odds"; these split it.
std::string folderOf(const std::string& path) {
  const std::size_t sl = path.find('/');
  return sl == std::string::npos ? std::string() : path.substr(0, sl);
}
std::string leafOf(const std::string& path) {
  const std::size_t sl = path.find('/');
  return sl == std::string::npos ? path : path.substr(sl + 1);
}
// A leaf saved into whichever folder is open.
std::string inProgDir(const std::string& leaf) {
  return g_progDir.empty() ? leaf : g_progDir + "/" + leaf;
}
plat::Where g_progWhere = plat::Where::Device;

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

// Every cell edit, so it can be taken back. A resistive panel mis-reads taps
// often enough that typing into the wrong cell is routine, and without this
// the only way back is remembering what was there. Bounded: one edit is four
// bytes, and 128 of them is further back than anyone reaches.
struct CellEdit { int16_t row, col; char was, now; };
std::vector<CellEdit> g_undo;      // oldest first
std::size_t g_undoAt = 0;          // how many of them are currently applied
constexpr std::size_t kUndoMax = 128;

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

// ZOOM shows 34 columns and 11 rows. A program that fits inside that has
// nothing to gain from the small font, so it is shown large and the WIDE/ZOOM
// toggle is not offered at all.
bool zoomOnly();
// The window onto the program: its leftmost column (the top row is g_gridRow,
// declared with the run page). One window for both pages and both views,
// which is what makes moving between them seamless: nothing to carry across.
int  g_gridCol = 0;

// Readout lines under the program. The runner list wants four; the output
// only ever shows a tail, so it gives a row back to the program.
// Both readouts are the depth of the EDIT page's keyboard block, so the
// program sits in the same rectangle on both pages. Jumping between them then
// does not move the grid, and the output readout gains a line into the
// bargain. 3 rows of 26 is 78; 4 lines of kContentH plus the 2 below is 78.
constexpr int kBandLines       = 4;
constexpr int kBandLinesOutput = 4;

// What sits under the program on RUN: the output, the runners, or nothing.
// One setting with three values. It used to be two toggles that disagreed --
// DEBUG silently beat RUN VIEW, which went on reading OUTPUT while the
// runners were on screen. Still kept in the two NVS keys it always used, so a
// device coming from an older build keeps whatever it was set to.
//
// RUNNERS also brings up the single-step buttons: looking at the runners and
// stepping through them are the same job, which is why they were one switch.
inline bool bandRunners() { return Store::debugMode(); }
inline bool bandOutput()  { return !Store::debugMode() && Store::runView() == 0; }
inline bool runViewNone() { return !bandRunners() && !bandOutput(); }

enum { kBandOutput = 0, kBandRunners = 1, kBandNothing = 2 };
int bandMode() {
  return bandRunners() ? kBandRunners : bandOutput() ? kBandOutput : kBandNothing;
}
void setBandMode(int m) {
  Store::setDebugMode(m == kBandRunners);
  Store::setRunView(m == kBandNothing ? 1 : 0);
}

// The row carrying the scroll arrows, and the grid height above it.
int runnerListY();
// The height the program has to itself on RUN. With the readout switched off
// it runs down to the tab bar, which is the point of switching it off.
int wideBandH() {
  return (runViewNone() ? (kTabY - 6) : (runnerListY() - 4)) - kWideY;
}

int edBandH();
// The band both pages centre a program in. It is the editor's, which is the
// smaller of the two whenever RUN has no readout under the program -- and if
// each page centred in its own, a program with no readout sat on different
// lines on the two pages. RUN still SHOWS the extra rows its taller band
// holds; it just does not push the program down into them.
int centringBandH() { return edBandH(); }
// ...and how many rows of a given height that band holds. Centring has to use
// this rather than the rows the page itself shows: RUN with no readout shows
// more of a tall program than the editor can, and centring on its own count
// put the two a couple of pixels apart.
int centringRows(int cellH) { const int n = centringBandH() / cellH; return n < 1 ? 1 : n; }

int gridRows() {
  const int avail = wideBandH();
  int n = avail / kWideCellH;
  // No flat cap. Eleven rows left a blank strip above the readout, and -- the
  // reason it mattered -- the editor has no such cap, so the same program was
  // a row shorter on RUN than on EDIT and everything measured from the foot
  // of the grid, the scroll arrow included, sat a line higher on one page.
  return n < 1 ? 1 : n;
}

// How many rows the current view actually shows. Both views size themselves
// to what is left under the program. Everything that scrolls, follows or
// clips has to ask this rather than gridRows(), or it works to the wrong
// window in ZOOM.
int gridRowsShown();
int maxGridRow();
int gridBandH() { return gridRows() * kWideCellH; }
// Under the program, wherever the program actually ends. This was always the
// WIDE calculation, so in ZOOM the scroll row -- and the fill that clears the
// band under it -- sat where the WIDE grid would have finished and cut into
// the zoomed one, taking the bottom rows off it.
int zoomOriginY();
void drawEdgeBars();          // defined with the editor's grid helpers
int zoomHeight();
int shiftRowY() {
  if (g_view == View::Zoom) return zoomOriginY() + zoomHeight() + 3;
  return kWideY + gridBandH() + 3;
}

// Centre the program when it is narrower than the window: a 25-column program
// sat against the left edge with half the screen empty beside it.
int gridRows();
int wideOriginY() {
  const int rows = centringRows(kWideCellH);
  const int h = g_edit.rows() < rows ? g_edit.rows() : rows;
  // Centred in the pixels available, the way the editor does it. Centring by
  // whole rows instead put the same program two pixels apart on the two pages.
  const int y = kWideY + (centringBandH() - h * kWideCellH) / 2;
  return y > kWideY ? y : kWideY;
}

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


int zoomRows();
void toggleView();
int edZoomRows();
int edZoomCols();
bool zoomOnly() {
  // The packed program is meant to be read across its whole width, so it opens
  // WIDE however small it is rather than being zoomed because it happens to
  // fit. ZOOM is still there if you want it.
  if (g_edit.isPacked()) return false;
  // It has to fit zoomed on BOTH pages, or the button would vanish on one of
  // them and leave no way back to the view the other page is showing. The
  // editor's window is the smaller of the two: a cell there is sized to be
  // tapped, so fewer of them fit.
  const int cols = kZoomCols   < edZoomCols() ? kZoomCols   : edZoomCols();
  const int rows = zoomRows()  < edZoomRows() ? zoomRows()  : edZoomRows();
  return g_edit.cols() <= cols && g_edit.rows() <= rows;
}

// A program that fits entirely leaves room underneath for the runner list,
// which ZOOM otherwise gives up in exchange for the whole body.
int runnerListY();          // the top of the readout band, defined with it

// Where the zoomed grid has to stop. A readout is only drawn under a program
// small enough to be shown large, and it is three lines for the output and
// four for the runners -- so ask what is actually there rather than reserving
// a fixed four lines for it.
int zoomBottom() {
  // The scroll row lives between the grid and the readout, so its height has
  // to come off the grid's. Without that the grid ran right down to the
  // readout and the arrows were drawn on top of it -- which is why scrolling
  // wiped "no output yet", and why the readout repainting afterwards looked
  // like the program being redrawn.
  //
  // The readout has to come off the height whenever it is SHOWN, not only for
  // a program small enough to be zoom-only. Asking about zoomOnly() here meant
  // a taller program in ZOOM was measured as if it ran all the way down to the
  // tab bar: the readout was then drawn over its last rows, and because
  // zoomRows() had counted those rows as visible, maxGridRow() was short by
  // exactly as many and the last line could not be scrolled to at all.
  if (runViewNone()) return kTabY - 4;
  return runnerListY() - 4;
}

// And how many rows of it fit above that. This used to be flatly eleven,
// which is where the two worst display bugs came from: a ten- or eleven-row
// program does not fit beside a readout at 23 px a row, so the grid was
// centred, clamped to the top and drawn straight through the readout -- the
// runner and the characters it crossed showing through the output. The same
// assumption told maxGridRow() there was nothing to scroll to, which is why
// the scroll chevrons never appeared on exactly those programs.
// The top of the zoomed grid, clear of the status bar and of the bar that
// scrolls up. WIDE gets this from kWideY; ZOOM centres, so it needs its own.
int zoomTop() { return kBodyY + 4; }

int zoomRows() {
  const int n = (zoomBottom() - zoomTop()) / kZoomCellH;
  return n < 1 ? 1 : n;
}

// Centre on the PROGRAM when it is narrower or shorter than the window, so a
// small program sits in the middle of the screen rather than up in a corner.
int zoomOriginX() {
  int w = g_edit.cols() < kZoomCols ? g_edit.cols() : kZoomCols;
  int x = (kScreenW - w * kZoomCellW) / 2;
  return x > 0 ? x : 0;
}
int zoomOriginY() {
  const int rows = centringRows(kZoomCellH);
  int h = g_edit.rows() < rows ? g_edit.rows() : rows;
  int y = zoomTop() + (centringBandH() - h * kZoomCellH) / 2;
  return y > zoomTop() ? y : zoomTop();
}
int zoomHeight() {
  int h = g_edit.rows() < zoomRows() ? g_edit.rows() : zoomRows();
  return h * kZoomCellH;
}

// Pick the view for whatever program has just been loaded: forced for one that
// fits, otherwise back to whatever the user last chose.
void syncViewToProgram() {
  g_view = zoomOnly() ? View::Zoom
         : ((Store::gridView() == (int)View::Zoom) ? View::Zoom : View::Wide);
  g_gridRow = 0;
  g_gridCol = 0;
}
bool g_follow = true;         // ZOOM: keep the active runner in view

int cellW() { return g_view == View::Zoom ? kZoomCellW : kWideCellW; }
int cellH() { return g_view == View::Zoom ? kZoomCellH : kWideCellH; }
// How many columns the window shows: the same on both pages, since the cell is.
int visibleCols() { return g_view == View::Zoom ? kZoomCols : kWideCols; }
int maxGridCol()  { const int m = g_edit.cols() - visibleCols(); return m > 0 ? m : 0; }
// Clamped to the loaded program: a fixed overhang would let a narrow one be
// scrolled clean off the side of the screen.
void setGridCol(int col) {
  const int max = maxGridCol();
  g_gridCol = col < 0 ? 0 : (col > max ? max : col);
}

// Pan by a pixel delta; content follows the finger. Separated out so the
// arithmetic is testable without synthesising touch events.

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



// Put the tapped cell in the middle of the ZOOM window and stay there: the
// point of aiming is that the view does not then wander off following a
// runner somewhere else.
void zoomToCell(int row, int col) {
  Store::setGridView((int)View::Zoom);
  g_view = View::Zoom;
  setGridCol(col - kZoomCols / 2);
  int maxRow = g_edit.rows() - zoomRows();
  if (maxRow < 0) maxRow = 0;
  int want = row - zoomRows() / 2;
  if (want < 0) want = 0;
  if (want > maxRow) want = maxRow;
  g_gridRow = want;
  g_follow = false;
  g_prevRunners.clear();            // stale coordinates from the other layout
  g_paint |= PaintBody;
  g_paint |= PaintHeader;              // the ZOOM button changes state
  g_dirty = true;
}




// Returns false when the cell is not currently on screen, which in ZOOM is the
// common case -- callers use that to skip drawing.
bool cellPos(int row, int col, int& x, int& y) {
  if (row < 0 || row >= g_edit.rows() || col < 0 || col >= g_edit.cols()) return false;
  if (g_view == View::Zoom) {
    if (col < g_gridCol || col >= g_gridCol + kZoomCols) return false;
    int zr = row - g_gridRow;
    if (zr < 0 || zr >= zoomRows()) return false;
    x = zoomOriginX() + (col - g_gridCol) * kZoomCellW;
    y = zoomOriginY() + zr * kZoomCellH;
    return true;
  }
  int c = col - g_gridCol;
  if (c < 0 || c >= kWideCols) return false;    // outside the column window
  int r = row - g_gridRow;
  if (r < 0 || r >= gridRows()) return false;   // outside the row window
  x = wideX() + c * kWideCellW;
  y = wideOriginY() + r * kWideCellH;
  return true;
}

bool cellAt(int px, int py, int& row, int& col) {
  if (g_view == View::Zoom) {
    if (py < zoomOriginY() || py >= zoomOriginY() + zoomHeight()) return false;
    int w = g_edit.cols() < kZoomCols ? g_edit.cols() : kZoomCols;
    if (px < zoomOriginX() || px >= zoomOriginX() + w * kZoomCellW) return false;
    row = g_gridRow + (py - zoomOriginY()) / kZoomCellH;
    col = g_gridCol + (px - zoomOriginX()) / kZoomCellW;
    return row >= 0 && row < g_edit.rows() && col < g_edit.cols();
  }
  if (py >= wideOriginY() + gridBandH()) return false;
  int shown = g_edit.cols() < kWideCols ? g_edit.cols() : kWideCols;
  if (px < wideX() || px >= wideX() + shown * kWideCellW) return false;
  row = g_gridRow + (py - wideOriginY()) / kWideCellH;
  col = (px - wideX()) / kWideCellW + g_gridCol;
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

// Every cell a runner has stood on, kept so the path can be drawn behind the
// program. Copied out of the run task once per repaint; reading it a cell at a
// time would take the mutex a thousand times a frame.
char g_trace[prog::kMaxRows * prog::kMaxCols];
// What is actually painted, so a frame only has to repaint the cells that
// became visited since the last one. A runner at QUICK crosses hundreds of
// cells between frames and drawRunners only restores the handful it left, so
// without this the path appears in fragments wherever a repaint happened to
// land.
char g_traceShown[prog::kMaxRows * prog::kMaxCols];
int  g_traceCols = 0;

void refreshTrace() {
  g_traceCols = Store::tracePath() ? run::visitsInto(g_trace, sizeof(g_trace)) : 0;
}
bool traced(int row, int col) {
  if (g_traceCols <= 0 || row < 0 || col < 0 || col >= g_traceCols) return false;
  const unsigned long i = (unsigned long)row * g_traceCols + col;
  return i < sizeof(g_trace) && g_trace[i] != 0;
}
uint16_t blend565(uint16_t from, uint16_t to, int num, int den);
// A wash rather than a fill: the character has to stay readable on top of it.
uint16_t traceBg() { return blend565(theme::accent, theme::bg, 2, 5); }

// The colours a cell is drawn in. Both pages use the same rule, so a program
// looks the same on each: quiet grey for padding, amber for a cell an edit
// changed, the accent for a parameter. RUN adds the path a run has walked
// behind the cell; the editor shows its cursor instead.
struct CellLook { uint16_t fg, bg; };
CellLook cellLook(int r, int c, bool editor) {
  if (editor && r == g_curRow && c == g_curCol) return { theme::bg, theme::accent };
  const char ch = g_edit.cell(r, c);
  const uint16_t bg = (!editor && traced(r, c)) ? traceBg() : theme::bg;
  const uint16_t fg = ch == '.' ? theme::blank
                    : g_edit.cellModified(r, c) ? theme::edited
                    : (slotAtCell(r, c) >= 0 ? theme::accent : theme::text);
  return { fg, bg };
}

// Whether the entry marker is drawn: when a tap can move the start, or when
// the program starts somewhere other than the top-left corner heading east,
// which is worth seeing whether or not you can move it.
bool showEntry() {
  return Store::gridTap() != Store::kTapNothing ||
         run::startRow() != 0 || run::startCol() != 0 || run::startDir() != 'E';
}

// Draw one cell at (x, y) on any target: the panel itself, or the row sprite
// that is pushed to it in one go. Everything that decides how a character
// looks goes through here, which is what keeps the two pages identical.
void paintCell(lgfx::LovyanGFX& t, int x, int y, int r, int c, bool editor) {
  const int cw = cellW(), chh = cellH();
  const CellLook look = cellLook(r, c, editor);
  char ch[2] = { g_edit.cell(r, c), 0 };
  t.fillRect(x, y, cw, chh, look.bg);
  // The entry marker is only meaningful when the entry point can move.
  if (!editor && showEntry() && r == run::startRow() && c == run::startCol())
    t.drawRect(x, y, cw, chh, theme::edited);
  t.setTextColor(look.fg, look.bg);
  t.setTextSize(1);
  if (g_view == View::Zoom) t.setFont(&fonts::FreeMono12pt7b);
  else                      t.setFont(&fonts::Font0);
  // Centre the glyph when the cell is taller than the font box; when they
  // match, centring rounds up into the row above, so top-align instead.
  if (t.fontHeight() < chh) {
    t.setTextDatum(textdatum_t::middle_center);
    t.drawString(ch, x + cw / 2, y + chh / 2);
  }
  else {
    t.setTextDatum(textdatum_t::top_left);
    t.drawString(ch, x, y);
  }
  t.setTextDatum(textdatum_t::top_left);
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

void paintGridRows(bool editor);   // defined with the grid geometry

#if defined(SK_HOST)
unsigned long g_gridPaints = 0;   // instrumentation, host only
unsigned long g_fullPaints = 0;   // whole-screen repaints, host only
#endif

void drawGrid() {
#if defined(SK_HOST)
  ++g_gridPaints;
#endif
  refreshTrace();
  // The whole grid is about to be painted, so what is shown becomes what is
  // known.
  std::memcpy(g_traceShown, g_trace, sizeof(g_traceShown));
  gfx.setTextDatum(textdatum_t::top_left);

  // The visible window grows when the readout is switched off or the view
  // changes, and an offset left over from the shorter window would strand the
  // program part-scrolled with no way back: maxGridRow() has dropped to zero,
  // so the chevrons that would scroll it are not drawn at all.
  {
    const int m = maxGridRow();
    if (g_gridRow > m) g_gridRow = m;
    if (g_gridRow < 0) g_gridRow = 0;
    setGridCol(g_gridCol);
  }
  paintGridRows(false);

  // The entry point's heading, drawn AFTER the grid: it sits in the margin of
  // a neighbouring cell, and that cell's own background fill would erase it.
  if (showEntry()) {
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
  const CellLook look = cellLook(row, col, false);
  drawCell(row, col, look.fg, look.bg);
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
  refreshTrace();      // the path grows while the run is going
  if (g_traceCols > 0) {
    // Only the newly-walked cells, and only the ones on screen.
    const int rowTo = g_gridRow + gridRowsShown() < g_edit.rows()
                    ? g_gridRow + gridRowsShown() : g_edit.rows();
    const int from = g_gridCol;
    int to = from + visibleCols();
    if (to > g_edit.cols()) to = g_edit.cols();
    for (int r = g_gridRow; r < rowTo; ++r)
      for (int c = from; c < to; ++c) {
        const unsigned long i = (unsigned long)r * g_traceCols + c;
        if (i >= sizeof(g_trace) || c >= g_traceCols) continue;
        if (g_trace[i] && !g_traceShown[i]) {
          g_traceShown[i] = g_trace[i];
          restoreCell(r, c);
        }
      }
  }
  for (const auto& p : g_prevRunners) restoreCell(p.row, p.col);
  g_prevRunners.clear();

  // Tails only make sense while you can follow them. At FAST/FULL a runner
  // crosses hundreds of cells between frames, so the tail is just noise --
  // and redrawing it would cost more than the run itself.
  run::Speed sp = run::speed();
  // TRAIL already keeps the whole path on screen. Fading tails on top of it
  // are a second set of marks over the same cells, so it is one or the other.
  bool trails = (sp == run::Speed::Slow) && !Store::tracePath();

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
    case run::Speed::Medium:  return "MED";
    case run::Speed::Quick: return "QUICK";
    case run::Speed::Full:  return "FULL";
  }
  return "?";
}

// Transport, right-anchored, in a fixed 142 px strip so the speed and view
// buttons to its left never move:  |<   <   >   >|
//
// Play and pause are not here. Tapping the RUN tab while RUN is open has
// always been the play/pause shortcut, and the tab is the biggest target on
// the screen; the tab draws the triangle or the bars to say so. That leaves
// the strip to the four buttons that have nowhere else to live, and they get
// the freed width -- 68 px each normally, 34 with the single-step buttons on
// (SYS > STEP BUTTONS), against 44 and 26 before.
// ZOOM sits in the same place on RUN and on EDIT. It says the same thing on
// both, and a button that jumps when the page changes reads as two buttons.
constexpr int kViewBtnX = 228;
constexpr int kViewBtnW = 50;
constexpr int kTransportX = kScreenW - 142;
inline bool steps() { return Store::stepButtons(); }
Btn btnEnd()   { return steps() ? Btn{ kScreenW -  34, 2, 34, 18, "", theme::text, theme::panel }
                                : Btn{ kScreenW -  70, 2, 68, 18, "", theme::text, theme::panel }; }
Btn btnFwd()   { return { kScreenW -  70, 2, 34, 18, "", theme::text, theme::panel }; }
Btn btnBack()  { return { kScreenW - 106, 2, 34, 18, "", theme::text, theme::panel }; }
Btn btnStart() { return { kTransportX, 2, steps() ? 34 : 68, 18, "", theme::text, theme::panel }; }
Btn btnSpeed()  { return { kViewBtnX + kViewBtnW + 4, 2, 34, 18, "FAST", theme::good, theme::panel }; }

// OUT's two controls live in the status bar with every other tab's, rather
// than floating over the top of the output itself. SAVE SD is drawn only when
// there is a card to save to -- a permanently greyed button is a question the
// device already knows the answer to -- so COLOUR slides right into its place
// when there is not.
constexpr int kOutBtnW = 62;
Btn btnOutSd()     { return { kScreenW - kOutBtnW - 4, 2, kOutBtnW, 18, "SAVE SD" }; }
Btn btnOutColour() {
  const int x = plat::sdPresent() ? kScreenW - 2 * kOutBtnW - 8 : kScreenW - kOutBtnW - 4;
  return { x, 2, kOutBtnW, 18, "COLOUR", theme::good };
}
Btn btnView()   { return { kViewBtnX, 2, kViewBtnW, 18, "ZOOM", theme::edited, theme::panel }; }

// Transport symbols, drawn rather than lettered.
enum class Glyph : uint8_t { Start, Back, Play, Pause, Fwd, End };
// Play, from a bar: the run is over and this starts it again from the top.
// The same weight as the other transport glyphs, which a curved arrow at this
// size was not.
void drawAgain(const Btn& b, uint16_t c);

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

void drawAgain(const Btn& b, uint16_t c) {
  const int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
  gfx.fillRect(cx - 7, cy - 5, 2, 10, c);
  gfx.fillTriangle(cx - 3, cy - 5, cx - 3, cy + 5, cx + 6, cy, c);
}

// The editor's status bar carries two controls: the program's name, and its
// size. Both open a dialog. They live up here because the body is entirely
// spoken for -- grid above, keyboard below.
// The editor's status bar, left to right after the title: the program's name,
// its size, the zoom toggle, SAVE, and then the cursor readout at the right
// edge. Saving belongs where the editing and the renaming happen, not two tabs
// away, so the name field gives up the width for it.
bool canUndo();
bool canRedo();
void clearUndo();

// The title shrank to "EDIT" -- the tab bar already says which page this is --
// which is what makes room for UNDO and REDO on the same row.
Btn btnEdName() { return {  42, 2, 116, 18, "", theme::text, theme::panel }; }
Btn btnEdSize() { return { 162, 2,  62, 18, "", theme::text, theme::panel }; }
Btn btnEdZoom() { return { kViewBtnX, 2, kViewBtnW, 18, "ZOOM", theme::edited, theme::panel }; }
Btn btnEdSave() { return { 282, 2,  50, 18, "SAVE", theme::good,  theme::panel }; }
Btn btnEdUndo() { return { 336, 2,  34, 18, "UNDO", theme::text,  theme::panel }; }
Btn btnEdRedo() { return { 374, 2,  34, 18, "REDO", theme::text,  theme::panel }; }
// The command list, one tap from where you are actually writing a program.
Btn btnEdHelp() { return { 412, 2,  20, 18, "?", theme::accent, theme::panel }; }
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
// "step" does not change; the number after it does, several times a second.
// They used to be one right-aligned string, so every new digit shifted the
// word and the word had to be repainted with it. The word is anchored and the
// number runs rightwards from a fixed point, leaving only the digits to
// clear -- six or seven characters' worth instead of the whole readout.
constexpr int kStepWordX = 156;
constexpr int kStepNumX  = kStepWordX + 5 * 6;   // just past "step "

void drawHeaderStepWord() {
  gfx.setFont(&fonts::Font0);
  gfx.setTextDatum(textdatum_t::middle_left);
  gfx.setTextColor(theme::dim, theme::panel);
  gfx.drawString("step", kStepWordX, kHeaderH / 2);
  gfx.setTextDatum(textdatum_t::top_left);
}

void drawHeaderStep(const run::Snapshot& snap) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%u", (unsigned)snap.step);
  const int right = btnView().x - 8;
  gfx.fillRect(kStepNumX, 1, right - kStepNumX, kHeaderH - 2, theme::panel);
  gfx.setFont(&fonts::Font0);
  gfx.setTextDatum(textdatum_t::middle_left);
  gfx.setTextColor(snap.finished ? theme::good : theme::text, theme::panel);
  gfx.drawString(buf, kStepNumX, kHeaderH / 2);
  gfx.setTextDatum(textdatum_t::top_left);
}

// The parts of the editor's status bar that a keystroke changes, and nothing
// else: the two history buttons and the cursor readout to the right of them.
void drawEdHeadBits() {
  drawBtn(btnEdUndo(), false, canUndo());
  drawBtn(btnEdRedo(), false, canRedo());
  // The clear has to start past the help button, or it wipes it: the readout
  // is right-aligned and only needs the room its own text takes.
  const int x0 = btnEdHelp().x + btnEdHelp().w + 2;
  gfx.fillRect(x0, 1, kScreenW - x0, kHeaderH - 2, theme::panel);
  drawBtn(btnEdHelp());
  char buf[24];
  snprintf(buf, sizeof(buf), "r%d c%d", g_curRow, g_curCol);
  gfx.setFont(&fonts::Font0);
  gfx.setTextDatum(textdatum_t::middle_right);
  gfx.setTextColor(g_edit.modifiedCells() ? theme::edited : theme::dim, theme::panel);
  gfx.drawString(buf, kScreenW - 4, kHeaderH / 2);
  gfx.setTextDatum(textdatum_t::top_left);
}

void drawHeader(const run::Snapshot& snap) {
  gfx.fillRect(0, 0, kScreenW, kHeaderH, theme::panel);
  gfx.drawFastHLine(0, kHeaderH - 1, kScreenW, theme::line);

  char buf[64];
  if (g_tab == Tab::Run) {
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
    drawHeaderStepWord();
    drawHeaderStep(snap);

    // One toggle: filled when the zoomed view is showing.
    if (!zoomOnly()) drawBtn(btnView(), g_view == View::Zoom);
    Btn sp = btnSpeed(); sp.label = speedName(run::speed());
    drawBtn(sp);

    // Nothing to go back to when the program is already sitting at the top.
    const bool atStart = snap.step == 0;
    drawBtn(btnStart(), false, !atStart);
    drawGlyph(btnStart(), Glyph::Start, false);
    if (steps()) {
      drawBtn(btnBack(), false, snap.step > 0);
      drawGlyph(btnBack(), Glyph::Back, false);
    }
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
      case Tab::Edit: title = Store::unlocked() ? pack::str(pack::kStrEditTitle) : "EDIT"; break;
      case Tab::Keys: title = "PARAMETER SETS"; break;
      case Tab::Out:  title = "OUTPUT"; break;
      case Tab::Save: title = "PRESETS"; break;
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
      if (!zoomOnly()) drawBtn(btnEdZoom(), g_view == View::Zoom);
      drawBtn(btnEdSave());
      drawBtn(btnEdUndo(), false, canUndo());
      drawBtn(btnEdRedo(), false, canRedo());
      drawBtn(btnEdHelp());
      snprintf(buf, sizeof(buf), "r%d c%d", g_curRow, g_curCol);
    }
    else if (g_tab == Tab::Out) {
      buf[0] = 0;                            // OUT shows the run, not the edit state
      if (plat::sdPresent()) drawBtn(btnOutSd());
        }
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
  // Changing pages changes two tabs -- the one you left and the one you
  // arrived on -- so only those two are repainted. Each tab's appearance is
  // summed into a small signature and compared with what is on the panel; a
  // tab whose signature is unchanged is left exactly as it is.
  static uint32_t sig[8] = { 0 };
  static bool sigValid = false;
  static int  sigCount = 0;
  if (g_tabsStale) { g_tabsStale = false; sigValid = false; }

  const int n = tabCount();
  const int w = kScreenW / n;
  if (!sigValid || sigCount != n) { sigValid = true; sigCount = n; for (int k = 0; k < 8; ++k) sig[k] = 0xFFFFFFFFu; }

  const run::Snapshot snap = run::snapshot();
  for (int i = 0; i < n; ++i) {
    const Tab t  = tabAt(i);
    const bool on   = g_tab == t;
    const bool flag = (t == Tab::Out) && g_outputUnseen && !on;
    const int  tw   = (i == n - 1) ? kScreenW - i * w : w;

    // Everything that decides how this tab looks -- the palette included, so
    // switching between day and night repaints the bar without anything
    // having to remember to say so.
    uint32_t k = (uint32_t)t | (on ? 0x100u : 0) | (flag ? 0x200u : 0)
               | (Store::dayMode() ? 0x2000u : 0);
    if (t == Tab::Run && on)
      k |= snap.running ? 0x400u : (snap.finished ? 0x800u : 0x1000u);
    if (t == Tab::Edit && on && !Store::unlocked() && onScreenKeys())
      k |= (uint32_t)(edCurKb()[0]) << 16;
    if (keyHints()) k |= 0x4000u;
    if (k == sig[i]) continue;
    sig[i] = k;

    gfx.fillRect(i * w, kTabY, tw, kTabH, on ? theme::accent : theme::panel);
    gfx.drawRect(i * w, kTabY, tw, kTabH, flag ? theme::good : theme::line);
    if (flag) gfx.fillCircle(i * w + tw - 8, kTabY + 8, 3, theme::good);
    gfx.setFont(&fonts::Font2);
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setTextColor(on ? theme::bg : theme::text, on ? theme::accent : theme::panel);
    // Tapping the tab you are already on is the play/pause shortcut, and it
    // is the only play control there is. While RUN is the current tab the tab
    // draws what the tap would do -- a triangle, the two bars once it is
    // going, or the go-again arrow once it has finished -- instead of naming
    // a page you are already looking at. From any other tab the same tap
    // navigates, so there it still says RUN.
    if (t == Tab::Run && on) {
      Btn tb = { i * w, kTabY, tw, kTabH, "" };
      tb.fg = theme::bg;
      if (snap.finished && !snap.running) drawAgain(tb, theme::bg);
      else drawGlyph(tb, snap.running ? Glyph::Pause : Glyph::Play, false);
      continue;
    }
    const char* label = (t == Tab::Edit && on && !Store::unlocked() && onScreenKeys())
                      ? edCurKb() : kTabNames[(int)t];
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
int gridRowsShown() { return g_view == View::Zoom ? zoomRows() : gridRows(); }
int maxGridRow()    { int m = g_edit.rows() - gridRowsShown(); return m > 0 ? m : 0; }

int bandLines()   { return bandRunners() ? kBandLines : kBandLinesOutput; }
int runnerListY() { return kTabY - bandLines() * kContentH - 2; }
#define kRunnerListY runnerListY()

// The output's own pair, on the left of the same row, so the two kinds of
// scrolling stay visibly separate: the program's on the right, the text's here.
// Older and newer output, shown as an ellipsis at whichever end has more to
// see. They sit on the first and last line of the readout rather than taking a
// row of their own, which is a row the program gets instead.
Btn btnOutMoreUp()   { return { 2, kRunnerListY, 30, kContentH, "..." }; }
Btn btnOutMoreDown() { return { kScreenW - 32, kRunnerListY + (bandLines() - 1) * kContentH,
                                30, kContentH, "..." }; }

Btn btnOutUp()     { return { 4,  kWideShiftY, 26, kContentH + 2, "^" }; }
Btn btnOutDown()   { return { 34, kWideShiftY, 26, kContentH + 2, "v" }; }


// Cheap FNV-1a over the things drawRunnerList actually shows.
uint32_t bandSignature(const run::Snapshot& snap) {
  uint32_t h = 2166136261u;
  auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix((uint32_t)Store::runView() + 1u);
  mix(Store::debugMode() ? 2u : 1u);
  mix((uint32_t)g_gridCol);
  mix((uint32_t)g_gridRow);
  mix((uint32_t)g_outLine);
  mix((uint32_t)g_runnerTop);
  mix((uint32_t)g_edit.cols());
  mix((uint32_t)g_edit.rows());
  mix(snap.step == 0 ? 1u : 0u);
  // Only what the band is actually showing. Mixing runner positions when the
  // band is not showing runners made the signature change on almost every
  // step, so the row was repainted a hundred and seventy times during a run
  // that never printed anything.
  if (bandOutput()) {
    mix((uint32_t)run::output().size());
  }
  else if (bandRunners()) {
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

// The row of scroll arrows directly under the program. Separate from the
// readout below it because scrolling the grid changes which arrows are
// available and nothing else -- what has been printed is unaffected.

void drawRunnerList(const run::Snapshot& snap) {
#if defined(SK_HOST)
  ++g_bandPaints;
#endif
  // Nothing under the program: the grid above has already taken the space.
  if (runViewNone()) {
    gfx.fillRect(0, kWideShiftY, kScreenW, kTabY - kWideShiftY, theme::bg);
    return;
  }

  // A rule between the program and what it has printed. Without it the two run
  // together and the readout looks like more of the grid. The clear goes round
  // it rather than over it: repainting the rule every time a character was
  // printed made it blink, and any repaint that stopped short of redrawing it
  // left it missing.
  const int ruleY = kRunnerListY - 3;
  if (ruleY > kWideShiftY)
    gfx.fillRect(0, kWideShiftY, kScreenW, ruleY - kWideShiftY, theme::bg);
  gfx.fillRect(0, ruleY + 1, kScreenW, kTabY - (ruleY + 1), theme::bg);
  gfx.drawFastHLine(4, ruleY, kScreenW - 8, theme::line);

  int y = kRunnerListY;

  // The other readout: what the program has printed so far, growing as it
  // runs, the way you would watch the output file on a desktop build.
  if (bandOutput()) {
    const std::string out = run::output();
    if (out.empty()) {
      g_outLine = 0;
      const char* msg = snap.step == 0
                          ? (keyHints() ? "Press (p)lay to begin IRCIS."
                                        : "Press play to begin IRCIS.")
                          : "No output yet.";
      useContentFont();
      gfx.setTextDatum(textdatum_t::middle_center);
      gfx.setTextColor(snap.step == 0 ? theme::accent : theme::dim, theme::bg);
      gfx.drawString(msg, kScreenW / 2, y + bandLines() * kContentH / 2);
      gfx.setTextDatum(textdatum_t::top_left);
      gfx.setTextSize(1);
      // The commonest way a first program does nothing at all: the cell the
      // runner starts on is blank, so it sets off over empty cells and leaves
      // the grid without ever reaching a command. The editor knows this before
      // you press play, so it may as well say so.
      return;
    }
    // Wrap to the screen, then keep the last four lines: the tail is where
    // anything new appears.
    auto wrapTo = [&out](std::size_t wide) {
      std::vector<std::string> v;
      std::string cur;
      for (char c : out) {
        if (c == '\n') { v.push_back(cur); cur.clear(); continue; }
        cur.push_back(c);
        if (cur.size() >= wide) { v.push_back(cur); cur.clear(); }
      }
      if (!cur.empty()) v.push_back(cur);
      return v;
    };
    const std::size_t full = (kScreenW - 16) / kContentW;
    std::vector<std::string> lines = wrapTo(full);
    // Once there is more than fits, an ellipsis appears at each end of the
    // band, and a full-width line would run underneath it. Give those two
    // lines the room by wrapping everything a little narrower.
    if (lines.size() > (std::size_t)bandLines())
      lines = wrapTo(full > 8 ? full - 6 : full);
    // Four lines fit. g_outLine is how far back from the end we are looking:
    // zero follows the tail as it grows, anything else holds still so you can
    // read what has already gone past.
    const std::size_t rows = (std::size_t)bandLines();
    const std::size_t total = lines.size();
    std::size_t maxBack = total > rows ? total - rows : 0;
    if ((std::size_t)g_outLine > maxBack) g_outLine = (int)maxBack;
    std::size_t end = total - (std::size_t)g_outLine;
    std::size_t from = end > rows ? end - rows : 0;

    // Centred both ways, because a line or two adrift in the corner of a wide
    // panel reads as though something is missing. The OUT page has always done
    // this; the readout under the program now matches it.
    // The first line goes where a single line would sit, and the rest grow
    // downward from it. Re-centring the block every time a line was added
    // walked the whole readout up the screen as the program printed -- most
    // obviously at the end, where the last line often arrives all at once.
    // Once there are more lines than will fit below that point the band fills
    // from the top, which is also the point at which it starts scrolling.
    const int shown  = (int)(end - from);
    const int anchor = (bandLines() - 1) * kContentH / 2;
    if (anchor + shown * kContentH <= bandLines() * kContentH) y += anchor;
    for (std::size_t i = from; i < end; ++i) {
      // While following the tail the newest line is the bright one.
      const bool last = (i + 1 == total) && g_outLine == 0;
      const uint16_t fg = last ? theme::text : theme::dim;
      useContentFont();
      gfx.setTextDatum(textdatum_t::middle_center);
      gfx.setTextColor(fg, theme::bg);
      gfx.drawString(lines[i].c_str(), kScreenW / 2, y + kContentH / 2);
      gfx.setTextDatum(textdatum_t::top_left);
      gfx.setTextSize(1);
      y += kContentH;
    }
    // An ellipsis at whichever end has more, on the same line as the text
    // rather than a row of its own. Tapping one moves a screenful that way.
    if (from > 0)                            drawBtn(btnOutMoreUp());
    if ((std::size_t)g_outLine > 0)          drawBtn(btnOutMoreDown());
    return;
  }

  if (snap.step == 0) {
    clabel(12, y, keyHints() ? "Press (p)lay to begin IRCIS."
                             : "Press play to begin IRCIS.", theme::accent);
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

  const int rows = nUsed > bandLines() ? bandLines() - 1 : bandLines();
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

// True while the program grid on the body is the one clearBodyAroundGrid left
// there. Every other page clears the body through clearBody(), which says so.
bool g_bodyHasGrid = false;
int  g_bodyGridX = 0, g_bodyGridY = 0, g_bodyGridW = 0, g_bodyGridH = 0;

// Wipe the body and note that whatever was on it has gone.
void clearBody() {
  gfx.fillRect(0, kBodyY, kScreenW, kBodyH, theme::bg);
  g_bodyHasGrid = false;
}

void clearBodyAroundGrid();   // defined with the grid geometry

// Hold the panel's write transaction for the length of a frame. Outside one,
// every fillRect and every glyph is its own transaction -- chip select,
// command, data, release -- and a full grid is a couple of thousand of them.
// Inside one they stream. The touch controller shares the bus, so this is
// only ever held while drawing, never across a poll; tick() reads the touch
// before any of these exist.
struct WriteBatch {
  WriteBatch()  { gfx.startWrite(); }
  ~WriteBatch() { gfx.endWrite(); }
  WriteBatch(const WriteBatch&) = delete;
  WriteBatch& operator=(const WriteBatch&) = delete;
};

void drawRun(const run::Snapshot& snap) {
  clearBodyAroundGrid();
  drawGrid();
  drawRunners(snap);

  // The readout is drawn in ZOOM as well. The zoomed grid already stops short
  // to leave room for it, so skipping it left a blank strip and no rule under
  // a zoomed program -- the one page where the output simply was not there.
  // The bars go on last: drawRunnerList clears everything under the program,
  // which is where the bottom one lives.
  drawRunnerList(snap);
  drawEdgeBars();
}

struct EditRow { int y, h; };
constexpr int kEditRowH = kContentH + 8;
constexpr int kEditRowsPerPage = kBodyH / kEditRowH;
// Page 1 is exactly the parameters a packed program exposes.
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
// Two keyboards, not one.
//
// Writing IRCIS needs about thirty characters; the other fifty are only there
// for names and captions. Putting all eighty on screen at once made every key
// 30 x 20, which is a coin-flip on a resistive panel. The working keyboard is
// now 11 x 3 at 43 x 26 -- a bit over twice the area -- and the letters live
// behind an `abc` key on a second page.
//
// Row 1 is movement and the blank. Row 2 is digits, with the quote that starts
// a number at the head of them. Row 3 is everything else you type often.
const char* const kEdKeysMain =
  "><^v.+-*/%V"                          // movement, the blank, arithmetic
  "'0123456789"                          // the quote that starts a number
  "\"@&#$?!prR|";                        // modes, output, control, random
// The letter pages carry ~ and , as well. A view tag needs both, and neither
// can live on the IRCIS page: that page is the characters IRCIS reads, and a
// tag works precisely because IRCIS reads neither of these.
//
// And a space, which is not the same as the blank. `.` is an empty cell; a
// space inside a string is the character a space, so without one on the
// keyboard "Hello World" could only ever be written Hello.World. It is drawn
// as an underscore, the way the picker draws its own space key -- a blank key
// on a blank background is not a key anyone can see.
// Laid out as a QWERTY keyboard rather than A-to-Z. Three rows of ten, which
// matches the IRCIS page, so the grid above does not jump when you switch
// between them -- and ten columns makes each key 48 px, wider than the IRCIS
// page's 43. Alphabetical order in fourteen narrow columns was neither
// familiar nor comfortable to hit.
const char* const kEdKeysUpper = "QWERTYUIOP"
                                 "ASDFGHJKL~"
                                 " ZXCVBNM,.";
const char* const kEdKeysLower = "qwertyuiop"
                                 "asdfghjkl~"
                                 " zxcvbnm,.";

constexpr int kEdKeyCols  = 11;
constexpr int kEdKeyRows  = 3;
constexpr int kEdKeyW     = kScreenW / kEdKeyCols;     // 43
constexpr int kEdKeyH     = 26;
// The letters need only two rows of thirteen, which makes those keys bigger
// than the working ones. Nothing is spent on a switch key: the EDIT tab does
// the switching, the way the RUN tab does play and pause.
constexpr int kEdAbcCols  = 10;
constexpr int kEdAbcRows  = 3;     // the same depth as the IRCIS page
constexpr int kEdAbcW     = kScreenW / kEdAbcCols;     // 48
constexpr int kEdAbcH     = 26;
int g_edKb = 0;                                        // 0 IRCIS, 1 ABC, 2 abc

inline bool edLetters() { return g_edKb != 0; }
inline int edKeyCols() { return edLetters() ? kEdAbcCols : kEdKeyCols; }
// With a real keyboard the on-screen one is not drawn at all, and because the
// grid band is measured from the top of the keys, those rows go to the program.
inline int edKeyRows() { return !onScreenKeys() ? 0
                              : edLetters() ? kEdAbcRows : kEdKeyRows; }
inline int edKeyW()    { return edLetters() ? kEdAbcW    : kEdKeyW; }
inline int edKeyH()    { return edLetters() ? kEdAbcH    : kEdKeyH; }
// Which keyboard is on screen, which is what the tab says while EDIT is the
// current page. Naming the NEXT one instead -- "ABC" written over a keyboard
// of IRCIS symbols -- reads as a mislabel rather than a promise, and there is
// nothing else on screen to tell you which it meant. Tapping cycles, and the
// label follows: one tap to learn.
const char* edCurKb() { return g_edKb == 0 ? "IRCIS" : g_edKb == 1 ? "ABC" : "abc"; }
inline int kEdKeyYf()  { return kTabY - edKeyRows() * edKeyH(); }
// Neither keyboard divides the panel evenly -- 11 x 43 leaves 7 px, 13 x 36
// leaves 12 -- so share the remainder between the two edges rather than
// letting it all pile up on the right.
inline int edKeyX0()   { return (kScreenW - edKeyCols() * edKeyW()) / 2; }
#define kEdKeyY kEdKeyYf()

// Set when the window was moved with the scroll arrows, cleared the moment the
// cursor moves. While it is set the window stays where it was put.
bool g_edManualScroll = false;
int g_dialogPage = 0;              // which page of a paged dialog is showing
int g_focus      = -1;             // control the keyboard is on, -1 for none
// The cell the keyboard is on while RUN is set to do something with one. -1
// until the arrows are used, so nothing is drawn over a program otherwise.
int g_runCellRow = -1, g_runCellCol = 0;
void typeIntoGrid(char k);         // defined with the editor, below

// The grid band runs from under the header down to the keyboard.
//
// A cell is a tap target, so it is sized like one: the same 26 px height as a
// keyboard key, which the keyboard already proves is hittable. It used to be
// 30 x 32 to leave whitespace for cursor arrows in the neighbouring cells;
// tapping the cell you want does the same job, so the room went back to the
// program -- 20 x 7 cells on screen instead of 16 x 5.
constexpr int kEdCellW = kZoomCellW;
constexpr int kEdCellH = kZoomCellH;

// The band the program occupies, on both pages: from under the header to the
// top of a three-row keyboard. A constant on purpose. It used to follow the
// keyboard that was up, and with a real keyboard attached (no rows on screen)
// the editor's band stretched to the tab bar while RUN's did not, so the
// same program sat on different lines on the two pages.
int edBandH() { return (kTabY - kEdKeyRows * kEdKeyH) - kEdGridY - 4; }
// ...and the band the editor can actually draw into. The same, unless there
// is no keyboard on screen, when the program takes the keyboard's rows too --
// it is centred as if the keyboard were there, so it sits where RUN puts it,
// and simply shows more of itself underneath. RUN does the same when a
// program turns its readout off.
int edShownH() { return kEdKeyY - kEdGridY - 4; }
int edCellW() { return g_view == View::Zoom ? kEdCellW : kWideCellW; }
int edCellH() { return g_view == View::Zoom ? kEdCellH : kWideCellH; }
int edCols()  { int n = kScreenW / edCellW();
                return n > prog::kMaxCols ? prog::kMaxCols : n; }
int edZoomCols() { int n = kScreenW / kEdCellW;
                   return n > prog::kMaxCols ? prog::kMaxCols : n; }
int edZoomRows() { int n = edBandH() / kEdCellH; return n < 1 ? 1 : n; }
int edRows()  { return edShownH() / edCellH(); }

// Centre on the program when it is smaller than the band.
int edGridX() {
  int w = g_edit.cols() < edCols() ? g_edit.cols() : edCols();
  int x = (kScreenW - w * edCellW()) / 2;
  return x > 0 ? x : 0;
}
int edGridY() {
  // Centred by the rows the centring band holds, not the rows the editor
  // shows: with no keyboard on screen it shows more, and counting those put
  // a tall program two pixels above where RUN puts it.
  const int rows = edBandH() / edCellH();
  int h = g_edit.rows() < rows ? g_edit.rows() : rows;
  int y = kEdGridY + (edBandH() - h * edCellH()) / 2;
  return y > kEdGridY ? y : kEdGridY;
}

struct GridEdges {
  int  x, y, w, h;                 // the program as drawn
  bool up, down, left, right;      // where there is more to reach
};

bool gridEdges(GridEdges& g) {
  if (g_tab == Tab::Run) {
    const int cw = (g_view == View::Zoom) ? kZoomCellW : kWideCellW;
    const int chh = (g_view == View::Zoom) ? kZoomCellH : kWideCellH;
    const int cols = (g_view == View::Zoom) ? kZoomCols : kWideCols;
    const int sc = g_edit.cols() < cols ? g_edit.cols() : cols;
    const int rows = gridRowsShown();
    const int sr = g_edit.rows() < rows ? g_edit.rows() : rows;
    if (g_view == View::Zoom) { g.x = zoomOriginX(); g.y = zoomOriginY(); }
    else                      { g.x = wideX();       g.y = wideOriginY(); }
    g.w = sc * cw; g.h = sr * chh;
    g.up    = g_gridRow > 0;
    g.down  = g_gridRow < maxGridRow();
    g.left  = g_gridCol > 0;
    g.right = g_gridCol + sc < g_edit.cols();
    return true;
  }
  if (g_tab == Tab::Edit && !Store::unlocked()) {
    const int sc = g_edit.cols() < edCols() ? g_edit.cols() : edCols();
    const int sr = g_edit.rows() < edRows() ? g_edit.rows() : edRows();
    g.x = edGridX(); g.y = edGridY();
    g.w = sc * edCellW(); g.h = sr * edCellH();
    g.up    = g_gridRow > 0;
    g.down  = g_gridRow + sr < g_edit.rows();
    g.left  = g_gridCol > 0;
    g.right = g_gridCol + sc < g_edit.cols();
    return true;
  }
  return false;
}

// One small button in the middle of each edge, barely larger than the arrow on
// it. A bar running the whole edge covered a great deal of program for
// something you press once.
constexpr int kBarLong = 46;
// A bar is a whole number of cells thick -- the count nearest to kEdgeBar,
// and never none -- so it sits on a cell boundary. Sixteen pixels over
// fifteen-pixel rows put its top edge one pixel into the row above, and the
// row it was taken to cover was that one: the strip cleared under it and
// the area that answered to a tap were both a row too high, so the bar
// blanked the text above it and a tap on it landed on the cell beneath.
int barThick(int cell) {
  int n = (kEdgeBar + cell / 2) / cell;
  if (n < 1) n = 1;
  return n * cell;
}
Btn barUp(const GridEdges& g) {
  const int t = barThick(cellH());
  return { g.x + (g.w - kBarLong) / 2, g.y, kBarLong, t, "" };
}
Btn barDown(const GridEdges& g) {
  const int t = barThick(cellH());
  return { g.x + (g.w - kBarLong) / 2, g.y + g.h - t, kBarLong, t, "" };
}
Btn barLeft(const GridEdges& g) {
  const int t = barThick(cellW());
  return { g.x, g.y + (g.h - kBarLong) / 2, t, kBarLong, "" };
}
Btn barRight(const GridEdges& g) {
  const int t = barThick(cellW());
  return { g.x + g.w - t, g.y + (g.h - kBarLong) / 2, t, kBarLong, "" };
}

// Clear the body except for the rectangle the program occupies. drawGrid and
// drawProgEditGrid both paint a background behind every cell they draw, so the
// program needs no clearing -- and clearing it made it blink on every full
// repaint, including one caused by nothing more than changing tabs.
void clearBodyAroundGrid() {
  GridEdges g;
  if (!gridEdges(g)) { clearBody(); return; }
  // The program is left standing only when the frame before it put the very
  // same rectangle there. Arriving from PROG or SYS the body still holds that
  // page, and drawing the grid over it cell by cell showed the old page being
  // eaten away a line at a time.
  const bool same = g_bodyHasGrid && g_bodyGridX == g.x && g_bodyGridY == g.y &&
                    g_bodyGridW == g.w && g_bodyGridH == g.h;
  // On the editor the body below the program is the keyboard, which is drawn
  // by its own function and only on a full repaint. Clearing it here on the
  // grid-only repaint after a scroll left the keys missing until something
  // else redrew them. The editor's clear stops where the keyboard starts.
  const int bodyEnd = editorTab() ? kEdKeyY : kBodyY + kBodyH;
  if (!same) {
    gfx.fillRect(0, kBodyY, kScreenW, bodyEnd - kBodyY, theme::bg);
    g_bodyHasGrid = false;
  }
  else {
    if (g.y > kBodyY)  gfx.fillRect(0, kBodyY, kScreenW, g.y - kBodyY, theme::bg);
    const int below = g.y + g.h;
    if (below < bodyEnd) gfx.fillRect(0, below, kScreenW, bodyEnd - below, theme::bg);
    if (g.x > 0) gfx.fillRect(0, g.y, g.x, g.h, theme::bg);
    const int right = g.x + g.w;
    if (right < kScreenW) gfx.fillRect(right, g.y, kScreenW - right, g.h, theme::bg);
  }
  g_bodyHasGrid = true;
  g_bodyGridX = g.x; g_bodyGridY = g.y; g_bodyGridW = g.w; g_bodyGridH = g.h;
}

// One row of the grid, rendered in RAM and sent to the panel as a single
// image. Drawn cell by cell the panel took a couple of thousand small writes
// per grid and you could watch it happen; this way it is one write per row.
// The sprite is kept at the width of the program as shown, which changes
// only with the view or the program. If there is no memory for it the rows
// are drawn cell by cell as before.
lgfx::LGFX_Sprite* rowSprite(int w, int h) {
  static lgfx::LGFX_Sprite sprite(&gfx);
  static int haveW = 0, haveH = 0;
  static bool failed = false;
  if (failed) return nullptr;
  if (w != haveW || h != haveH) {
    if (haveW) sprite.deleteSprite();
    sprite.setColorDepth(16);
    if (!sprite.createSprite(w, h)) { failed = true; haveW = haveH = 0; return nullptr; }
    haveW = w; haveH = h;
  }
  return &sprite;
}

// Every visible cell of the program, a row at a time. `editor` picks the
// editor's window and cursor over the run page's window and path.
void paintGridRows(bool editor) {
  GridEdges g;
  if (!gridEdges(g)) return;
  const int cw = cellW(), chh = cellH();
  const int rows = g.h / chh, cols = g.w / cw;
  const int top = g_gridRow, left = g_gridCol;
  lgfx::LGFX_Sprite* sp = rowSprite(g.w, chh);
  for (int i = 0; i < rows; ++i) {
    const int r = top + i;
    if (r >= g_edit.rows()) break;
    if (sp) {
      for (int j = 0; j < cols && left + j < g_edit.cols(); ++j)
        paintCell(*sp, j * cw, 0, r, left + j, editor);
      sp->pushSprite(g.x, g.y + i * chh);
      // On the board the push goes out by DMA and returns before the panel
      // has the row. Drawing the next row into the same buffer while that
      // was still in flight sent the panel a mixture of the two, which came
      // out as garbled characters everywhere a runner had not yet been.
      gfx.waitDMA();
    }
    else {
      for (int j = 0; j < cols && left + j < g_edit.cols(); ++j)
        paintCell(gfx, g.x + j * cw, g.y + i * chh, r, left + j, editor);
    }
  }
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.setTextSize(1);
}

// The whole cells a bar sits on, as one rectangle. They are cleared before
// the bar is drawn, so that is what the eye sees as the bar; and a bar is at
// the very edge of the panel, where a finger lands short, so that whole
// rectangle plus a further reach inward is what a tap on the bar means. A tap
// there used to land on the character under the bar instead, select it, and
// draw it over the arrow.
constexpr int kBarReach = 12;
Btn barCells(const GridEdges& g, const Btn& b, bool horizontal) {
  const int cw = cellW(), chh = cellH();
  int x0, x1, y0, y1;
  // Every cell the bar's rectangle touches, on both axes: from the cell
  // holding its near edge to the one holding its far edge.
  (void)horizontal;
  x0 = g.x + (b.x - g.x) / cw * cw;
  x1 = g.x + ((b.x + b.w - g.x + cw - 1) / cw) * cw;
  y0 = g.y + (b.y - g.y) / chh * chh;
  y1 = g.y + ((b.y + b.h - g.y + chh - 1) / chh) * chh;
  if (x0 < g.x) x0 = g.x;
  if (y0 < g.y) y0 = g.y;
  if (x1 > g.x + g.w) x1 = g.x + g.w;
  if (y1 > g.y + g.h) y1 = g.y + g.h;
  return { x0, y0, x1 - x0, y1 - y0, "" };
}
// growForward says the reach goes down or right (the top and left bars);
// otherwise it goes up or left (the bottom and right ones). It used to be
// the other way round, which sent every bar's reach off the edge of the
// panel, where no finger ever is.
Btn barHit(const GridEdges& g, const Btn& b, bool horizontal, bool growForward) {
  Btn r = barCells(g, b, horizontal);
  if (horizontal) { if (!growForward) r.y -= kBarReach; r.h += kBarReach; }
  else            { if (!growForward) r.x -= kBarReach; r.w += kBarReach; }
  return r;
}

// A bar runs the whole length of the edge it belongs to, so it reads as that
// side of the program rather than as a button that happens to be near it. It
// is drawn only when there is something that way, which is what says you are
// against an edge.
void drawEdgeBar(const Btn& b, bool horizontal, bool forward) {
  // The same face as every other button on the device: panel fill, a hairline
  // in the line colour, and the mark on it in the ordinary text colour.
  gfx.fillRoundRect(b.x, b.y, b.w, b.h, 3, theme::panel);
  gfx.drawRoundRect(b.x, b.y, b.w, b.h, 3, theme::line);
  // One solid triangle in the middle, pointing the way the bar goes.
  const int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
  const int a = 6;
  if (horizontal) {
    if (forward) gfx.fillTriangle(cx - a, cy - a, cx + a, cy - a, cx, cy + a, theme::text);
    else         gfx.fillTriangle(cx - a, cy + a, cx + a, cy + a, cx, cy - a, theme::text);
  }
  else {
    if (forward) gfx.fillTriangle(cx - a, cy - a, cx - a, cy + a, cx + a, cy, theme::text);
    else         gfx.fillTriangle(cx + a, cy - a, cx + a, cy + a, cx - a, cy, theme::text);
  }
}

// A press moves almost a full screen, keeping the row or column that was at
// the far edge as the one now at the near edge, so there is something in
// common between the two views to place yourself by. Stops at the end of the
// program rather than running past it.
bool handleEdgeBars(int x, int y) {
  GridEdges g;
  if (!gridEdges(g)) return false;
  const int cw = (g_tab == Tab::Edit) ? edCellW()
               : (g_view == View::Zoom) ? kZoomCellW : kWideCellW;
  const int ch = (g_tab == Tab::Edit) ? edCellH()
               : (g_view == View::Zoom) ? kZoomCellH : kWideCellH;
  const int stepC = g.w / cw > 1 ? g.w / cw - 1 : 1;
  const int stepR = g.h / ch > 1 ? g.h / ch - 1 : 1;

  int* row = &g_gridRow;
  int* col = &g_gridCol;
  const int maxRow = (g_tab == Tab::Edit)
                       ? (g_edit.rows() - g.h / ch > 0 ? g_edit.rows() - g.h / ch : 0)
                       : maxGridRow();
  const int maxCol = g_edit.cols() - g.w / cw > 0 ? g_edit.cols() - g.w / cw : 0;

  auto moved = [&]() {
    if (g_tab == Tab::Edit) {
      // The editor normally keeps the cursor on screen and would undo this
      // before it was drawn. Scrolling by hand says where you want to look, so
      // it stops doing that until the cursor is moved again. The cursor stays
      // exactly where it was: moving it would read as selecting a character.
      g_edManualScroll = true;
      g_paint |= PaintEdGrid;
    }
    // Scrolling by hand is a choice about where to look, so stop chasing the
    // runner. Without this the next frame put the view straight back.
    else                    { g_paint |= PaintRunGrid; g_follow = false; }
    g_dirty = true;
  };
  if (g.up && hit(barHit(g, barUp(g), true, true), x, y)) {
    *row -= stepR; if (*row < 0) *row = 0; moved(); return true;
  }
  if (g.down && hit(barHit(g, barDown(g), true, false), x, y)) {
    *row += stepR; if (*row > maxRow) *row = maxRow; moved(); return true;
  }
  if (g.left && hit(barHit(g, barLeft(g), false, true), x, y)) {
    *col -= stepC; if (*col < 0) *col = 0; moved(); return true;
  }
  if (g.right && hit(barHit(g, barRight(g), false, false), x, y)) {
    *col += stepC; if (*col > maxCol) *col = maxCol; moved(); return true;
  }
  return false;
}

// A bar is thinner than a cell, so drawing it straight onto the grid left the
// top of whatever character was underneath sticking out above it. Clear the
// whole cells it covers first and it sits on a clean strip instead.
void clearUnderBar(const GridEdges& g, const Btn& b, bool horizontal) {
  const Btn r = barCells(g, b, horizontal);
  if (r.w > 0 && r.h > 0) gfx.fillRect(r.x, r.y, r.w, r.h, theme::bg);
}

void drawEdgeBars() {
  GridEdges g;
  if (!gridEdges(g)) return;
  auto bar = [&g](Btn b, bool horizontal, bool forward) {
    clearUnderBar(g, b, horizontal);
    drawEdgeBar(b, horizontal, forward);
  };
  if (g.up)    bar(barUp(g),    true,  false);
  if (g.down)  bar(barDown(g),  true,  true);
  if (g.left)  bar(barLeft(g),  false, false);
  if (g.right) bar(barRight(g), false, true);
}

// Keep the cursor on screen after any movement.
// Queue a cell for the narrow repaint. Falls back to the whole grid once more
// than three pile up, which cannot happen from one tap but keeps it honest.
void touchEdCell(int r, int c) {
  for (int i = 0; i < g_edCellCount; ++i)
    if (g_edCells[i].row == r && g_edCells[i].col == c) return;
  if (g_edCellCount >= 3) { g_paint |= PaintEdGrid; return; }
  g_edCells[g_edCellCount++] = { (int16_t)r, (int16_t)c };
}

void edFollow() {
  // Left alone while the reader is looking somewhere of their own choosing.
  if (g_edManualScroll) return;
  const int vr = edRows(), vc = visibleCols();
  if (g_curRow < g_gridRow)        g_gridRow = g_curRow;
  if (g_curRow >= g_gridRow + vr)  g_gridRow = g_curRow + 1 - vr;
  if (g_curCol < g_gridCol)        g_gridCol = g_curCol;
  if (g_curCol >= g_gridCol + vc)  g_gridCol = g_curCol + 1 - vc;
  int mr = g_edit.rows() - vr; if (mr < 0) mr = 0;
  if (g_gridRow > mr) g_gridRow = mr;
  if (g_gridRow < 0)  g_gridRow = 0;
  setGridCol(g_gridCol);
}

// index 0..79 -> the character that key types
// 0 for a gap, otherwise the character that key types.
char edKeyChar(int i) {
  const char* tbl = g_edKb == 1 ? kEdKeysUpper
                  : g_edKb == 2 ? kEdKeysLower : kEdKeysMain;
  const int n = (int)std::strlen(tbl);
  if (i < 0 || i >= n) return 0;
  const char k = tbl[i];
  return k == '\x01' ? 0 : k;
}


// One cell of the editor grid, background and all. Typing changes exactly
// three of them at most -- the character replaced, the cell the cursor left
// and the cell it arrived on -- so the other hundred-odd are left alone.
void drawEdCell(int r, int c) {
  if (r < g_gridRow || r >= g_gridRow + edRows()) return;
  if (c < g_gridCol || c >= g_gridCol + visibleCols()) return;
  if (r < 0 || r >= g_edit.rows() || c < 0 || c >= g_edit.cols()) return;
  const int cw = edCellW(), chh = edCellH();
  const int x = edGridX() + (c - g_gridCol) * cw;
  const int y = edGridY() + (r - g_gridRow) * chh;
  paintCell(gfx, x, y, r, c, true);
  gfx.setFont(&fonts::Font0);
  gfx.setTextSize(1);
}

void drawProgEditGrid() {
  clearBodyAroundGrid();
  paintGridRows(true);
}

void drawProgEditKeys() {
  gfx.fillRect(0, kEdKeyY, kScreenW, kTabY - kEdKeyY, theme::bg);
  if (!onScreenKeys()) return;
  gfx.setTextDatum(textdatum_t::middle_center);
  const int kw = edKeyW(), kh = edKeyH();
  for (int r = 0; r < edKeyRows(); ++r) {
    for (int c = 0; c < edKeyCols(); ++c) {
      char k = edKeyChar(r * edKeyCols() + c);
      if (!k) continue;
      int x = edKeyX0() + c * kw, y = kEdKeyY + r * kh;
      // A command gets the accent; a base64 digit stays plain. Seven letters
      // are both (v V + / r R p), and keep the digit's face so the key still
      // reads as a digit that happens to do two jobs.
      // A command gets the accent. Several base64 digits are also commands
      // (v V r R p + /), so they show as commands here and on the letters
      // page too -- the key still types a digit, it just does two jobs.
      const bool command = std::strchr("><^v+-*/%V'\"@&#$?!prR|", k) != nullptr;
      // No outline. Thirty-three boxed keys is a lot of ruled lines for a
      // small panel; the fill alone separates them, the way the picker's
      // keyboards already do it.
      uint16_t kbg = theme::panel;
      gfx.fillRect(x + 1, y + 1, kw - 2, kh - 2, kbg);
      // The key is set in the same face and size the grid uses, so what you
      // tap looks like what lands in the cell. It used to be the 5 x 7 pixel
      // font at double size, which matched nothing else on the device.
      char lbl[2] = { k == ' ' ? '_' : k, 0 };
      useContentFont(true);
      gfx.setTextColor(command ? theme::accent : theme::text, kbg);
      gfx.drawString(lbl, x + kw / 2, y + kh / 2);
    }
  }
  gfx.setTextSize(1);
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.setFont(&fonts::Font0);
}

void drawProgEdit() {
  edFollow();
  drawProgEditGrid();
  drawProgEditKeys();
  drawEdgeBars();          // last, so nothing is painted over them
}

void openSizeDialog();

// Rows x columns for a new or resized program. Kept as a stepper rather than a
// keypad: the range is small, the bounds are hard, and a stepper cannot be
// used to enter something out of range in the first place.
int  g_sizeRows = 11, g_sizeCols = 40;
bool g_sizeIsNew = true;         // false when resizing the loaded program

// A new program is two numbers; resizing an existing one is four edges, and
// needs the room. RESIZE asks which SIDE to add to or take from, because
// "eleven rows" does not say whether the new one lands above the program or
// below it -- and for a program whose runner starts at 0,0, that is the whole
// question.
inline int szY() { return g_sizeIsNew ? 60 : 34; }
inline int szH() { return g_sizeIsNew ? 150 : 222; }
#define kSzY szY()
#define kSzH szH()

// top, bottom, left, right -- how many rows or columns to add at each edge,
// negative to take away. Applied together when OK is pressed, so CANCEL
// really does leave the program alone.
int g_szEdge[4] = { 0, 0, 0, 0 };
const char* const kEdgeName[4] = { "top", "bottom", "left", "right" };
inline int szEdgeY(int i) { return kSzY + 40 + i * 34; }
Btn btnSzEdgeDn(int i) { return { 196, szEdgeY(i), 40, 26, "-" }; }
Btn btnSzEdgeUp(int i) { return { 300, szEdgeY(i), 40, 26, "+" }; }

// The second page: a row or a column named by number, inserted before it or
// deleted. RESIZE's four edges can only add at the outside; this is for making
// room in the middle, or taking a line out of it.
int  g_szPage    = 0;      // 0 the edges, 1 row and column surgery
int  g_szAtRow   = 0;
int  g_szAtCol   = 0;
// A delete throws cells away, so the button asks once. Cleared by any other
// press, and by leaving the page.
int  g_szArmed   = 0;      // 0 none, 1 the row delete, 2 the column delete

// What this page has done, so it can be taken back. Each entry carries what it
// would take to reverse it: an insert is undone by deleting the same line, and
// a delete by putting the line back with the cells it had.
struct ShapeOp {
  uint8_t kind;                  // 0 insert row, 1 delete row, 2 insert col, 3 delete col
  int16_t at;
  int16_t n;                     // cells saved, for a delete
  char    cells[prog::kMaxCols]; // the line that was taken out
};
constexpr int kShapeUndoMax = 12;
ShapeOp g_szUndo[kShapeUndoMax];
int     g_szUndoCount = 0;

void pushShapeOp(uint8_t kind, int at) {
  if (g_szUndoCount >= kShapeUndoMax) {
    // Oldest out. Twelve is more than anyone does in one visit to this page.
    for (int i = 1; i < kShapeUndoMax; ++i) g_szUndo[i - 1] = g_szUndo[i];
    --g_szUndoCount;
  }
  ShapeOp& op = g_szUndo[g_szUndoCount++];
  op.kind = kind;
  op.at = (int16_t)at;
  op.n = 0;
  if (kind == 1) {                                  // a row about to go
    op.n = (int16_t)g_edit.cols();
    for (int c = 0; c < op.n; ++c) op.cells[c] = g_edit.cell(at, c);
  }
  else if (kind == 3) {                             // a column about to go
    op.n = (int16_t)g_edit.rows();
    for (int r = 0; r < op.n; ++r) op.cells[r] = g_edit.cell(r, at);
  }
}

void undoShapeOp() {
  if (g_szUndoCount <= 0) return;
  const ShapeOp& op = g_szUndo[--g_szUndoCount];
  switch (op.kind) {
    case 0: g_edit.deleteRow(op.at); break;
    case 2: g_edit.deleteCol(op.at); break;
    case 1:
      if (g_edit.insertRow(op.at))
        for (int c = 0; c < op.n && c < g_edit.cols(); ++c)
          g_edit.setCell(op.at, c, op.cells[c]);
      break;
    case 3:
      if (g_edit.insertCol(op.at))
        for (int r = 0; r < op.n && r < g_edit.rows(); ++r)
          g_edit.setCell(r, op.at, op.cells[r]);
      break;
  }
  markEdited();
}

// Two groups of stepper-then-buttons, then the pair at the foot. Worked out
// so the last group clears the foot rather than sitting on it.
inline int szOpY(int i) { return kSzY + 40 + i * 68; }
Btn btnSzAtDn(int i)  { return { 196, szOpY(i), 40, 28, "-" }; }
Btn btnSzAtUp(int i)  { return { 300, szOpY(i), 40, 28, "+" }; }
Btn btnSzIns(int i)   { return { 50,  szOpY(i) + 32, 180, 28, "insert before" }; }
Btn btnSzDel(int i)   { return { 250, szOpY(i) + 32, 180, 28, "delete", theme::bad }; }
Btn btnSzPage()       { return { kScreenW - 96, kSzY + 4, 66, 24,
                                 g_szPage == 0 ? "rows..." : "edges" }; }
// Page two's changes happen as they are pressed, so instead of an OK that has
// nothing left to do and a CANCEL that cannot cancel, it has a way back one
// step at a time. With nothing done yet the same button is simply the way out.
Btn btnSzUndo()       { return { 50, kSzY + kSzH - 46, 180, 28,
                                 g_szUndoCount > 0 ? "UNDO" : "CANCEL",
                                 g_szUndoCount > 0 ? theme::warn : theme::bad }; }
Btn btnSzDone()       { return { 250, kSzY + kSzH - 46, 180, 28,
                                 "DONE", theme::bg, theme::good }; }

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
  gfx.drawString(g_sizeIsNew ? "NEW PROGRAM"
                             : (g_szPage == 1 ? "ROWS AND COLUMNS" : "RESIZE"),
                 30, kSzY + 6);

  char buf[32];

  if (!g_sizeIsNew && g_szPage == 1) {
    gfx.setFont(&fonts::Font0);
    gfx.setTextDatum(textdatum_t::top_right);
    gfx.setTextColor(theme::dim, theme::panel);
    char now[32];
    snprintf(now, sizeof(now), "%d x %d", g_edit.rows(), g_edit.cols());
    gfx.drawString(now, kScreenW - 106, kSzY + 12);
    gfx.setTextDatum(textdatum_t::top_left);
    drawBtn(btnSzPage());

    const char* const what[2] = { "row", "column" };
    for (int i = 0; i < 2; ++i) {
      const int at  = i == 0 ? g_szAtRow : g_szAtCol;
      const int max = (i == 0 ? g_edit.rows() : g_edit.cols()) - 1;
      clabel(40, szOpY(i) + 6, what[i], theme::text, theme::panel);
      snprintf(buf, sizeof(buf), "%d", at);
      gfx.setFont(&fonts::Font2);
      gfx.setTextDatum(textdatum_t::middle_center);
      gfx.setTextColor(theme::accent, theme::panel);
      gfx.drawString(buf, 268, szOpY(i) + 14);
      gfx.setTextDatum(textdatum_t::top_left);
      gfx.setFont(&fonts::Font0);
      drawBtn(btnSzAtDn(i), false, at > 0);
      drawBtn(btnSzAtUp(i), false, at < max);
      const int cap = i == 0 ? prog::kMaxRows : prog::kMaxCols;
      const int n   = i == 0 ? g_edit.rows() : g_edit.cols();
      drawBtn(btnSzIns(i), false, n < cap);
      Btn del = btnSzDel(i);
      if (g_szArmed == i + 1) { del.label = "sure? delete"; }
      drawBtn(del, g_szArmed == i + 1, n > 1);
    }
    drawBtn(btnSzUndo());
    drawBtn(btnSzDone());
    gfx.setFont(&fonts::Font0);
    return;
  }

  if (!g_sizeIsNew) {
    drawBtn(btnSzPage());
    // What the program is now, and what it would become.
    const int nr = g_edit.rows() + g_szEdge[0] + g_szEdge[1];
    const int nc = g_edit.cols() + g_szEdge[2] + g_szEdge[3];
    snprintf(buf, sizeof(buf), "%d x %d  ->  %d x %d",
             g_edit.rows(), g_edit.cols(), nr, nc);
    gfx.setFont(&fonts::Font0);
    gfx.setTextDatum(textdatum_t::top_right);
    gfx.setTextColor((nr != g_edit.rows() || nc != g_edit.cols())
                       ? theme::edited : theme::dim, theme::panel);
    // Clear of the page button in the corner, which this used to run under.
    gfx.drawString(buf, kScreenW - 106, kSzY + 10);
    gfx.setTextDatum(textdatum_t::top_left);

    for (int i = 0; i < 4; ++i) {
      clabel(40, szEdgeY(i) + 5, kEdgeName[i], theme::text, theme::panel);
      snprintf(buf, sizeof(buf), "%+d", g_szEdge[i]);
      gfx.setFont(&fonts::Font2);
      gfx.setTextDatum(textdatum_t::middle_center);
      gfx.setTextColor(g_szEdge[i] ? theme::edited : theme::dim, theme::panel);
      gfx.drawString(g_szEdge[i] ? buf : "0", 268, szEdgeY(i) + 13);
      gfx.setTextDatum(textdatum_t::top_left);
      const bool isRow = i < 2;
      const int  now   = isRow ? g_edit.rows() + g_szEdge[0] + g_szEdge[1]
                               : g_edit.cols() + g_szEdge[2] + g_szEdge[3];
      const int  cap   = isRow ? prog::kMaxRows : prog::kMaxCols;
      drawBtn(btnSzEdgeDn(i), false, now > 1);
      drawBtn(btnSzEdgeUp(i), false, now < cap);
    }
    drawBtn(btnSzCancel()); drawBtn(btnSzOk());
    gfx.setFont(&fonts::Font0);
    return;
  }

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
  if (!g_sizeIsNew && hit(btnSzPage(), x, y)) {
    g_szPage = 1 - g_szPage;
    g_szArmed = 0;
    g_dirty = true;
    return;
  }
  if (!g_sizeIsNew && g_szPage == 1) {
    for (int i = 0; i < 2; ++i) {
      int& at = i == 0 ? g_szAtRow : g_szAtCol;
      const int n = i == 0 ? g_edit.rows() : g_edit.cols();
      if (hit(btnSzAtDn(i), x, y)) { if (at > 0) --at; g_szArmed = 0; g_dirty = true; return; }
      if (hit(btnSzAtUp(i), x, y)) { if (at < n - 1) ++at; g_szArmed = 0; g_dirty = true; return; }
      if (hit(btnSzIns(i), x, y)) {
        const int cap = i == 0 ? prog::kMaxRows : prog::kMaxCols;
        if (n < cap) {
          pushShapeOp(i == 0 ? 0 : 2, at);
          if (i == 0) g_edit.insertRow(at); else g_edit.insertCol(at);
          markEdited();
        }
        g_szArmed = 0;
        g_dirty = true;
        return;
      }
      if (hit(btnSzDel(i), x, y)) {
        // Asked once, because a line of cells is not something UNDO can bring
        // back and a mis-tap here costs work.
        if (g_szArmed != i + 1) { g_szArmed = i + 1; g_dirty = true; return; }
        if (n > 1) {
          pushShapeOp(i == 0 ? 1 : 3, at);       // saves the line first
          if (i == 0) g_edit.deleteRow(at); else g_edit.deleteCol(at);
          if (at >= (i == 0 ? g_edit.rows() : g_edit.cols()))
            at = (i == 0 ? g_edit.rows() : g_edit.cols()) - 1;
          markEdited();
        }
        g_szArmed = 0;
        g_dirty = true;
        return;
      }
    }
    if (hit(btnSzUndo(), x, y)) {
      if (g_szUndoCount > 0) undoShapeOp();
      else                   g_modal = Modal::None;
      g_szArmed = 0; g_dirty = true;
      return;
    }
    if (hit(btnSzDone(), x, y)) { g_modal = Modal::None; g_szArmed = 0; wantAll(); }
    return;
  }
  auto clampR = [] { if (g_sizeRows < 1) g_sizeRows = 1;
                     if (g_sizeRows > prog::kMaxRows) g_sizeRows = prog::kMaxRows; };
  auto clampC = [] { if (g_sizeCols < 1) g_sizeCols = 1;
                     if (g_sizeCols > prog::kMaxCols) g_sizeCols = prog::kMaxCols; };
  if (!g_sizeIsNew) {
    for (int i = 0; i < 4; ++i) {
      const bool isRow = i < 2;
      const int  cap   = isRow ? prog::kMaxRows : prog::kMaxCols;
      const int  now   = isRow ? g_edit.rows() + g_szEdge[0] + g_szEdge[1]
                               : g_edit.cols() + g_szEdge[2] + g_szEdge[3];
      if (hit(btnSzEdgeDn(i), x, y)) {
        if (now > 1) { --g_szEdge[i]; g_dirty = true; }
        return;
      }
      if (hit(btnSzEdgeUp(i), x, y)) {
        if (now < cap) { ++g_szEdge[i]; g_dirty = true; }
        return;
      }
    }
  }
  if (hit(btnSzRowsDn(), x, y)) { --g_sizeRows; clampR(); g_dirty = true; return; }
  if (hit(btnSzRowsUp(), x, y)) { ++g_sizeRows; clampR(); g_dirty = true; return; }
  if (hit(btnSzColsDn(), x, y)) { --g_sizeCols; clampC(); g_dirty = true; return; }
  if (hit(btnSzColsUp(), x, y)) { ++g_sizeCols; clampC(); g_dirty = true; return; }
  if (hit(btnSzCancel(), x, y)) { g_modal = Modal::None; wantAll(); return; }
  if (hit(btnSzOk(), x, y)) {
    if (g_sizeIsNew) { g_edit.newProgram(g_sizeRows, g_sizeCols); g_progFile.clear(); }
    else {
      // Each edge in turn. Adding at the top or the left shifts the program
      // down or right; adding at the bottom or the right leaves it where it
      // is. Taking away does the same in reverse.
      bool ok = true;
      for (int k = 0; k < g_szEdge[0]; ++k)  ok = ok && g_edit.insertRow(0);
      for (int k = 0; k > g_szEdge[0]; --k)  ok = ok && g_edit.deleteRow(0);
      for (int k = 0; k < g_szEdge[1]; ++k)  ok = ok && g_edit.insertRow(g_edit.rows());
      for (int k = 0; k > g_szEdge[1]; --k)  ok = ok && g_edit.deleteRow(g_edit.rows() - 1);
      for (int k = 0; k < g_szEdge[2]; ++k)  ok = ok && g_edit.insertCol(0);
      for (int k = 0; k > g_szEdge[2]; --k)  ok = ok && g_edit.deleteCol(0);
      for (int k = 0; k < g_szEdge[3]; ++k)  ok = ok && g_edit.insertCol(g_edit.cols());
      for (int k = 0; k > g_szEdge[3]; --k)  ok = ok && g_edit.deleteCol(g_edit.cols() - 1);
      if (!ok) {
        g_modal = Modal::None;
        message("Cannot resize", pack::str(pack::kStrResizeBody));
        return;
      }
      // Every cell may have moved, so the history no longer describes this
      // grid.
      clearUndo();
    }
    run::load(g_edit);
    markLoaded();
    g_curRow = g_curCol = 0;
    g_gridRow = g_gridCol = 0;
    syncViewToProgram();
    g_modal = Modal::None;
    g_tab = Tab::Edit;
    g_dirty = true;
  }
}

void openSizeDialog() {
  g_sizeIsNew = false;
  g_szEdge[0] = g_szEdge[1] = g_szEdge[2] = g_szEdge[3] = 0;
  g_sizeRows = g_edit.rows();
  g_sizeCols = g_edit.cols();
  // Opens on the edges, with the second page starting wherever the cursor is:
  // the row you were looking at is the one you are most likely to mean.
  g_szPage = 0;
  g_szArmed = 0;
  g_szUndoCount = 0;
  g_szAtRow = g_curRow < g_edit.rows() ? g_curRow : 0;
  g_szAtCol = g_curCol < g_edit.cols() ? g_curCol : 0;
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
  g_edit.loadProgram(prog::kOpeningExample);
  applyViewTags(g_edit.text());
  g_appliedTag = tagIn(g_edit.text());
  run::load(g_edit);
  markLoaded();
  syncViewToProgram();
  g_curRow = g_curCol = 0;
  g_gridRow = g_gridCol = 0;
  g_tab = Tab::Run;
  g_dirty = true;
}

void openRenameDialog() {
  openPicker("Program name", "", kKbText, g_edit.programName(), 24,
             [](const std::string& v) { g_edit.setProgramName(v); g_dirty = true; },
             kKbSplit);
}

// Record an edit, dropping any redo branch: once you type after undoing, the
// path you undid is gone, which is what every editor does.
void noteEdit(int row, int col, char was, char now) {
  if (was == now) return;
  g_undo.resize(g_undoAt);
  g_undo.push_back({ (int16_t)row, (int16_t)col, was, now });
  if (g_undo.size() > kUndoMax) g_undo.erase(g_undo.begin());
  g_undoAt = g_undo.size();
}
void clearUndo() { g_undo.clear(); g_undoAt = 0; }

bool canUndo() { return g_undoAt > 0; }
bool canRedo() { return g_undoAt < g_undo.size(); }

void doUndo() {
  if (!canUndo()) return;
  const CellEdit& e = g_undo[--g_undoAt];
  touchEdCell(g_curRow, g_curCol);
  g_edit.setCell(e.row, e.col, e.was);
  g_curRow = e.row; g_curCol = e.col;      // show what moved
  touchEdCell(g_curRow, g_curCol);
  markCellEdited(e.row, e.col, e.was);
  g_paint |= PaintEdHead; g_dirty = true;
}
void doRedo() {
  if (!canRedo()) return;
  const CellEdit& e = g_undo[g_undoAt++];
  touchEdCell(g_curRow, g_curCol);
  g_edit.setCell(e.row, e.col, e.now);
  g_curRow = e.row; g_curCol = e.col;
  g_edManualScroll = false;
  touchEdCell(g_curRow, g_curCol);
  markCellEdited(e.row, e.col, e.now);
  g_paint |= PaintEdHead; g_dirty = true;
}

void handleProgEditTouch(int x, int y) {
  // The bars run along the edges of the program, so they are tested before
  // anything that reads a cell.
  if (y >= kBodyY && handleEdgeBars(x, y)) return;
  // The status bar's two controls.
  if (hit(btnEdName(), x, y)) { openRenameDialog(); return; }
  if (hit(btnEdSize(), x, y)) { openSizeDialog();   return; }
  if (!zoomOnly() && hit(btnEdZoom(), x, y)) {
    toggleView();
    return;
  }
  if (hit(btnEdSave(), x, y)) { saveCurrentProgram(); return; }
  if (hit(btnEdUndo(), x, y)) { doUndo(); return; }
  if (hit(btnEdRedo(), x, y)) { doRedo(); return; }
  // The command list, from the page where you would want it.
  if (hit(btnEdHelp(), x, y)) { g_modal = Modal::Ircis; g_dialogPage = 0; wantAll(); return; }
  // The grid: tap a cell to put the cursor there. Bounded by the rows and
  // columns actually on screen, not by how many WOULD fit -- edRows() counts
  // the space available, so under a short program this claimed the keyboard
  // as well and swallowed every key press that landed on it. A two-row
  // program lost the whole top row of keys; a five-row program lost part of
  // it; anything seven rows or taller was unaffected, which is what made it
  // look intermittent rather than broken.
  const int shownRows = g_edit.rows() - g_gridRow < edRows()
                          ? g_edit.rows() - g_gridRow : edRows();
  const int shownCols = g_edit.cols() - g_gridCol < edCols()
                          ? g_edit.cols() - g_gridCol : edCols();
  if (y >= edGridY() && y < edGridY() + shownRows * edCellH() &&
      x >= edGridX() && x < edGridX() + shownCols * edCellW()) {
    int r = g_gridRow + (y - edGridY()) / edCellH();
    int c = g_gridCol + (x - edGridX()) / edCellW();
    // Tapping the cell the cursor is already on changes nothing, so it draws
    // nothing: on a panel that mis-reads taps, the same cell gets hit twice
    // often enough for that to be worth saying.
    if (r >= 0 && r < g_edit.rows() && c >= 0 && c < g_edit.cols() &&
        (r != g_curRow || c != g_curCol)) {
      touchEdCell(g_curRow, g_curCol);        // the one it is leaving
      g_curRow = r; g_curCol = c;
      g_edManualScroll = false;
      touchEdCell(g_curRow, g_curCol);        // and the one it lands on
      g_paint |= PaintEdHead; g_dirty = true;
    }
    return;
  }
  // character keys: type and advance, wrapping to the next row
  if (onScreenKeys() && y >= kEdKeyY && y < kTabY) {
    int r = (y - kEdKeyY) / edKeyH();
    if (x < edKeyX0()) return;
    int c = (x - edKeyX0()) / edKeyW();
    if (c >= edKeyCols()) return;
    char k = edKeyChar(r * edKeyCols() + c);
    if (!k) return;
    typeIntoGrid(k);
  }
}

// One character into the cell under the cursor, then move on. Shared by the
// on-screen keys and by a real keyboard, so the two cannot drift apart.
void typeIntoGrid(char k) {
  const char was = g_edit.cell(g_curRow, g_curCol);
  if (g_edit.setCell(g_curRow, g_curCol, k)) {
    noteEdit(g_curRow, g_curCol, was, k);
    markCellEdited(g_curRow, g_curCol, k);
    touchEdCell(g_curRow, g_curCol);          // the character just replaced
    if (g_curCol < g_edit.cols() - 1) ++g_curCol;
    else if (g_curRow < g_edit.rows() - 1) { g_curCol = 0; ++g_curRow; }
    g_edManualScroll = false;
    touchEdCell(g_curRow, g_curCol);          // and where the cursor went
  }
  // Those cells and the readout change; the keys and the rest do not.
  g_paint |= PaintEdHead; g_dirty = true;
}

// Anything typed on a real keyboard while the editor is open. Arrows move the
// cursor, backspace writes a blank -- which is what '.' means here anyway.
// One row of the parameter page: its label, its value, and UNDO when the
// value has been changed. The row paints its own background, so one can be
// redrawn on its own when only it changed.
void drawEditRow(int i) {
  const int idx = editPageFirst(g_editPage) + i;
  if (idx >= prog::slotCount()) return;
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

void drawEdit() {
  clearBody();
  for (int i = 0; i < editPageCount(g_editPage); ++i) drawEditRow(i);
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

// What the sets page shows, hashed, so a run can be watched from it without
// the page being cleared and redrawn on every step. Which set is active can
// only change when the program's parameters do, which is rarely.
uint32_t g_keysSig = 0;
uint32_t keysSignature() {
  uint32_t h = 2166136261u;
  auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
  const int maxRows = setMaxRows();
  for (int kind = 0; kind < setKinds(); ++kind) {
    const int n = setEntryCount(kind);
    mix((uint32_t)n); mix(currentSetKept(kind) ? 1u : 2u);
    for (int i = 0; i < n && i < maxRows; ++i) {
      mix(setActive(kind, i) ? 3u : 4u);
      mix(setIsCustom(kind, i) ? 5u : 6u);
      for (char c : setEntry(kind, i)) mix((uint32_t)(uint8_t)c);
    }
  }
  return h;
}

void drawKeys() {
  g_keysSig = keysSignature();
  clearBody();

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
// Only drawn when the output is longer than the page, on a footer row of
// their own so they cannot land on top of a line of output.
constexpr int kOutFootH = kContentH + 4;
Btn btnOutPgUp()   { return { kScreenW - 62, kTabY - kOutFootH, 28, kContentH + 2, "^" }; }
Btn btnOutPgDn()   { return { kScreenW - 32, kTabY - kOutFootH, 28, kContentH + 2, "v" }; }
// Stepping back through what the device has already printed. On the left, so
// they are nowhere near the page arrows on the right.
Btn btnOutOlder()  { return { 4,  kTabY - kOutFootH, 28, kContentH + 2, "<" }; }
Btn btnOutNewer()  { return { 34, kTabY - kOutFootH, 28, kContentH + 2, ">" }; }

// The runs this device has finished, most recent last. SD LOG already writes
// every completed run to the card, but only the web view ever read them back
// and a card is optional -- so the last few are kept here as well, where the
// device can show its own history with nothing plugged in.
//
// Capped on both counts: this is for looking back at what a program printed,
// not a log. An output longer than the cap is kept up to it and marked.
constexpr int         kHistoryMax   = 10;
constexpr std::size_t kHistoryBytes = 1024;
struct PastRun {
  std::string name;
  std::string out;
  uint32_t    step    = 0;
  uint8_t     runners = 0;
  bool        clipped = false;
};
std::vector<PastRun> g_history;
// -1 is the run that just happened; otherwise an index into g_history.
int g_histView = -1;

void recordRun(const run::Snapshot& snap, const std::string& out,
               const char* name) {
  PastRun r;
  r.name    = name ? name : "";
  r.step    = snap.step;
  r.runners = snap.runnersCreated;
  r.clipped = out.size() > kHistoryBytes;
  r.out     = r.clipped ? out.substr(0, kHistoryBytes) : out;
  g_history.push_back(std::move(r));
  if ((int)g_history.size() > kHistoryMax) g_history.erase(g_history.begin());
}

// What the OUT page is showing: the live output, or one out of the history.
const std::string& shownOutput() {
  static std::string live;
  if (g_histView >= 0 && g_histView < (int)g_history.size())
    return g_history[g_histView].out;
  live = run::output();
  return live;
}

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
    char what[80];
    if (g_histView >= 0 && g_histView < (int)g_history.size()) {
      // A past run: its own name and figures, not the loaded program's.
      const PastRun& h = g_history[g_histView];
      snprintf(what, sizeof(what), "%s", h.name.c_str());
      clabel(4, kBodyY + 1, what, theme::dim);
      char how[80];
      snprintf(how, sizeof(how), "%u steps   %u runner%s%s",
               (unsigned)h.step, (unsigned)h.runners, h.runners == 1 ? "" : "s",
               h.clipped ? "   (output clipped)" : "");
      clabel(4, kBodyY + 1 + kContentH, how, theme::dim);
      gfx.drawFastHLine(0, kBodyY + kOutHeaderH - 2, kScreenW, theme::line);
      return;
    }
    snprintf(what, sizeof(what), "%s   %d x %d",
             g_ranGrid.programName(), g_ranGrid.rows(), g_ranGrid.cols());
    clabel(4, kBodyY + 1, what, theme::accent);

    const run::Snapshot snap = run::snapshot();
    if (snap.step > 0) {
      // No elapsed time here: it is the one figure that changes run to run,
      // and it would make this screen impossible to compare in the regression.
      // SYS carries the timings.
      char how[80];
      snprintf(how, sizeof(how), "%u steps   %u runner%s",
               (unsigned)snap.step,
               (unsigned)snap.runnersCreated, snap.runnersCreated == 1 ? "" : "s");
      clabel(4, kBodyY + 1 + kContentH, how, theme::dim);

      // RUN TO END stops after five million steps rather than spinning for
      // ever. Without this line that looks exactly like a program that ended
      // on its own, which is the one thing it is not. Only the give-up is
      // worth a line: "running", "paused" and "stepped" are all visible from
      // the transport already.
      if (!snap.finished && std::strncmp(snap.lastEvent, "stopped:", 8) == 0)
        clabel(4, kBodyY + 1 + 2 * kContentH, snap.lastEvent, theme::warn);
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

// What the OUT page last drew, line by line, and where. A character
// arriving while a run is going changes one line; the rest of the page is
// the same page, and clearing and redrawing all of it for each character was
// a flicker at the run's pace.
std::vector<std::string> g_outDrawn;
int g_outDrawnOx = -1, g_outDrawnOy = -1;

void drawOutBody(bool full);
void drawOut() {
  clearBody();
  drawOutHeader();
  drawOutBody(true);
}

// The header's two or three lines, cleared and written again. Its step and
// runner counts move while the run is going.
void drawOutHeaderOnly() {
  gfx.fillRect(0, kBodyY, kScreenW - kOutBtnW - 8, kOutHeaderH - 2, theme::bg);
  drawOutHeader();
}

void drawOutBody(bool full) {
  // Colouring by chunk length and sign describes a packed program's output.
  // For any other program the output is just text.
  if (full && g_ranGrid.isPacked()) drawBtn(btnOutColour(), Store::outputColour());

  int top = kBodyY + kOutHeaderH;
  int cw = kContentBigW, ch = kContentBigH;
  int cols = (kScreenW - 8) / cw;

  std::string text = shownOutput();
  std::vector<LineSpan> spans = wrapSpans(text, cols);
  g_outTotal = (int)spans.size();
  if (spans.empty()) {
    g_outLines = (kTabY - 2 - top) / ch;
    if (!full) gfx.fillRect(0, top, kScreenW, kTabY - top, theme::bg);
    clabel(4, top, "(no output yet)", theme::dim, theme::bg, true);
    g_outDrawn.clear(); g_outDrawnOx = g_outDrawnOy = -1;
    return;
  }

  // The footer row costs a line, so only give it up when the output actually
  // needs scrolling: measure without it, and measure again if it does.
  auto linesFor = [&](int reserve) {
    int n = (kTabY - 2 - reserve - top) / ch;
    return n < 1 ? 1 : n;
  };
  // The footer row is always reserved, whether or not there is anything to
  // put in it yet. It is where the history arrows appear the moment a run
  // finishes, and taking its height out of the block only then moved every
  // line of output up at exactly that moment.
  g_outLines = linesFor(kOutFootH);

  std::vector<run::Chunk> chunks =
      (g_ranGrid.isPacked() && Store::outputColour()) ? run::chunks() : std::vector<run::Chunk>();

  // Clamp here rather than at the tap: the output grows while a run is going,
  // so what was the last page a moment ago may not be any more.
  const int maxTop = g_outTotal > g_outLines ? g_outTotal - g_outLines : 0;
  if (g_outTop > maxTop) g_outTop = maxTop;
  if (g_outTop < 0) g_outTop = 0;

  // Centre the block on the widest line it is showing, so a short output sits
  // in the middle of the panel rather than hugging the left edge. A long one
  // fills the width anyway and does not move.
  int widest = 0;
  for (int i = 0; i < g_outLines && g_outTop + i < g_outTotal; ++i) {
    const int n = (int)spans[g_outTop + i].len;
    if (n > widest) widest = n;
  }
  int ox = (kScreenW - widest * cw) / 2;
  if (ox < 4) ox = 4;

  // And centre it down the panel too. Most outputs are a line or two and were
  // sitting against the top edge with the rest of the screen empty under
  // them. Once the output is long enough to scroll it fills the space anyway,
  // so this only moves the short ones.
  const int shown  = g_outTotal - g_outTop < g_outLines
                       ? g_outTotal - g_outTop : g_outLines;
  const int room   = (kTabY - 2 - kOutFootH) - top;
  // The first line sits where a single line would, and the rest grow down
  // from it, so nothing already printed moves when the next line arrives.
  // Once the block is too tall for that it starts from the top, which is
  // also when it starts to scroll.
  const int anchor = (room - ch) / 2;
  int oy = (anchor + shown * ch <= room) ? top + anchor : top;

  // The lines as they will be drawn, so a partial repaint can compare them
  // with the ones already on the panel and touch only the ones that differ.
  std::vector<std::string> lines;
  for (int i = 0; i < g_outLines && g_outTop + i < g_outTotal; ++i)
    lines.push_back(text.substr(spans[g_outTop + i].start, spans[g_outTop + i].len));
  const bool lineByLine = !full && chunks.empty() &&
                          ox == g_outDrawnOx && oy == g_outDrawnOy;
  if (!full && !lineByLine) gfx.fillRect(0, top, kScreenW, kTabY - top, theme::bg);

  for (int i = 0; i < (int)lines.size(); ++i) {
    const LineSpan& ln = spans[g_outTop + i];
    int y = oy + i * ch;
    if (chunks.empty()) {
      if (lineByLine) {
        if (i < (int)g_outDrawn.size() && g_outDrawn[i] == lines[i]) continue;
        gfx.fillRect(0, y, kScreenW, ch, theme::bg);
      }
      clabel(ox, y, lines[i].c_str(), theme::text, theme::bg, true);
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
      clabel(ox + (int)(pos - ln.start) * cw, y, text.substr(pos, stop - pos).c_str(),
             col, theme::bg, true);
      pos = stop;
    }
  }
  if (lineByLine)
    for (int i = (int)lines.size(); i < (int)g_outDrawn.size(); ++i)
      gfx.fillRect(0, oy + i * ch, kScreenW, ch, theme::bg);      // lines that went
  g_outDrawn = lines; g_outDrawnOx = ox; g_outDrawnOy = oy;

  if (maxTop > 0) {
    drawBtn(btnOutPgUp(), false, g_outTop > 0);
    drawBtn(btnOutPgDn(), false, g_outTop < maxTop);
    char where[64];
    const int last = g_outTop + g_outLines < g_outTotal ? g_outTop + g_outLines : g_outTotal;
    snprintf(where, sizeof(where), "lines %d-%d of %d", g_outTop + 1, last, g_outTotal);
    // Right-aligned beside its own arrows, leaving the left of the footer to
    // say which run you are looking at.
    gfx.setFont(&fonts::Font0);
    gfx.setTextDatum(textdatum_t::top_right);
    gfx.setTextColor(theme::dim, theme::bg);
    gfx.drawString(where, btnOutPgUp().x - 8, kTabY - kOutFootH + 2);
    gfx.setTextDatum(textdatum_t::top_left);
  }

  if (!g_history.empty()) {
    const int n = (int)g_history.size();
    drawBtn(btnOutOlder(), false, g_histView != 0);
    drawBtn(btnOutNewer(), false, g_histView >= 0);
    char which[64];
    if (g_histView < 0) snprintf(which, sizeof(which), "this run");
    else                snprintf(which, sizeof(which), "run %d of %d back",
                                 n - g_histView, n);
    clabel(68, kTabY - kOutFootH + 2, which,
           g_histView < 0 ? theme::dim : theme::accent);
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

std::vector<std::string> g_devFiles;    // the board's own flash
std::vector<std::string> g_cardFiles;   // the SD card, if there is one

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

void applyViewTags(const std::string& text);

// Every keystroke used to copy the whole program through the mutex and rebuild
// the machine -- once per character typed. A burst of typing is worth one
// rebuild, not twenty, so the reload waits for a short gap in the typing.
// Nothing on screen waits with it: the grid is drawn from g_edit either way,
// and an edited program restarts from the top whenever the reload lands.
constexpr uint32_t kEditQuietMs = 180;
uint32_t g_editDirtyAt    = 0;
bool     g_editReloadDue  = false;   // the whole grid changed
bool     g_editRebuildDue = false;   // single cells already sent; just rebuild

// force: the caller is about to need the machine to match the grid -- running
// it, stepping it, or leaving the page -- so the wait is over.
// The tag as it is written in the program: from the tilde to the next blank.
std::string tagIn(const std::string& text) {
  const std::size_t at = text.find('~');
  if (at == std::string::npos) return std::string();
  std::size_t end = at;
  while (end < text.size() && text[end] != '.' && !std::isspace((unsigned char)text[end])) ++end;
  return text.substr(at, end - at);
}
// The tag last applied, so an edit that changes it is noticed and one that
// does not leaves the settings alone -- applying a tag resets everything it
// does not mention, which is the wrong thing to do on every keystroke.
std::string g_appliedTag;

void flushEdits(bool force = false) {
  if (!g_editReloadDue && !g_editRebuildDue) return;
  if (!force && (uint32_t)(plat::millis() - g_editDirtyAt) < kEditQuietMs) return;
  // A tag edited on the device used to mean nothing until the program was
  // loaded again. It is read here, before the machine is rebuilt, so a
  // changed start cell is the one the rebuild starts from.
  const std::string tag = tagIn(g_edit.text());
  if (tag != g_appliedTag) { applyViewTags(g_edit.text()); g_appliedTag = tag; g_editReloadDue = true; }
  // A reload carries the grid with it; a rebuild does not need to, because
  // the cells went across one at a time as they were typed.
  if (g_editReloadDue) run::load(g_edit);
  else                 run::rebuild();
  g_editReloadDue = g_editRebuildDue = false;
}

void afterProgramChange() {
  // A different program: its cells are not the ones the history is about.
  clearUndo();
  // No markLoaded() here. run::load rebuilds on the run task and the watch in
  // tick() paints once it has, which is both one frame instead of two and the
  // right frame -- painting now would put the old machine's runners on the
  // new grid.
  g_curRow = g_curCol = 0;
  g_gridRow = g_gridCol = 0;
  syncViewToProgram();
  g_histView = -1;      // a new program means the live run, not one looked back at
  // After syncViewToProgram, so a program that asks for a view gets it rather
  // than having it chosen for it -- and BEFORE the load goes out. The load is
  // built on the run task, which reads the start cell as it builds; with the
  // tag applied afterwards, a program that asked to start somewhere other
  // than the top-left corner sometimes did and sometimes did not.
  applyViewTags(g_edit.text());
  g_appliedTag = tagIn(g_edit.text());
  run::load(g_edit);
}

// A different program: it brings its own identity and its own blank baseline.
// A program can ask to be shown a particular way, in one short tag written
// anywhere in the grid. None of the tag characters is an IRCIS command, so a
// runner that crosses one steps over it -- which is why the tag can sit in the
// middle of a program rather than needing a row of its own. A tilde, then
// single letters:
//
//     n  nothing under the program        s  slow      q  quick
//     d  debug: step buttons + runners     m  medium    f  full
//     <row>,<col>[NESW]  start here, heading east unless a letter says
//
// So `~nm3,1` is: nothing underneath, medium, start at row 3 column 1 heading
// east. The comma is what marks a coordinate, and either side of it may be
// left off -- `~,2` starts at row 0 column 2.
//
// A tag speaks for every setting it could set, so anything it leaves out goes
// back to the default: the output underneath, fast, no debug, starting at the
// top left heading east. A bare `~` therefore means "the usual", and there is
// no letter to write for a setting you are happy with. No tag at all leaves
// the device exactly as the user left it.
//
// The tilde is not an IRCIS command, so a runner that does cross one steps
// over it. Tags set the ordinary settings, so what a program asked for is
// visible on SYS afterwards rather than being an invisible state.
// Read a program's view tag and put the device into the state it asks for.
//
// The defaults are applied whether or not there is a tag, so a program without
// one opens the same way every time instead of inheriting whatever the last
// program happened to set. That is what lets an ordinary program carry no tag
// at all: only a program that wants something OTHER than the defaults needs to
// say so.
//
// Where a tag contradicts itself -- "~sq" asking for both slow and quick --
// the first one wins and the rest of that group is ignored, so the reading is
// left to right and never depends on which letter came last.
void applyViewTags(const std::string& text) {
  Store::setRunView(0);                 // the output, underneath
  Store::setDebugMode(false);
  Store::setTracePath(false);
  Store::setFollowRunners(true);
  Store::setRunSpeed(1);                // medium
  run::setSpeed(run::Speed::Medium);
  // A tag that names no start puts the runner back at the top left. It does
  // not touch GRID TAP: that is the reader's setting, not the program's.
  Store::setStartPoint(0, 0, 'E');
  run::setStart(0, 0, 'E');
  // What a tap on the grid does is a way of looking at THIS program, so it
  // goes back too; a program loaded while the inspector was on is otherwise
  // opened with the inspector still on. STEP BUTTONS, the theme and the
  // keyboard are the device's settings, not the program's, and stay.
  Store::setGridTap(Store::kTapNothing);

  const std::size_t at = text.find('~');
  if (at == std::string::npos) return;

  bool bandSet = false, speedSet = false, startSet = false;

  auto setSpeed = [&speedSet](int n, run::Speed s) {
    if (speedSet) return;
    speedSet = true;
    Store::setRunSpeed(n);
    run::setSpeed(s);
  };

  for (std::size_t i = at + 1; i < text.size(); ++i) {
    const char c = text[i];
    if (std::isspace((unsigned char)c) || c == '.') break;   // a blank ends it

    // A comma marks a coordinate; the digits around it are optional.
    if (std::isdigit((unsigned char)c) || c == ',') {
      std::size_t j = i;
      int col = 0, row = 0;
      // <row>,<col>: the order the editor's corner and the run page's
      // "entry" line use, so one habit serves everywhere. Either number may
      // be left off, and reads as zero.
      while (j < text.size() && std::isdigit((unsigned char)text[j])) {
        if (row < 10000) row = row * 10 + (text[j] - '0');
        ++j;
      }
      if (j >= text.size() || text[j] != ',') { i = j - 1; continue; }  // digits alone
      ++j;
      while (j < text.size() && std::isdigit((unsigned char)text[j])) {
        if (col < 10000) col = col * 10 + (text[j] - '0');
        ++j;
      }
      char dir = 'E';
      if (j < text.size() && std::strchr("NESW", text[j])) dir = text[j++];
      i = j - 1;
      if (startSet) continue;           // the first coordinate is the one
      startSet = true;
      // A start outside the grid would put the runner nowhere; clamp it to a
      // cell that exists rather than refusing the whole tag.
      if (col >= g_edit.cols()) col = g_edit.cols() - 1;
      if (row >= g_edit.rows()) row = g_edit.rows() - 1;
      if (col < 0) col = 0;
      if (row < 0) row = 0;
      // A program asking to start somewhere needs that honoured, so a device
      // set to NOTHING is moved up to letting the start be placed.
      // No switching GRID TAP on behind the reader's back: the marker below
      // shows a start that is not the default whatever that setting is.
      Store::setStartPoint(col, row, dir);
      run::setStart(col, row, dir);
      continue;
    }

    switch (c) {
      case 'n': if (!bandSet) { bandSet = true; Store::setRunView(1); }   break;
      case 'd': if (!bandSet) { bandSet = true; Store::setDebugMode(true); } break;
      // s m q f, in order. 'r' would have been the obvious letter for the
      // third one, but r is an IRCIS command -- push a random number -- and a
      // tag can sit on a cell a runner crosses, where it would be executed.
      // Keep the path on screen. Its own group, so it composes with any of
      // the others; 't' is not an IRCIS command, so a runner crossing it steps
      // straight over.
      case 't': Store::setTracePath(true); break;
      // Hold the view still while the program runs. 'd' would have been the
      // obvious letter and is already the runner readout, so 'h' for hold.
      case 'h': Store::setFollowRunners(false); break;
      case 's': setSpeed(0, run::Speed::Slow);   break;
      case 'm': setSpeed(1, run::Speed::Medium); break;
      case 'q': setSpeed(2, run::Speed::Quick);  break;
      case 'f': setSpeed(3, run::Speed::Full);   break;
      default: break;                   // anything else is just a character
    }
  }
}

// `name` is taken before the machine is handed the program, not after. It
// used to be set by the caller afterwards, which meant a second run::load to
// carry the name across -- two rebuilds and two full repaints for one load.
bool loadProgramText(const std::string& text, const char* name = nullptr) {
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
  if (name) g_edit.setProgramName(name);
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
// A program name as a file name. Spaces and punctuation become a single
// hyphen rather than being dropped, so "Count to 20" stays readable as
// Count-to-20 instead of collapsing to Countto20. Hyphens keep the name safe
// for FAT, for LittleFS, and inside the web view's ?name= links, which are
// not percent-decoded.
std::string fileNameFor(const char* name) {
  std::string out;
  bool gap = false;
  for (const char* n = name; *n && out.size() < 24; ++n) {
    if (std::isalnum((unsigned char)*n) || *n == '_') {
      if (gap && !out.empty()) out.push_back('-');
      gap = false;
      out.push_back(*n);
    }
    else gap = true;                    // a run of anything else is one hyphen
  }
  return out;
}

void refreshProgFiles(bool force = false);
void promptSaveAs(plat::Where w);

// SAVE writes the loaded program to the card under the name in the status bar,
// which is editable right beside it, so what it writes is never a surprise.
const char* whereName(plat::Where w) {
  return w == plat::Where::Device ? "device" : "card";
}

void saveProgramTo(plat::Where w) {
  if (!plat::progStoreReady(w)) {
    message("No card", "Insert an SD card to save programs there.");
    return;
  }
  const std::string leaf = fileNameFor(g_edit.programName());
  if (leaf.empty()) { promptSaveAs(w); return; }
  // Into whichever folder PROG has open, so saving puts the program where you
  // were already looking rather than somewhere you then have to find.
  const std::string name = inProgDir(leaf);
  auto write = [name, leaf, w] {
    if (!plat::progWrite(w, name, g_edit.text())) {
      message("Save failed", std::string("Could not write to the ") + whereName(w) + ".");
      return;
    }
    // What is saved IS the program now, so nothing in it is an unsaved edit
    // any more. Without this every changed cell stayed marked as edited on
    // the RUN grid after the file was written, and REVERT would have gone
    // back past a version you had deliberately kept.
    g_edit.adoptBaseline();
    clearUndo();
    g_edit.setProgramName(leaf);       // the title bar shows the name, not the path
    g_progFile  = name;
    g_progWhere = w;
    refreshProgFiles(true);
    message("Saved", name + ".txt on the " + whereName(w));
  };
  // Overwriting the file this program came from is what you meant; overwriting
  // somebody else's is worth one question.
  std::string existing;
  if ((name != g_progFile || w != g_progWhere) &&
      plat::progRead(w, name, existing))
    confirm("Replace " + name + ".txt?",
            std::string("Another program of that name is already on the ")
              + whereName(w) + ".", write);
  else
    write();
}

// SAVE writes back to wherever this program came from. If that was a card
// that has since been pulled, the device's own storage is always there, so
// save goes there rather than refusing -- there is no longer any state in
// which the device cannot keep your work.
void saveCurrentProgram() {
  saveProgramTo(plat::progStoreReady(g_progWhere) ? g_progWhere
                                                  : plat::Where::Device);
}

// The built-in programs ship inside the firmware as const arrays. On first
// run they are copied into the board's own filesystem, where they stop being
// special: you can edit one, save over it, rename it or delete it like
// anything else. The firmware copies stay where they are and are the source
// for RESTORE BUILT-INS, so nothing is lost by editing.
//
// The packed program is never copied. Its grid exists decrypted only in RAM,
// and writing it to a filesystem would leave it there in plain text.
std::string builtInText(int i) {
  const prog::ProgramDef& d = prog::programAt(i);
  std::string t;
  for (int r = 0; r < d.rows_n; ++r) { t += d.rows[r]; t += '\n'; }
  return t;
}

int writeBuiltIns(bool overwrite) {
  int written = 0;
  // What the device holds now, read once: a built-in that has moved folder
  // since it was last written is still sitting in the old one.
  std::vector<std::string> have;
  plat::progList(plat::Where::Device, have);
  for (int i = prog::kFirstExample; i < prog::programCount(); ++i) {
    const prog::ProgramDef& d = prog::programAt(i);
    if (d.packed) continue;                           // never the packed one
    const std::string leaf = fileNameFor(d.name);
    if (leaf.empty()) continue;
    // Each built-in goes in the folder its table names, which is what gives
    // PROG something to open at the top level.
    std::string name = leaf;
    if (d.folder && *d.folder) {
      name = std::string(d.folder) + "/" + leaf;
      // The same program in some other folder is a copy from before it moved.
      // If it has not been written to its new folder yet the old copy goes
      // there and the old one is removed (seeding then overwrites it with the
      // shipped text, as it does every built-in; RESTORE keeps it). If the
      // new one already exists, the old copy is removed only when it is
      // exactly the shipped text: anything else is somebody's work, and
      // stays where they can find it.
      for (const std::string& h : have) {
        const std::size_t slash = h.find('/');
        if (slash == std::string::npos || h.substr(slash + 1) != leaf || h == name) continue;
        std::string stale;
        if (!plat::progRead(plat::Where::Device, h, stale)) continue;
        std::string current;
        const bool haveNew = plat::progRead(plat::Where::Device, name, current);
        if (!haveNew) {
          if (plat::progWrite(plat::Where::Device, name, stale)) plat::progDelete(plat::Where::Device, h);
        }
        else if (stale == builtInText(i)) plat::progDelete(plat::Where::Device, h);
      }
      // Builds before folders existed wrote this same program loose at the
      // top of the store. Left there it would show up twice, once in its
      // folder and once above it, so the flat copy goes when the folder one
      // is written -- and only then, so a failed write cannot lose it.
      if (overwrite && plat::progWrite(plat::Where::Device, name, builtInText(i))) {
        std::string stale;
        if (plat::progRead(plat::Where::Device, leaf, stale))
          plat::progDelete(plat::Where::Device, leaf);
        ++written;
        continue;
      }
    }
    std::string existing;
    if (!overwrite && plat::progRead(plat::Where::Device, name, existing)) continue;
    if (plat::progWrite(plat::Where::Device, name, builtInText(i))) ++written;
  }
  return written;
}

// What the last seeding wrote, one path per line. A build that renames or
// drops a built-in would otherwise leave the old file behind for ever, and
// there is no way to tell an orphan from something the owner made by looking
// at it. Comparing against this list says exactly which files were ours.
void forgetStaleBuiltIns() {
  const std::string prev = plat::kv::getString("seedlist", "");
  if (prev.empty()) return;
  std::vector<std::string> now;
  for (int i = prog::kFirstExample; i < prog::programCount(); ++i) {
    const prog::ProgramDef& d = prog::programAt(i);
    if (d.packed) continue;
    const std::string leaf = fileNameFor(d.name);
    if (leaf.empty()) continue;
    now.push_back(d.folder && *d.folder ? std::string(d.folder) + "/" + leaf : leaf);
  }
  std::size_t at = 0;
  while (at < prev.size()) {
    std::size_t nl = prev.find('\n', at);
    if (nl == std::string::npos) nl = prev.size();
    const std::string old = prev.substr(at, nl - at);
    at = nl + 1;
    if (old.empty()) continue;
    if (std::find(now.begin(), now.end(), old) != now.end()) continue;
    // A path that is gone because the program moved folder is not an orphan:
    // writeBuiltIns carries it across, edits and all. Only a program the
    // bundle no longer has at all is deleted here.
    const std::size_t slash = old.find('/');
    const std::string leaf = slash == std::string::npos ? old : old.substr(slash + 1);
    bool moved = false;
    for (const std::string& n : now)
      if (n.size() > leaf.size() && n.compare(n.size() - leaf.size(), leaf.size(), leaf) == 0 &&
          n[n.size() - leaf.size() - 1] == '/') { moved = true; break; }
    if (!moved) plat::progDelete(plat::Where::Device, old);
  }
}

void rememberBuiltIns() {
  std::string list;
  for (int i = prog::kFirstExample; i < prog::programCount(); ++i) {
    const prog::ProgramDef& d = prog::programAt(i);
    if (d.packed) continue;
    const std::string leaf = fileNameFor(d.name);
    if (leaf.empty()) continue;
    list += d.folder && *d.folder ? std::string(d.folder) + "/" + leaf : leaf;
    list += '\n';
  }
  plat::kv::putString("seedlist", list.c_str());
}

void seedDeviceProgramsOnce() {
  if (plat::kv::getBool("seeded", false)) return;
  if (!plat::progStoreReady(plat::Where::Device)) return;
  // Overwrite: the flag is cleared when the firmware changes, and the point of
  // that is to replace the shipped programs with the versions this build
  // carries. Anything the owner made has a name of its own and is not touched.
  forgetStaleBuiltIns();
  writeBuiltIns(true);
  rememberBuiltIns();
  plat::kv::putBool("seeded", true);
}

bool g_progListStale = true;      // something changed the stores

void refreshProgFiles(bool force) {
  seedDeviceProgramsOnce();
  if (!force && !g_progListStale) return;
  g_progListStale = false;
  g_devFiles.clear();
  g_cardFiles.clear();
  plat::progList(plat::Where::Device, g_devFiles);
  if (plat::sdPresent()) plat::progList(plat::Where::Card, g_cardFiles);

  // At power-on the loaded program came from the examples table, before any of
  // this existed as files. It is one of these files now, so point at it --
  // otherwise the list shows nothing as loaded on a device fresh out of the
  // box. Only the pointer moves; the grid is left exactly as it is.
  if (g_progFile.empty() && !g_edit.isScratch() && !g_edit.isPacked()) {
    const std::string name = fileNameFor(g_edit.programName());
    for (const std::string& f : g_devFiles)
      if (leafOf(f) == name) { g_progFile = f; g_progWhere = plat::Where::Device; break; }
  }

  // The folders are whatever the two stores between them contain, counted so
  // a folder row can say how much is inside without opening it.
  g_progFolders.clear();
  g_progFolderCount.clear();
  std::vector<std::string> dirs;
  for (const std::vector<std::string>* v : { &g_devFiles, &g_cardFiles })
    for (const std::string& f : *v) {
      const std::string d = folderOf(f);
      if (!d.empty()) dirs.push_back(d);
    }
  std::sort(dirs.begin(), dirs.end());
  for (const std::string& d : dirs) {
    if (!g_progFolders.empty() && g_progFolders.back() == d) ++g_progFolderCount.back();
    else { g_progFolders.push_back(d); g_progFolderCount.push_back(1); }
  }
  // A folder that has just been emptied should not keep the page inside it.
  if (!g_progDir.empty() &&
      !std::binary_search(g_progFolders.begin(), g_progFolders.end(), g_progDir))
    g_progDir.clear();
}

void promptSaveAs(plat::Where w) {
  openPicker(std::string("Save on the ") + whereName(w), "", kKbText,
             fileNameFor(g_edit.programName()), 24,
             [w](const std::string& v) {
               if (v.empty()) return;
               if (!plat::progWrite(w, inProgDir(v), g_edit.text())) {
                 message("Save failed",
                         std::string("Could not write to the ") + whereName(w) + ".");
                 return;
               }
               // The program is now that file, edits and all.
               g_edit.adoptBaseline();
               clearUndo();
               const std::string path = inProgDir(v);
               g_edit.setProgramName(v);
               g_progFile  = path;
               g_progWhere = w;
               refreshProgFiles(true);
               message("Saved", path + ".txt on the " + whereName(w));
             },
             kKbSplit);
}

void drawSave() {
  clearBody();
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
// what the RUN page does, then the About pages, then the reset and the guide.
// Two of the tiles only exist once unlocked, and the rows below them close up
// when they are absent.
Btn btnSysWifi()    { return sysTile(0, 0, "WIFI"); }
Btn btnSysTheme()   { return sysTile(0, 1, "THEME: NIGHT"); }
Btn btnSysSd()      { return sysTile(3, 0, "SD LOG: OFF"); }
Btn btnSysDebug()   { return sysTile(1, 1, "DIAGNOSTICS"); }
// What the RUN page shows under the program: OUTPUT, RUNNERS or NOTHING.
Btn btnSysBand()    { return sysTile(1, 0, "UNDER GRID: OUTPUT"); }
Btn btnSysCal()     { return sysTile(2, 1, "CHECK TOUCH"); }
Btn btnSysSteps()   { return sysTile(4, 0, "STEP BUTTONS: OFF"); }
Btn btnSysTrail()   { return sysTile(4, 1, "TRAIL: OFF"); }
// What a tap on the RUN grid does, which is one thing at a time.
Btn btnSysFollow()  { return sysTile(5, 0, "FOLLOW RUNNER: ON"); }
// Only where a real keyboard can exist. On the board the tile is absent
// rather than present and useless, and it sits last so the space it leaves is
// at the foot of the grid instead of a hole in the middle of it.
Btn btnSysKeys()    { return sysTile(5, 1, "KEYBOARD: ON SCREEN"); }
Btn btnSysStart()   { return sysTile(2, 0, "GRID TAP: NOTHING"); }
Btn btnSysRestore() { return sysTile(3, 1, "RESTORE BUILT-INS"); }
// Everything below here is anchored to the foot of the page rather than to a
// row of the settings grid: the pages that tell you about the device, and
// under them the reset and the way to the guide. They keep the same place
// whichever mode the device is in, and a rule separates the lot from the
// settings above. sysFootTile counts rows UP from the bottom, so row 0 is the
// bottom row and adding a mode's extra tiles pushes the block up, never down
// off the screen.
Btn sysFootTile(int rowUp, int col, const char* label, uint16_t fg) {
  const int w = (kScreenW - 12) / 2;
  return { 4 + col * (w + 4), kTabY - 26 - rowUp * kSysRowH, w, 22,
           label, fg, theme::panel };
}
// One more row of About tiles when unlocked, so the block starts a row higher.
int  sysAboutRows()  { return Store::unlocked() ? 3 : 2; }
int  sysFootRuleY()  { return kTabY - 32 - sysAboutRows() * kSysRowH + kSysRowH - 4; }

Btn btnSysInfo()  { return sysFootTile(2, 0, pack::str(pack::kStrInfoTile), theme::accent); }
Btn btnSysIrcis() { return Store::unlocked() ? sysFootTile(2, 1, "ABOUT IRCIS", theme::accent)
                                            : sysFootTile(1, 0, "ABOUT IRCIS", theme::accent); }
Btn btnSysRead()  { return Store::unlocked() ? sysFootTile(1, 0, "ABOUT THIS DEVICE", theme::accent)
                                            : sysFootTile(1, 1, "ABOUT THIS DEVICE", theme::accent); }
// Puts the device back to looking like a plain IRCIS interpreter without
// throwing away anything else. Re-entering means setting the WiFi credentials
// again. Only exists once unlocked, for obvious reasons.
Btn btnSysExit()  { return sysFootTile(1, 1, pack::str(pack::kStrExitTile), theme::warn); }
// The bottom row: erasing everything, and the learning guide as a link and a
// code to scan. The guide sits down here with the About pages because it is
// the one thing on this page a beginner is looking for.
Btn btnSysReset() { return sysFootTile(0, 0, "RESET ALL DATA", theme::bad); }
Btn btnSysLearn() { return sysFootTile(0, 1, "LEARN IRCIS", theme::accent); }

// ---------------------------------------------------------------------------
// PROG: pick a program to load. While locked this lists only the bundled IRCIS
// examples; unlocked, the packed program joins them at the top.
// ---------------------------------------------------------------------------

// The first program the list will show. The packed one is index 0, so locking
// simply starts the list one further along.
constexpr int kProgRowH = 26;
// How many rows fit above the tab bar, leaving the last one for the scroll
// arrows when the list is longer than that.
int progRows()    { return (kTabY - 8 - (kBodyY + 4)) / kProgRowH; }
int g_progTop = 0;                       // first program shown
// Centred under the list and a good deal bigger than they were: these are the
// only way through a long list, and 26 x 21 in the corner was a small target.
Btn btnProgUp()   { return { kScreenW / 2 - 92, kTabY - 30, 84, 26, "^" }; }
Btn btnProgDown() { return { kScreenW / 2 + 8,  kTabY - 30, 84, 26, "v" }; }
// PROGRAMS is one list: the two things you can do to it, then your own saved
// programs, then the built-ins. It used to be two tabs -- PROG for the
// built-ins and SAVE for the card -- which split "pick a program to run"
// across two places and left SAVE showing one sentence and one button on a
// device with no card. Rows are built by one function and both drawn and
// hit-tested from it, so the list and the taps cannot drift apart.
struct ProgRow {
  enum Kind { Packed, SaveDev, SaveCard, Discard, NewProg, Up, Folder, File };
  Kind kind;
  int  index;              // File: into that store's list. Folder: into g_progFolders
  plat::Where where;       // File: which store the row came from
};

std::vector<std::string>& filesIn(plat::Where w) {
  return w == plat::Where::Device ? g_devFiles : g_cardFiles;
}

// What you can do to the list, as against what is in it. These stay put at the
// top of the page: scrolling to the bottom of twenty-odd programs and finding
// Save gone is the sort of thing you only forgive once.
void buildProgActions(std::vector<ProgRow>& out) {
  out.clear();
  const bool card = plat::sdPresent();
  // A packed program is the one thing here that is never a file, so it is listed on
  // its own above everything else.
  if (Store::unlocked() && g_progDir.empty())
    out.push_back({ ProgRow::Packed, prog::kPackedIndex, plat::Where::Device });
  out.push_back({ ProgRow::SaveDev,  0, plat::Where::Device });
  if (card) out.push_back({ ProgRow::SaveCard, 0, plat::Where::Card });
  // Only when there is something to throw away, so the row is never a dead
  // option sitting next to Save.
  if (g_edit.modifiedCells() > 0)
    out.push_back({ ProgRow::Discard, 0, plat::Where::Device });
  out.push_back({ ProgRow::NewProg, 0, plat::Where::Device });
  // The way out of a folder goes last, under the rows that are on this page
  // whatever you are looking at. Putting it first moved every one of them
  // along by a place the moment you opened a folder.
  if (!g_progDir.empty())
    out.push_back({ ProgRow::Up, 0, plat::Where::Device });
}

// And the files themselves, which are what scrolls.
void buildProgRows(std::vector<ProgRow>& out) {
  out.clear();
  // At the top of the list the folders come first, newest question first:
  // which kind of program, then which one.
  if (g_progDir.empty())
    for (int i = 0; i < (int)g_progFolders.size(); ++i)
      out.push_back({ ProgRow::Folder, i, plat::Where::Device });
  // One list, in name order, whichever store a program is in -- looking for a
  // program by name should not mean knowing where it was saved first. The
  // icon down the left says which store each one is in.
  const std::size_t firstFile = out.size();
  for (int i = 0; i < (int)g_devFiles.size(); ++i)
    if (folderOf(g_devFiles[i]) == g_progDir)
      out.push_back({ ProgRow::File, i, plat::Where::Device });
  for (int i = 0; i < (int)g_cardFiles.size(); ++i)
    if (folderOf(g_cardFiles[i]) == g_progDir)
      out.push_back({ ProgRow::File, i, plat::Where::Card });
  std::sort(out.begin() + firstFile, out.end(),
            [](const ProgRow& a, const ProgRow& b) {
              const std::string na = leafOf(filesIn(a.where)[a.index]);
              const std::string nb = leafOf(filesIn(b.where)[b.index]);
              // Case-insensitive, so MyThing sorts beside mything rather than
              // ahead of every lower-case name on the device.
              const int c = strcasecmp(na.c_str(), nb.c_str());
              return c != 0 ? c < 0 : a.where < b.where;
            });
}

// A file row says which store it is in with a small mark rather than a word:
// a chip with legs for the board's own flash, a notched card for the SD slot.
// Both are drawn at the same 9 x 11 so the names still line up.
void drawStoreMark(int x, int y, plat::Where w, uint16_t c) {
  if (w == plat::Where::Device) {
    gfx.fillRect(x + 2, y + 1, 5, 9, c);            // the chip
    for (int i = 0; i < 3; ++i) {                   // and its legs
      gfx.drawFastHLine(x, y + 2 + i * 3, 2, c);
      gfx.drawFastHLine(x + 7, y + 2 + i * 3, 2, c);
    }
  }
  else {
    gfx.fillRect(x + 1, y, 7, 11, c);               // the card
    gfx.fillTriangle(x + 6, y, x + 8, y + 2, x + 8, y, theme::bg);  // notched corner
  }
}

// A folder gets a mark of its own, at the same 9 x 11 as the store marks so
// every name on the page starts at the same x.
void drawFolderMark(int x, int y, uint16_t c) {
  gfx.fillRect(x, y + 3, 9, 8, c);                  // the body
  gfx.fillRect(x, y + 1, 4, 2, c);                  // and its tab
}

// On the LEFT. It used to sit at the right end of the row, which is where a
// thumb travelling down the list lands, and deleting a program by mis-tapping
// a scroll is not a mistake worth making available.
// The actions at the top go two to a row: their labels are short, and every
// row they save is a program you can see without scrolling. The programs below
// keep a row each, because their names are long and finding one is what you
// came here for.
inline int progActionRows(int fixed) { return (fixed + 1) / 2; }

Btn progActionTile(int slot) {
  const int w = (kScreenW - 12) / 2;
  return { 4 + (slot % 2) * (w + 4), kBodyY + 4 + (slot / 2) * kProgRowH,
           w, 22, "", theme::text, theme::panel };
}

Btn progFileTile(int slot, int fixed) {
  const int row = progActionRows(fixed) + (slot - fixed);
  return { 4, kBodyY + 4 + row * kProgRowH, kScreenW - 8, 22,
           "", theme::text, theme::panel };
}

Btn progDel(int slot, int fixed) {
  Btn b = progFileTile(slot, fixed);
  return { 4, b.y, 28, 22, "X", theme::bad, theme::panel };
}

// A row's tile, narrowed to leave room for the delete button when it has one.
Btn progRowTile(const ProgRow& r, int slot, int fixed) {
  if (slot < fixed) return progActionTile(slot);
  Btn b = progFileTile(slot, fixed);
  if (r.kind == ProgRow::File) { b.x = 36; b.w = kScreenW - 40; }  // room for X
  return b;
}

int progVisible() { return progRows(); }
// Slots the page can hold: the paired action rows, then a row per program.
int progSlots(int fixed) {
  const int rows = progRows() - progActionRows(fixed);
  return fixed + (rows > 0 ? rows : 0);
}

void drawProg() {
  clearBody();
  std::vector<ProgRow> actions, files;
  buildProgActions(actions);
  buildProgRows(files);

  const int fixed   = (int)actions.size();
  const int visible = progSlots(fixed) - fixed;      // room left for the list
  const int total   = (int)files.size();
  const int maxTop  = total > visible ? total - visible : 0;
  if (g_progTop > maxTop) g_progTop = maxTop;
  if (g_progTop < 0) g_progTop = 0;

  int shown = 0;
  auto drawRow = [&](const ProgRow& r, int slot) {
    Btn b = progRowTile(r, slot, fixed);

    const char* name;
    const char* hint = nullptr;
    char sizebuf[32];
    uint16_t edge = theme::line, fg = theme::text;
    bool on = false;

    switch (r.kind) {
      case ProgRow::Packed: {
        const prog::ProgramDef& d = prog::programAt(r.index);
        name = d.name;
        on   = (g_edit.programIndex() == r.index && g_progFile.empty());
        fg   = theme::accent;
        snprintf(sizebuf, sizeof(sizebuf), "%d x %d", d.rows_n, d.cols_n);
        hint = sizebuf;
        break;
      }
      case ProgRow::SaveDev:
        name = "Save on device";
        edge = theme::good; fg = theme::good;
        break;
      case ProgRow::SaveCard:
        name = "Save on card";
        edge = theme::good; fg = theme::good;
        break;
      case ProgRow::Discard:
        name = "Discard changes";
        edge = theme::warn; fg = theme::warn;
        break;
      case ProgRow::NewProg:
        name = "New program";
        edge = theme::good; fg = theme::good;
        break;
      case ProgRow::Up: {
        static char buf[40];
        snprintf(buf, sizeof(buf), "%s", g_progDir.c_str());
        name = "Back"; hint = buf;
        edge = theme::dim;
        break;
      }
      case ProgRow::Folder: {
        static char buf[24];
        const int n = g_progFolderCount[r.index];
        snprintf(buf, sizeof(buf), "%d program%s", n, n == 1 ? "" : "s");
        name = g_progFolders[r.index].c_str();
        hint = buf;
        break;
      }
      default: {
        // The leaf only: inside Counting you are looking at odds, not at
        // Counting/odds, which you can already see at the top of the page.
        static std::string leaf;
        leaf = leafOf(filesIn(r.where)[r.index]);
        name = leaf.c_str();
        on   = (g_progWhere == r.where && g_progFile == filesIn(r.where)[r.index]);
        break;
      }
    }

    gfx.fillRoundRect(b.x, b.y, b.w, b.h, 3, on ? theme::accent : theme::panel);
    gfx.drawRoundRect(b.x, b.y, b.w, b.h, 3, on ? theme::accent : edge);
    uint16_t bg = on ? theme::accent : theme::panel;
    int textX = b.x + 6;
    if (r.kind == ProgRow::File) {
      drawStoreMark(b.x + 6, b.y + (b.h - 11) / 2, r.where,
                    on ? theme::bg : theme::dim);
      textX = b.x + 20;
    }
    else if (r.kind == ProgRow::Folder) {
      drawFolderMark(b.x + 6, b.y + (b.h - 11) / 2, theme::accent);
      textX = b.x + 20;
    }
    clabel(textX, b.y + (b.h - kContentH) / 2 + 1, name, on ? theme::bg : fg, bg);
    if (hint) {
      gfx.setFont(&fonts::Font0);
      gfx.setTextDatum(textdatum_t::middle_right);
      gfx.setTextColor(on ? theme::bg : theme::dim, bg);
      gfx.drawString(hint, b.x + b.w - 6, b.y + b.h / 2);
      gfx.setTextDatum(textdatum_t::top_left);
    }
    if (r.kind == ProgRow::File) drawBtn(progDel(slot, fixed));
  };

  for (const ProgRow& r : actions) drawRow(r, shown++);
  for (int i = g_progTop; i < total && shown < progSlots(fixed); ++i, ++shown)
    drawRow(files[i], shown);

  // Delete everything and the page was three action rows and silence, with
  // nothing to say where the programs went or how to get them back.
  if (total == 0)
    clabel(6, kBodyY + 8 + shown * kProgRowH,
           g_progDir.empty()
             ? "No programs. SYS > RESTORE BUILT-INS puts the bundled ones back."
             : "This folder is empty.",
           theme::dim);

  if (maxTop > 0) {
    drawBtn(btnProgUp(),   false, g_progTop > 0);
    drawBtn(btnProgDown(), false, g_progTop < maxTop);
    char where[40];
    snprintf(where, sizeof(where), "%d-%d of %d", g_progTop + 1,
             g_progTop + shown - fixed, total);
    clabel(6, kTabY - kContentH - 4, where, theme::dim);
  }
}

void handleProgTouch(int x, int y) {
  std::vector<ProgRow> actions, files;
  buildProgActions(actions);
  buildProgRows(files);

  const int fixed   = (int)actions.size();
  const int visible = progSlots(fixed) - fixed;
  const int total   = (int)files.size();
  const int maxTop  = total > visible ? total - visible : 0;
  if (maxTop > 0) {
    // A whole page a tap, not a line. Twenty-odd programs one row at a time is
    // twenty taps on a panel that mis-reads some of them.
    const int page = visible > 1 ? visible : 1;
    if (hit(btnProgUp(), x, y)) {
      if (g_progTop > 0) {
        g_progTop -= page; if (g_progTop < 0) g_progTop = 0;
        g_paint |= PaintBody; g_dirty = true;
      }
      return;
    }
    if (hit(btnProgDown(), x, y)) {
      if (g_progTop < maxTop) {
        g_progTop += page; if (g_progTop > maxTop) g_progTop = maxTop;
        g_paint |= PaintBody; g_dirty = true;
      }
      return;
    }
  }

  // The rows on screen, pinned actions first, then the scrolled slice.
  std::vector<ProgRow> rows = actions;
  for (int i = g_progTop; i < total && (int)rows.size() < progSlots(fixed); ++i)
    rows.push_back(files[i]);

  for (int slot = 0; slot < (int)rows.size(); ++slot) {
    const ProgRow& r = rows[slot];

    if (r.kind == ProgRow::File && hit(progDel(slot, fixed), x, y)) {
      std::string name = filesIn(r.where)[r.index];
      plat::Where w = r.where;
      confirm("Delete program?",
              name + ".txt on the " + whereName(w)
                + (w == plat::Where::Device
                     ? ". RESTORE BUILT-INS in SYS brings back the ones that "
                       "shipped with the device." : "."),
              [name, w] {
        plat::progDelete(w, name);
        refreshProgFiles(true);
      });
      return;
    }
    if (!hit(progRowTile(r, slot, fixed), x, y)) continue;

    switch (r.kind) {
      case ProgRow::SaveDev:  saveProgramTo(plat::Where::Device); return;
      case ProgRow::SaveCard: saveProgramTo(plat::Where::Card);   return;

      // Opening and leaving a folder changes the list and nothing else, so
      // only the list is repainted.
      case ProgRow::Folder:
        g_progDir  = g_progFolders[r.index];
        g_progTop  = 0;
        g_paint |= PaintBody;
        g_dirty    = true;
        return;

      case ProgRow::Up:
        g_progDir.clear();
        g_progTop  = 0;
        g_paint |= PaintBody;
        g_dirty    = true;
        return;

      case ProgRow::Discard:
        confirm("Discard changes?",
                "The program goes back to the last version saved, and every "
                "edit since is lost.",
                [] { g_edit.revertAll(); clearUndo(); markEdited(); });
        return;

      case ProgRow::NewProg:
        g_sizeIsNew = true;
        g_sizeRows  = 11;
        g_sizeCols  = 40;
        g_modal     = Modal::Size;
        g_dirty     = true;
        return;

      case ProgRow::Packed:
        if (g_edit.programIndex() != r.index || !g_progFile.empty()) {
          g_edit.loadProgram(r.index);
          g_progFile.clear();
          afterProgramChange();
          g_follow = true;
        }
        g_tab = Tab::Run;
        g_dirty = true;
        return;

      default: {
        const std::string name = filesIn(r.where)[r.index];
        std::string text;
        if (!plat::progRead(r.where, name, text)) {
          message("Load failed", "Could not read that file.");
          return;
        }
        // The title bar gets the name; the folder is context, not part of it.
        if (!loadProgramText(text, leafOf(name).c_str())) {
          message("Not a program", "That file is empty, or too big for the grid.");
          return;
        }
        g_progFile  = name;
        g_progWhere = r.where;
        g_tab = Tab::Run;
        return;                 // the rebuild's own repaint covers this
      }
    }
  }
}

constexpr int kMaxSysTiles = 20;

int sysTiles(Btn* out) {
  int n = 0;
  auto add = [&](Btn b) { if (n < kMaxSysTiles) out[n++] = b; };
  add(btnSysWifi());  add(btnSysTheme());
  add(btnSysBand());  add(btnSysDebug());
  add(btnSysStart()); add(btnSysCal());
  add(btnSysSd());    add(btnSysRestore());
  add(btnSysSteps()); add(btnSysTrail());
  add(btnSysFollow());
  if (plat::haveKeyboard()) add(btnSysKeys());
  if (Store::unlocked())    { add(btnSysInfo()); add(btnSysExit()); }
  add(btnSysIrcis()); add(btnSysRead());
  add(btnSysReset()); add(btnSysLearn());
  std::sort(out, out + n, [](const Btn& a, const Btn& b) {
    return a.y != b.y ? a.y < b.y : a.x < b.x;
  });
  return n;
}

// Which SYS tile a partial repaint is for. The order is the order the tiles
// are drawn in below, and nothing else depends on it.
enum SysTile { SysWifi, SysTheme, SysSd, SysDebug, SysCal, SysBand, SysRestore,
               SysSteps, SysKeys, SysFollow, SysTrail, SysStart, SysLearn, SysInfo,
               SysExit, SysIrcis, SysRead, SysReset, SysTileCount };

// One tile, with its label as it currently reads. Every tile paints its own
// face, so redrawing one leaves the rest of the page untouched.
void drawSysTile(int which) {
  // SYS is a page of labelled settings rather than a strip of controls, so
  // its tiles are set in the tab bar's face instead of the small one the
  // compact buttons share.
  auto tile = [](const Btn& b, bool on = false, bool enabled = true) {
    drawBtn(b, on, enabled, true);
  };
  switch (which) {
    case SysWifi: {
      Btn w = btnSysWifi();
      std::string wl = std::string("WIFI: ") + (web::running() ? web::ipAddress() : "off");
      // Whether anything is actually looking. An address alone does not say
      // whether the browser on the other end ever connected.
      if (web::running() && web::lastServedMs() != 0) {
        const uint32_t ago = (plat::millis() - web::lastServedMs()) / 1000;
        char seen[24];
        if (ago < 60) snprintf(seen, sizeof(seen), "  seen %us", (unsigned)ago);
        else          snprintf(seen, sizeof(seen), "  seen %um", (unsigned)(ago / 60));
        wl += seen;
      }
      w.label = wl.c_str();
      tile(w, web::running());
      break;
    }
    // A tile is filled when it is set to something OTHER than the default, so
    // the SYS page reads as a list of what has been changed rather than a wall
    // of highlights. Day and Output are the defaults.
    case SysTheme: {
      Btn th = btnSysTheme();
      std::string tl = std::string("THEME: ") + (Store::dayMode() ? "DAY" : "NIGHT");
      th.label = tl.c_str();
      tile(th, !Store::dayMode());
      break;
    }
    case SysSd: {
      // With no card there is nowhere to log, so the tile reads OFF and is
      // greyed rather than offering a switch that cannot do anything.
      const bool card = plat::sdPresent();
      Btn sd = btnSysSd();
      std::string sl = std::string("SD LOG: ")
                     + ((card && Store::sdLoggingEnabled()) ? "ON" : "OFF");
      sd.label = sl.c_str();
      tile(sd, card && Store::sdLoggingEnabled(), card);
      break;
    }
    case SysDebug: tile(btnSysDebug()); break;
    case SysCal:   tile(btnSysCal());   break;
    case SysBand: {
      const int band = bandMode();
      Btn bt = btnSysBand();
      std::string bl = std::string("UNDER GRID: ")
                     + (band == kBandRunners ? "RUNNERS"
                      : band == kBandNothing ? "NOTHING" : "OUTPUT");
      bt.label = bl.c_str();
      tile(bt, band != kBandOutput);       // OUTPUT is the default, so it reads plain
      break;
    }
    case SysRestore: tile(btnSysRestore()); break;
    case SysSteps: {
      Btn sb = btnSysSteps();
      std::string sbl = std::string("STEP BUTTONS: ")
                      + (Store::stepButtons() ? "ON" : "OFF");
      sb.label = sbl.c_str();
      tile(sb, Store::stepButtons());
      break;
    }
    case SysKeys: {
      if (!plat::haveKeyboard()) break;
      Btn kb = btnSysKeys();
      std::string kbl = std::string("KEYBOARD: ")
                      + (Store::hardwareKeys() ? "REAL" : "ON SCREEN");
      kb.label = kbl.c_str();
      tile(kb, Store::hardwareKeys());
      break;
    }
    case SysFollow: {
      Btn fl = btnSysFollow();
      std::string fll = std::string("FOLLOW RUNNER: ")
                      + (Store::followRunners() ? "ON" : "OFF");
      fl.label = fll.c_str();
      tile(fl, !Store::followRunners());    // ON is the default, so it reads plain
      break;
    }
    case SysTrail: {
      Btn tr = btnSysTrail();
      std::string trl = std::string("TRAIL: ") + (Store::tracePath() ? "ON" : "OFF");
      tr.label = trl.c_str();
      tile(tr, Store::tracePath());        // OFF is the default, so it reads plain
      break;
    }
    case SysStart: {
      Btn st = btnSysStart();
      const int gt = Store::gridTap();
      std::string stl = std::string("GRID TAP: ")
                      + (gt == Store::kTapInspector ? "INSPECTOR"
                       : gt == Store::kTapStart     ? "START POINT" : "NOTHING");
      st.label = stl.c_str();
      tile(st, gt != Store::kTapNothing);
      break;
    }
    case SysLearn: tile(btnSysLearn()); break;
    case SysInfo:  if (Store::unlocked()) tile(btnSysInfo()); break;
    case SysExit:  if (Store::unlocked()) tile(btnSysExit()); break;
    case SysIrcis: tile(btnSysIrcis()); break;
    case SysRead:  tile(btnSysRead());  break;
    case SysReset: tile(btnSysReset()); break;
    default: break;
  }
}

void drawSys() {
  clearBody();
  for (int t = 0; t < SysTileCount; ++t) drawSysTile(t);
  gfx.drawFastHLine(4, sysFootRuleY(), kScreenW - 8, theme::line);
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

// The title, the value being built and the hint or "was" line under it: the
// part of the picker a keystroke changes. The keys underneath do not.
void drawPickerTop() {
  const PickerGeom g = pickerGeom();
  gfx.fillRect(0, 0, kScreenW, g.y0, theme::bg);
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
}

// CLEAR while there is something to clear, REVERT once the value differs
// from what it opened with: the one button whose face follows the value.
void drawPickerClear() {
  Btn clr = btnPickClear();
  clr.label = (g_pickerValue != g_pickerOriginal) ? "REVERT" : "CLEAR";
  drawBtn(clr);
}

void drawPicker() {
  gfx.fillScreen(theme::bg);
  drawPickerTop();
  PickerGeom g = pickerGeom();
  for (int i = 0; i < (int)g_pickerSet.size(); ++i) {
    if (g_pickerSet[i] == '\x01') continue;          // spacer, not a key
    int r, c;
    if (!pickerCell(i, g, r, c)) continue;
    int x = g.x0 + c * g.kw, y = g.y0 + r * g.kh;
    bool symbol = g_pickerSplit && (std::size_t)i >= g_pickerSplit;
    // On the program keyboard the base64 letters that are also commands --
    // r R p v V -- take the accent, the way the editor's keyboard shows them.
    const bool command = symbol ||
        (g_pickerSet == kKbProgram && std::strchr("vVprR", g_pickerSet[i]) != nullptr);
    gfx.fillRect(x + 1, y + 1, g.kw - 2, g.kh - 2, symbol ? theme::line : theme::panel);
    useContentFont();
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setTextColor(command ? theme::accent : theme::text, symbol ? theme::line : theme::panel);
    char ch[2] = { g_pickerSet[i] == ' ' ? '_' : g_pickerSet[i], 0 };
    gfx.drawString(ch, x + g.kw / 2, y + g.kh / 2);
    gfx.setTextDatum(textdatum_t::top_left);
  }
  gfx.setTextSize(1);

  drawPickerClear();
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
// The window is six rows deep whatever it is looking at, so a short program
// drew two rows at the top and left four rows of nothing under them. Centre
// what there is. The hit test measures from here too, so the two agree.
int insTop() {
  const int base = 3 * kContentH + 8;
  const int rows = g_edit.rows() < kInsRows ? g_edit.rows() : kInsRows;
  return base + (kInsRows - rows) * kInsCellH / 2;
}
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

Btn btnCellSet()   { return { modalBtnX(0), kModalBtnY, kModalBtnW, 26, "EDIT CHAR", theme::bg, theme::good }; }
Btn btnCellRevert(){ return { modalBtnX(1), kModalBtnY, kModalBtnW, 26, "REVERT", theme::warn }; }
Btn btnCellStart() { return { modalBtnX(2), kModalBtnY, kModalBtnW, 26, "START", theme::edited }; }
Btn btnCellClose() { return { modalBtnX(3), kModalBtnY, kModalBtnW, 26, "CLOSE", theme::bad }; }

// Small chevrons pushed to the outer edge of their cell, so the character in
// the middle stays readable behind them.

void drawCellModal(bool full = true) {
  if (full) {
    gfx.fillScreen(theme::bg);
    gfx.setFont(&fonts::Font2);
    gfx.setTextColor(theme::accent, theme::bg);
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.drawString("CHARACTER INSPECTOR", 6, 4);
  }
  else {
    // The title stays. The character top right, the lines under it, the
    // window on the grid and the buttons are what a tap here changes.
    gfx.fillRect(220, 0, kScreenW - 220, 26, theme::bg);
    gfx.fillRect(0, 26, kScreenW, kScreenH - 26, theme::bg);
  }

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


  // Nothing is changed under a running program: the three buttons that
  // would are drawn off until it is paused or has finished.
  const bool live = run::snapshot().running;
  const bool hints = keyHints();
  Btn set = btnCellSet();    if (hints) set.label = "(E)DIT CHAR";
  Btn rev = btnCellRevert(); if (hints) rev.label = "(R)EVERT";
  Btn cls = btnCellClose();  if (hints) cls.label = "(C)LOSE";
  drawBtn(set, false, !live);
  drawBtn(rev, false, cur != orig && !live);
  if (Store::gridTap() != Store::kTapNothing) {
    // Pressing START again cycles the direction, so one button sets both.
    Btn stb = btnCellStart();
    bool isStart = (run::startRow() == g_cellRow && run::startCol() == g_cellCol);
    char lbl[16];
    snprintf(lbl, sizeof(lbl), hints ? "(S)TART %c" : "START %c", isStart ? run::startDir() : 'E');
    stb.label = lbl;
    drawBtn(stb, isStart, !live);
  }
  drawBtn(cls);
}

// ---------------------------------------------------------------------------
// Paged dialogs: DIAGNOSTICS (keys / system) and ABOUT (IRCIS, the device)
// ---------------------------------------------------------------------------


constexpr int kDlgY = 26;
int dlgH() { return kTabY - kDlgY - 8; }
Btn btnDlgPrev()  { return { 20, kDlgY + dlgH() - 32, 70, 26, "<" }; }
Btn btnDlgNext()  { return { 94, kDlgY + dlgH() - 32, 70, 26, ">" }; }
Btn btnDlgClose() { return { kScreenW - 92, kDlgY + dlgH() - 32, 72, 26,
                             keyHints() ? "(C)LOSE" : "CLOSE", theme::bad }; }
// DIAGNOSTICS only. Writing the grid and its edits to the console is a thing
// you do while looking at the console, which is what this dialog is for; as a
// SYS tile it sat among settings and did nothing visible on the device.
Btn btnDlgDump()  { return { 172, kDlgY + dlgH() - 32, 110, 26,
                             keyHints() ? "(D)UMP GRID" : "DUMP GRID" }; }

void dumpGrid() {
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

// Set while a paged dialog is redrawn for a page turn: the box, its border
// and its title are already on the panel, so dlgFrame clears only the page
// inside it and the counter, and the buttons paint their own faces.
bool g_dlgPageOnly = false;

int dlgFrame(const char* title, const char* page, int pages) {
  int w = kScreenW - 24, h = dlgH();
  if (!g_dlgPageOnly) {
    gfx.fillRect(12, kDlgY, w, h, theme::panel);
    gfx.drawRect(12, kDlgY, w, h, theme::accent);
    gfx.setFont(&fonts::Font2);
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.setTextColor(theme::accent, theme::panel);
    gfx.drawString(title, 20, kDlgY + 5);
  }
  else {
    const int top = kDlgY + 26, bottom = btnDlgPrev().y - 4;
    gfx.fillRect(13, top, w - 2, bottom - top, theme::panel);
    gfx.fillRect(kScreenW - 200, kDlgY + 8, 180, 12, theme::panel);   // the counter
  }
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
  if (g_modal == Modal::Debug) drawBtn(btnDlgDump());
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
  // Wide enough for the longest death reason plus the runner and the cell.
  char buf[96];
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
  y += kContentH;

  // The interpreter records why every runner died and, until now, nothing but
  // the serial console ever said so. Deaths at '!' or off the edge are how a
  // program ends and are left out; what is left is a real fault.
  const int bottom = kDlgY + dlgH() - 40;      // clear of the dialog buttons
  int shown = 0;
  if (snap.deathNoteCount) y += 4;
  for (int i = 0; i < snap.deathNoteCount; ++i) {
    const run::DeathNote& d = snap.deathNotes[i];
    snprintf(buf, sizeof(buf), "runner %d died at row %d, col %d: %s",
             (int)d.id, (int)d.row, (int)d.col, d.why);
    // Measure first: half a reason is worse than a count of what is missing.
    if (wrapped(20, y, buf, theme::bad, 11, INT16_MAX, false) > bottom) break;
    y = wrapped(20, y, buf, theme::bad, 11, bottom);
    ++shown;
  }
  if (shown < snap.deathNoteTotal) {
    snprintf(buf, sizeof(buf), "+%d more, see the serial console",
             snap.deathNoteTotal - shown);
    wrapped(20, y, buf, theme::dim, 11, bottom + 11);
  }
}

void drawDebug() {
  // The globals page reads named variables out of the machine, which only a
  // packed program can label. It is not shown at all in plain mode.
  const bool keysPage = Store::unlocked();
  const int pages = keysPage ? 2 : 1;
  if (!keysPage) g_dialogPage = 0;
  int y = dlgFrame("DIAGNOSTICS", keysPage && g_dialogPage == 0 ? "keys" : "system", pages);
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
  "MOVEMENT AND SPLITTING",
  "",
  "  >   walk east      <   walk west",
  "  ^   walk north     v   walk south",
  "  !   end this runner",
  "",
  "  *   looks at the four neighbours;",
  "      for each one holding that way's",
  "      own arrow, a runner goes there.",
  "      New runners copy the stack.",
};
// The three modes are the idea the rest of these pages hang off: several
// characters do one job walking and another between a ' and a blank.
const char* const kIrcis2b[] = {
  "THE THREE MODES",
  "",
  " normal   walking. IRCIS characters",
  "          act, the rest are ignored",
  " stack    between two \" marks; every",
  "          character is pushed",
  " integer  between ' and a blank; a",
  "          number or an operator",
  "",
  " Many characters mean two things.",
};
const char* const kIrcis3[] = {
  "VALUES",
  "",
  "  '   integer mode. A number, ending",
  "      at the next blank.",
  "        '42.  all digits: decimal",
  "        'fU.  a letter: base64",
  "",
  "  \"   stack mode. Everything between",
  "      the quotes is pushed, one",
  "      character at a time.",
};
const char* const kIrcis3b[] = {
  "BASE64 NUMBERS",
  "",
  "  The 64 digits, in order:",
  "    A-Z is 0-25,  a-z is 26-51,",
  "    0-9 is 52-61,  + is 62,  / is 63",
  "",
  "  So 'B. is 1 and 'BA. is 64. Leading",
  "  As are zeros and are dropped, which",
  "  is why padding with A is harmless.",
  "  %  prints a number back as base64.",
};
const char* const kIrcis4[] = {
  "THE STACK AND VARIABLES",
  "",
  "  Letters name a variable:",
  "    &N.  copy the top into N,",
  "         leaving it on the stack",
  "    @N.  push what is in N",
  "    Capitals are shared by every",
  "    runner, lower case is private.",
  "  Digits mean the stack itself:",
  "    &2. drop two   @2. copy the 2nd",
};
const char* const kIrcis5[] = {
  "ARITHMETIC - INTEGER MODE",
  "",
  "  Each takes the top two off the",
  "  stack, TOP FIRST, and pushes the",
  "  answer. All need the ' prefix.",
  "",
  "    '+ add        '- subtract",
  "    '* multiply   '/ divide",
  "    '% remainder  '^ power",
  "    '& and '| or 'V xor '< '> shift",
};
const char* const kIrcis6[] = {
  "OUTPUT, CHANCE AND TIME",
  "",
  "  #   pop and print",
  "  %   pop and print as base64",
  "  $   end the line",
  "",
  "  r   push a random 0 or 1",
  "  R   pop a limit, push a random",
  "      number from 0 up to it",
  "  p   pause for the number on top",
};
const char* const kIrcis7[] = {
  "CONDITIONS AND BLANKS",
  "",
  "  ?   looks at the top without",
  "      removing it. Zero turns the",
  "      runner, anything else carries",
  "      on. It turns to whichever side",
  "      has a non-blank cell, trying",
  "      left first.",
  "  .   and space are blanks. Any",
  "      other character is walked over.",
};
// Not part of IRCIS. The last two pages say so, because someone reading these
// pages to learn the language should not come away with a command that only
// exists here.
const char* const kIrcis8[] = {
  "VIEW TAGS - pIRCIS ONLY",
  "",
  "Not IRCIS. A tilde and some letters",
  "where no runner reaches, saying how",
  "to show the program. Any order.",
  "",
  "Anything a tag leaves out takes the",
  "default: the output under the grid,",
  "medium speed, no trail, starting at",
  "0,0 heading east, and GRID TAP off.",
};
const char* const kIrcis9[] = {
  "VIEW TAGS - THE LETTERS",
  "",
  "under grid  n nothing  d runners",
  "speed       s slow     m medium",
  "            q quick    f full",
  "trail       t keep the path on screen",
  "hold        h do not follow runners",
  "start       row,col and one of NESW",
  "",
  "eg ~nth3,1N  no readout, trail, held",
};

// Shown when SYS > KEYBOARD is switched to REAL, and on F1 after that. No
// heading on any of these: the tag in the corner of the frame already says
// which page you are on, and the two lines it costs are the two lines that
// were pushing the last one down onto the buttons.
const char* const kShortcuts1[] = {
  "Tab          the next page",
  "Shift-Tab    the page before",
  "Esc          close what is open",
  "F1           this list",
  "",
  "Arrows       move between the",
  "             controls on the page",
  "Space Enter  press the one picked",
  "             out by the ring",
};
const char* const kShortcuts2[] = {
  "  p  play or pause    s  speed",
  "  f  forward a step   b  back a step",
  "  r  back to the start",
  "  e  run to the end   z  ZOOM, WIDE",
  "  n  rename it        x  delete (PROG)",
  "  Ctrl or Cmd V pastes one in",
  "",
  "  On RUN the arrows scroll, or move",
  "  between cells when GRID TAP is on.",
};
const char* const kShortcuts3[] = {
  "  Letters go into the program, so",
  "  the commands on the last page do",
  "  not apply here. Arrows move the",
  "  cursor and Backspace clears a cell.",
  "",
  "  Ctrl or Cmd with S, Z, Y, N and G",
  "  save, undo, redo, rename and switch",
  "  the grid view. Add Shift and ? to",
  "  that and you get this list.",
};
const char* const kShortcuts4[] = {
  "  Arrows       move between its",
  "               controls, or turn its",
  "               pages",
  "  Space Enter  press one",
  "  c  or  Esc   close it",
  "  d            DUMP GRID, in",
  "               DIAGNOSTICS",
  "",
  "  In the inspector the arrows move",
  "  the cell, and e r s c press its",
  "  buttons: edit, revert, start, close.",
};
const char* const kShortcuts5[] = {
  "  Alt-R  Alt-L    rotate",
  "  Alt-1 to Alt-6  scale",
  "",
  "  These need Alt so that r, l and",
  "  the digits stay ordinary",
  "  characters in a program.",
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

const char* const kDevice4bLocked[] = {
  "RESIZE has two pages.",
  "",
  "The first adds rows and columns at a",
  "named edge, or takes them away, so",
  "you can say whether a new row lands",
  "above the program or below it.",
  "",
  "The second inserts a row or column",
  "before a numbered one, or deletes it.",
  "Delete asks twice; UNDO takes back.",
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
  SK_PAGE(kIrcis1,  "the language", 9), SK_PAGE(kIrcis2,  "movement",   -1),
  SK_PAGE(kIrcis2b, "modes",      -1), SK_PAGE(kIrcis3,  "values",     -1),
  SK_PAGE(kIrcis3b, "base64",     -1), SK_PAGE(kIrcis4,  "the stack",  -1),
  SK_PAGE(kIrcis5,  "arithmetic", -1), SK_PAGE(kIrcis6,  "output",     -1),
  SK_PAGE(kIrcis7,  "conditions", -1), SK_PAGE(kIrcis8,  "pIRCIS only", -1),
  SK_PAGE(kIrcis9,  "pIRCIS only", -1),
};
const TextPage kShortcutPages[] = {
  SK_PAGE(kShortcuts1, "everywhere", -1), SK_PAGE(kShortcuts2, "a program", -1),
  SK_PAGE(kShortcuts3, "the editor", -1), SK_PAGE(kShortcuts4, "dialogs",   -1),
  SK_PAGE(kShortcuts5, "the window", -1),
};
const TextPage kDeviceLockedPages[] = {
  SK_PAGE(kDevice1Locked, "what it is",  9), SK_PAGE(kDevice2Locked, "the tabs",   -1),
  SK_PAGE(kDevice3Locked, "the tabs",   -1), SK_PAGE(kDevice4Locked, "editing",    -1),
  SK_PAGE(kDevice4bLocked, "resizing", -1),
  SK_PAGE(kDevice5Locked, "start point", -1),
};
#undef SK_PAGE

constexpr int kIrcisCount  = (int)(sizeof(kIrcisPages) / sizeof(TextPage));
constexpr int kShortcutCount = (int)(sizeof(kShortcutPages) / sizeof(TextPage));
constexpr int kDeviceCount = (int)(sizeof(kDeviceLockedPages) / sizeof(TextPage));

// How many pages a dialog has. The packed groups are described by the pack, so
// adding one there cannot leave the ">" button refusing to reach it.
// The written pages, plus one the device fills in about itself.
int devicePageBase() {
  const int n = pack::pageCount(pack::kGroupDevice);
  return n > 0 ? n : kDeviceCount;
}
int devicePageCount() { return devicePageBase() + 1; }

// How many pages the dialog that is open has. The touch handler and the
// keyboard both page through them, so they ask the same question here.
int dialogPageCount() {
  return g_modal == Modal::Info      ? pack::pageCount(pack::kGroupInfo)
       : g_modal == Modal::Shortcuts ? kShortcutCount
       : g_modal == Modal::Ircis     ? kIrcisCount
       : g_modal == Modal::Device    ? devicePageCount()
       : g_modal == Modal::Debug     ? (Store::unlocked() ? 2 : 1)
       : 1;
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
void drawShortcuts() { drawTextPages("KEYBOARD SHORTCUTS", kShortcutPages, kShortcutCount); }
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

// The last page of ABOUT THIS DEVICE says which build is on the board. The
// version names the release; the build stamp pins the exact binary, which is
// what you actually need when a bug report says "the latest one"; the id is
// the first eight bytes of the app image hash, which is also what the device
// watches to notice it has been reflashed.
void drawFirmwarePage(int count) {
  int y = dlgFrame("THIS DEVICE", "firmware", count);
  y += 4;
  struct { const char* k; std::string v; } rows[] = {
    { "version", PIRCIS_VERSION },
    { "built",   plat::firmwareBuilt() },
    { "id",      plat::firmwareId() },
    { "panel",   std::string(SK_PANEL_NAME) },
  };
  char buf[96];
  for (const auto& r : rows) {
    snprintf(buf, sizeof(buf), "%-8s %s", r.k, r.v.c_str());
    clabel(20, y, buf, theme::text, theme::panel);
    y += kContentH;
  }
  y += 6;
  clabel(20, y, "github.com/jamesleaver/pIRCIS", theme::accent, theme::panel);
}

// Where the guide lives, as text and as a code a phone can read.
const char* const kLearnUrl = "https://github.com/jamesleaver/pIRCIS/blob/main/LEARN.md";

// A QR code, w pixels square, in black on a white quiet zone whatever the
// palette -- a phone wants the contrast. Drawn module by module from the
// library's generator rather than by its own routine, which on this panel
// painted each dark module as a single pixel.
// The library's own qrcode() reads modules through an accessor whose C-side
// bool is a plain byte holding the raw bit mask; a C++ caller sees only its
// low bit, so seven modules in eight go missing.  Read the bits ourselves.
void drawQr(const char* text, int x, int y, int w) {
  for (uint8_t v = 1; v <= 40; ++v) {
    QRCode q;
    std::vector<uint8_t> buf(lgfx_qrcode_getBufferSize(v));
    if (lgfx_qrcode_initText(&q, buf.data(), v, 0, text) != 0) continue;   // too small: next
    const int n = q.size, t = w / n, off = (w - t * n) / 2;
    gfx.fillRect(x, y, w, w, TFT_WHITE);
    for (int iy = 0; iy < n; ++iy)
      for (int ix = 0; ix < n; ++ix) {
        const int bit = iy * n + ix;
        if ((buf[bit >> 3] >> (7 - (bit & 7))) & 1)
          gfx.fillRect(x + off + ix * t, y + off + iy * t, t, t, TFT_BLACK);
      }
    return;
  }
}
void drawLearn() {
  int y = dlgFrame("LEARN IRCIS", "", 1);
  const char* lines[] = {
    "A guide to the language,",
    "from the first runner to",
    "reading a whole program,",
    "in twenty short sections.",
    "Every example in it runs.",
    "",
    "Scan it, or type:",
    "github.com/jamesleaver/",
    "pIRCIS/blob/main/LEARN.md",
  };
  for (const char* l : lines) { clabel(20, y, l, theme::text, theme::panel); y += kContentH; }
  drawQr(kLearnUrl, kScreenW - 12 - 8 - 148, kDlgY + 32, 148);
}

void drawDevice() {
  const int count = devicePageCount();
  if (g_dialogPage >= count - 1) { drawFirmwarePage(count); return; }
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

void drawWifi(bool full = true) {
  int w = kScreenW - 24, h = 162;
  if (full) {
    gfx.fillRect(12, kDlgY, w, h, theme::panel);
    gfx.drawRect(12, kDlgY, w, h, theme::accent);
    gfx.setFont(&fonts::Font2);
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.setTextColor(theme::accent, theme::panel);
    gfx.drawString("WIFI", 20, kDlgY + 5);
  }
  else {
    // Every button paints its own face; only the line of text under them
    // needs clearing before it is written again.
    gfx.fillRect(13, kDlgY + 118, w - 2, kContentH + 2, theme::panel);
  }

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


// A dialog is set in the same face as everything else it sits over. It used
// to be the 5 x 7 pixel font at ten pixels a line inside a fixed 120 px box,
// which was small enough to look like a different program had drawn it -- and
// on a screen this size, a warning you have to lean in to read is a warning
// half-read. The box sizes itself to the wrapped text instead of the text
// being squeezed into the box.
constexpr int kMsgWrap = 36;          // characters a line at the content font
constexpr int kMsgPad  = 12;
constexpr int kMsgBtnH = 26;

std::vector<std::string> msgLines() {
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < g_msgBody.size()) {
    std::size_t take = g_msgBody.size() - i;
    if (take > kMsgWrap) {
      // Wrap on word boundaries. Breaking mid-word reads as a rendering fault
      // rather than a long message, which is exactly the wrong impression for
      // a dialog whose whole job is to be believed.
      std::size_t sp = g_msgBody.rfind(' ', i + kMsgWrap);
      take = (sp == std::string::npos || sp <= i) ? kMsgWrap : sp - i;
    }
    out.push_back(g_msgBody.substr(i, take));
    i += take;
    while (i < g_msgBody.size() && g_msgBody[i] == ' ') ++i;
  }
  if (out.empty()) out.push_back("");
  return out;
}

int msgH() {
  return kMsgPad + kContentBigH + 8 + (int)msgLines().size() * kContentH
       + kMsgPad + kMsgBtnH + kMsgPad;
}
int msgY() {
  int y = (kScreenH - msgH()) / 2;
  return y < 26 ? 26 : y;
}
int msgBtnY() { return msgY() + msgH() - kMsgPad - kMsgBtnH; }

Btn btnMsgCancel()  { return { kScreenW / 2 - 118, msgBtnY(), 104, kMsgBtnH, "CANCEL", theme::bad }; }
Btn btnMsgConfirm() { return { kScreenW / 2 + 14,  msgBtnY(), 104, kMsgBtnH, "CONFIRM", theme::bg, theme::good }; }
Btn btnMsgOk()      { return { kScreenW / 2 - 52,  msgBtnY(), 104, kMsgBtnH, "OK", theme::bg, theme::good }; }

void drawMessage() {
  const std::vector<std::string> lines = msgLines();
  const int y0 = msgY(), h = msgH();
  gfx.fillRect(20, y0, kScreenW - 40, h, theme::panel);
  gfx.drawRect(20, y0, kScreenW - 40, h, theme::accent);

  // The title in the larger content face, so it reads as the title rather
  // than as a caption above bigger text.
  useContentFont(true);
  gfx.setTextDatum(textdatum_t::top_center);
  gfx.setTextColor(theme::accent, theme::panel);
  gfx.drawString(g_msgTitle.c_str(), kScreenW / 2, y0 + kMsgPad);

  useContentFont();
  gfx.setTextDatum(textdatum_t::top_center);
  gfx.setTextColor(theme::text, theme::panel);
  int y = y0 + kMsgPad + kContentBigH + 8;
  for (const std::string& ln : lines) {
    gfx.drawString(ln.c_str(), kScreenW / 2, y);
    y += kContentH;
  }
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.setFont(&fonts::Font0);

  if (g_modal == Modal::Confirm) { drawBtn(btnMsgCancel()); drawBtn(btnMsgConfirm()); }
  else                           { drawBtn(btnMsgOk()); }
}

void message(const std::string& title, const std::string& body) {
  g_msgTitle = title; g_msgBody = body; g_modal = Modal::Message; wantAll();
}

void confirm(const std::string& title, const std::string& body, std::function<void()> yes) {
  g_msgTitle = title; g_msgBody = body; g_confirmYes = yes;
  g_modal = Modal::Confirm; wantAll();
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
  Store::setRunView(0);
  Store::setOutputColour(false);
  // The inspector is the tool this mode is for, so the grid tap opens it
  // rather than leaving the reader to find the setting first.
  Store::setGridTap(Store::kTapInspector);
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

// The window is shared, so nothing moves when the page changes. Two things
// still need saying: the editor keeps its cursor on screen, so on the way in
// the cursor is brought inside the window rather than the window being pulled
// to the cursor; and a window the reader scrolled by hand keeps still on the
// way back rather than chasing the runner away from it.
void carryViewAcross(Tab from, Tab to) {
  if (Store::unlocked()) return;
  if (from == Tab::Run && to == Tab::Edit) {
    const int vr = edRows(), vc = visibleCols();
    if (g_curRow < g_gridRow)       g_curRow = g_gridRow;
    if (g_curRow >= g_gridRow + vr) g_curRow = g_gridRow + vr - 1;
    if (g_curCol < g_gridCol)       g_curCol = g_gridCol;
    if (g_curCol >= g_gridCol + vc) g_curCol = g_gridCol + vc - 1;
  }
  else if (from == Tab::Edit && to == Tab::Run) {
    if (g_edManualScroll) g_follow = false;
  }
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
      flushEdits(true);
      if (run::snapshot().running) run::cmdPause();
      else                         run::cmdRun();
      // The grid is the same grid either way. A restart winds the step count
      // back, and the run loop redraws in full when it sees that.
      g_paint |= PaintHeader;
      g_paint |= PaintTabs;
      g_dirty = true;
      return;
    }
    // On the editor, tapping EDIT again cycles the keyboard rather than
    // navigating nowhere. The tab says which one the tap will bring up.
    if (tabAt(i) == Tab::Edit && g_tab == Tab::Edit && !Store::unlocked()
        && onScreenKeys()) {
      g_edKb = (g_edKb + 1) % 3;
      // Both pages are three rows deep now, so the grid above does not move
      // and only the keys themselves change. The tab names the keyboard, so
      // that repaints too.
      g_paint |= PaintEdKeys;
      g_paint |= PaintTabs;
      g_dirty = true;
      return;
    }
    flushEdits(true);            // leaving the page: the machine catches up now
    // Stepping into the editor stops the run. Reading a program while a
    // runner walks over it is hard enough; changing one under a live machine
    // is worse. OUT only shows what has been printed, so it keeps running.
    if (tabAt(i) == Tab::Edit && run::snapshot().running) run::cmdPause();
    carryViewAcross(g_tab, tabAt(i));
    g_tab = tabAt(i);
    // The card is only read on entry, not on every repaint.
    if (g_tab == Tab::Prog) refreshProgFiles();
    wantAll();
  }
}

// Both pages share one magnification, so both change it the same way.
void toggleView() {
  const int next = (g_view == View::Zoom) ? (int)View::Wide : (int)View::Zoom;
  Store::setGridView(next);
  g_view = (View)next;
  g_follow = true;                  // a fresh zoom starts following again
  g_prevRunners.clear();            // stale coordinates from the other layout
  wantAll();
}

void runCellAction(int r, int c);   // defined just below, shared with the keyboard

// Play or pause from the keyboard. A program is not edited while it runs, so
// starting one from either editor goes to RUN first, where it can be watched
// -- otherwise the page you are on is one whose every control changes the
// program, and none of them would be allowed to.
void playPause() {
  flushEdits(true);
  if (run::snapshot().running) run::cmdPause();
  else {
    if (g_tab == Tab::Edit) { carryViewAcross(Tab::Edit, Tab::Run); g_tab = Tab::Run; }
    run::cmdRun();
  }
  wantAll();
}

void handleRunTouch(int x, int y) {
  if (y < kHeaderH) {
    if (!zoomOnly() && hit(btnView(), x, y)) {
      toggleView();
      return;
    }
    if (run::snapshot().step > 0 && hit(btnStart(), x, y)) {
      // The dedicated reset, not a reload: buildMachine already clears the
      // output, the timer, the runners and the step count, and this avoids
      // copying the whole program back through the mutex to say so.
      flushEdits(true);
      run::cmdReset();
      g_follow = true;
      // The grid is untouched: reset moves the runners back to the start and
      // clears the output. g_prevRunners is deliberately NOT cleared -- it is
      // the list of cells the old runners and their trails were painted over,
      // and restoring exactly those is what makes this cost a handful of
      // cells instead of the whole program.
      // Only when there is nothing on the grid but the runners. With the
      // trail turned on the old path is painted into the cells, and restoring
      // just the cells the runners stood on would leave the rest of it behind.
      g_resetSameGrid = !Store::tracePath();
      // Deliberately NOT g_dirty. The rebuild happens on the run task, so
      // painting now would draw the state we are leaving, and the version
      // watch below would then paint the state we are going to -- the grid
      // twice for one press. Let the watch do it, once, when it is true.
    }
    else if (steps() && hit(btnBack(), x, y)) { flushEdits(true); run::cmdStepBack(); g_dirty = true; }
    else if (steps() && hit(btnFwd(), x, y))  { flushEdits(true); run::cmdStep(1); }
    else if (hit(btnEnd(), x, y))   { flushEdits(true); run::cmdRunToEnd(); g_dirty = true; }
    else if (hit(btnSpeed(), x, y)) {
      int n = ((int)run::speed() + 1) % 4;
      run::setSpeed((run::Speed)n);
      Store::setRunSpeed(n);
      g_dirty = true; g_paint |= PaintHeader;
    }
    return;
  }
  if (handleEdgeBars(x, y)) return;
  if (bandOutput()) {
    // A screenful a tap, which is what the ellipsis at each end means.
    if (hit(btnOutMoreUp(), x, y))   { g_outLine += bandLines(); g_paint |= PaintBand; g_dirty = true; return; }
    if (hit(btnOutMoreDown(), x, y)) {
      g_outLine -= bandLines(); if (g_outLine < 0) g_outLine = 0;
      g_paint |= PaintBand; g_dirty = true; return;
    }
  }
  else if (bandRunners()) {
    // The same pair of buttons, driving the runner list instead. Clamping is
    // left to the drawing code, which is the only thing that knows how many
    // runners there are and how many of them fit.
    if (hit(btnOutUp(), x, y))   { if (g_runnerTop > 0) --g_runnerTop; g_paint |= PaintBand; g_dirty = true; return; }
    if (hit(btnOutDown(), x, y)) { ++g_runnerTop; g_paint |= PaintBand; g_dirty = true; return; }
  }
  int r, c;
  if (cellAt(x, y, r, c)) {
    // Zoomed out, a tap is aim rather than choice. A WIDE cell is six pixels
    // across, and picking one of eighty on a resistive panel is a lottery --
    // so the first tap zooms to what was touched and the second, on a cell
    // four times the area, is the one that means something. Applies to the
    // parameter editor and the character inspector alike, and to any zoomable
    // program, not just a packed one. A program small enough to be shown
    // large already has nothing to zoom into and is exempt.
    if (!zoomOnly() && g_view != View::Zoom) { zoomToCell(r, c); return; }

    runCellAction(r, c);
  }
}

// What choosing a cell on RUN means. Taps come here through handleRunTouch,
// and the keyboard comes here directly.
void runCellAction(int r, int c) {
    int slot = slotAtCell(r, c);
    // Straight to the parameter -- unless the program is running, in which
    // case the tap falls through to whatever GRID TAP does, and the inspector
    // shows the cell with its editing buttons off. Pause to change it.
    if (slot >= 0 && !run::snapshot().running) { openSlotEditor(slot); return; }
    // What the tap does is GRID TAP's business, and it can only be one of
    // these. The inspector is pack-agnostic already -- slotAtCell() returns
    // -1 for an unpacked program and drawCellModal() guards every line that
    // needs a pack -- so a plain program gets the same panel without the
    // parameter names.
    const int mode = Store::gridTap();
    if (mode == Store::kTapNothing) return;

    if (mode == Store::kTapStart) {
      // Tap a cell to start there, tap it again to turn the runner. The
      // chevron beside the cell says which way it will set off.
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
      markEdited();       // the run restarts from the new entry point
      return;
    }

    g_cellRow = r; g_cellCol = c;
    g_modal = Modal::Cell;
    g_dirty = true;
}

// Keep the active runner on screen. Only re-centres when it reaches the edge
// margin, so the view is not repainted on every step.
void followRunner(const run::Snapshot& snap) {
  if (!Store::followRunners()) return;      // the view stays where it was put
  if (!g_follow || snap.runnerCount == 0) return;
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
  if (g_edit.cols() > visibleCols()) {
    const int vc = visibleCols();
    if (col < g_gridCol + margin || col >= g_gridCol + vc - margin) {
      const int before = g_gridCol;
      setGridCol(col - vc / 2);
      if (g_gridCol != before) { g_paint |= PaintRunGrid; g_dirty = true; }
    }
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
      if (want != g_gridRow) { g_gridRow = want; g_paint |= PaintRunGrid; g_dirty = true; }
    }
  }
}

void handleEditTouch(int x, int y) {
  if (hit(btnEditPrev(), x, y))   { if (g_editPage > 0) { --g_editPage; g_paint |= PaintBody; g_dirty = true; } return; }
  if (hit(btnEditNext(), x, y))   { if (g_editPage == 0) { g_editPage = 1; g_paint |= PaintBody; g_dirty = true; } return; }
  if (hit(btnEditRevert(), x, y)) {
    confirm("Revert everything?",
            pack::str(pack::kStrRevertBody),
            [] { g_edit.revertAll(); clearUndo(); markEdited(); });
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
        // That row, and REVERT ALL, whose state follows the count of edits.
        g_editRow = i; g_paint |= PaintEditRow; g_dirty = true;
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
  if (plat::sdPresent() && hit(btnOutSd(), x, y)) {
    std::string path;
    run::loadedGridInto(g_ranGrid);
    if (sinks::saveRunToSd(shownOutput(), g_ranGrid, path))
      message("Saved", path);
    else
      message("SD failed", "Could not write to the card.");
    return;
  }
  if (!g_history.empty()) {
    const int n = (int)g_history.size();
    // Older: from the live run into the most recent stored one, then back.
    if (hit(btnOutOlder(), x, y)) {
      const int next = g_histView < 0 ? n - 1 : g_histView - 1;
      if (next >= 0) { g_histView = next; g_outTop = 0; g_paint |= PaintBody; g_dirty = true; }
      return;
    }
    if (hit(btnOutNewer(), x, y)) {
      if (g_histView >= 0) {
        g_histView = (g_histView + 1 >= n) ? -1 : g_histView + 1;
        g_outTop = 0; g_paint |= PaintBody; g_dirty = true;
      }
      return;
    }
  }

  // A page at a time, leaving one line of overlap so nothing is skipped over.
  const int page = g_outLines > 1 ? g_outLines - 1 : 1;
  if (hit(btnOutPgUp(), x, y)) {
    if (g_outTop > 0) { g_outTop -= page; if (g_outTop < 0) g_outTop = 0; g_paint |= PaintBody; g_dirty = true; }
    return;
  }
  if (hit(btnOutPgDn(), x, y)) {
    if (g_outTop + g_outLines < g_outTotal) { g_outTop += page; g_paint |= PaintBody; g_dirty = true; }
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
  if (hit(btnSysWifi(), x, y))       { g_modal = Modal::Wifi; wantAll(); }
  else if (hit(btnSysSd(), x, y)) {
    if (plat::sdPresent()) {
      Store::setSdLogging(!Store::sdLoggingEnabled());
      g_sysTile = SysSd; g_paint |= PaintSysTile; g_dirty = true;
    }
  }
  else if (hit(btnSysDebug(), x, y)) { g_modal = Modal::Debug; g_dialogPage = 0; wantAll(); }
  else if (hit(btnSysBand(), x, y)) {
    setBandMode((bandMode() + 1) % 3);      // output -> runners -> nothing
    g_outLine = g_runnerTop = 0;
    g_sysTile = SysBand; g_paint |= PaintSysTile; g_dirty = true;
  }
  else if (hit(btnSysTheme(), x, y)) {
    bool day = !Store::dayMode();
    Store::setDayMode(day);
    theme::setDay(day);
    g_dirty = true;
  }
  else if (Store::unlocked() && hit(btnSysExit(), x, y)) {
    confirm(pack::str(pack::kStrExitTitle),
            "The device goes back to being a plain IRCIS interpreter. "
            "Set the WiFi credentials again to return.", [] {
      relock();
    });
  }
  else if (Store::unlocked() && hit(btnSysInfo(), x, y))
                                     { g_modal = Modal::Info; g_dialogPage = 0; wantAll(); }
  else if (hit(btnSysIrcis(), x, y)) { g_modal = Modal::Ircis;  g_dialogPage = 0; wantAll(); }
  else if (plat::haveKeyboard() && hit(btnSysKeys(), x, y)) {
    Store::setHardwareKeys(!Store::hardwareKeys());
    // Turning the on-screen keyboards off is the moment the shortcuts start
    // mattering, so say what they are rather than leaving them to be found.
    if (Store::hardwareKeys()) { g_modal = Modal::Shortcuts; g_dialogPage = 0; wantAll(); }
    else { g_sysTile = SysKeys; g_paint |= PaintSysTile; g_dirty = true; }
  }
  else if (hit(btnSysTrail(), x, y)) {
    Store::setTracePath(!Store::tracePath());
    // The path is painted into the cells on RUN; that page is repainted in
    // full next time it is shown. Here only the tile changes.
    g_sysTile = SysTrail; g_paint |= PaintSysTile; g_dirty = true;
  }
  else if (hit(btnSysFollow(), x, y)) {
    Store::setFollowRunners(!Store::followRunners());
    // Switching it on means follow from here, whatever was done to the window
    // before. Without this the setting read as on and nothing followed.
    if (Store::followRunners()) g_follow = true;
    g_sysTile = SysFollow; g_paint |= PaintSysTile; g_dirty = true;
  }
  else if (hit(btnSysSteps(), x, y)) {
    Store::setStepButtons(!Store::stepButtons());
    g_sysTile = SysSteps; g_paint |= PaintSysTile; g_dirty = true;
  }
  else if (hit(btnSysRead(), x, y))  { g_modal = Modal::Device; g_dialogPage = 0; wantAll(); }
  else if (hit(btnSysLearn(), x, y)) { g_modal = Modal::Learn;  g_dialogPage = 0; wantAll(); }
  else if (hit(btnSysStart(), x, y)) {
    const int next = (Store::gridTap() + 1) % 3;   // nothing -> start -> inspector
    Store::setGridTap(next);
    if (next == Store::kTapNothing) {
      // NOTHING also means the program starts where IRCIS would start it.
      run::setStart(0, 0, 'E');
      Store::setStartPoint(0, 0, 'E');
      markEdited();
    }
    g_sysTile = SysStart; g_paint |= PaintSysTile; g_dirty = true;
  }
  else if (hit(btnSysCal(), x, y)) {
    // Checking used to mean recalibrating: the only way to find out whether
    // the calibration was any good was to throw it away and do another one,
    // and then read the number that came out. Now the tile measures first and
    // offers the recalibration afterwards, so a bad key press can be told from
    // a bad build without losing a calibration that was fine.
    const int miss = gfx.checkTouch();
    char body[200];
    const char* title;
    if (miss <= 6) {
      title = "Touch is good";
      snprintf(body, sizeof(body),
               "Within %d pixels, comfortably inside a key. Recalibrate anyway?",
               miss);
    }
    else if (miss <= 14) {
      title = "Touch is usable";
      snprintf(body, sizeof(body),
               "Out by %d pixels. A key is about 43 wide, so this works but is "
               "worth another go. Recalibrate now?", miss);
    }
    else {
      title = "Touch is out";
      snprintf(body, sizeof(body),
               "Out by %d pixels, more than half a key. Recalibrate now, and "
               "press the very centre of each corner marker.", miss);
    }
    confirm(title, body, [] {
      Store::clearTouchCalibration();
      gfx.beginTouch(true);
      // And measure the new one, so the answer to "is that better?" does not
      // need a second trip through the menu.
      const int after = gfx.checkTouch();
      char done[140];
      snprintf(done, sizeof(done),
               after <= 6 ? "Within %d pixels. Comfortably inside a key."
                          : "Out by %d pixels. Press the very centre of each "
                            "corner marker if you run it again.", after);
      message(after <= 6 ? "Calibrated" : "Calibrated, roughly", done);
    });
  }

  else if (hit(btnSysRestore(), x, y)) {
    confirm("Restore built-in programs?",
            "The programs that shipped with the device are written back to its "
            "own storage, replacing your changes to those. Programs you made "
            "yourself are left alone.",
            [] {
              int written = writeBuiltIns(true);
              refreshProgFiles(true);
              message("Restored", std::to_string(written) + " built-in programs.");
            });
  }
  else if (hit(btnSysReset(), x, y)) {
    // Plain mode has no presets or saved sets. Both readings are true: the
    // built-in programs live in flash and survive either way.
    confirm("Erase everything?",
            Store::unlocked()
              ? std::string(pack::str(pack::kStrResetBody))
              : std::string("WiFi and calibration go, and the device returns to "
                            "how it shipped -- including its programs, which "
                            "are written back over anything you changed."),
            [] {
              gfx.fillScreen(theme::bg);
              useContentFont(true);
              gfx.setTextDatum(textdatum_t::middle_center);
              gfx.setTextColor(theme::accent, theme::bg);
              gfx.drawString("Erasing...", kScreenW / 2, kScreenH / 2);
              gfx.setTextDatum(textdatum_t::top_left);
              gfx.setTextSize(1);
              Store::factoryReset();
              // factoryReset() wipes the seeded flag with everything else, so
              // the built-ins are written back on the next look at PROGRAMS.
              // Anything the user saved on the device stays where it is.
              refreshProgFiles(true);
              // factoryReset() clears the unlock too, so the packed program
              // is no longer listed -- do not leave it loaded with no way back.
              // The device should come back exactly as it ships: day palette,
              // an example loaded, and the welcome dialog it shows at power-on.
              theme::setDay(Store::dayMode());
              g_wasUnlocked = false;
              g_edit.loadProgram(prog::kOpeningExample);
              applyViewTags(g_edit.text());      // resets to 0,0 east unless the program says otherwise
              g_appliedTag = tagIn(g_edit.text());
              syncViewToProgram();
              g_curRow = g_curCol = 0;
              g_gridRow = g_gridCol = 0;
              g_outLine = 0;
              g_tab = Tab::Run;
              markEdited();
              g_dialogPage = 0;
              g_modal = Modal::Splash;
            });
  }
}

void handleWifiTouch(int x, int y) {
  if (hit(btnWifiClose(), x, y)) { g_modal = Modal::None; wantAll(); return; }
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
    if (g_modal == Modal::Wifi) wantModal();     // message() may have taken over
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
    g_rebuildPending = false;
    if (commit) commit(v);          // a commit may still send you elsewhere
    // A commit that changed the program has a repaint coming with the
    // rebuild, and that is the one showing the new value. Painting here as
    // well drew the same screen twice -- which is why OK repainted the grid
    // twice while CANCEL, changing nothing, only did it once.
    if (!g_rebuildPending) g_dirty = true;
    return;
  }
  if (hit(btnPickClear(), x, y)) {
    if (g_pickerValue != g_pickerOriginal) g_pickerValue = g_pickerOriginal;
    else                                   g_pickerValue.clear();
    wantModal();
    return;
  }
  if (hit(btnPickBack(), x, y)) {
    if (!g_pickerValue.empty()) g_pickerValue.pop_back();
    wantModal();
    return;
  }
  PickerGeom g = pickerGeom();
  if (x < g.x0 || y < g.y0) return;
  int c = (x - g.x0) / g.kw, r = (y - g.y0) / g.kh;
  if (c < 0 || c >= g.cols || r < 0 || r >= g.rows) return;

  // Ask pickerCell where each key was DRAWN and take the one that lands here,
  // rather than inverting its arithmetic by hand. The hand-written inverse
  // knew about the main block and the two side columns but not the third
  // case -- the extras in full rows underneath -- so on the text keyboard the
  // last fifteen keys typed whatever the main block would have had at those
  // coordinates: space gave '!', '(' gave '"'. That keyboard names programs
  // and presets and takes the WiFi password. Ninety-five comparisons is
  // nothing, and the two can no longer disagree.
  int i = -1;
  for (int k = 0; k < (int)g_pickerSet.size(); ++k) {
    int kr, kc;
    if (pickerCell(k, g, kr, kc) && kr == r && kc == c) { i = k; break; }
  }
  if (i < 0 || i >= (int)g_pickerSet.size()) return;
  if (g_pickerSet[i] == '\x01') return;          // spacer
  // A single-character field replaces on every press -- there is nothing to
  // clear first.
  char picked = g_pickerSet[i];
  if (g_pickerMax == 1) g_pickerValue = std::string(1, picked);
  else if (g_pickerValue.size() < g_pickerMax) g_pickerValue.push_back(picked);
  else return;
  wantModal();
}

void handleCellTouch(int x, int y) {
  // Tap a character in the window to inspect it. The window is pinned to the
  // program's edges rather than always centred on the cursor, so the mapping
  // has to go through the window origin -- assuming the cursor sits at
  // kInsMidR/kInsMidC lands up to four columns away near any edge.
  // Only the rows actually drawn. The window is centred when the program is
  // shorter than six rows, and claiming all six here put its hit box over
  // the button row underneath, so on a short program no button could be
  // pressed at all.
  const int insRowsShown = g_edit.rows() < kInsRows ? g_edit.rows() : kInsRows;
  if (y >= insTop() && y < insTop() + insRowsShown * kInsCellH &&
      x >= kInsX && x < kInsX + kInsCols * kInsCellW) {
    int r = insOriginRow() + (y - insTop()) / kInsCellH;
    int c = insOriginCol() + (x - kInsX) / kInsCellW;
    if (r >= 0 && r < g_edit.rows() && c >= 0 && c < g_edit.cols() &&
        (r != g_cellRow || c != g_cellCol)) {
      g_cellRow = r; g_cellCol = c; wantModal();
    }
    return;
  }

  // The buttons below change the program, and are off while it runs.
  if (run::snapshot().running &&
      (hit(btnCellSet(), x, y) || hit(btnCellRevert(), x, y) || hit(btnCellStart(), x, y)))
    return;
  if (hit(btnCellSet(), x, y)) {
    int r = g_cellRow, c = g_cellCol;
    std::string set = std::string(kCellBase64) + kCellSymbols;
    openPicker("EDIT CHARACTER", "DEL then OK writes a space", kKbProgram,
               std::string(1, g_edit.cell(r, c)), 1,
               [r, c](const std::string& v) {
                 // There is no space key; an emptied field is the space.
                 const char now = v.empty() ? ' ' : v[0];
                 const char was = g_edit.cell(r, c);
                 if (was == now) return;                  // no change, no edit
                 g_edit.setCell(r, c, now);
                 noteEdit(r, c, was, now);
                 markCellEdited(r, c, now);
               },
               kKbSplit);
    g_pickerBack = Modal::Cell;      // back to the inspector, not out to RUN
    return;
  }
  if (hit(btnCellRevert(), x, y)) {
    // The button is drawn disabled when the cell already matches; honour that,
    // or a tap here would raise "edits pending" without changing anything.
    char orig = g_edit.baselineCell(g_cellRow, g_cellCol);
    const char was = g_edit.cell(g_cellRow, g_cellCol);
    if (was != orig) {
      g_edit.setCell(g_cellRow, g_cellCol, orig);
      noteEdit(g_cellRow, g_cellCol, was, orig);
      markCellEdited(g_cellRow, g_cellCol, orig);
    }
    return;
  }
  if (Store::gridTap() != Store::kTapNothing && hit(btnCellStart(), x, y)) {
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
  if (hit(btnCellClose(), x, y)) { g_modal = Modal::None; wantAll(); }
}

void handleMessageTouch(int x, int y) {
  // Through the same rectangles the buttons are drawn from, so moving one
  // moves both. These used to be hand-written coordinates repeated here.
  if (g_modal == Modal::Confirm) {
    if (hit(btnMsgConfirm(), x, y)) {
      auto f = g_confirmYes;
      // Some of these take a moment -- deleting a program, restoring the
      // built-ins, wiping the store -- and nothing on the panel changed until
      // they were done, which read as a hang. Say so on the button first.
      Btn busy = btnMsgConfirm(); busy.label = "WORKING"; drawBtn(busy, true);
      g_modal = Modal::None; wantAll(); if (f) f(); return;
    }
    if (hit(btnMsgCancel(), x, y)) { g_modal = Modal::None; wantAll(); }
    return;
  }
  if (hit(btnMsgOk(), x, y)) {
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
bool g_deferTap = false;      // act on release, so a long press can be told apart
uint32_t g_pressMs = 0;
int  g_pressX = 0, g_pressY = 0;
constexpr uint32_t kLongPressMs = 600;
// How far the finger must travel from where it went down before the gesture
// counts as a drag rather than a tap. Wider than a zoom cell: a press on a
// resistive panel wanders, and a wander used to pan the grid and swallow the
// tap with it.

// A resistive panel's first reading is its worst: the pressure is still rising
// and the value can be several pixels out. On a 43 px key that is the
// difference between a character and its neighbour, which is what made the
// keyboards feel like they were typing next door. Take a handful of readings
// and use the middle of each axis, which throws away the outlier without
// waiting for anything.
//
// Whatever the panel gives is used -- one sample on a build whose touch is
// synthetic, five on the board -- so this changes nothing except accuracy.
bool readTouch(int32_t& x, int32_t& y) {
  constexpr int kSamples = 5;
  int32_t xs[kSamples], ys[kSamples];
  int n = 0;
  for (int i = 0; i < kSamples; ++i) {
    int32_t tx, ty;
    if (gfx.getTouch(&tx, &ty)) { xs[n] = tx; ys[n] = ty; ++n; }
  }
  if (n == 0) return false;
  std::sort(xs, xs + n);
  std::sort(ys, ys + n);
  x = xs[n / 2];
  y = ys[n / 2];
  return true;
}

void onTap(int x, int y) {
  // The paged dialogs stop 8 px above the tab bar, so a tap down there is not
  // aimed at anything of theirs. It plainly means "take me to that tab", and
  // having to find CLOSE first was busywork. The dialogs that ask a question
  // keep their hold, because their own buttons sit in that strip and throwing
  // an unanswered question away on a stray tap would be worse.
  if (y >= kTabY && (g_modal == Modal::Info   || g_modal == Modal::Ircis ||
                     g_modal == Modal::Device || g_modal == Modal::Debug ||
                     g_modal == Modal::Shortcuts || g_modal == Modal::Learn)) {
    g_modal = Modal::None;
    g_dialogPage = 0;
    handleTabs(x, y);
    if (g_tab == Tab::Out) g_outputUnseen = false;
    g_dirty = true;
    return;
  }
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
    case Modal::Shortcuts:
    case Modal::Ircis:
    case Modal::Learn:
    case Modal::Device: {
      const int pages = dialogPageCount();
      if (hit(btnDlgClose(), x, y)) { g_modal = Modal::None; wantAll(); }
      else if (g_modal == Modal::Debug && hit(btnDlgDump(), x, y)) dumpGrid();
      else if (hit(btnDlgPrev(), x, y) && g_dialogPage > 0) { --g_dialogPage; wantModal(); }
      else if (hit(btnDlgNext(), x, y) && g_dialogPage + 1 < pages) { ++g_dialogPage; wantModal(); }
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
    case Tab::Save: handleSaveTouch(x, y); break;
    case Tab::Prog: handleProgTouch(x, y); break;
    case Tab::Sys:  handleSysTouch(x, y); break;
    default: break;
  }
}

// Holds the machine while the screen is drawn. Only at the two speeds meant
// for watching: at QUICK and FULL the runners are moving faster than the eye
// follows anyway, and holding would just make the run take longer.
struct PaintHold {
  bool held = false;
  explicit PaintHold(const run::Snapshot& snap) {
    const run::Speed sp = run::speed();
    if (snap.running && (sp == run::Speed::Slow || sp == run::Speed::Medium)) {
      held = true;
      run::hold(true);
    }
  }
  ~PaintHold() { if (held) run::hold(false); }
};

void drawFocusRing();          // defined with the rest of the keyboard handling
void repaintAll();

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
    case Tab::Save: drawSave(); break;
    case Tab::Prog: drawProg(); break;
    case Tab::Sys:  drawSys(); break;
    default: break;
  }
}

void drawAll(const run::Snapshot& snap) {
  // A modal paints over the tab bar, and so does the frame after it closes,
  // so what drawTabs() believes is on the panel stops being true either way.
  // A page change does not: the header, the body and the bar each clear their
  // own area, so the bar is still exactly where it was.
  static Modal lastModal = Modal::None;
  if (g_modal != lastModal) { g_tabsStale = true; lastModal = g_modal; }
  if (g_modal == Modal::Picker) { drawPicker(); return; }
  if (g_modal == Modal::Cell)   { drawCellModal(); drawFocusRing(); return; }
  if (g_modal == Modal::Debug)  { drawDebug(); return; }
  if (g_modal == Modal::Info)   { drawInfo(); return; }
  if (g_modal == Modal::Wifi)   { drawWifi(); drawFocusRing(); return; }
  if (g_modal == Modal::Splash) {
    // The header and the tab bar, because the splash is also raised on
    // unlocking and after a reset, when both have just changed and would
    // otherwise show the state they were in before. NOT the body: the panel
    // covers everything but four pixels of it, and drawing the program
    // underneath is what made opening a packed program look like the grid
    // loading in twice -- once invisibly under this, once when it closed.
    gfx.fillScreen(theme::bg);
    drawHeader(snap);
    // The fill has just wiped the tab bar, so every tab has to be painted
    // again whatever its signature says. Without this a repaint while the
    // splash was up left the bar blank, which is also why the test that
    // captures this screen only failed sometimes.
    g_tabsStale = true;
    drawTabs();
    drawSplash();
    return;
  }
  if (g_modal == Modal::Size)   { drawSize();   drawFocusRing(); return; }
  if (g_modal == Modal::Ircis)  { drawIrcis();  return; }
  if (g_modal == Modal::Shortcuts) { drawShortcuts(); return; }
  if (g_modal == Modal::Device) { drawDevice(); return; }
  if (g_modal == Modal::Learn)  { drawLearn();  return; }
  // No fillScreen. drawHeader clears the status bar, every page's drawer
  // clears the body, and drawTabs paints each tab it touches -- so clearing
  // the whole panel first only wiped the tab bar and forced all six tabs to
  // be redrawn for a change that affects two of them.
  drawHeader(snap);
  drawBody(snap);
  drawTabs();
  drawFocusRing();
  if (g_modal == Modal::Confirm || g_modal == Modal::Message) { drawMessage(); drawFocusRing(); }
}

// How many rows PROG is actually showing, counted the same way the drawer
// counts them, so the keyboard cannot pick a row that is not there.
int progRowsShown() {
  std::vector<ProgRow> actions, files;
  buildProgActions(actions);
  buildProgRows(files);
  const int fixed = (int)actions.size();
  const int rest  = (int)files.size() - g_progTop;
  const int n     = fixed + (rest > 0 ? rest : 0);
  const int cap   = progSlots(fixed);
  return n < cap ? n : cap;
}

// True while the keyboard belongs to the program being written, which is the
// one place a letter has to mean itself rather than a command.
inline bool typingIntoGrid() { return g_tab == Tab::Edit && !Store::unlocked(); }

// Everything on the page a tap does something to, in the order the eye reads
// it. The keyboard walks this list and Space presses what it lands on, which
// is handed to the same handler a finger would have reached.
constexpr int kMaxFocus = 24;
int focusList(Btn* out) {
  int n = 0;
  auto add = [&](Btn b) { if (n < kMaxFocus) out[n++] = b; };
  // The modals with buttons are pages in their own right, so they get a ring
  // too. The picker is the exception: it takes typing, and its keys are the
  // keyboard's already.
  if (g_modal == Modal::Cell) {
    add(btnCellSet()); add(btnCellRevert());
    if (Store::gridTap() != Store::kTapNothing) add(btnCellStart());
    add(btnCellClose());
    return n;
  }
  if (g_modal == Modal::Size) {
    add(btnSzRowsDn()); add(btnSzRowsUp());
    add(btnSzColsDn()); add(btnSzColsUp());
    add(btnSzCancel()); add(btnSzOk());
    return n;
  }
  if (g_modal == Modal::Wifi) {
    add(btnWifiSsid()); add(btnWifiPass()); add(btnWifiConn());
    add(btnWifiWeb());  add(btnWifiClose());
    return n;
  }
  if (g_modal == Modal::Confirm) { add(btnMsgCancel()); add(btnMsgConfirm()); return n; }
  if (g_modal == Modal::Message) { add(btnMsgOk()); return n; }
  switch (g_tab) {
    case Tab::Run:
      break;                       // letters cover it; the arrows scroll
    case Tab::Out:
      if (plat::sdPresent()) add(btnOutSd());
      add(btnOutColour());
      add(btnOutOlder()); add(btnOutNewer());
      add(btnOutPgUp());  add(btnOutPgDn());
      break;
    case Tab::Prog: {
      std::vector<ProgRow> actions, files;
      buildProgActions(actions);
      buildProgRows(files);
      const int fixed = (int)actions.size();
      const int n = progRowsShown();
      for (int i = 0; i < n; ++i)
        add(i < fixed ? progActionTile(i) : progFileTile(i, fixed));
      break;
    }
    case Tab::Sys:
      return sysTiles(out);
    case Tab::Edit:
      if (Store::unlocked()) {
        add(btnEditPrev()); add(btnEditNext()); add(btnEditRevert());
      }
      break;
    case Tab::Keys:
      for (int kind = 0; kind < setKinds(); ++kind) {
        add(btnSetAdd(kind));
        for (int i = 0; i < setEntryCount(kind); ++i) add(btnSetRow(kind, i));
      }
      break;
    case Tab::Save:
      for (int i = 0; i < Store::kMaxPresets; ++i) {
        add(btnSaveSlot(i)); add(btnSaveWrite(i)); add(btnSaveDel(i));
      }
      break;
    default: break;
  }
  return n;
}

// Repaint everything, and mean it. The partial-paint flags are set from
// several places in a tick, and any one of them left standing turns a full
// repaint into a partial one that leaves the keyboard's ring undrawn.
void repaintAll() {
  g_paint = PaintAll;
  g_dirty = true;
  g_edCellCount = 0;
}

// Keep the keyboard's cell inside the window, moving the window if it has to.
void showRunCell() {
  if (g_runCellRow < 0) return;
  if (g_view == View::Zoom) { zoomToCell(g_runCellRow, g_runCellCol); return; }
  if (g_runCellRow < g_gridRow) g_gridRow = g_runCellRow;
  else if (g_runCellRow >= g_gridRow + gridRows()) g_gridRow = g_runCellRow - gridRows() + 1;
  if (g_runCellCol < g_gridCol) g_gridCol = g_runCellCol;
  else if (g_runCellCol >= g_gridCol + visibleCols()) g_gridCol = g_runCellCol - visibleCols() + 1;
  if (g_gridRow < 0) g_gridRow = 0;
  setGridCol(g_gridCol);
}

// Where the keyboard is, if it is being used. Drawn over the top of whatever
// page is up rather than by each page's own drawer.
void drawFocusRing() {
  // On RUN the keyboard is on a cell rather than a button, and without
  // something drawn round it the arrows look as though they do nothing.
  if (g_modal == Modal::None && g_tab == Tab::Run && g_runCellRow >= 0) {
    int x, y;
    if (cellPos(g_runCellRow, g_runCellCol, x, y)) {
      const int w = (g_view == View::Zoom) ? kZoomCellW : kWideCellW;
      const int h = (g_view == View::Zoom) ? kZoomCellH : kWideCellH;
      gfx.drawRect(x - 1, y - 1, w + 2, h + 2, theme::accent);
      gfx.drawRect(x - 2, y - 2, w + 4, h + 4, theme::accent);
    }
  }
  if (g_focus < 0) return;
  Btn list[kMaxFocus];
  const int n = focusList(list);
  if (g_focus >= n) return;
  const Btn& b = list[g_focus];
  gfx.drawRect(b.x - 2, b.y - 2, b.w + 4, b.h + 4, theme::accent);
  gfx.drawRect(b.x - 1, b.y - 1, b.w + 2, b.h + 2, theme::accent);
}

// Hand the focused control to the handler that owns its page.
void pressFocused() {
  Btn list[kMaxFocus];
  const int n = focusList(list);
  if (g_focus < 0 || g_focus >= n) return;
  const Btn& b = list[g_focus];
  const int x = b.x + b.w / 2, y = b.y + b.h / 2;
  if (g_modal == Modal::Cell)    { handleCellTouch(x, y); return; }
  if (g_modal == Modal::Size)    { handleSizeTouch(x, y); return; }
  if (g_modal == Modal::Wifi)    { handleWifiTouch(x, y); return; }
  if (g_modal == Modal::Confirm ||
      g_modal == Modal::Message) { handleMessageTouch(x, y); return; }
  switch (g_tab) {
    case Tab::Run:  handleRunTouch(x, y); break;
    case Tab::Out:  handleOutTouch(x, y); break;
    case Tab::Prog: handleProgTouch(x, y); break;
    case Tab::Sys:  handleSysTouch(x, y); break;
    case Tab::Edit: if (Store::unlocked()) handleEditTouch(x, y); break;
    case Tab::Keys: handleKeysTouch(x, y); break;
    case Tab::Save: handleSaveTouch(x, y); break;
    default: break;
  }
}

// Move the ring. Left and right step through the list; up and down look for
// the nearest control on the next line that has one, so a line holding a
// single control does not trap the focus.
void moveFocus(char k) {
  Btn list[kMaxFocus];
  const int n = focusList(list);
  if (n == 0) return;
  if (g_focus < 0) { g_focus = 0; repaintAll(); return; }
  if (g_focus >= n) g_focus = n - 1;
  if (k == plat::kKeyLeft)  { if (g_focus > 0)     --g_focus; }
  else if (k == plat::kKeyRight) { if (g_focus < n - 1) ++g_focus; }
  else {
    const Btn cur = list[g_focus];
    const bool up = (k == plat::kKeyUp);
    int best = -1, bestDx = 0;
    for (int i = 0; i < n; ++i) {
      if (up ? list[i].y >= cur.y : list[i].y <= cur.y) continue;
      if (best >= 0 && (up ? list[i].y < list[best].y : list[i].y > list[best].y)) continue;
      const int dx = std::abs(list[i].x - cur.x);
      if (best < 0 || list[i].y != list[best].y || dx < bestDx) { best = i; bestDx = dx; }
    }
    // On PROG the list scrolls, so running off the end moves the window
    // instead of stopping.
    if (best < 0 && g_tab == Tab::Prog) {
      std::vector<ProgRow> actions, files;
      buildProgActions(actions);
      buildProgRows(files);
      const int maxTop = (int)files.size() - (progVisible() - (int)actions.size());
      if (!up && g_progTop < maxTop) ++g_progTop;
      else if (up && g_progTop > 0)  --g_progTop;
    }
    if (best >= 0) g_focus = best;
  }
  repaintAll();
}

// Keyboard control. The emulator is driven from the keys as a desktop program
// would be: Tab between pages, Esc to close, the usual chords, and the arrows
// moving a ring around the controls of whatever page is up. Single letters do
// the transport, which is safe everywhere except in the program editor, where
// a letter has to mean itself.
void pollTypedKeys() {
  for (char k = plat::pollKey(); k; k = plat::pollKey()) {
    // --- anywhere ---
    if (k == plat::kKeyHelp) {
      g_modal = Modal::Shortcuts; g_dialogPage = 0; wantAll(); continue;
    }
    // c closes, but only the dialogs that are pages of text. In a field that
    // takes typing it is a letter, and in a question it is not an answer.
    const bool pagedDialog = g_modal == Modal::Info || g_modal == Modal::Ircis
                          || g_modal == Modal::Device || g_modal == Modal::Debug
                          || g_modal == Modal::Shortcuts || g_modal == Modal::Learn;
    if (k == plat::kKeyEsc || (k == 'c' && pagedDialog)) {
      g_focus = -1;                        // the ring belongs to what is closing
      if (pagedDialog)                     { g_modal = Modal::None; g_dialogPage = 0; wantAll(); }
      else if (g_modal == Modal::Picker)   { handlePickerTouch(btnPickCancel().x + 4, btnPickCancel().y + 4); }
      else if (g_modal == Modal::Confirm)  { handleMessageTouch(btnMsgCancel().x + 4, btnMsgCancel().y + 4); }
      else if (g_modal == Modal::Message)  { handleMessageTouch(btnMsgOk().x + 4, btnMsgOk().y + 4); }
      else if (g_modal == Modal::Size)     { handleSizeTouch(btnSzCancel().x + 4, btnSzCancel().y + 4); }
      else if (g_modal != Modal::None)     { g_modal = Modal::None; g_dialogPage = 0; wantAll(); }
      else if (g_focus >= 0)               { g_focus = -1; g_dirty = true; }
      continue;
    }

    // --- a field that takes typing: the keyboard is the keyboard ---
    if (g_modal == Modal::Picker) {
      if (k == plat::kKeyPaste) {
        for (char c : plat::clipboard()) {
          if (c < 0x20 || c >= 0x7f) continue;
          if (g_pickerSet.find(c) == std::string::npos) continue;
          if (g_pickerMax == 1) { g_pickerValue = std::string(1, c); break; }
          if (g_pickerValue.size() >= g_pickerMax) break;
          g_pickerValue.push_back(c);
        }
        wantModal();
        continue;
      }
      if (k == '\r') { handlePickerTouch(btnPickOk().x + 4, btnPickOk().y + 4); continue; }
      if (k == '\b') {
        if (!g_pickerValue.empty()) { g_pickerValue.pop_back(); wantModal(); }
        continue;
      }
      // Only characters this field actually accepts, which is the same set the
      // on-screen keyboard offers for it.
      if (k >= 0x20 && k < 0x7f && g_pickerSet.find(k) != std::string::npos) {
        if (g_pickerMax == 1) { g_pickerValue = std::string(1, k); wantModal(); }
        else if (g_pickerValue.size() < g_pickerMax) { g_pickerValue.push_back(k); wantModal(); }
      }
      continue;
    }
    if ((g_modal == Modal::Confirm || g_modal == Modal::Message) && k == '\r') {
      handleMessageTouch(g_modal == Modal::Confirm ? btnMsgConfirm().x + 4 : btnMsgOk().x + 4,
                         g_modal == Modal::Confirm ? btnMsgConfirm().y + 4 : btnMsgOk().y + 4);
      continue;
    }
    if (k == plat::kKeyTab || k == plat::kKeyBack) {
      if (g_modal != Modal::None) { g_modal = Modal::None; g_dialogPage = 0; }
      const int n = tabCount();
      int i = tabSlot(g_tab) + (k == plat::kKeyTab ? 1 : n - 1);
      flushEdits(true);
      carryViewAcross(g_tab, tabAt(((i % n) + n) % n));
      g_tab = tabAt(((i % n) + n) % n);
      g_focus = -1;
      g_runCellRow = -1;
      if (g_tab == Tab::Prog) refreshProgFiles();
      if (g_tab == Tab::Out)  g_outputUnseen = false;
      g_dirty = true;
      continue;
    }
    if (k == plat::kKeyRun) { playPause(); continue; }

    // --- the inspector: the arrows move the cell, a letter presses a button ---
    if (g_modal == Modal::Cell) {
      int r = g_cellRow, c = g_cellCol;
      if      (k == plat::kKeyUp)    --r;
      else if (k == plat::kKeyDown)  ++r;
      else if (k == plat::kKeyLeft)  --c;
      else if (k == plat::kKeyRight) ++c;
      if (r != g_cellRow || c != g_cellCol) {
        if (r >= 0 && r < g_edit.rows() && c >= 0 && c < g_edit.cols()) {
          g_cellRow = r; g_cellCol = c; wantModal();
        }
        continue;
      }
      // Pressed through the same handler as a tap on the button, so what
      // is allowed while a program runs is decided in one place.
      auto press = [](Btn b) { handleCellTouch(b.x + b.w / 2, b.y + b.h / 2); };
      if      (k == 'e') press(btnCellSet());
      else if (k == 'r') press(btnCellRevert());
      else if (k == 's') press(btnCellStart());
      else if (k == 'c' || k == plat::kKeyEsc || k == plat::kKeyBack) press(btnCellClose());
      else if (k == ' ' || k == '\r') pressFocused();
      continue;
    }
    // --- a dialog with controls: the arrows move its ring ---
    if (g_modal == Modal::Size ||
        g_modal == Modal::Wifi || g_modal == Modal::Confirm) {
      if (k == plat::kKeyUp || k == plat::kKeyDown ||
          k == plat::kKeyLeft || k == plat::kKeyRight) { moveFocus(k); continue; }
      if (k == ' ' || k == '\r') { pressFocused(); continue; }
      continue;
    }
    if (g_modal != Modal::None) {
      if (k == 'd' && g_modal == Modal::Debug) { dumpGrid(); continue; }
      const int pages = dialogPageCount();
      if      (k == plat::kKeyLeft  && g_dialogPage > 0)         { --g_dialogPage; g_dirty = true; }
      else if (k == plat::kKeyRight && g_dialogPage + 1 < pages) { ++g_dialogPage; g_dirty = true; }
      continue;
    }

    // Chords, so the editor can reach these too -- there a bare letter is part
    // of the program being written.
    // A program copied out of the guide, or out of anywhere else, pasted
    // straight in. The grid takes the shape of what arrives.
    if (k == plat::kKeyPaste) {
      const std::string text = plat::clipboard();
      if (text.empty()) message("Nothing to paste", "The clipboard is empty.");
      else if (!loadProgramText(text, "Pasted"))
        message("Cannot paste that",
                "It has to be lines of characters, no wider than 96 and no more "
                "than 32 of them.");
      else { g_tab = Tab::Run; g_dirty = true; }
      continue;
    }
    if (k == plat::kKeyName) { openRenameDialog(); continue; }
    if (k == plat::kKeyZoom) {
      if (typingIntoGrid()) toggleView();
      else {
        const int next = (g_view == View::Zoom) ? (int)View::Wide : (int)View::Zoom;
        Store::setGridView(next); g_view = (View)next;
        g_follow = true; g_prevRunners.clear();
      }
      g_dirty = true; continue;
    }
    if (k == plat::kKeySave) { saveCurrentProgram(); continue; }
    if (k == plat::kKeyUndo) { if (canUndo()) doUndo(); continue; }
    if (k == plat::kKeyRedo) { if (canRedo()) doRedo(); continue; }

    // --- the program editor keeps its letters for the program ---
    if (typingIntoGrid()) {
      switch (k) {
        case plat::kKeyUp:    g_edManualScroll = false;
                              if (g_curRow > 0) { touchEdCell(g_curRow, g_curCol); --g_curRow; touchEdCell(g_curRow, g_curCol); } break;
        case plat::kKeyDown:  g_edManualScroll = false;
                              if (g_curRow < g_edit.rows() - 1) { touchEdCell(g_curRow, g_curCol); ++g_curRow; touchEdCell(g_curRow, g_curCol); } break;
        case plat::kKeyLeft:  g_edManualScroll = false;
                              if (g_curCol > 0) { touchEdCell(g_curRow, g_curCol); --g_curCol; touchEdCell(g_curRow, g_curCol); } break;
        case plat::kKeyRight: g_edManualScroll = false;
                              if (g_curCol < g_edit.cols() - 1) { touchEdCell(g_curRow, g_curCol); ++g_curCol; touchEdCell(g_curRow, g_curCol); } break;
        case '\b':
          if (g_curCol > 0) { touchEdCell(g_curRow, g_curCol); --g_curCol; }
          typeIntoGrid('.');
          if (g_curCol > 0) --g_curCol;         // typeIntoGrid advanced past it
          touchEdCell(g_curRow, g_curCol);
          continue;
        case '\r': continue;
        default:
          if (k >= 0x20 && k < 0x7f) { typeIntoGrid(k); continue; }
          continue;
      }
      g_paint |= PaintEdHead; g_dirty = true;
      continue;
    }

    // --- everywhere else a letter is a command ---
    switch (k) {
      case 'p': playPause(); continue;
      case 'f': flushEdits(true); run::cmdStep(1); continue;
      case 'b': flushEdits(true); run::cmdStepBack(); g_dirty = true; continue;
      case 'r':
        // What the |< button does: back to the top, output cleared. Not a
        // reload, so the grid is untouched and g_prevRunners still describes
        // the cells the old runners were painted over.
        flushEdits(true);
        run::cmdReset();
        g_follow = true;
        g_resetSameGrid = !Store::tracePath();   // the trail has to go with it
        continue;
      case 'e': flushEdits(true); run::cmdRunToEnd(); g_dirty = true; continue;
      case 's': {
        const int n = ((int)run::speed() + 1) % 4;
        run::setSpeed((run::Speed)n);
        Store::setRunSpeed(n);
        g_dirty = true; g_paint |= PaintHeader;
        continue;
      }
      case 'z': {
        const int next = (g_view == View::Zoom) ? (int)View::Wide : (int)View::Zoom;
        Store::setGridView(next);
        g_view = (View)next;
        g_follow = true;
        g_prevRunners.clear();
        g_dirty = true;
        continue;
      }
      case 'n': openRenameDialog(); continue;
      case 'x':
        // Delete the program the ring is on, which only PROG has.
        if (g_tab == Tab::Prog && g_focus >= 0) {
          Btn list[kMaxFocus];
          if (g_focus < focusList(list)) {
            const Btn& b = list[g_focus];
            handleProgTouch(8, b.y + b.h / 2);   // the X sits at the row's left
          }
        }
        continue;
      default: break;
    }

    // On RUN, when a tap on a cell means something, the arrows move between
    // cells and Space does whatever GRID TAP is set to.
    if (g_tab == Tab::Run && Store::gridTap() != Store::kTapNothing &&
        (k == plat::kKeyUp || k == plat::kKeyDown ||
         k == plat::kKeyLeft || k == plat::kKeyRight)) {
      if (g_runCellRow < 0) { g_runCellRow = run::startRow(); g_runCellCol = run::startCol(); }
      else if (k == plat::kKeyUp   && g_runCellRow > 0) --g_runCellRow;
      else if (k == plat::kKeyDown && g_runCellRow < g_edit.rows() - 1) ++g_runCellRow;
      else if (k == plat::kKeyLeft && g_runCellCol > 0) --g_runCellCol;
      else if (k == plat::kKeyRight && g_runCellCol < g_edit.cols() - 1) ++g_runCellCol;
      g_follow = false;             // the keyboard is driving, not the runner
      showRunCell();
      repaintAll();
      continue;
    }
    if (g_tab == Tab::Run && g_runCellRow >= 0 && (k == ' ' || k == '\r')) {
      runCellAction(g_runCellRow, g_runCellCol);
      // Whatever that opened, start its ring on the first control rather than
      // wherever the page behind happened to leave it.
      if (g_modal != Modal::None) g_focus = 0;
      continue;
    }
    // On RUN the arrows scroll what is on screen, the way they would in
    // anything else that shows more than it can fit.
    if (g_tab == Tab::Run &&
        (k == plat::kKeyUp || k == plat::kKeyDown ||
         k == plat::kKeyLeft || k == plat::kKeyRight)) {
      if (k == plat::kKeyUp) {
        if (maxGridRow() > 0 && g_gridRow > 0) { --g_gridRow; g_paint |= PaintRunGrid; g_dirty = true; }
        else if (bandOutput()) { ++g_outLine; g_paint |= PaintBand; g_dirty = true; }
      }
      else if (k == plat::kKeyDown) {
        if (maxGridRow() > 0 && g_gridRow < maxGridRow()) { ++g_gridRow; g_paint |= PaintRunGrid; g_dirty = true; }
        else if (bandOutput() && g_outLine > 0) { --g_outLine; g_paint |= PaintBand; g_dirty = true; }
      }
      else if (k == plat::kKeyLeft) {
        if (g_gridCol > 0) { --g_gridCol; g_paint |= PaintRunGrid; g_dirty = true; }
      }
      else {
        if (g_gridCol < maxGridCol()) { ++g_gridCol; g_paint |= PaintRunGrid; g_dirty = true; }
      }
      continue;
    }
    if (k == plat::kKeyUp || k == plat::kKeyDown ||
        k == plat::kKeyLeft || k == plat::kKeyRight) { moveFocus(k); continue; }
    if (k == ' ' || k == '\r') {
      const Modal was = g_modal;
      pressFocused();
      if (g_modal != was && g_modal != Modal::None) g_focus = 0;
      continue;
    }
  }
}


} // namespace

prog::Program& editGrid() { return g_edit; }
// Every edit is pushed into the interpreter immediately and the run restarts.
// There is no "pending" state to forget about -- what is on screen is always
// what will run.
void markEdited() {
  g_rebuildPending = true;
  g_editReloadDue  = true;
  g_editDirtyAt    = plat::millis();
}

void applyEditsNow() { flushEdits(true); }

void markCellEdited(int row, int col, char ch) {
  g_rebuildPending = true;
  run::setCell(row, col, ch);        // three bytes across the queue, not three KB
  g_editRebuildDue = true;
  g_editDirtyAt    = plat::millis();
}

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
// The console's drag commands, kept so a scene written before the edge bars
// still scrolls. A drag of dx pixels is the columns it would have crossed.
void injectDrag(int dx) {
  if (g_view != View::Zoom || g_edit.cols() <= kZoomCols) return;
  setGridCol(g_gridCol - dx / cellW());
  g_follow = false;
  g_dirty = true;
}

void injectDragV(int dy) {
  const int max = maxGridRow();
  if (max <= 0) return;
  int r = g_gridRow - dy / cellH();
  g_gridRow = r < 0 ? 0 : (r > max ? max : r);
  g_follow = false;
  g_dirty = true;
}
void showMessage(const std::string& title, const std::string& body) { message(title, body); }
void notifyUnlocked() { announceUnlock(); }
#if defined(SK_HOST)
unsigned long gridPaints() { return g_gridPaints; }
unsigned long fullPaints() { return g_fullPaints; }
unsigned long bandPaints() { return g_bandPaints; }
#else
unsigned long gridPaints() { return 0; }
unsigned long bandPaints() { return 0; }
#endif
bool loadProgramTextPublic(const std::string& t, const char* name) { return loadProgramText(t, name); }
bool applyProgramTextPublic(const std::string& t) { return applyProgramText(t); }
void lockDevice() { relock(); }

void begin() {
  theme::setDay(Store::dayMode());   // before a single pixel is drawn

  // Copy the built-in programs into the device's own store at start-up rather
  // than the first time PROG is opened. Everything else that reads programs --
  // the web view's list, the console's progload, a program loaded straight
  // into the machine -- would otherwise find an empty store on a device whose
  // owner had not happened to visit that tab yet.
  refreshProgFiles(true);
  int sc, sr; char sd;
  Store::startPoint(sc, sr, sd);
  // A stored entry point only applies while it is allowed to move; otherwise
  // the program starts where IRCIS would start it, whatever was saved.
  if (Store::gridTap() == Store::kTapNothing) { sc = 0; sr = 0; sd = 'E'; }
  run::setStart(sc, sr, sd);
  run::setSpeed((run::Speed)Store::runSpeed());
  // Locked, the packed program is not in the list, so open on an example.
  g_edit = prog::Program();
  if (!Store::unlocked()) {
    g_edit.loadProgram(prog::kOpeningExample);
    // The opening program's tag, the way PROG would apply it. Without this
    // the device came up with the program's speed but not its start cell.
    applyViewTags(g_edit.text());
    g_appliedTag = tagIn(g_edit.text());
  }
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

  // The welcome is a welcome: it belongs on the first power-on, not on every
  // one. ABOUT THIS DEVICE says the same things afterwards, and a factory
  // reset clears the flag with everything else, so a device handed on shows
  // it again.
  if (plat::kv::getBool("welcomed", false)) {
    g_modal = Modal::None;
  }
  else {
    plat::kv::putBool("welcomed", true);
    g_modal = Modal::Splash;
  }
  g_dirty = true;
}

void tick() {
  int32_t tx, ty;
  bool touched = readTouch(tx, ty);
  uint32_t now = plat::millis();

  // A pause in the typing is what makes the deferred rebuild land.
  flushEdits();
  pollTypedKeys();

  if (touched && !g_wasTouched) {
    g_pressX = tx; g_pressY = ty;
    g_pressMs = now;
    // The SETS list waits for release, so holding an entry can offer to
    // delete it instead of selecting it. Nothing else does now that the
    // program is scrolled by its edge bars rather than by dragging.
    bool setsList = (g_modal == Modal::None && g_tab == Tab::Keys &&
                     ty >= kBodyY && ty < kTabY);
    g_deferTap = setsList;
    if (!g_deferTap && now - g_lastTouchMs > 120) {
      g_lastTouchMs = now;
      onTap(tx, ty);
    }
  }
  else if (!touched && g_wasTouched && g_deferTap) {
    if (now - g_lastTouchMs > 120) {
      g_lastTouchMs = now;
      // Press coordinates, not release: resistive touch gets noisy as the
      // pressure drops.
      if (now - g_pressMs >= kLongPressMs) handleKeysLongPress(g_pressX, g_pressY);
      else                                 onTap(g_pressX, g_pressY);
    }
    g_deferTap = false;
  }
  g_wasTouched = touched;

  static uint32_t lastDraw = 0;
  static uint32_t lastStep = 0xFFFFFFFF;
  static bool lastRunning = false;
  static bool lastFinished = false;
  static uint32_t lastRunVersion = 0xFFFFFFFF;
  run::Snapshot snap = run::snapshot();
  if (snap.running != lastRunning) {
    lastRunning = snap.running;
    if (g_modal == Modal::Cell) g_paint |= PaintModal;   // its buttons follow the run
    // Starting or stopping changes the transport and the RUN tab's glyph and
    // nothing else. Repainting everything for it meant one tap on play drew
    // the grid twice -- once for the tap, once when the machine actually
    // started a moment later -- and on a large grid the second one is long
    // enough to miss the opening steps of the run.
    g_paint |= PaintHeader;
    g_paint |= PaintTabs;
    g_dirty = true;
  }
  if (snap.finished && !lastFinished) {
    lastFinished = true;
    // Whitespace is not output. Programs like race are watched on the grid and
    // print nothing; several still emit a stray space, and jumping to a page
    // showing one space is worse than staying where the program is.
    const std::string& out = run::output();
    const bool printed = out.find_first_not_of(" \t\r\n") != std::string::npos;
    if (printed) {
      // Only runs that printed something: a watcher finishing in silence
      // would otherwise push a run you might want back out of the list.
      run::loadedGridInto(g_ranGrid);
      recordRun(snap, out, g_ranGrid.programName());
      g_histView = -1;             // a new run is what you are looking at
      // Stay where you are. Being thrown onto another page the moment a run
      // ends takes the grid away at exactly the moment you were watching it;
      // the OUT tab wears a dot instead, and the RUN tab's glyph becomes the
      // go-again arrow, which between them say the same thing without moving
      // anything.
      g_outTop = 0;
      if (g_tab != Tab::Out) g_outputUnseen = true;
      else if (g_modal == Modal::None) {
        // The counts in the header settle and the history arrows appear. The
        // lines themselves have not changed, and are left alone.
        WriteBatch batch;
        drawOutHeaderOnly();
        drawOutBody(false);
      }
      g_paint |= PaintHeader;
      g_paint |= PaintTabs;
      g_paint |= PaintBand;      // the readout still says "press play"
      g_dirty = true;
    }
  }
  else if (!snap.finished) lastFinished = false;

  // A rebuild -- a program loaded, an edit re-run, a reset -- happens on the
  // run task. Painting when the request goes out draws the new grid with the
  // OLD machine's runners on it, and the rebuild then paints it again: two
  // frames for one change, the first of them wrong. So the request paths
  // leave g_dirty alone and this is the single paint, whichever page is up.
  const uint32_t bv = run::buildVersion();
  if (bv != lastRunVersion) {
    lastRunVersion = bv;
    if (g_resetSameGrid && g_tab == Tab::Run && g_modal == Modal::None) {
      // Same characters, different runners: put back the cells the old ones
      // were drawn over and leave the grid alone.
      g_resetSameGrid = false;
      WriteBatch batch;
      drawRunners(snap);
      drawHeader(snap);      g_headerSig = headerSignature(snap);
      drawRunnerList(snap);  g_bandSig   = bandSignature(snap);
      // Resetting a running program stops it, so the RUN tab has to come off
      // the pause bars. This path repainted the grid and the header and left
      // the tab bar alone, so a reset mid-run left it saying pause for a run
      // that was no longer going. Only the tab that changed is redrawn.
      drawTabs();
      g_dirty = false; g_paint &= ~PaintHeader; g_paint &= ~PaintTabs;
      g_paint &= ~PaintBody; g_paint &= ~PaintBand;
      g_paint &= ~PaintEdGrid; g_paint &= ~PaintEdHead;
      g_lastRunPaintMs = now;
      lastStep = snap.step;
      return;
    }
    g_resetSameGrid = false;
    g_prevRunners.clear();
    // Only RUN and OUT show anything the machine owns. The editor draws from
    // the grid in memory, which was already right the instant the edit was
    // made, so a rebuild there changes nothing on screen -- and repainting
    // for it would put a full frame behind every keystroke.
    if (g_modal != Modal::None) {
      // A change made from inside a dialog -- the inspector's REVERT or
      // START -- rebuilds the machine. The dialog is still up, and it is the
      // dialog that has to say the new state, not the page underneath it.
      wantModal();
    }
    else if (g_tab == Tab::Run || g_tab == Tab::Out) {
      g_paint &= ~(PaintHeader | PaintBody | PaintBand);
      g_paint &= ~(PaintEdGrid | PaintEdHead | PaintTabs);
      wantAll();                    // one full repaint, through the path below
    }
  }

  if (g_dirty) {
    WriteBatch batch;
    // The partial repaints compose: a tap that changed the cursor and the
    // readout asks for the grid and the header, and gets exactly those. Only
    // a request that names no part at all falls through to the whole screen.
    // Whatever else was asked for, arriving on a different page means the
    // page has to be drawn. Without this, a run finishing while the
    // running-state change had asked for header-and-tabs left the OUT page
    // showing nothing but its tab bar.
    static Tab lastDrawnTab = Tab::Run;
    static bool everDrawn = false;
    static Modal lastDrawnModal = Modal::None;
    const bool tabChanged = !everDrawn || g_tab != lastDrawnTab;
    const bool anyPart = g_modal == Modal::None && !tabChanged &&
                         !(g_paint & PaintAll) && (g_paint != 0 || g_edCellCount > 0);
    // A dialog that is already on the panel and asked only for its own part.
    // One that has just opened, or changed into another, is drawn whole.
    const bool modalPart = g_modal != Modal::None && g_modal == lastDrawnModal &&
                           (g_paint & PaintModal) && !(g_paint & PaintAll);
    if (g_modal == Modal::None) { lastDrawnTab = g_tab; everDrawn = true; }
    lastDrawnModal = g_modal;
    if (modalPart) {
      // A boxed dialog leaves the page's header showing above it, so that
      // still follows the run; the full-screen ones cover it.
      const bool boxed = g_modal != Modal::Picker && g_modal != Modal::Cell;
      if (boxed && (g_paint & PaintHeader)) { drawHeader(snap); g_headerSig = headerSignature(snap); }
      switch (g_modal) {
        case Modal::Picker: drawPickerTop(); drawPickerClear(); break;
        case Modal::Cell:   drawCellModal(false); break;
        case Modal::Wifi:   drawWifi(false); drawFocusRing(); break;
        case Modal::Debug: case Modal::Info: case Modal::Shortcuts:
        case Modal::Ircis: case Modal::Device:
          g_dlgPageOnly = true; drawAll(snap); g_dlgPageOnly = false; break;
        default: drawAll(snap); break;
      }
      // The tab bar is under the dialog; the full repaint drops this request
      // for the same reason, and drawing it here painted it over the buttons.
      g_paint &= ~PaintTabs;
    }
    else if (!anyPart) {
#if defined(SK_HOST)
      ++g_fullPaints;
#endif
      drawAll(snap);
      g_bandSig = 0; g_headerSig = 0; g_paint &= ~PaintTabs;
    }
    else {
      // Header-only leaves the grid alone: it is the same grid, and redrawing
      // it is the whole cost.
      if ((g_paint & PaintHeader))   { drawHeader(snap); g_headerSig = headerSignature(snap); }
      if ((g_paint & PaintBand) && g_tab == Tab::Run) {
        drawRunnerList(snap); g_bandSig = bandSignature(snap);
      }
      if ((g_paint & PaintBody))     { drawBody(snap); g_bandSig = 0; }
      if ((g_paint & PaintRunGrid) && g_tab == Tab::Run) {
        // The program and the arrows under it. The readout below them says
        // what has been printed, which scrolling does not change.
        const int top = (g_view == View::Zoom) ? kHeaderH : kWideY;
        gfx.fillRect(0, top, kScreenW, shiftRowY() - top, theme::bg);
        g_prevRunners.clear();          // cleared along with the cells
        drawGrid();
        drawRunners(snap);
        drawEdgeBars();          // last: the runners are drawn over the top
      }
      if (((g_paint & PaintEdGrid) || g_edCellCount) &&
          g_tab == Tab::Edit && !Store::unlocked()) {
        // If following the cursor scrolled the window, every cell moved and
        // the queued handful means nothing; otherwise repaint just those.
        const int r0 = g_gridRow, c0 = g_gridCol;
        edFollow();
        if ((g_paint & PaintEdGrid) || g_gridRow != r0 || g_gridCol != c0) { drawProgEditGrid(); drawEdgeBars(); }
        else {
          for (int i = 0; i < g_edCellCount; ++i)
            drawEdCell(g_edCells[i].row, g_edCells[i].col);
          drawEdgeBars();          // a cell under a bar was just painted over it
        }
      }
      if ((g_paint & PaintEdHead) && g_tab == Tab::Edit && !Store::unlocked())
        drawEdHeadBits();
      if ((g_paint & PaintEdKeys) && g_tab == Tab::Edit && !Store::unlocked())
        drawProgEditKeys();
      if ((g_paint & PaintSysTile) && g_tab == Tab::Sys && g_sysTile >= 0)
        drawSysTile(g_sysTile);
      if ((g_paint & PaintEditRow) && g_tab == Tab::Edit && Store::unlocked() && g_editRow >= 0) {
        drawEditRow(g_editRow);
        drawBtn(btnEditRevert(), false, g_edit.modifiedCells() > 0);
      }
    }
    if ((g_paint & PaintTabs)) drawTabs();
    // Only a frame that drew the runners has caught up with the machine. The
    // play tap asks for the header and the tab bar; by the time that frame
    // lands the run task has taken its first step, and recording that step
    // here as drawn meant the runner was next painted at step two -- the
    // first cell of every run was never shown.
    const bool drewRunners = !anyPart || (g_paint & (PaintBody | PaintRunGrid));
    g_dirty = false;
    g_paint = 0;
    g_edCellCount = 0;
    lastDraw = now;
    if (drewRunners) { g_lastRunPaintMs = now; lastStep = snap.step; }
    g_outVersion = run::outputVersion();
    return;
  }

  if (now - lastDraw < 33) return;
  lastDraw = now;

  // Every modal used to stop the clock here. That is right for all of them
  // but one: DIAGNOSTICS exists to show heap, steps and steps/sec moving, and
  // it was showing whatever they happened to be when it opened. It gets a
  // repaint on a slower cadence of its own; the rest still cost nothing.
  if (g_modal != Modal::None) {
    if (g_modal == Modal::Debug) {
      static uint32_t lastDiag = 0;
      static uint32_t lastSig  = 0;
      if ((uint32_t)(now - lastDiag) >= 500) {
        lastDiag = now;
        // Only when something on it has actually moved. Twice a second on a
        // stopped program was a panel-wide repaint with nothing to show for
        // it, which on the emulator reads as a flash.
        const uint32_t sig = (uint32_t)plat::freeHeap() ^ (snap.step * 2654435761u)
                           ^ (uint32_t)(snap.elapsedMs / 1000) ^ (uint32_t)snap.runnersCreated
                           ^ (uint32_t)snap.oobReads ^ (uint32_t)snap.ubReads
                           ^ (uint32_t)snap.deathNoteTotal;
        if (sig != lastSig) { lastSig = sig; WriteBatch batch; drawDebug(); }
      }
    }
    return;
  }

  if (g_tab == Tab::Run) {
    // A rebuild -- reset, restart, an edit re-running -- happens on the run
    // task, so the repaint the button asked for can land before the machine
    // has actually gone back to the top, leaving the old runners drawn. The
    // step count alone cannot catch that: pressing reset twice, or on a run
    // that had barely started, leaves it unchanged. buildMachine bumps this
    // version every time, so watch that instead.
    if (snap.step != lastStep) {
      // Everything below draws while the machine is going. Hold it for the
      // duration at the watching speeds, so the runners are where the picture
      // says they are when it lands.
      PaintHold holdWhileDrawing(snap);
      WriteBatch batch;
      followRunner(snap);
      if (g_dirty) {
        drawAll(snap); g_dirty = false; g_paint &= ~PaintHeader;
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
        drawEdgeBars();       // runners near an edge paint over them
        // The band is under the program in both views, so it refreshes in
        // both -- but only when it would actually look different.
        {
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
    if (v != g_outVersion) {
      g_outVersion = v;
      WriteBatch batch;
      drawOutHeaderOnly();
      drawOutBody(false);
      drawFocusRing();
    }
  }
  else if (g_tab == Tab::Keys) {
    // The keys appear partway through the run; refresh while it is moving.
    if (snap.step != lastStep) {
      // Only when a set has changed state; otherwise this was a full clear
      // and redraw of the page on every step, which on the panel is a flicker.
      if (keysSignature() != g_keysSig) { WriteBatch batch; drawKeys(); }
      lastStep = snap.step;
    }
  }
}

} // namespace ui
