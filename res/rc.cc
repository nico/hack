/*
clang++ -std=c++11 -o rc rc.cc -Wall -Wno-c++11-narrowing
./rc < foo.rc

A sketch of a reimplemenation of rc.exe, for research purposes.
Doesn't do any preprocessing for now.

Missing for chromium:
- DIALOG(EX)
- VERSIONINFO
- LANGUAGE
- #pragma code_page() and unicode handling
- case-insensitive keywords
- string ids in addition to int ids
- custom types (both int and string)
  - includes TEXTINCLUDE, TYPELIB, EULA, REGISTRY,
    GOOGLEUPDATEAPPLICATIONCOMMANDS
- inline block data for RCDATA, DLGINCLUDE, HTML, custom types
- real string and int literal parsers (L"\0", 0xff)
- int expression parse/eval (+ - | & ~) for DIALOGEX (DIALOG?) MENU
  VERSIONINFO and maybe more
- preprocessor
- (chrome uses DESIGNINFO but only behind `#ifdef APSTUDIO_INVOKED` which is
  only set by MSVC not rc, and rc.exe doesn't understand DESIGNINFO)

Also missing, but not yet for chromium:
- FONT
- MENUITEM SEPARATOR
- MENUEX
- MESSAGETABLE
*/

#include <experimental/string_view>
#include <iostream>
#include <map>
#include <unordered_map>
#include <vector>

//////////////////////////////////////////////////////////////////////////////
// Lexer

struct Token {
 public:
  enum Type {
    kInvalid,
    kInt,          // 123
    kString,       // "foo"
    kIdentifier,   // foo
    kComma,        // ,
    kStartBlock,   // { or BEGIN (rc.exe accepts `{ .. END`)
    kEndBlock,     // } or END
    kDirective,    // #foo
    kLineComment,  // //foo
    kStarComment,  // /* foo */
  };

  Token(Type type, std::experimental::string_view value)
      : type_(type), value_(value) {}

  Type type() const { return type_; }

  Type type_;
  std::experimental::string_view value_;
};

class Tokenizer {
 public:
  static std::vector<Token> Tokenize(std::experimental::string_view source,
                                     std::string* err);

  static bool IsDigit(char c);
  static bool IsIdentifierFirstChar(char c);
  static bool IsIdentifierContinuingChar(char c);

 private:
  Tokenizer(std::experimental::string_view input) : input_(input), cur_(0) {}
  std::vector<Token> Run(std::string* err);

  void Advance() { ++cur_; }  // Must only be called if not at_end().
  void AdvanceToNextToken();
  bool IsCurrentWhitespace() const;  // Must only be called if not at_end().
  Token::Type ClassifyCurrent() const;
  void AdvanceToEndOfToken(Token::Type type);

  bool IsCurrentStringTerminator(char quote_char) const;
  bool IsCurrentNewline() const;

  bool done() const { return at_end() || has_error(); }

  bool at_end() const { return cur_ == input_.size(); }
  char cur_char() const { return input_[cur_]; }

  bool has_error() const { return !err_.empty(); }

  std::vector<Token> tokens_;
  const std::experimental::string_view input_;
  std::string err_;
  size_t cur_;  // Byte offset into input buffer.
};

// static
std::vector<Token> Tokenizer::Tokenize(std::experimental::string_view source,
                                       std::string* err) {
  Tokenizer t(source);
  return t.Run(err);
}

std::vector<Token> Tokenizer::Run(std::string* err) {
  while (!done()) {
    AdvanceToNextToken();
    if (done())
      break;
    Token::Type type = ClassifyCurrent();
    if (type == Token::kInvalid) {
      err_ = "invalid token around " + input_.substr(cur_, 20).to_string();
      break;
    }
    size_t token_begin = cur_;
    AdvanceToEndOfToken(type);
    if (has_error())
      break;
    size_t token_end = cur_;

    std::experimental::string_view token_value(&input_.data()[token_begin],
                                               token_end - token_begin);
    if (type == Token::kIdentifier) {
      if (token_value == "BEGIN")
        type = Token::kStartBlock;
      else if (token_value == "END")
        type = Token::kEndBlock;
    }
    if (type != Token::kLineComment && type != Token::kStarComment)
      tokens_.push_back(Token(type, token_value));
  }
  if (has_error()) {
    tokens_.clear();
    *err = err_;
  }
  return tokens_;
}

// static
bool Tokenizer::IsDigit(char c) {
  return c >= '0' && c <= '9';
}

// static
bool Tokenizer::IsIdentifierFirstChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

// static
bool Tokenizer::IsIdentifierContinuingChar(char c) {
  // Also allow digits after the first char.
  return IsIdentifierFirstChar(c) || IsDigit(c);
}

bool Tokenizer::IsCurrentStringTerminator(char quote_char) const {
  if (cur_char() != quote_char)
    return false;

  // Check for escaping. \" is not a string terminator, but \\" is. Count
  // the number of preceeding backslashes.
  int num_backslashes = 0;
  for (int i = static_cast<int>(cur_) - 1; i >= 0 && input_[i] == '\\'; i--)
    num_backslashes++;

  // Even backslashes mean that they were escaping each other and don't count
  // as escaping this quote.
  return (num_backslashes % 2) == 0;
}

bool Tokenizer::IsCurrentNewline() const {
  return cur_char() == '\n';
}

void Tokenizer::AdvanceToNextToken() {
  while (!at_end() && IsCurrentWhitespace())
    Advance();
}

Token::Type Tokenizer::ClassifyCurrent() const {
  char next_char = cur_char();
  if (IsDigit(next_char))
    return Token::kInt;
  if (next_char == '"')
    return Token::kString;

  if (IsIdentifierFirstChar(next_char))
    return Token::kIdentifier;

  if (next_char == ',')
    return Token::kComma;
  if (next_char == '{')
    return Token::kStartBlock;
  if (next_char == '}')
    return Token::kEndBlock;

  if (next_char == '#')
    return Token::kDirective;
  if (next_char == '/' && cur_ + 1 < input_.size()) {
    // FIXME: probably better to not make tokens for those at all
    if (input_[cur_ + 1] == '/')
      return Token::kLineComment;
    if (input_[cur_ + 1] == '*')
      return Token::kStarComment;
  }

  return Token::kInvalid;
}

void Tokenizer::AdvanceToEndOfToken(Token::Type type) {
  switch (type) {
    case Token::kInt:
      do {
        Advance();
      } while (!at_end() && IsDigit(cur_char()));
      break;

    case Token::kString: {
      char initial = cur_char();
      Advance();  // Advance past initial "
      for (;;) {
        if (at_end()) {
          err_ = "Unterminated string literal.";
          break;
        }
        if (IsCurrentStringTerminator(initial)) {
          Advance();  // Skip past last "
          break;
        } else if (IsCurrentNewline()) {
          err_ = "Newline in string constant.";
        }
        Advance();
      }
      break;
    }

    case Token::kIdentifier:
      while (!at_end() && IsIdentifierContinuingChar(cur_char()))
        Advance();
      break;

    case Token::kComma:
    case Token::kStartBlock:
    case Token::kEndBlock:
      Advance();  // All are one char.
      break;

    case Token::kDirective:
    case Token::kLineComment:
      // Eat to EOL.
      while (!at_end() && !IsCurrentNewline())
        Advance();
      break;

    case Token::kStarComment:
      // Eat to */.
      while (!at_end()) {
        bool is_star = cur_char() == '*';
        Advance();
        if (is_star && !at_end() && cur_char() == '/') {
          Advance();
          break;
        }
      }
      break;

    case Token::kInvalid:
      err_ = "Everything is all messed up";
      return;
  }
}

