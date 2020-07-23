/**************************************************************************/
/*                                                                        */
/*                              WWIV Version 5.x                          */
/*                   Copyright (C)2020, WWIV Software Services            */
/*                                                                        */
/*    Licensed  under the  Apache License, Version  2.0 (the "License");  */
/*    you may not use this  file  except in compliance with the License.  */
/*    You may obtain a copy of the License at                             */
/*                                                                        */
/*                http://www.apache.org/licenses/LICENSE-2.0              */
/*                                                                        */
/*    Unless  required  by  applicable  law  or agreed to  in  writing,   */
/*    software  distributed  under  the  License  is  distributed on an   */
/*    "AS IS"  BASIS, WITHOUT  WARRANTIES  OR  CONDITIONS OF ANY  KIND,   */
/*    either  express  or implied.  See  the  License for  the specific   */
/*    language governing permissions and limitations under the License.   */
/**************************************************************************/
#include "bbs/fsed.h"

#include "bbs/bbs.h"
#include "bbs/bgetch.h"
#include "bbs/full_screen.h"
#include "bbs/message_editor_data.h"
#include "bbs/output.h"
#include "bbs/pause.h"
#include "bbs/quote.h"
#include "core/stl.h"
#include "core/strings.h"
#include "core/textfile.h"
#include "fmt/format.h"
#include "local_io/keycodes.h"

