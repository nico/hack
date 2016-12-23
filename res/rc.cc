/*
clang++ -std=c++11 -o rc rc.cc -Wall -Wno-c++11-narrowing
./rc < foo.rc

A sketch of a reimplemenation of rc.exe, for research purposes.
Doesn't do any preprocessing for now.
*/

#include <experimental/string_view>
#include <iostream>
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
    kLeftBrace,    // {
    kRightBrace,   // }
    kDirective,    // #foo
    kLineComment,  // //foo
    kStarComment,  // /* foo */
  };

  Token(Type type, std::experimental::string_view value)
      : type_(type), value_(value) {}

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
    return Token::kLeftBrace;
  if (next_char == '}')
    return Token::kRightBrace;

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
    case Token::kLeftBrace:
    case Token::kRightBrace:
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
        if (is_star && !at_end() && cur_char() == '/')
          break;
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

class IconResource;

class Visitor {
 public:
  virtual bool VisitIconResource(const IconResource* r) = 0;

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

class IconResource : public FileResource {
 public:
  IconResource(uint16_t name, std::experimental::string_view path)
      : FileResource(name, path) {}

  bool Visit(Visitor* v) const override { return v->VisitIconResource(this); }
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
  std::unique_ptr<Resource> ParseResource();
  std::unique_ptr<FileBlock> ParseFile(std::string* err);

  const Token& Consume();

  // Call this only if !at_end().
  const Token& cur_token() const { return tokens_[cur_]; }

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

const Token& Parser::Consume() {
  return tokens_[cur_++];
}

std::unique_ptr<Resource> Parser::ParseResource() {
  const Token& id = Consume();  // Either int or ident.

  if (at_end()) {
    err_ = "expected resource name";
    return std::unique_ptr<Resource>();
  }

  if (id.type_ != Token::kInt) {
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

  // FIXME: case-insensitive
  if (type.type_ == Token::kIdentifier && type.value_ == "ICON") {
    const Token& path = Consume();
    // XXX pass id
    if (path.type_ == Token::kString) {
      std::experimental::string_view path_val = path.value_;
      // The literal includes quotes, strip them.
      return std::unique_ptr<Resource>(
          new IconResource(id_num, path_val.substr(1, path_val.size() - 2)));
    }
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

  bool VisitIconResource(const IconResource* r) override;

 private:
  FILE* out_;
  std::string* err_;
  int next_icon_id_;
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

bool SerializationVisitor::VisitIconResource(const IconResource* r) {
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
    uint16_t type;      // must be 1 for icons (2 for cursors)
    uint16_t count;
  } dir;

  dir.reserved = read_little_short(f);
  if (dir.reserved != 0) {
    *err_ = "reserved not 0 in " + r->path().to_string();
    return false;
  }
  dir.type = read_little_short(f);
  if (dir.type != 1) {
    *err_ = "type not 1 in " + r->path().to_string();
    return false;
  }
  dir.count = read_little_short(f);

  // Read entries.
  struct IconEntry {
    uint8_t width;
    uint8_t height;
    uint8_t num_colors;   // 0 if more than 256 colors
    uint8_t reserved;     // must be 0
    uint16_t num_planes;  // Color Planes
    uint16_t bpp;         // bits per pixel
    uint32_t data_size;
    // This is a 4-byte data_offset in a .ico file, but a 2-byte id in
    // the kRT_GROUP_ICON resource.
    uint32_t data_offset_id;
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
    entry.data_offset_id = read_little_long(f);
  }

  // For each entry, write a kRT_ICON resource.
  for (IconEntry& entry : entries) {
    // XXX padding?
    write_little_long(out_, entry.data_size);
    write_little_long(out_, 0x20);  // header size
    write_numeric_type(out_, kRT_ICON);
    write_numeric_name(out_, next_icon_id_);
    write_little_long(out_, 0);        // data version
    write_little_short(out_, 0x1010);  // memory flags XXX
    write_little_short(out_, 1033);    // language id XXX
    write_little_long(out_, 0);     // version
    write_little_long(out_, 0);     // characteristics

    fseek(f, entry.data_offset_id, SEEK_SET);
    copy(out_, f, entry.data_size);

    entry.data_offset_id = next_icon_id_++;
  }

  // Write final kRT_GROUP_ICON resource.
  uint16_t group_size = 6 + dir.count * 14;   // 1 IconDir + n IconEntry
  uint8_t group_padding = ((4 - (group_size & 3)) & 3);  // DWORD-align.
  write_little_long(out_, group_size);
  write_little_long(out_, 0x20);  // header size
  write_numeric_type(out_, kRT_GROUP_ICON);
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
    fputc(entry.width, out_);
    fputc(entry.height, out_);
    fputc(entry.num_colors, out_);
    fputc(entry.reserved, out_);
    write_little_short(out_, entry.num_planes);
    write_little_short(out_, entry.bpp);
    write_little_long(out_, entry.data_size);
    write_little_short(out_, entry.data_offset_id);
  }
  //fwrite(":)", 1, group_padding, out_);
  fwrite("\0\0", 1, group_padding, out_);

  return true;
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