bool Tokenizer::IsCurrentWhitespace() const {
  char c = input_[cur_];
  // Note that tab (0x09), vertical tab (0x0B), and formfeed (0x0C) are illegal.
  return c == '\n' || c == '\r' || c == ' ';
}

//////////////////////////////////////////////////////////////////////////////
// AST

class CursorResource;
class BitmapResource;
class IconResource;
class MenuResource;
class StringtableResource;
class AcceleratorsResource;
class RcdataResource;
class VersioninfoResource;
class DlgincludeResource;
class HtmlResource;

class Visitor {
 public:
  virtual bool VisitCursorResource(const CursorResource* r) = 0;
  virtual bool VisitBitmapResource(const BitmapResource* r) = 0;
  virtual bool VisitIconResource(const IconResource* r) = 0;
  virtual bool VisitMenuResource(const MenuResource* r) = 0;
  virtual bool VisitStringtableResource(const StringtableResource* r) = 0;
  virtual bool VisitAcceleratorsResource(const AcceleratorsResource* r) = 0;
  virtual bool VisitRcdataResource(const RcdataResource* r) = 0;
  virtual bool VisitVersioninfoResource(const VersioninfoResource* r) = 0;
  virtual bool VisitDlgincludeResource(const DlgincludeResource* r) = 0;
  virtual bool VisitHtmlResource(const HtmlResource* r) = 0;

 protected:
  ~Visitor() {}
};

// Root of AST class hierarchy.
class Resource {
 public:
  // XXX make a ResName struct that's either int or string16
  Resource(uint16_t name) : name_(name) {}
  virtual bool Visit(Visitor* v) const = 0;

  uint16_t name() const { return name_; }
  private:
   uint16_t name_;
};

class FileResource : public Resource {
 public:
  FileResource(uint16_t name, std::experimental::string_view path)
      : Resource(name), path_(path) {}

  std::experimental::string_view path() const { return path_; }

 private:
  std::experimental::string_view path_;
};

class CursorResource : public FileResource {
 public:
  CursorResource(uint16_t name, std::experimental::string_view path)
      : FileResource(name, path) {}

  bool Visit(Visitor* v) const override { return v->VisitCursorResource(this); }
};

class BitmapResource : public FileResource {
 public:
  BitmapResource(uint16_t name, std::experimental::string_view path)
      : FileResource(name, path) {}

  bool Visit(Visitor* v) const override { return v->VisitBitmapResource(this); }
};

class IconResource : public FileResource {
 public:
  IconResource(uint16_t name, std::experimental::string_view path)
      : FileResource(name, path) {}

  bool Visit(Visitor* v) const override { return v->VisitIconResource(this); }
};

enum MenuStyle {
  kMenuGRAYED = 1,
  kMenuINACTIVE = 2,
  kMenuBITMAP = 4,
  kMenuCHECKED = 8,
  kMenuPOPUP = 0x10,
  kMenuMENUBARBREAK = 0x20,
  kMenuMENUBREAK = 0x40,
  kMenuLASTITEM = 0x80,
  kMenuOWNERDRAW = 0x100,
  kMenuHELP = 0x4000,
};

class MenuResource : public Resource {
 public:
  struct EntryData {};
  struct Entry {
    Entry(uint16_t style,
          std::experimental::string_view name,
          std::unique_ptr<EntryData> data)
        : style(style), name(name), data(std::move(data)) {}
    uint16_t style;
    std::experimental::string_view name;
    std::unique_ptr<EntryData> data;
  };
  struct ItemEntryData : public EntryData {
    explicit ItemEntryData(uint32_t id) : id(id) {}
    uint32_t id;
  };
  struct SubmenuEntryData : public EntryData {
    SubmenuEntryData() {}
    explicit SubmenuEntryData(std::vector<std::unique_ptr<Entry>> subentries)
        : subentries(std::move(subentries)) {}
    std::vector<std::unique_ptr<Entry>> subentries;
  };

  MenuResource(uint16_t name, SubmenuEntryData entries)
      : Resource(name), entries_(std::move(entries)) {}


  bool Visit(Visitor* v) const override { return v->VisitMenuResource(this); }

  SubmenuEntryData entries_;
};

class StringtableResource : public Resource {
 public:
  struct Entry {
    uint16_t id;
    std::experimental::string_view value;
  };

  StringtableResource(Entry* entries, size_t num_entries)
      // STRINGTABLES have no names.
      : Resource(0), entries_(entries, entries + num_entries) {}

  bool Visit(Visitor* v) const override {
    return v->VisitStringtableResource(this);
  }

  typedef std::vector<Entry>::const_iterator const_iterator;
  const_iterator begin() const { return entries_.begin(); }
  const_iterator end() const { return entries_.end(); }

 private:
  std::vector<Entry> entries_;
};

class AcceleratorsResource : public Resource {
 public:
  struct Accelerator {
    //  1h - VIRTKEY
    //  2h - NOINVERT
    //  4h - SHIFT
    //  8h - CONTROL
    // 10h - ALT
    // 80h - Set for the last accelerator in .res file.
    uint16_t flags;
    uint16_t key;
    uint16_t id;
    uint16_t pad;  // Seems to be always 0.
  };

  AcceleratorsResource(uint16_t name, std::vector<Accelerator> accelerators)
      : Resource(name), accelerators_(std::move(accelerators)) {}

  bool Visit(Visitor* v) const override {
    return v->VisitAcceleratorsResource(this);
  }

  const std::vector<Accelerator>& accelerators() const { return accelerators_; }

 private:
  std::vector<Accelerator> accelerators_;
};

// FIXME: either file, or block with data
class RcdataResource : public FileResource {
 public:
  RcdataResource(uint16_t name, std::experimental::string_view path)
      : FileResource(name, path) {}

  bool Visit(Visitor* v) const override { return v->VisitRcdataResource(this); }
};

class VersioninfoResource : public Resource {
 public:

  struct FixedInfo {
    uint32_t fileversion_high;
    uint32_t fileversion_low;
    uint32_t productversion_high;
    uint32_t productversion_low;
    uint32_t fileflags_mask;
    uint32_t fileflags;
    uint32_t fileos;
    uint32_t filetype;
    uint32_t filesubtype;
  };

  VersioninfoResource(uint16_t name, const FixedInfo& info)
      : Resource(name), fixed_info_(info) {}

  bool Visit(Visitor* v) const override {
    return v->VisitVersioninfoResource(this);
  }

  FixedInfo fixed_info_;
};

class DlgincludeResource : public Resource {
 public:
  DlgincludeResource(uint16_t name, std::experimental::string_view data)
      : Resource(name), data_(data) {}

  bool Visit(Visitor* v) const override {
    return v->VisitDlgincludeResource(this);
  }

  std::experimental::string_view data() const { return data_; }

 private:
  std::experimental::string_view data_;
};