namespace wwiv::bbs::fsed {

using namespace wwiv::stl;
using namespace wwiv::strings;

/////////////////////
// LOCALS

std::vector<line_t> read_file(const std::filesystem::path& path, int line_length) {
  TextFile f(path, "rt");
  if (!f) {
    return {};
  }
  auto lines = f.ReadFileIntoVector();

  std::vector<line_t> out;
  for (auto l : lines) {
    do {
      const auto size_wc = size_without_colors(l);
      if (size_wc <= line_length) {
        line_t lt{};
        lt.wrapped = false;
        lt.text = l;
        out.emplace_back(lt);
        break;
      }
      // We have a long line
      auto pos = line_length;
      while (pos > 0 && l[pos] > 32) {
        pos--;
      }
      if (pos == 0) {
        pos = line_length;
      }
      auto subset_of_l = l.substr(0, pos);
      l = l.substr(pos + 1);
      line_t lt{};
      lt.wrapped = true;
      lt.text = l;
      out.emplace_back(lt);
    } while (true);
  }
  return out;
}

/////////////////////////////////////////////////////////////////////////////
// LINE

line_add_result_t line_t::add(int x, char c, ins_ovr_mode_t mode) {
  while (ssize(text) < x) {
    text.push_back(' ');
  }
  if (x == ssize(text)) {
    text.push_back(c);
    return line_add_result_t::no_redraw;
  } else if (mode == ins_ovr_mode_t::ins) {
    wwiv::stl::insert_at(text, x, c);
    return line_add_result_t::needs_redraw;
  } else {
    text[x] = c;
    return line_add_result_t::no_redraw;
  }
}

line_add_result_t line_t::del(int x, ins_ovr_mode_t) {
  if (x < 0) {
    return line_add_result_t::error;
  }
  auto result = (x == ssize(text)) ? line_add_result_t::no_redraw : line_add_result_t::needs_redraw;
  if (!wwiv::stl::erase_at(text, x)) {
    return line_add_result_t ::error;
  }
  return result;
}

line_add_result_t line_t::bs(int x, ins_ovr_mode_t mode) { 
  if (x <= 0) {
    return line_add_result_t::error;
  }
  return del(x - 1, mode);
}

int line_t::size() const { 
  return ssize(text);
}

/////////////////////////////////////////////////////////////////////////////
// EDITOR

line_t& editor_t::curline() {
  // TODO: insert return statement here
  while (curli >= ssize(lines)) {
    lines.emplace_back();
  }
  return lines.at(curli);
} 

bool editor_t::insert_line() { 
  if (ssize(lines)  >= this->maxli) {
    return false;
  }
  return wwiv::stl::insert_at(lines, curli, line_t{});
}

bool editor_t::remove_line() { 
  if (lines.empty()) {
    return false;
  }
  return wwiv::stl::erase_at(lines, curli);
}

editor_add_result_t editor_t::add(char c) { 
  auto& line = curline(); 
  auto line_result = line.add(cx, c, mode_);
  if (line_result == line_add_result_t::error) {
    return editor_add_result_t::error;
  }
  auto start_line = curli;
  ++cx;
  if (cx < max_line_len) {
    // no  wrapping is needed
    if (line_result == line_add_result_t::needs_redraw) {
      invalidate_range(start_line, curli);
    }
    return editor_add_result_t::added;
  }
  int last_space = line.text.find_last_of(" \t");
  line.wrapped = true;
  if (last_space != -1 && (max_line_len - last_space) < (max_line_len / 2)) {
    // Word Wrap
    auto nline = line.text.substr(last_space);
    line.text = line.text.substr(0, last_space);
    ++curli;
    curline().text = nline;
    cx = ssize(curline().text);
  } else {
    // Character wrap    
    ++curli;
    cx %= max_line_len;
  }
  // Create the new line.
  curline();
  invalidate_range(start_line, curli);
  return editor_add_result_t::wrapped;
}

char editor_t::current_char() {
  const auto& line = curline();
  return (cx >= wwiv::stl::ssize(line.text)) ? ' ' : line.text.at(cx);
}

bool editor_t::del() { 
  auto r = curline().del(cx, mode_);
  if (r == line_add_result_t::error) {
    return false;
  }
  if (r == line_add_result_t::needs_redraw) {
    invalidate_to_eof(curli);
  }
  return true;
}

bool editor_t::bs() { 
  auto r = curline().bs(cx, mode_);
  if (r == line_add_result_t::error) {
    return false;
  }
  if (r == line_add_result_t::needs_redraw) {
    invalidate_to_eof(curli);
  }
  return true;
}

bool editor_t::add_callback(editor_range_invalidated_fn fn) {
  callbacks_.emplace_back(fn);
  return true;
}
void editor_t::invalidate_to_eol() {
  editor_range_t r{};
  r.start.line = r.end.line = curli;
  r.start.x = cx;
  r.end.x = ssize(curline().text);
  for (auto& c : callbacks_) {
    c(*this, r);
  }
}

void editor_t::invalidate_to_eof() {
  invalidate_to_eof(curli);
}

void editor_t::invalidate_to_eof(int start_line) {
  invalidate_range(start_line, ssize(lines));
}

void editor_t::invalidate_range(int start_line, int end_line) {
  editor_range_t r{};
  r.start.line = start_line;
  r.start.x = cx;
  r.end.line = end_line;
  for (auto& c : callbacks_) {
    c(*this, r);
  }
}

std::vector<std::string> editor_t::to_lines() { 
  std::vector<std::string> out;
  for (auto l : lines) {
    wwiv::strings::StringTrimCRLF(&l.text);
    if (l.wrapped) {
      // wwiv line wrapping character.
      l.text.push_back('\x1');
    }
    out.emplace_back(l.text);
  }

  return out;
}


/////////////////////////////////////////////////////////////////////////////
// MAIN

static FsedView create_frame(MessageEditorData& data, bool file) {
  const auto screen_width = a()->user()->GetScreenChars();
  const auto screen_length = a()->user()->GetScreenLines() - 1;
  const auto num_header_lines = 4;
  auto fs = FullScreenView(bout, num_header_lines, screen_width, screen_length);
  auto view = FsedView(fs, data, file);
  view.redraw();
  return view;
}

static void gotoxy(const editor_t& ed, const FullScreenView& fs) { 
  bout.GotoXY(ed.cx + 1, ed.cy + fs.lines_start());
}

static void advance_cy(editor_t& ed, FsedView& view, bool invalidate = true) {
  // advancy cy if we have room, scroll region otherwise
  if (ed.cy < view.max_view_lines()) {
    ++ed.cy;
  } else {
    // scroll
    view.top_line = ed.curli - ed.cy;
    if (invalidate) {
      ed.invalidate_to_eof(view.top_line);
    }
  }
}

static int last_space_before(line_t& line, int maxlen) {
  if (line.size() < maxlen) {
    return line.size();
  }
  auto& text = line.text;
  if (text.empty()) {
    return 0;
  }
  for (int i = ssize(text) - 1; i > 0; i--) {
    char c = text.at(i);
    if (c == '\t' || c == ' ') {
      return i;
    }
  }
  return 0;
}

bool fsed(const std::filesystem::path& path) {
  MessageEditorData data{};
  data.title = path.string();
  editor_t ed{};
  auto file_lines = read_file(path, ed.maxli);
  if (!file_lines.empty()) {
    ed.lines = std::move(file_lines);
  }

  int anon = 0;
  auto save = fsed(ed, data, &anon, true);

  bout.cls();
  bout << "Text:" << wwiv::endl;
  for (const auto& l : ed.to_lines()) {
    bout << l << wwiv::endl;
  }
  if (!save) {
    return false;
  }

  TextFile f(path, "wt");
  if (!f) {
    return false;
  }
  for (const auto& l : ed.to_lines()) {
    f.WriteLine(l);
  }
  return true;
}

bool fsed(std::vector<std::string>& lin, int maxli, int* setanon, MessageEditorData& data,
  bool file) {
  editor_t ed{};
  ed.maxli = maxli;
  // TODO anon
  for (auto l : lin) {
    bool wrapped = !l.empty() && l.back() == '\x1';
    ed.lines.emplace_back(line_t{ wrapped, l });
  }
  if (!fsed(ed, data, setanon, file)) {
    return false;
  }

  lin = ed.to_lines();
  return true;
}


bool fsed(editor_t& ed, MessageEditorData& data, int* setanon, bool file) {
  auto view = create_frame(data, file);
  auto& fs = view.fs();
  ed.add_callback([&view](editor_t& e, editor_range_t t) {
    view.handle_editor_invalidate(e, t);
    });

  int saved_topdata = a()->topdata;
  if (a()->topdata != LocalIO::topdataNone) {
    a()->topdata = LocalIO::topdataNone;
    a()->UpdateTopScreen();
  }

  // Draw the initial contents of the file.
  ed.invalidate_to_eof(0);
  // Draw the bottom bar once to start with.
  bout.Color(0);
  view.draw_bottom_bar(ed);
  fs.GotoContentAreaTop();
  bool done = false;
  bool save = false;
  // top editor line number in thw viewable area.
  while (!done) {
    gotoxy(ed, fs);

    auto key = view.bgetch(ed);
    switch (key) {
    case COMMAND_UP: {
      if (ed.cy > 0) {
        --ed.cy;
        --ed.curli;
      }
      else if (ed.curli > 0) {
        // scroll
        --ed.curli;
        view.top_line = ed.curli;
        ed.invalidate_to_eof(view.top_line);
      }
    } break;
    case COMMAND_DOWN: {
      if (ed.curli < ssize(ed.lines) - 1) {
        ++ed.curli;
        advance_cy(ed, view);
      }
    } break;
    case COMMAND_PAGEUP: {
      const auto up = std::min<int>(ed.curli, view.max_view_lines());
      // nothing to do!
      if (up == 0) {
        break;
      }
      ed.cy = std::max<int>(ed.cy - up, 0);
      ed.curli = std::max<int>(ed.curli - up, 0);
      view.top_line = ed.curli - ed.cy;
      ed.invalidate_to_eof(view.top_line);
    } break;
    case COMMAND_PAGEDN: {
      const auto dn =
          std::min<int>(view.max_view_lines(), std::max<int>(0, ssize(ed.lines) - ed.curli - 1));
      if (dn == 0) {
        // nothing to do!
        break;
      }
      ed.curli += dn;
      ed.cy += dn;
      if (ed.cy >= view.max_view_lines()) {
        // will need to scroll
        ed.cy = view.max_view_lines();
        view.top_line = ed.curli - ed.cy;
        ed.invalidate_to_eof(view.top_line);
      }
    } break;
    case COMMAND_LEFT: {
      if (ed.cx > 0) {
        --ed.cx;
      }
    } break;
    case COMMAND_RIGHT: {
      // TODO: add option to only cursor right to EOL
      const auto right_max = view.max_view_columns();
      if (ed.cx < right_max) {
        ++ed.cx;
      }
    } break;
    case CA:
    case COMMAND_HOME: {
      ed.cx = 0;
    } break;
    case CD: {
      if (ed.remove_line()) {
        ed.invalidate_to_eof(ed.curli);
      }
    } break;
    case CE:
    case COMMAND_END: {
      ed.cx = ed.curline().size();
    } break;
    case CK: {
      if (ed.cx < ed.curline().size()) {
        auto& oline = ed.curline();
        oline.text = oline.text.substr(0, ed.cx);
        oline.wrapped = false;
        ed.invalidate_to_eol();
      } else if (ed.curline().size() == 0) {
        // delete line
        if (ed.remove_line()) {
          ed.invalidate_to_eof(ed.curli);
        }
      }
    } break;
    case CL: { // redraw
      ed.invalidate_to_eof(0);
    } break;
    case COMMAND_DELETE: {
      // TODO keep mode state;
      ed.del();
      gotoxy(ed, fs);
      ed.invalidate_to_eol();
    } break;
    case BACKSPACE: {
      // TODO keep mode state;
      ed.bs();
      if (ed.cx > 0) {
        --ed.cx;
      } else if (ed.curli > 0 && ed.curline().size() == 0) {
        // If current line is empty then delete it and move up one line
        if (ed.remove_line()) {
          --ed.cy;
          --ed.curli;
          ed.cx = ed.curline().size();
          ed.invalidate_to_eof(ed.curli);
        }
      } else if (ed.curli > 0) {
        auto& prev = ed.lines.at(ed.curli - 1);
        auto& cur = ed.curline();
        auto last_pos = std::max<int>(0, ed.max_line_len - prev.size() - 1);
        const int new_cx = prev.size();
        if (cur.size() < last_pos) {
          prev.text.append(cur.text);
          ed.remove_line();
        } else if (int space = last_space_before(cur, last_pos) > 0) {
          prev.text.append(cur.text.substr(0, space));
          cur.text = cur.text.substr(space);
        }
        --ed.cy;
        --ed.curli;
        ed.cx = new_cx;
        ed.invalidate_to_eof(ed.curli);
      }
      gotoxy(ed, fs);
      bout.bputch(ed.current_char());
    } break;
    case RETURN: {
      int orig_start_line = ed.curli;
      ed.curline().wrapped = false;
      // Insert inserts after the current line
      if (ed.cx >= ed.curline().size()) {
        ++ed.curli;
        ed.insert_line();
      } else {
        // Wrap
        auto& oline = ed.curline();
        auto ntext = oline.text.substr(ed.cx);
        oline.text = oline.text.substr(0, ed.cx);
        oline.wrapped = false;

        ++ed.curli;
        ed.insert_line();
        ed.curline().text = ntext;
      }
      advance_cy(ed, view);
      ed.cx = 0;
      ed.invalidate_to_eof(orig_start_line);
    } break;
    case ESC: {
      bout.PutsXY(1, fs.command_line_y(), "|#9(|#2ESC|#9=Return, |#2A|#9=Abort, |#2Q|#9=Quote, |#2S|#9=Save, |#2?|#9=Help{not impl}): ");
      switch (fs.bgetch()) { 
      case 's':
        [[fallthrough]];
      case 'S': {
        done = true;
        save = true;
      } break;
      case 'a':
        [[fallthrough]];
      case 'A': {
        done = true;
      } break;
      case 'q':
      case 'Q': {
        // Hacky quote solution for now.
        // TODO(rushfan): Do something less lame here.
        bout.cls();
        auto quoted_lines = query_quote_lines();
        if (quoted_lines.empty()) {
          break;
        }
        while (!quoted_lines.empty()) {
          // Insert all quote lines.
          const auto ql = quoted_lines.front();
          quoted_lines.pop_front();
          ++ed.curli;
          ed.insert_line();
          ed.curline().text = ql;
          advance_cy(ed, view, false);
        }
        // Add blank line afterwards to use to start new text.
        ++ed.curli;
        ed.insert_line();
        advance_cy(ed, view, false);

        // Redraw everything, the whole enchilada!
        view.redraw();
        ed.invalidate_to_eof(0);
      } break;
      case ESC:
        [[fallthrough]];
      default: {
        fs.ClearCommandLine();
      } break;
      }
    } break;
    case CI: {
      ed.toggle_ins_ovr_mode();
    } break;
    default: {
      if (key < 0xff && key >= 32) {
        const auto c = static_cast<char>(key & 0xff);
        gotoxy(ed, fs);
        bout.bputch(c);
        if (ed.add(c) == editor_add_result_t::wrapped) {
          advance_cy(ed, view);
        }
      }
    } break;
    } // switch

  }

  a()->topdata = saved_topdata;
  a()->UpdateTopScreen();

  return save;
}

FsedView::FsedView(FullScreenView fs, MessageEditorData& data, bool file)
    : fs_(std::move(fs)), data_(data), file_(file) {
  max_view_lines_ = std::min<int>(20, fs.message_height() - 1);
  max_view_columns_ = std::min<int>(fs.screen_width(), 79);
}

FullScreenView& FsedView::fs() { return fs_; }

void FsedView::gotoxy(const editor_t& ed) {
  bout.GotoXY(ed.cx + 1, ed.cy - top_line + fs_.lines_start());
}

void FsedView::handle_editor_invalidate(editor_t& e, editor_range_t t) {
  // TODO: optimize for first line

  // Never go below top line.
  auto start_line = std::max<int>(t.start.line, top_line);

  for (int i = start_line; i <= t.end.line; i++) {
    auto y = i - top_line + fs_.lines_start();
    if (y > fs_.lines_end() || i >= ssize(e.lines)) {
      // clear the current and then remaining
      bout.GotoXY(0, y);
      bout.clreol();
      for (auto z = t.end.line + 1 - top_line + fs_.lines_start(); z < fs_.lines_end(); z++) {
        bout.GotoXY(0, z);
        bout.clreol();
      }
      break;
    }
    bout.GotoXY(0, y);
    auto& rl = e.lines.at(i);
    bout.bputs(rl.text);
    bout.clreol();
  }
  gotoxy(e);
}

void FsedView::redraw() {
  auto oldcuratr = bout.curatr();
  bout.cls();
  auto to = data_.to_name.empty() ? "All" : data_.to_name;
  bout << "|#7From: |#2" << data_.from_name << wwiv::endl;
  bout << "|#7To:   |#2" << to << wwiv::endl;
  bout << "|#7Area: |#2" << data_.sub_name << wwiv::endl;
  bout << "|#7" << (file_ ? "File: " : "Subj: ") << "|#2" << data_.title << wwiv::endl;

  bout.SystemColor(oldcuratr);
  fs_.DrawTopBar();
  fs_.DrawBottomBar("");
}

void FsedView::draw_bottom_bar(editor_t& ed) {
  auto sc = bout.curatr();
  static int sx = -1, sy = -1, sl = -1;
  if (sx != ed.cx || sy != ed.cy || sl != ed.curli) {
    const auto mode = ed.mode() == ins_ovr_mode_t::ins ? "INS" : "OVR";
    fs_.DrawBottomBar(fmt::format("X:{} Y:{} L:{} T:{} C:{} [{}]", ed.cx, ed.cy, ed.curli, top_line,
                                  static_cast<int>(ed.current_char()), mode));
    sx = ed.cx;
    sy = ed.cy;
    sl = ed.curli;
  }
  bout.SystemColor(sc);
  gotoxy(ed);
}

int FsedView::bgetch(editor_t& ed) {
  return bgetch_event(numlock_status_t::NUMBERS, std::chrono::seconds(1), [&](bgetch_timeout_status_t status, int s) {
    if (status == bgetch_timeout_status_t::WARNING) {
      fs_.PrintTimeoutWarning(s);
    } else if (status == bgetch_timeout_status_t::CLEAR) {
      fs_.ClearCommandLine();
    } else if (status == bgetch_timeout_status_t::IDLE) {
      draw_bottom_bar(ed);
    }
  });
}

} // namespace wwiv::bbs::fsed