// FIXME: either file, or block with data
class HtmlResource : public FileResource {
 public:
  HtmlResource(uint16_t name, std::experimental::string_view path)
      : FileResource(name, path) {}

  bool Visit(Visitor* v) const override { return v->VisitHtmlResource(this); }
};

class FileBlock {
 public:
  std::vector<std::unique_ptr<Resource>> res_;
};

//////////////////////////////////////////////////////////////////////////////
// Parser

class Parser {
 public:
  static std::unique_ptr<FileBlock> Parse(std::vector<Token> tokens,
                                          std::string* err);

 private:
  Parser(std::vector<Token> tokens);
  void MaybeParseMenuOptions(uint16_t* style);
  std::unique_ptr<MenuResource::SubmenuEntryData> ParseMenuBlock();
  std::unique_ptr<MenuResource> ParseMenu(uint16_t id_num);
  std::unique_ptr<StringtableResource> ParseStringtable();
  bool ParseAccelerator(AcceleratorsResource::Accelerator* accelerator);
  std::unique_ptr<AcceleratorsResource> ParseAccelerators(uint16_t id_num);
  std::unique_ptr<VersioninfoResource> ParseVersioninfo(uint16_t id_num);
  std::unique_ptr<Resource> ParseResource();
  std::unique_ptr<FileBlock> ParseFile(std::string* err);

  bool Is(Token::Type type);
  bool Match(Token::Type type);
  const Token& Consume();

  // Call this only if !at_end().
  const Token& cur_token() const { return tokens_[cur_]; }

  const Token& cur_or_last_token() const {
    return at_end() ? tokens_[tokens_.size() - 1] : cur_token();
  }

  bool done() const { return at_end() || has_error(); }
  bool at_end() const { return cur_ >= tokens_.size(); }
  bool has_error() const { return !err_.empty(); }

  std::vector<Token> tokens_;
  std::string err_;
  size_t cur_;
};

// static
std::unique_ptr<FileBlock> Parser::Parse(std::vector<Token> tokens,
                                         std::string* err) {
  Parser p(std::move(tokens));
  return p.ParseFile(err);
}

Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens)), cur_(0) {}

bool Parser::Is(Token::Type type) {
  if (at_end())
    return false;
  return cur_token().type() == type;
}

bool Parser::Match(Token::Type type) {
  if (!Is(type))
    return false;
  Consume();
  return true;
}

const Token& Parser::Consume() {
  return tokens_[cur_++];
}

void Parser::MaybeParseMenuOptions(uint16_t* style) {
  std::unordered_map<std::experimental::string_view, uint16_t> styles = {
    {"GRAYED", kMenuGRAYED},
    {"INACTIVE", kMenuINACTIVE},
    {"BITMAP", kMenuBITMAP},
    {"CHECKED", kMenuCHECKED},
    {"MENUBARBREAK", kMenuMENUBARBREAK},
    {"MENUBREAK", kMenuMENUBREAK},
    {"OWNERDRAW", kMenuOWNERDRAW},
    {"HELP", kMenuHELP},
  };
  while (Is(Token::kComma) || Is(Token::kIdentifier)) {
    if (Match(Token::kComma))
      continue;

    auto it = styles.find(cur_token().value_);
    if (it == styles.end())
      return;

    *style |= it->second;
    Consume();
  }
}

std::unique_ptr<MenuResource::SubmenuEntryData> Parser::ParseMenuBlock() {
  if (!Match(Token::kStartBlock)) {
    err_ = "expected START or {, got " + cur_or_last_token().value_.to_string();
    return std::unique_ptr<MenuResource::SubmenuEntryData>();
  }

  std::unique_ptr<MenuResource::SubmenuEntryData> entries(
      new MenuResource::SubmenuEntryData);
  while (!at_end() && cur_token().type() != Token::kEndBlock) {
    if (!Is(Token::kIdentifier) ||
        (cur_token().value_ != "MENUITEM" && cur_token().value_ != "POPUP")) {
      err_ = "expected MENUITEM or POPUP, got " +
             cur_or_last_token().value_.to_string();
      return std::unique_ptr<MenuResource::SubmenuEntryData>();
    }
    bool is_item = cur_token().value_ == "MENUITEM";
    Consume();
    if (!Is(Token::kString)) {
      err_ = "expected string, got " + cur_or_last_token().value_.to_string();
      return std::unique_ptr<MenuResource::SubmenuEntryData>();
    }
    const Token& name = Consume();
    std::experimental::string_view name_val = name.value_;
    // The literal includes quotes, strip them.
    // FIXME: give Token a StringValue() function that handles \-escapes,
    // "quoting""rules", L"asdf", etc.
    name_val = name_val.substr(1, name_val.size() - 2);
    std::unique_ptr<MenuResource::EntryData> entry_data;

    uint16_t style = 0;
    if (is_item) {
      if (!Match(Token::kComma)) {
        err_ = "expected comma, got " + cur_or_last_token().value_.to_string();
        return std::unique_ptr<MenuResource::SubmenuEntryData>();
      }
      if (!Is(Token::kInt)) {
        err_ = "expected int, got " + cur_or_last_token().value_.to_string();
        return std::unique_ptr<MenuResource::SubmenuEntryData>();
      }
      const Token& id = Consume();
      // FIXME: give Token an IntValue() function that handles 0x123, 0o123,
      // 1234L.
      uint16_t id_num = atoi(id.value_.to_string().c_str());

      MaybeParseMenuOptions(&style);
      entry_data.reset(new MenuResource::ItemEntryData(id_num));
    } else {
      MaybeParseMenuOptions(&style);
      style |= kMenuPOPUP;

      std::unique_ptr<MenuResource::SubmenuEntryData> subentries =
          ParseMenuBlock();
      if (!entries)
        return std::unique_ptr<MenuResource::SubmenuEntryData>();
      entry_data = std::move(subentries);
    }
    entries->subentries.push_back(std::unique_ptr<MenuResource::Entry>(
        new MenuResource::Entry(style, name_val, std::move(entry_data))));
  }
  if (!Match(Token::kEndBlock)) {
    err_ = "expected END or }, got " + cur_or_last_token().value_.to_string();
    return std::unique_ptr<MenuResource::SubmenuEntryData>();
  }
  if (entries->subentries.empty()) {
    err_ = "empty menus are not allowed";
    return std::unique_ptr<MenuResource::SubmenuEntryData>();
  }
  return entries;
}


std::unique_ptr<MenuResource> Parser::ParseMenu(uint16_t id_num) {
  std::unique_ptr<MenuResource::SubmenuEntryData> entries = ParseMenuBlock();
  if (!entries)
    return std::unique_ptr<MenuResource>();
  return std::unique_ptr<MenuResource>(
      new MenuResource(id_num, std::move(*entries.get())));
}

std::unique_ptr<StringtableResource> Parser::ParseStringtable() {
  if (!Match(Token::kStartBlock)) {
    err_ = "expected START or {, got " + cur_or_last_token().value_.to_string();
    return std::unique_ptr<StringtableResource>();
  }
  std::vector<StringtableResource::Entry> entries;
  while (!at_end() && cur_token().type() != Token::kEndBlock) {
    if (!Is(Token::kInt)) {
      err_ = "expected int, got " + cur_or_last_token().value_.to_string();
      return std::unique_ptr<StringtableResource>();
    }
    const Token& key = Consume();
    Match(Token::kComma);  // Eat optional comma between key and value.

    if (!Is(Token::kString)) {
      err_ = "expected string, got " + cur_or_last_token().value_.to_string();
      return std::unique_ptr<StringtableResource>();
    }
    const Token& value = Consume();

    // FIXME: give Token an IntValue() function that handles 0x123, 0o123,
    // 1234L.
    uint16_t key_num = atoi(key.value_.to_string().c_str());
    std::experimental::string_view str_val = value.value_;
    // The literal includes quotes, strip them.
    // FIXME: give Token a StringValue() function that handles \-escapes,
    // "quoting""rules", L"asdf", etc.
    str_val = str_val.substr(1, str_val.size() - 2);
    entries.push_back(StringtableResource::Entry{key_num, str_val});
  }
  if (!Match(Token::kEndBlock)) {
    err_ = "expected END or }, got " + cur_or_last_token().value_.to_string();
    return std::unique_ptr<StringtableResource>();
  }
  return std::unique_ptr<StringtableResource>(
      new StringtableResource(entries.data(), entries.size()));
}

bool Parser::ParseAccelerator(AcceleratorsResource::Accelerator* accelerator) {
  if (!Is(Token::kString) && !Is(Token::kInt)) {
    err_ =
        "expected string or int, got " + cur_or_last_token().value_.to_string();
    return false;
  }
  const Token& name = Consume();
  if (!Match(Token::kComma)) {
      err_ = "expected comma, got " + cur_or_last_token().value_.to_string();
      return false;
  }
  if (!Is(Token::kInt)) {
    err_ = "expected int, got " + cur_or_last_token().value_.to_string();
    return false;
  }
  const Token& id = Consume();
  // FIXME: give Token an IntValue() function that handles 0x123, 0o123,
  // 1234L.
  uint16_t id_num = atoi(id.value_.to_string().c_str());

  uint16_t flags = 0;
  while (Is(Token::kComma)) {
    Consume();
    if (!Is(Token::kIdentifier)) {
      err_ = "expected ident, got " + cur_or_last_token().value_.to_string();
      return false;
    }
    const Token& flag = Consume();
    if (flag.value_ == "ASCII")
      ;
    else if (flag.value_ == "VIRTKEY")
      flags |= 1;
    else if (flag.value_ == "NOINVERT")
      flags |= 2;
    else if (flag.value_ == "SHIFT")
      flags |= 4;
    else if (flag.value_ == "CONTROL")
      flags |= 8;
    else if (flag.value_ == "ALT")
      flags |= 0x10;
    else {
      err_ = "unknown flag " + flag.value_.to_string();
      return false;
    }
  }

  accelerator->flags = flags;
  accelerator->key = 0;
  if (name.type() == Token::kInt) {
    // FIXME: give Token an IntValue() function that handles 0x123, 0o123,
    // 1234L.
    accelerator->key = atoi(name.value_.to_string().c_str());
  } else {
    std::experimental::string_view name_val = name.value_;
    // The literal includes quotes, strip them.
    name_val = name_val.substr(1, name_val.size() - 2);

    if (name_val.size() == 0 || name_val.size() > 2) {
      err_ = "invalid key \"" + name_val.to_string() + "\"";
      return false;
    }

    // XXX check "^A^B"
    // XXX check "^0"
    // XXX check "^ "
    // XXX check L"a"

    if (name_val.size() == 2 && name_val[0] == '^') {
      char c = name_val[1];
      if (c >= 'A' && c <= 'Z')
        c = c - 'A' + 1;
      else if (c >= 'a' && c <= 'z')
        c = c - 'a' + 1;
      accelerator->key = c;
    } else if (!name_val.empty()) {
      // XXX rc.exe rejects chars with >= 3 letters, but it's not clear why it
      // has its weird behavior for two-letter keys. Something something UTF-16?
      for (char c : name_val)
        accelerator->key = (accelerator->key << 8) + c;
    }
    // Convert char to upper if SHIFT is specified, or if it's a VIRTKEY.
    if ((flags & 5) && accelerator->key >= 'a' && accelerator->key <= 'z')
      accelerator->key = accelerator->key - 'a' + 'A';
  }
  accelerator->id = id_num;
  accelerator->pad = 0;
  return true;
}

std::unique_ptr<AcceleratorsResource> Parser::ParseAccelerators(
    uint16_t id_num) {
  if (!Match(Token::kStartBlock)) {
    err_ = "expected START or {, got " + cur_or_last_token().value_.to_string();
    return std::unique_ptr<AcceleratorsResource>();
  }

  std::vector<AcceleratorsResource::Accelerator> entries;
  while (!at_end() && cur_token().type() != Token::kEndBlock) {
    AcceleratorsResource::Accelerator accelerator;
    if (!ParseAccelerator(&accelerator))
      return std::unique_ptr<AcceleratorsResource>();
    entries.push_back(accelerator);
  }
  if (!Match(Token::kEndBlock)) {
    err_ = "expected END or }, got " + cur_or_last_token().value_.to_string();
    return std::unique_ptr<AcceleratorsResource>();
  }
  return std::unique_ptr<AcceleratorsResource>(
      new AcceleratorsResource(id_num, std::move(entries)));
}

std::unique_ptr<VersioninfoResource> Parser::ParseVersioninfo(uint16_t id_num) {
  // Parse fixed info.
  VersioninfoResource::FixedInfo fixed_info = {};
  std::unordered_map<std::experimental::string_view, uint32_t*> fields = {
    {"FILEFLAGSMASK", &fixed_info.fileflags_mask},
    {"FILEFLAGS", &fixed_info.fileflags},
    {"FILEOS", &fixed_info.fileos},
    {"FILETYPE", &fixed_info.filetype},
    {"FILESUBTYPE", &fixed_info.filesubtype},
  };
  while (!at_end() && cur_token().type() != Token::kStartBlock) {
    if (!Is(Token::kIdentifier)) {
      err_ = "expected identifier START or {, got " +
             cur_or_last_token().value_.to_string();
      return std::unique_ptr<VersioninfoResource>();
    }
    const Token& name = Consume();
    if (!Is(Token::kInt)) {
      err_ = "expected int, got " + cur_or_last_token().value_.to_string();
      return std::unique_ptr<VersioninfoResource>();
    }
    const Token& val = Consume();
    // FIXME: give Token an IntValue() function that handles 0x123, 0o123,
    // 1234L.
    uint16_t val_num = atoi(val.value_.to_string().c_str());
    if (name.value_ == "FILEVERSION" || name.value_ == "PRODUCTVERSION") {
      uint16_t val_nums[4] = { val_num };
      for (int i = 0; i < 3 && Is(Token::kComma); ++i) {
        Consume();  // Eat comma.
        if (!Is(Token::kInt)) {
          err_ = "expected int, got " + cur_or_last_token().value_.to_string();
          return std::unique_ptr<VersioninfoResource>();
        }
        const Token& val = Consume();
        // FIXME: give Token an IntValue() function that handles 0x123, 0o123,
        // 1234L.
        val_nums[i + 1] = atoi(val.value_.to_string().c_str());
      }
      if (name.value_ == "FILEVERSION") {
        fixed_info.fileversion_high = (val_nums[0] << 16) | val_nums[1];
        fixed_info.fileversion_low = (val_nums[2] << 16) | val_nums[3];
      } else {
        fixed_info.productversion_high = (val_nums[0] << 16) | val_nums[1];
        fixed_info.productversion_low = (val_nums[2] << 16) | val_nums[3];
      }
    } else {
      auto it = fields.find(name.value_);
      if (it == fields.end()) {
        err_ = "unknown field " + name.value_.to_string();
        return std::unique_ptr<VersioninfoResource>();
      }
      *it->second = val_num;
    }
  }

  // Parse block info.
  if (!Match(Token::kStartBlock)) {
    err_ = "expected START or {, got " + cur_or_last_token().value_.to_string();
    return std::unique_ptr<VersioninfoResource>();
  }

  int depth = 1;  // FIXME actually look at blocks instead of skipping
  while (!at_end() /* FIXME && cur_token().type() != Token::kEndBlock*/ &&
         depth > 0) {
    // FIXME
    if (cur_token().type() == Token::kStartBlock)
      ++depth;
    else if (cur_token().type() == Token::kEndBlock)
      --depth;
    Consume();
  }
  //if (!Match(Token::kEndBlock)) {
    //err_ = "expected END or }, got " + cur_or_last_token().value_.to_string();
    //return std::unique_ptr<VersioninfoResource>();
  //}
  return std::unique_ptr<VersioninfoResource>(
      new VersioninfoResource(id_num, fixed_info));
}

std::unique_ptr<Resource> Parser::ParseResource() {
  const Token& id = Consume();  // Either int or ident.

  if (at_end()) {
    err_ = "expected resource name";
    return std::unique_ptr<Resource>();
  }

  if (id.type() == Token::kIdentifier) {
    if (id.value_ == "STRINGTABLE")
      return ParseStringtable();
  }

  if (id.type() != Token::kInt) {
    err_ = "only int names supported for now";
    return std::unique_ptr<Resource>();
  }
  // Fun fact: rc.exe silently truncates to 16 bit as well.
  uint16_t id_num = atoi(id.value_.to_string().c_str());

  // Normally, name first.
  //
  // Exceptions:
  // - LANGUAGE
  //   "When the LANGUAGE statement appears before the beginning of the body of
  //    an ACCELERATORS, DIALOGEX, MENU, RCDATA, or STRINGTABLE resource
  //    definition, the specified language applies only to that resource."
  // - STRINGTABLE
  // - VERSION, in front of ACCELERATORS, DIALOGEX, MENU, RCDATA, STRINGTABLE 


  const Token& type = Consume();
  if (at_end()) {
    err_ = "expected resource type";
    return std::unique_ptr<Resource>();
  }

  // FIXME: skip Common Resource Attributes if needed:
  // https://msdn.microsoft.com/en-us/library/windows/desktop/aa380908(v=vs.85).aspx
  // They have been ignored since the 16-bit days, so hopefully they no longer
  // exist in practice.

  // FIXME: MENUEX
  if (type.type_ == Token::kIdentifier && type.value_ == "MENU")
    return ParseMenu(id_num);
  if (type.type_ == Token::kIdentifier && type.value_ == "ACCELERATORS")
    return ParseAccelerators(id_num);
  if (type.type_ == Token::kIdentifier && type.value_ == "VERSIONINFO")
    return ParseVersioninfo(id_num);

  if (type.type_ == Token::kIdentifier && cur_token().type_ == Token::kString) {
    const Token& string = Consume();
    std::experimental::string_view str_val = string.value_;
    // The literal includes quotes, strip them.
    str_val = str_val.substr(1, str_val.size() - 2);

    // FIXME: case-insensitive
    if (type.value_ == "CURSOR")
      return std::unique_ptr<Resource>(new CursorResource(id_num, str_val));
    if (type.value_ == "BITMAP")
      return std::unique_ptr<Resource>(new BitmapResource(id_num, str_val));
    if (type.value_ == "ICON")
      return std::unique_ptr<Resource>(new IconResource(id_num, str_val));
    if (type.value_ == "RCDATA")
      return std::unique_ptr<Resource>(new RcdataResource(id_num, str_val));
    if (type.value_ == "DLGINCLUDE")
      return std::unique_ptr<Resource>(new DlgincludeResource(id_num, str_val));
    if (type.value_ == "HTML")
      return std::unique_ptr<Resource>(new HtmlResource(id_num, str_val));
  }

  err_ = "unknown resource";
  return std::unique_ptr<Resource>();
}

std::unique_ptr<FileBlock> Parser::ParseFile(std::string* err) {
  std::unique_ptr<FileBlock> file(new FileBlock);
  for (;;) {
    if (at_end())
      break;

    std::unique_ptr<Resource> res = ParseResource();
    if (!res)
      break;
    file->res_.push_back(std::move(res));
  }
  if (has_error()) {
    *err = err_;
    return std::unique_ptr<FileBlock>();
  }
  return file;
}

//////////////////////////////////////////////////////////////////////////////
// Writer

// https://msdn.microsoft.com/en-us/library/windows/desktop/ms648009(v=vs.85).aspx
// k prefix so that names don't collide with Win SDK on Windows.
enum {
  kRT_CURSOR = 1,
  kRT_BITMAP = 2,
  kRT_ICON = 3,
  kRT_MENU = 4,
  kRT_DIALOG = 5,
  kRT_STRING = 6,
  kRT_FONTDIR = 7,
  kRT_FONT = 8,
  kRT_ACCELERATOR = 9,
  kRT_RCDATA = 10,
  kRT_MESSAGETABLE = 11,
  kRT_GROUP_CURSOR = 12,
  kRT_GROUP_ICON = 14,
  kRT_VERSION = 16,    // Not stored in image file.
  kRT_DLGINCLUDE = 17,
  kRT_PLUGPLAY = 19,
  kRT_VXD = 20,
  kRT_ANICURSOR = 21,
  kRT_ANIICON = 22,
  kRT_HTML = 23,
  kRT_MANIFEST = 24,
};

static void write_little_long(FILE* f, uint32_t l) {
  uint8_t bytes[] = { l & 0xff, (l >> 8) & 0xff, (l >> 16) & 0xff, l >> 24 };
  fwrite(bytes, 1, sizeof(bytes), f);
}

static void write_little_short(FILE* f, uint16_t l) {
  uint8_t bytes[] = { l & 0xff, (l >> 8) };
  fwrite(bytes, 1, sizeof(bytes), f);
}

static void write_numeric_type(FILE* f, uint16_t n) {
  write_little_short(f, 0xffff);
  write_little_short(f, n);
}

static void write_numeric_name(FILE* f, uint16_t n) {
  write_little_short(f, 0xffff);
  write_little_short(f, n);
}

static uint16_t read_little_short(FILE* f) {
  uint16_t r = fgetc(f);  // XXX error handling
  r |= fgetc(f) << 8;
  return r;
}

static uint32_t read_little_long(FILE* f) {
  uint32_t r = fgetc(f);  // XXX error handling
  r |= fgetc(f) << 8;
  r |= fgetc(f) << 16;
  r |= fgetc(f) << 24;
  return r;
}

class SerializationVisitor : public Visitor {
 public:
  SerializationVisitor(FILE* f, std::string* err)
      : out_(f), err_(err), next_icon_id_(1) {}

  bool VisitCursorResource(const CursorResource* r) override;
  bool VisitBitmapResource(const BitmapResource* r) override;
  bool VisitIconResource(const IconResource* r) override;
  bool VisitMenuResource(const MenuResource* r) override;
  bool VisitStringtableResource(const StringtableResource* r) override;
  bool VisitAcceleratorsResource(const AcceleratorsResource* r) override;
  bool VisitRcdataResource(const RcdataResource* r) override;
  bool VisitVersioninfoResource(const VersioninfoResource* r) override;
  bool VisitDlgincludeResource(const DlgincludeResource* r) override;
  bool VisitHtmlResource(const HtmlResource* r) override;

  void EmitOneStringtable(const std::experimental::string_view** bundle,
                         uint16_t bundle_start);
  void WriteStringtables();

 private:
  enum GroupType { kIcon = 1, kCursor = 2 };
  bool WriteIconOrCursorGroup(const FileResource* r, GroupType type);

  FILE* out_;
  std::string* err_;
  int next_icon_id_;

  std::map<uint16_t, std::experimental::string_view> stringtable_;
};

struct FClose {
  FClose(FILE* f) : f_(f) {}
  ~FClose() { fclose(f_); }
  FILE* f_;
};

void copy(FILE* to, FILE* from, size_t n) {
  char buf[64 << 10];
  size_t len;
  while ((len = fread(buf, 1, std::min(sizeof(buf), n), from)) > 0) {
    fwrite(buf, 1, len, to);
    n -= len;
  }
  // XXX if (ferror(from) || ferror(to))...
}

bool SerializationVisitor::WriteIconOrCursorGroup(const FileResource* r,
                                                  GroupType type) {
  // An .ico file usually contains several bitmaps. In a .res file, one
  //kRT_ICON is written per bitmap, and they're tied together with one
  //kRT_GROUP_ICON.
  // .ico format: https://msdn.microsoft.com/en-us/library/ms997538.aspx

  FILE* f = fopen(r->path().to_string().c_str(), "rb");
  if (!f) {
    *err_ = "failed to open " + r->path().to_string();
    return false;
  }
  FClose closer(f);

  struct IconDir {
    uint16_t reserved;  // must be 0
    uint16_t type;      // must be 1 for icons, 2 for cursors
    uint16_t count;
  } dir;

  dir.reserved = read_little_short(f);
  if (dir.reserved != 0) {
    *err_ = "reserved not 0 in " + r->path().to_string();
    return false;
  }
  dir.type = read_little_short(f);
  // rc.exe allows both 1 and 2 here for both ICON and CURSOR.
  if (dir.type != type) {
    *err_ = "unexpected type in " + r->path().to_string();
    return false;
  }
  dir.count = read_little_short(f);

  // Read entries.
  struct IconEntry {
    uint8_t width;
    uint8_t height;
    uint8_t num_colors;   // 0 if more than 256 colors
    uint8_t reserved;     // must be 0
    uint16_t num_planes;  // hotspot_x in .cur files
    uint16_t bpp;         // bits per pixel, hotspot_y in .cur files
    uint32_t data_size;
    // This is a 4-byte data_offset in a .ico file, but a 2-byte id in
    // the kRT_GROUP_ICON resource.
    uint32_t data_offset;  // used when reading
    uint16_t id;           // used when writing
  };
  std::vector<IconEntry> entries(dir.count);
  for (IconEntry& entry : entries) {
    entry.width = fgetc(f);
    entry.height = fgetc(f);
    entry.num_colors = fgetc(f);
    entry.reserved = fgetc(f);
    entry.num_planes = read_little_short(f);
    entry.bpp = read_little_short(f);
    entry.data_size = read_little_long(f);
    entry.data_offset = read_little_long(f);
  }

  // For each entry, write a kRT_ICON resource.
  for (IconEntry& entry : entries) {
    // XXX padding?
    // Cursors are prepended by their hotspot.
    write_little_long(out_, entry.data_size + (type == kCursor ? 4 : 0));
    write_little_long(out_, 0x20);  // header size
    write_numeric_type(out_, type == kIcon ? kRT_ICON : kRT_CURSOR);
    write_numeric_name(out_, next_icon_id_);
    write_little_long(out_, 0);        // data version
    write_little_short(out_, 0x1010);  // memory flags XXX
    write_little_short(out_, 1033);    // language id XXX
    write_little_long(out_, 0);     // version
    write_little_long(out_, 0);     // characteristics

    if (type == kCursor) {
      write_little_short(out_, entry.num_planes);  // hotspot_x
      write_little_short(out_, entry.bpp);         // hotspot_y
    }

    fseek(f, entry.data_offset, SEEK_SET);
    copy(out_, f, entry.data_size);
    if (type == kCursor)
      entry.data_size += 4;

    entry.id = next_icon_id_++;
  }

  // Write final kRT_GROUP_ICON resource.
  uint16_t group_size = 6 + dir.count * 14;   // 1 IconDir + n IconEntry
  uint8_t group_padding = ((4 - (group_size & 3)) & 3);  // DWORD-align.
  write_little_long(out_, group_size);
  write_little_long(out_, 0x20);  // header size
  write_numeric_type(out_, type == kIcon ? kRT_GROUP_ICON : kRT_GROUP_CURSOR);
  write_numeric_name(out_, r->name());  // XXX support string names too
  write_little_long(out_, 0);        // data version
  write_little_short(out_, 0x1030);  // memory flags XXX
  write_little_short(out_, 1033);    // language id XXX
  write_little_long(out_, 0);        // version
  write_little_long(out_, 0);        // characteristics

  write_little_short(out_, dir.reserved);
  write_little_short(out_, dir.type);
  write_little_short(out_, dir.count);
  for (IconEntry& entry : entries) {
    if (type == kIcon) {
      fputc(entry.width, out_);
      fputc(entry.height, out_);
      fputc(entry.num_colors, out_);
      fputc(entry.reserved, out_);
      write_little_short(out_, entry.num_planes);
      write_little_short(out_, entry.bpp);
    } else {
      // .cur files are weird, they store width and height as shorts instead
      // of bytes here, multiply height by 2 apparently, and then store
      // the actual num_planes and bpp as shorts (remember, num_planes and bpp
      // are actually the hotspot coordinates in a .cur file, so they can't be
      // used here).
      write_little_short(out_, entry.width);
      write_little_short(out_, 2*entry.height);
      // Grab actual non-hotspot num_planes, bpp from BITMAPINFOHEADER.
      fseek(f, entry.data_offset + 12, SEEK_SET);
      uint16_t num_planes = read_little_short(f);
      uint16_t bpp = read_little_short(f);
      write_little_short(out_, num_planes);
      write_little_short(out_, bpp);
    }
    write_little_long(out_, entry.data_size);
    write_little_short(out_, entry.id);
  }
  //fwrite(":)", 1, group_padding, out_);
  fwrite("\0\0", 1, group_padding, out_);

  return true;
}

bool SerializationVisitor::VisitCursorResource(const CursorResource* r) {
  return WriteIconOrCursorGroup(r, kCursor);
}

bool SerializationVisitor::VisitBitmapResource(const BitmapResource* r) {
  FILE* f = fopen(r->path().to_string().c_str(), "rb");
  if (!f) {
    *err_ = "failed to open " + r->path().to_string();
    return false;
  }
  FClose closer(f);
  fseek(f, 0, SEEK_END);
  size_t size = ftell(f);
  // https://support.microsoft.com/en-us/kb/67883
  // "NOTE: The BITMAPFILEHEADER structure is NOT present in the packed DIB;
  //  however, it is present in a DIB read from disk."
  const int kBitmapFileHeaderSize = 14;
  size -= kBitmapFileHeaderSize;

  // XXX padding?
  write_little_long(out_, size);  // data size
  write_little_long(out_, 0x20);  // header size
  write_numeric_type(out_, kRT_BITMAP);
  write_numeric_name(out_, r->name());
  write_little_long(out_, 0);      // data version
  write_little_short(out_, 0x30);  // memory flags XXX
  write_little_short(out_, 1033);  // language id XXX
  write_little_long(out_, 0);      // version
  write_little_long(out_, 0);      // characteristics

  fseek(f, kBitmapFileHeaderSize, SEEK_SET);
  copy(out_, f, size);

  return true;
}

bool SerializationVisitor::VisitIconResource(const IconResource* r) {
  return WriteIconOrCursorGroup(r, kIcon);
}

void CountMenu(const MenuResource::SubmenuEntryData& submenu,
               size_t* num_submenus,
               size_t* num_items,
               size_t* total_string_length) {
  for (const auto& item : submenu.subentries) {
    *total_string_length += item->name.size() + 1;
    if (item->style & kMenuPOPUP) {
      ++*num_submenus;
      CountMenu(static_cast<MenuResource::SubmenuEntryData&>(*item->data),
                num_submenus, num_items, total_string_length);
    } else {
      ++*num_items;
    }
  }
}

void WriteMenu(FILE* out, const MenuResource::SubmenuEntryData& submenu) {
  for (const auto& item : submenu.subentries) {
    uint16_t style = item->style;
    if (&item == &submenu.subentries.back())
      style += kMenuLASTITEM;
    write_little_short(out, style);
    if (!(item->style & kMenuPOPUP)) {
      write_little_short(
          out, static_cast<MenuResource::ItemEntryData&>(*item->data).id);
    }
    for (int j = 0; j < item->name.size(); ++j) {
      // FIXME: Real UTF16 support.
      fputc(item->name[j], out);
      fputc('\0', out);
    }
    write_little_short(out, 0);  // \0-terminate.
    if (item->style & kMenuPOPUP) {
      WriteMenu(out, static_cast<MenuResource::SubmenuEntryData&>(*item->data));
    }
  }
}


bool SerializationVisitor::VisitMenuResource(const MenuResource* r) {
  size_t num_submenus = 0, num_items = 0, total_string_length = 0;
  CountMenu(r->entries_, &num_submenus, &num_items, &total_string_length);

  size_t size = 4 + (num_submenus + 2*num_items + total_string_length) * 2;

  // XXX padding?
  write_little_long(out_, size);  // data size
  write_little_long(out_, 0x20);  // header size
  write_numeric_type(out_, kRT_MENU);
  write_numeric_name(out_, r->name());
  write_little_long(out_, 0);      // data version
  write_little_short(out_, 0x1030);  // memory flags XXX
  write_little_short(out_, 1033);  // language id XXX
  write_little_long(out_, 0);      // version
  write_little_long(out_, 0);      // characteristics

  // After header, 4 bytes, always 0 as far as I can tell.
  // Maybe style and name of toplevel menu.
  write_little_long(out_, 0);

  // Each item, (1 + 1|0 + strlen(label) + 1) * 2 bytes:
  // - uint16_t flags
  //   https://msdn.microsoft.com/en-us/library/windows/desktop/aa381024(v=vs.85).aspx
  //      1h - GRAYED
  //      2h - INACTIVE
  //      8h - CHECKED (MF_CHECKED)
  //     10h - open new submenu (?) (MF_POPUP?)
  //     20h - MENUBARBREAK
  //     40h - MENUBREAK
  //     80h - last item in menu / close previous menu
  //   4000h - HELP
  // - if item (flags doesn't have 10 set), uint16_t id
  // - 0-terminated utf-16le string with label (including &)
  // (Turns out this representation is much easier to work with than what I
  // have chosen!)
  WriteMenu(out_, r->entries_);
  return true;
}

bool SerializationVisitor::VisitStringtableResource(
    const StringtableResource* r) {
  for (auto& str : *r) {
    bool is_new = stringtable_.insert(std::make_pair(str.id, str.value)).second;
    if (!is_new) {
      *err_ = "duplicate string table key " + std::to_string(str.id);
      return false;
    }
  }
  return true;
}

bool SerializationVisitor::VisitAcceleratorsResource(
    const AcceleratorsResource* r) {
  size_t size = r->accelerators().size() * 8;
  write_little_long(out_, size);  // data size
  write_little_long(out_, 0x20);  // header size
  write_numeric_type(out_, kRT_ACCELERATOR);
  write_numeric_name(out_, r->name());
  write_little_long(out_, 0);      // data version
  write_little_short(out_, 0x30);  // memory flags XXX
  write_little_short(out_, 1033);  // language id XXX
  write_little_long(out_, 0);      // version
  write_little_long(out_, 0);      // characteristics

  for (const auto& accelerator : r->accelerators()) {
    uint16_t flags = accelerator.flags;
    if (&accelerator == &r->accelerators().back())
      flags |= 0x80;
    write_little_short(out_, flags);
    write_little_short(out_, accelerator.key);
    write_little_short(out_, accelerator.id);
    write_little_short(out_, accelerator.pad);
  }
  return true;
}

bool SerializationVisitor::VisitRcdataResource(const RcdataResource* r) {
  FILE* f = fopen(r->path().to_string().c_str(), "rb");
  if (!f) {
    *err_ = "failed to open " + r->path().to_string();
    return false;
  }
  FClose closer(f);
  fseek(f, 0, SEEK_END);
  size_t size = ftell(f);

  write_little_long(out_, size);  // data size
  write_little_long(out_, 0x20);  // header size
  write_numeric_type(out_, kRT_RCDATA);
  write_numeric_name(out_, r->name());
  write_little_long(out_, 0);      // data version
  write_little_short(out_, 0x30);  // memory flags XXX
  write_little_short(out_, 1033);  // language id XXX
  write_little_long(out_, 0);      // version
  write_little_long(out_, 0);      // characteristics

  fseek(f, 0, SEEK_SET);
  copy(out_, f, size);
  uint8_t padding = ((4 - (size & 3)) & 3);  // DWORD-align.
  fwrite("\0\0", 1, padding, out_);

  return true;
}

bool SerializationVisitor::VisitVersioninfoResource(
    const VersioninfoResource* r) {
  const size_t kFixedInfoSize = 0x5c;
  size_t size = kFixedInfoSize;
  write_little_long(out_, size);  // data size
  write_little_long(out_, 0x20);      // header size
  write_numeric_type(out_, kRT_VERSION);
  write_numeric_name(out_, r->name());
  write_little_long(out_, 0);         // data version
  write_little_short(out_, 0x30);   // memory flags XXX
  write_little_short(out_, 1033);     // language id XXX
  write_little_long(out_, 0);         // version
  write_little_long(out_, 0);         // characteristics

  // The fixed info block seems to always start with the same fixed bytes:
  write_little_short(out_, kFixedInfoSize);
  write_little_long(out_, 52);
  fwrite(u"VS_VERSION_INFO", 2, 16, out_);
  write_little_short(out_, 0);
  write_little_long(out_, 0xfeef04bd);
  write_little_short(out_, 0);
  write_little_short(out_, 1);

  // Actual fixed info:
  write_little_long(out_, r->fixed_info_.fileversion_high);
  write_little_long(out_, r->fixed_info_.fileversion_low);
  write_little_long(out_, r->fixed_info_.productversion_high);
  write_little_long(out_, r->fixed_info_.productversion_low);
  write_little_long(out_, r->fixed_info_.fileflags_mask);
  write_little_long(out_, r->fixed_info_.fileflags);
  write_little_long(out_, r->fixed_info_.fileos);
  write_little_long(out_, r->fixed_info_.filetype);
  write_little_long(out_, r->fixed_info_.filesubtype);
  // Two more mystery fields :-(
  write_little_long(out_, 0);
  write_little_long(out_, 0);

  // FIXME: implement writing of variable-size fields too
  return true;
}

bool SerializationVisitor::VisitDlgincludeResource(
    const DlgincludeResource* r) {
  size_t size = r->data().size() + 1; // include trailing \0
  write_little_long(out_, size);  // data size
  write_little_long(out_, 0x20);      // header size
  write_numeric_type(out_, kRT_DLGINCLUDE);
  write_numeric_name(out_, r->name());
  write_little_long(out_, 0);         // data version
  write_little_short(out_, 0x1030);   // memory flags XXX
  write_little_short(out_, 1033);     // language id XXX
  write_little_long(out_, 0);         // version
  write_little_long(out_, 0);         // characteristics
  // data() is a string_view and might not be \0-terminated, so write \0
  // separately.
  fwrite(r->data().data(), 1, r->data().size(), out_);
  fputc('\0', out_);
  uint8_t padding = ((4 - (size & 3)) & 3);  // DWORD-align.
  fwrite("\0\0", 1, padding, out_);
  return true;
}

bool SerializationVisitor::VisitHtmlResource(const HtmlResource* r) {
  FILE* f = fopen(r->path().to_string().c_str(), "rb");
  if (!f) {
    *err_ = "failed to open " + r->path().to_string();
    return false;
  }
  FClose closer(f);
  fseek(f, 0, SEEK_END);
  size_t size = ftell(f);

  write_little_long(out_, size);  // data size
  write_little_long(out_, 0x20);  // header size
  write_numeric_type(out_, kRT_HTML);
  write_numeric_name(out_, r->name());
  write_little_long(out_, 0);      // data version
  write_little_short(out_, 0x30);  // memory flags XXX
  write_little_short(out_, 1033);  // language id XXX
  write_little_long(out_, 0);      // version
  write_little_long(out_, 0);      // characteristics

  fseek(f, 0, SEEK_SET);
  copy(out_, f, size);
  uint8_t padding = ((4 - (size & 3)) & 3);  // DWORD-align.
  fwrite("\0\0", 1, padding, out_);

  return true;
}

void SerializationVisitor::EmitOneStringtable(
    const std::experimental::string_view** bundle,
    uint16_t bundle_start) {
  // Each string is written as uint16_t length, followed by string data without
  // a trailing \0.
  // FIXME: rc.exe /n null-terminates strings in string table, have option
  // for that.
  size_t size = 0;
  for (int i = 0; i < 16; ++i)
    size += 2 * (1 + (bundle[i] ? bundle[i]->size() : 0));

  write_little_long(out_, size);  // data size
  write_little_long(out_, 0x20);      // header size
  write_numeric_type(out_, kRT_STRING);
  write_numeric_name(out_, (bundle_start / 16) + 1);
  write_little_long(out_, 0);        // data version
  write_little_short(out_, 0x1030);  // memory flags XXX
  write_little_short(out_, 1033);    // language id XXX
  write_little_long(out_, 0);        // version
  write_little_long(out_, 0);        // characteristics
  for (int i = 0; i < 16; ++i) {
    if (!bundle[i]) {
      fwrite("\0", 1, 2, out_);
      continue;
    }
    write_little_short(out_, bundle[i]->size());
    for (int j = 0; j < bundle[i]->size(); ++j) {
      // FIXME: Real UTF16 support.
      fputc((*bundle[i])[j], out_);
      fputc('\0', out_);
    }
  }
  // XXX padding?

  std::fill_n(bundle, 16, nullptr);
}

void SerializationVisitor::WriteStringtables() {
  // https://blogs.msdn.microsoft.com/oldnewthing/20040130-00/?p=40813
  // "The strings listed in the *.rc file are grouped together in bundles of
  //  sixteen. So the first bundle contains strings 0 through 15, the second
  //  bundle contains strings 16 through 31, and so on. In general, bundle N
  //  contains strings (N-1)*16 through (N-1)*16+15.
  //  The strings in each bundle are stored as counted UNICODE strings, not
  //  null-terminated strings. If there are gaps in the numbering, null strings
  //  are used. So for example if your string table had only strings 16 and 31,
  //  there would be one bundle (number 2), which consists of string 16,
  //  fourteen null strings, then string 31."

  const std::experimental::string_view* bundle[16] = {};
  size_t n_bundle = 0;
  uint16_t bundle_start = 0;

  for (const auto& it : stringtable_) {
    if (it.first - bundle_start >= 16) {
      if (n_bundle > 0) {
        EmitOneStringtable(bundle, bundle_start);
        n_bundle = 0;
      }
      bundle_start = it.first & ~0xf;
    }
    bundle[it.first - bundle_start] = &it.second;
    ++n_bundle;
    continue;
  }
  if (n_bundle > 0) {
    EmitOneStringtable(bundle, bundle_start);
    n_bundle = 0;
  }

  stringtable_.clear();
}

bool WriteRes(const FileBlock& file, std::string* err) {
  FILE* f = fopen("out.res", "wb");
  if (!f) {
    *err = "failed to open out.res";
    return false;
  }
  FClose closer(f);

  // First write the "this is not the ancient 16-bit format" header.
  write_little_long(f, 0);     // data size
  write_little_long(f, 0x20);  // header size
  write_numeric_type(f, 0);    // type
  write_numeric_name(f, 0);    // name
  write_little_long(f, 0);     // data version
  write_little_short(f, 0);    // memory flags
  write_little_short(f, 0);    // language id
  write_little_long(f, 0);     // version
  write_little_long(f, 0);     // characteristics

  SerializationVisitor serializer(f, err);
  for (int i = 0; i < file.res_.size(); ++i) {
    const Resource* res = file.res_[i].get();
    if (!res->Visit(&serializer))
      return false;
  }
  serializer.WriteStringtables();

  return true;
}

//////////////////////////////////////////////////////////////////////////////
// Driver

int main(int argc, char* argv[]) {
  std::istreambuf_iterator<char> begin(std::cin), end;
  std::string s(begin, end);

  std::string err;
  std::vector<Token> tokens = Tokenizer::Tokenize(s, &err);
  if (tokens.empty() && !err.empty()) {
    fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
  std::unique_ptr<FileBlock> file = Parser::Parse(std::move(tokens), &err);
  if (!file) {
    fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
  if (!WriteRes(*file.get(), &err)) {
    fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
}
