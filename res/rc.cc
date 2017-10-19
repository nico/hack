/*
  clang++ -std=c++14 -o rc rc.cc -Wall -Wno-c++11-narrowing
or
  cl rc.cc /EHsc /wd4838 /nologo shlwapi.lib
./rc < foo.rc

A sketch of a reimplemenation of rc.exe, for research purposes.
Doesn't do any preprocessing for now.

For files that this successfully processes, the goal is that the output is
bit-for-bit equal to what Microsoft rc.exe produces.  It's ok if this program
rejects some inputs that Microsoft rc.exe accepts.

Missing for chromium:
- real string literal parser (L"\0")
- preprocessor (but see pptest next to this; `pptest file | rc` kinda works)

Also missing, but not yet for chromium:
- #pragma code_page()
- FONT
- MENUITEM SEPARATOR
- MENUEX (including int expression parse/eval)
- MESSAGETABLE
- inline block data for DIALOG controls
- rc.exe probably supports int exprs in more places (see all the IntValue calls)
- mem attrs (PRELOAD LOADONCALL FIXED MOVEABLE DISCARDABLE PURE IMPURE SHARED
  NONSHARED) on all resources. all no-ops nowadays, but sometimes in rc files.
  https://msdn.microsoft.com/en-us/library/windows/desktop/aa380908(v=vs.85).aspx
- CHARACTERISTICS LANGUAGE VERSION for ACCELERATORS, DIALOG(EX), MENU(EX),
  RCDATA, or STRINGTABLE (and custom elts?)
- MUI, https://msdn.microsoft.com/en-us/library/windows/desktop/ee264325(v=vs.85).aspx

Unicode handling:
MS rc.exe allows either UTF-16LE input (FIXME: test non-BMP files) or codepage'd
inputs.  This program either accepts UTF-16 or UTF-8 input.  UTF-16 is converted
to UTF-8 at the start.  If the input wasn't UTF-16, this program will error
out on bytes > 127 in string literals so that real codepage'd inputs are
rejected rather than miscompiled.

Warning ideas:
- duplicate IDs in a dialog
- duplicate names for a given resource type with the same LANGUAGE
- VERSIONINFO with ID != 1
- non-numeric resource names
*/
#if defined(__linux__)
// Work around broken older libstdc++s, http://llvm.org/PR31562
extern char *gets (char *__s) __attribute__ ((__deprecated__));
#endif

#include <algorithm>
#include <assert.h>
#include <iostream>
#include <list>
#include <locale>
#include <limits.h>
#include <map>
#include <memory>
#include <stdint.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define GCC_VERSION (__GNUC__ * 10000 \
                     + __GNUC_MINOR__ * 100 \
                     + __GNUC_PATCHLEVEL__)
#if !(defined(__linux__) && GCC_VERSION < 50100)
// No codecvt before libstdc++5.1, need to roll our own utf8<->utf16 routines.
#include <codecvt>
#endif

#if defined(_MSC_VER)
#include <direct.h>
#include <io.h>
#include <fcntl.h>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>  // GetFullPathName
#include <shlwapi.h>  // PathIsRelative
#else
#include <unistd.h>
#endif

#if !defined(_MSC_VER) && !defined(__linux__)
#include <experimental/optional>
#include <experimental/string_view>
#else
namespace std {
namespace experimental {
inline namespace fundamentals_v1 {
class string_view {
  const char* str_;
  size_t size_;
 public:
  string_view() : size_(0) {}
  string_view(const char* str) : str_(str), size_(strlen(str)) {}
  string_view(const char* str, size_t size) : str_(str), size_(size) {}
  string_view(const std::string& s) : str_(s.data()), size_(s.size()) {}
  size_t size() const { return size_; }
  char operator[](size_t i) const { return str_[i]; }
  bool operator==(const string_view& rhs) const {
    return size_ == rhs.size_ && strncmp(str_, rhs.str_, size_) == 0;
  }
  bool operator!=(const string_view& rhs) const { return !(*this == rhs); }
  string_view substr(size_t start, size_t len = -1) const {
    return string_view(str_ + start, std::min(len, size() - start));
  }
  std::string to_string() const { return std::string(str_, size_); }
  const char* data() const { return str_; }
  bool empty() const { return size_ == 0; }
  const char* begin() const { return data(); }
  const char* end() const { return begin() + size(); }
  size_t find(char c) {
    const char* found = (const char*)memchr(str_, c, size_);
    return found ? found - str_ : -1;
  }
  static constexpr size_t npos = -1;
};
template<class T>
class optional {
  bool has_val = false;
  T val;
 public:
  optional() = default;
  optional(const T& t) { val = t; has_val = true; }
  void operator=(const T& t) { val = t; has_val = true; }
  operator bool() const { return has_val; }
  T* operator->() { return &val; }
  const T* operator->() const { return &val; }
  T& operator*() { return val; }
  const T& operator*() const { return val; }
};
}  // fundamentals_v1
}  // experimental
template<> struct hash<experimental::string_view> {
  size_t operator()(const experimental::string_view& x) const {
    size_t hash = 5381;  // djb2
    for (char c : x)
      hash = 33 * hash + c;
    return hash;
  }
};
#if defined(__linux__) && GCC_VERSION <= 40800
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
#endif
}
#endif
// Like toupper(), but locale-independent.
int ascii_toupper(int c) {
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 'A';
  return c;
}
bool IsEqualAsciiUppercaseChar(char a, char b) {
  return ascii_toupper(a) == ascii_toupper(b);
}
bool IsEqualAsciiUppercase(std::experimental::string_view a,
                       std::experimental::string_view b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), IsEqualAsciiUppercaseChar);
}
size_t CaseInsensitiveHash(std::experimental::string_view x) {
  size_t hash = 5381;  // djb2
  for (char c : x)
    hash = 33 * hash + ascii_toupper(c);
  return hash;
}
template <class T>
using CaseInsensitiveStringMap = std::unordered_map<
    std::experimental::string_view, T,
    size_t(*)(std::experimental::string_view),
    bool(*)(std::experimental::string_view, std::experimental::string_view)>;
using CaseInsensitiveStringSet = std::unordered_set<
    std::experimental::string_view,
    size_t(*)(std::experimental::string_view),
    bool(*)(std::experimental::string_view, std::experimental::string_view)>;

bool hexchar(int c, int* nibble) {
  if (c >= '0' && c <= '9') {
    *nibble = c - '0';
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    *nibble = c - 'a' + 10;
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    *nibble = c - 'A' + 10;
    return true;
  }
  return false;
}

typedef uint8_t UTF8;
#if !defined(_MSC_VER) && !(defined(__linux__) && GCC_VERSION < 50100)
bool isLegalUTF8String(const UTF8 *source, const UTF8 *sourceEnd) {
  // Validate that the input if valid utf-8.
  std::mbstate_t mbstate;
  return std::codecvt_utf8<char32_t>().length(
      mbstate, (char*)source, (char*)sourceEnd,
      std::numeric_limits<size_t>::max())
      == sourceEnd - source;
}
#else
// codecvt_utf8::length() is broken with MSVC, and doesn't exist for gcc
// older than 5.1.. Use code from the Unicode consortium there. License
// below applies to contents of this #else block only.
/*
 * Copyright 2001-2004 Unicode, Inc.
 *
 * Disclaimer
 *
 * This source code is provided as is by Unicode, Inc. No claims are
 * made as to fitness for any particular purpose. No warranties of any
 * kind are expressed or implied. The recipient agrees to determine
 * applicability of information provided. If this file has been
 * purchased on magnetic or optical media from Unicode, Inc., the
 * sole remedy for any claim will be exchange of defective media
 * within 90 days of receipt.
 *
 * Limitations on Rights to Redistribute This Code
 *
 * Unicode, Inc. hereby grants the right to freely use the information
 * supplied in this file in the creation of products supporting the
 * Unicode Standard, and to make copies of this file in any form
 * for internal or external distribution as long as this notice
 * remains attached.
 */
static const char trailingBytesForUTF8[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};
static bool isLegalUTF8(const UTF8 *source, int length) {
  UTF8 a;
  const UTF8 *srcptr = source+length;
  switch (length) {
    default: return false;
      /* Everything else falls through when "true"... */
    case 4: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return false;
    case 3: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return false;
    case 2: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return false;

      switch (*source) {
        /* no fall-through in this inner switch */
        case 0xE0: if (a < 0xA0) return false; break;
        case 0xED: if (a > 0x9F) return false; break;
        case 0xF0: if (a < 0x90) return false; break;
        case 0xF4: if (a > 0x8F) return false; break;
        default:   if (a < 0x80) return false;
      }

    case 1: if (*source >= 0x80 && *source < 0xC2) return false;
  }
  if (*source > 0xF4) return false;
  return true;
}
bool isLegalUTF8String(const UTF8 *source, const UTF8 *sourceEnd) {
  while (source != sourceEnd) {
    int length = trailingBytesForUTF8[*source] + 1;
    if (length > sourceEnd - source || !isLegalUTF8(source, length))
      return false;
    source += length;
  }
  return true;
}
#endif

#if defined(__linux__) && GCC_VERSION < 50100
// No codecvt before libstdc++5.1, need to roll our own utf8<->utf16 routines.
// Use code from the Unicode consortium there. License below applies to
// contents of this #if block only.
/*
 * Copyright 2001-2004 Unicode, Inc.
 *
 * Disclaimer
 *
 * This source code is provided as is by Unicode, Inc. No claims are
 * made as to fitness for any particular purpose. No warranties of any
 * kind are expressed or implied. The recipient agrees to determine
 * applicability of information provided. If this file has been
 * purchased on magnetic or optical media from Unicode, Inc., the
 * sole remedy for any claim will be exchange of defective media
 * within 90 days of receipt.
 *
 * Limitations on Rights to Redistribute This Code
 *
 * Unicode, Inc. hereby grants the right to freely use the information
 * supplied in this file in the creation of products supporting the
 * Unicode Standard, and to make copies of this file in any form
 * for internal or external distribution as long as this notice
 * remains attached.
 */
typedef uint16_t UTF16;
typedef uint32_t UTF32;

#define UNI_REPLACEMENT_CHAR (UTF32)0x0000FFFD
#define UNI_MAX_BMP (UTF32)0x0000FFFF
#define UNI_MAX_UTF16 (UTF32)0x0010FFFF

typedef enum {
	conversionOK, 		/* conversion successful */
	sourceExhausted,	/* partial character in source, but hit end */
	targetExhausted,	/* insuff. room in target for conversion */
	sourceIllegal		/* source sequence is illegal/malformed */
} ConversionResult;

typedef enum {
	strictConversion = 0,
	lenientConversion
} ConversionFlags;

ConversionResult ConvertUTF8toUTF16 (
		const UTF8** sourceStart, const UTF8* sourceEnd,
		UTF16** targetStart, UTF16* targetEnd, ConversionFlags flags);

ConversionResult ConvertUTF16toUTF8 (
		const UTF16** sourceStart, const UTF16* sourceEnd,
		UTF8** targetStart, UTF8* targetEnd, ConversionFlags flags);

static const int halfShift  = 10; /* used for shifting by 10 bits */

static const UTF32 halfBase = 0x0010000UL;
static const UTF32 halfMask = 0x3FFUL;

#define UNI_SUR_HIGH_START  (UTF32)0xD800
#define UNI_SUR_HIGH_END    (UTF32)0xDBFF
#define UNI_SUR_LOW_START   (UTF32)0xDC00
#define UNI_SUR_LOW_END     (UTF32)0xDFFF

/* --------------------------------------------------------------------- */

/*
 * Magic values subtracted from a buffer value during UTF8 conversion.
 * This table contains as many values as there might be trailing bytes
 * in a UTF-8 sequence.
 */
static const UTF32 offsetsFromUTF8[6] = { 0x00000000UL, 0x00003080UL, 0x000E2080UL,
		     0x03C82080UL, 0xFA082080UL, 0x82082080UL };

/*
 * Once the bits are split out into bytes of UTF-8, this is a mask OR-ed
 * into the first byte, depending on how many bytes follow.  There are
 * as many entries in this table as there are UTF-8 sequence types.
 * (I.e., one byte sequence, two byte... etc.). Remember that sequencs
 * for *legal* UTF-8 will be 4 or fewer bytes total.
 */
static const UTF8 firstByteMark[7] = { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };

/* The interface converts a whole buffer to avoid function-call overhead.
 * Constants have been gathered. Loops & conditionals have been removed as
 * much as possible for efficiency, in favor of drop-through switches.
 * (See "Note A" at the bottom of the file for equivalent code.)
 * If your compiler supports it, the "isLegalUTF8" call can be turned
 * into an inline function.
 */

/* --------------------------------------------------------------------- */

ConversionResult ConvertUTF16toUTF8 (
	const UTF16** sourceStart, const UTF16* sourceEnd,
	UTF8** targetStart, UTF8* targetEnd, ConversionFlags flags) {
    ConversionResult result = conversionOK;
    const UTF16* source = *sourceStart;
    UTF8* target = *targetStart;
    while (source < sourceEnd) {
	UTF32 ch;
	unsigned short bytesToWrite = 0;
	const UTF32 byteMask = 0xBF;
	const UTF32 byteMark = 0x80;
	const UTF16* oldSource = source; /* In case we have to back up because of target overflow. */
	ch = *source++;
	/* If we have a surrogate pair, convert to UTF32 first. */
	if (ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_HIGH_END) {
	    /* If the 16 bits following the high surrogate are in the source buffer... */
	    if (source < sourceEnd) {
		UTF32 ch2 = *source;
		/* If it's a low surrogate, convert to UTF32. */
		if (ch2 >= UNI_SUR_LOW_START && ch2 <= UNI_SUR_LOW_END) {
		    ch = ((ch - UNI_SUR_HIGH_START) << halfShift)
			+ (ch2 - UNI_SUR_LOW_START) + halfBase;
		    ++source;
		} else if (flags == strictConversion) { /* it's an unpaired high surrogate */
		    --source; /* return to the illegal value itself */
		    result = sourceIllegal;
		    break;
		}
	    } else { /* We don't have the 16 bits following the high surrogate. */
		--source; /* return to the high surrogate */
		result = sourceExhausted;
		break;
	    }
	} else if (flags == strictConversion) {
	    /* UTF-16 surrogate values are illegal in UTF-32 */
	    if (ch >= UNI_SUR_LOW_START && ch <= UNI_SUR_LOW_END) {
		--source; /* return to the illegal value itself */
		result = sourceIllegal;
		break;
	    }
	}
	/* Figure out how many bytes the result will require */
	if (ch < (UTF32)0x80) {	     bytesToWrite = 1;
	} else if (ch < (UTF32)0x800) {     bytesToWrite = 2;
	} else if (ch < (UTF32)0x10000) {   bytesToWrite = 3;
	} else if (ch < (UTF32)0x110000) {  bytesToWrite = 4;
	} else {			    bytesToWrite = 3;
					    ch = UNI_REPLACEMENT_CHAR;
	}

	target += bytesToWrite;
	if (target > targetEnd) {
	    source = oldSource; /* Back up source pointer! */
	    target -= bytesToWrite; result = targetExhausted; break;
	}
	switch (bytesToWrite) { /* note: everything falls through. */
	    case 4: *--target = (UTF8)((ch | byteMark) & byteMask); ch >>= 6;
	    case 3: *--target = (UTF8)((ch | byteMark) & byteMask); ch >>= 6;
	    case 2: *--target = (UTF8)((ch | byteMark) & byteMask); ch >>= 6;
	    case 1: *--target =  (UTF8)(ch | firstByteMark[bytesToWrite]);
	}
	target += bytesToWrite;
    }
    *sourceStart = source;
    *targetStart = target;
    return result;
}

/* --------------------------------------------------------------------- */

ConversionResult ConvertUTF8toUTF16 (
	const UTF8** sourceStart, const UTF8* sourceEnd,
	UTF16** targetStart, UTF16* targetEnd, ConversionFlags flags) {
    ConversionResult result = conversionOK;
    const UTF8* source = *sourceStart;
    UTF16* target = *targetStart;
    while (source < sourceEnd) {
	UTF32 ch = 0;
	unsigned short extraBytesToRead = trailingBytesForUTF8[*source];
	if (source + extraBytesToRead >= sourceEnd) {
	    result = sourceExhausted; break;
	}
	/* Do this check whether lenient or strict */
	if (! isLegalUTF8(source, extraBytesToRead+1)) {
	    result = sourceIllegal;
	    break;
	}
	/*
	 * The cases all fall through. See "Note A" below.
	 */
	switch (extraBytesToRead) {
	    case 5: ch += *source++; ch <<= 6; /* remember, illegal UTF-8 */
	    case 4: ch += *source++; ch <<= 6; /* remember, illegal UTF-8 */
	    case 3: ch += *source++; ch <<= 6;
	    case 2: ch += *source++; ch <<= 6;
	    case 1: ch += *source++; ch <<= 6;
	    case 0: ch += *source++;
	}
	ch -= offsetsFromUTF8[extraBytesToRead];

	if (target >= targetEnd) {
	    source -= (extraBytesToRead+1); /* Back up source pointer! */
	    result = targetExhausted; break;
	}
	if (ch <= UNI_MAX_BMP) { /* Target is a character <= 0xFFFF */
	    /* UTF-16 surrogate values are illegal in UTF-32 */
	    if (ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_LOW_END) {
		if (flags == strictConversion) {
		    source -= (extraBytesToRead+1); /* return to the illegal value itself */
		    result = sourceIllegal;
		    break;
		} else {
		    *target++ = UNI_REPLACEMENT_CHAR;
		}
	    } else {
		*target++ = (UTF16)ch; /* normal case */
	    }
	} else if (ch > UNI_MAX_UTF16) {
	    if (flags == strictConversion) {
		result = sourceIllegal;
		source -= (extraBytesToRead+1); /* return to the start */
		break; /* Bail out; shouldn't continue */
	    } else {
		*target++ = UNI_REPLACEMENT_CHAR;
	    }
	} else {
	    /* target is a character in range 0xFFFF - 0x10FFFF. */
	    if (target + 1 >= targetEnd) {
		source -= (extraBytesToRead+1); /* Back up source pointer! */
		result = targetExhausted; break;
	    }
	    ch -= halfBase;
	    *target++ = (UTF16)((ch >> halfShift) + UNI_SUR_HIGH_START);
	    *target++ = (UTF16)((ch & halfMask) + UNI_SUR_LOW_START);
	}
    }
    *sourceStart = source;
    *targetStart = target;
    return result;
}

#else
#endif

#if _MSC_VER == 1900 || _MSC_VER == 1911
    // lol msvc: http://stackoverflow.com/questions/32055357/visual-studio-c-2015-stdcodecvt-with-char16-t-or-char32-t
    using Char16 = int16_t;
#else
    using Char16 = char16_t;
#endif
typedef std::basic_string<Char16> C16string;

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
    kBeginBlock,   // { or BEGIN (rc.exe accepts `{ .. END`)
    kEndBlock,     // } or END

    kPlus,        // +
    kMinus,       // -
    kPipe,        // |
    kAmp,         // &
    kTilde,       // ~
    kLeftParen,   // (
    kRightParen,  // )

    kDirective,    // #foo
    kLineComment,  // //foo
    kStarComment,  // /* foo */
  };

  Token(Type type, std::experimental::string_view value)
      : type_(type), value_(value) {}

  Type type() const { return type_; }

  // Only valid if type() == kInt.
  uint32_t IntValue(bool* is_32 = nullptr) const;

  Type type_;
  std::experimental::string_view value_;
};

uint32_t Token::IntValue(bool* is_32) const {
  // C++11 has std::stoll, C++17 will likey have string_view, but there's
  // no std::stoll overload taking a string_view. C has atoi / strtol, but
  // both assume \0-termination.  Also no std::stoul.
  size_t idx;
  int64_t val;
  if (value_.size() >= 2 && value_[0] == '0' &&
      ascii_toupper(value_[1]) == 'X') {
    val = std::stoll(value_.substr(2).to_string(), &idx, 16);
    idx += 2;
  } else if (value_.size() >= 2 && value_[0] == '0' &&
             ascii_toupper(value_[1]) == 'O') {
    val = std::stoll(value_.substr(2).to_string(), &idx, 8);
    idx += 2;
  } else
    val = std::stoll(value_.to_string(), &idx, 10);

  if (is_32 && idx < value_.size() && ascii_toupper(value_[idx]) == 'L')
    *is_32 = true;
  return (uint32_t)(uint64_t)val;;
}

class Tokenizer {
 public:
  static std::vector<Token> Tokenize(std::experimental::string_view source,
                                     std::string* err);

  static bool IsDigit(char c);
  static bool IsDigitContinuingChar(char c);
  static bool IsIdentifierFirstChar(char c);
  static bool IsIdentifierContinuingChar(char c);
  static bool IsWhitespace(char c);

 private:
  Tokenizer(std::experimental::string_view input) : input_(input), cur_(0) {}
  std::vector<Token> Run(std::string* err);

  void Advance() { ++cur_; }  // Must only be called if not at_end().
  void AdvanceToNextToken();
  Token::Type ClassifyCurrent() const;
  void AdvanceToEndOfToken(Token::Type type);

  bool FindStringTerminator(char quote_char);
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
  // Skip optional UTF-8 BOM.
  if (input_.size() >= 3 && uint8_t(input_[0]) == 0xef &&
      uint8_t(input_[1]) == 0xbb && uint8_t(input_[2]) == 0xbf) {
    cur_ = 3;
  }

  while (!done()) {
    AdvanceToNextToken();
    if (done())
      break;
    Token::Type type = ClassifyCurrent();
    if (type == Token::kInvalid) {
      err_ = "invalid token around " + input_.substr(cur_, 20).to_string() +
             ", utf-8 byte position " + std::to_string(cur_);
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
      if (IsEqualAsciiUppercase(token_value, "BEGIN"))
        type = Token::kBeginBlock;
      else if (IsEqualAsciiUppercase(token_value, "END"))
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
bool Tokenizer::IsDigitContinuingChar(char c) {
  // FIXME: it feels like rc.exe just allows most non-whitespace things here
  // (but not + - ~).
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F') || c == 'l' || c == 'L' || c == 'x' ||
         c == 'X' || c == 'o' || c == 'O';
}

// static
bool Tokenizer::IsIdentifierFirstChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

// static
bool Tokenizer::IsIdentifierContinuingChar(char c) {
  // FIXME: for '/', need to strip comments from middle of identifier
  return !IsWhitespace(c) && c != ',';
}

// static
bool Tokenizer::IsWhitespace(char c) {
  // Note that tab (0x09), vertical tab (0x0B), and formfeed (0x0C) are illegal.
  return c == '\t' || c == '\n' || c == '\r' || c == ' ';
}

bool Tokenizer::FindStringTerminator(char quote_char) {
  if (cur_char() != quote_char)
    return false;

  // Check for escaping. "" is not a string terminator, but """ is. Count
  // the number of preceeding quotes. \" is a string terminator (but \"" isn't).
  int num_inner_quotes = 0;
  while (cur_ + 1 < input_.size() && input_[cur_ + 1] == quote_char) {
    Advance();
    num_inner_quotes++;
  }

  // Even quotes mean that they were escaping each other and don't count
  // as terminating this string.
  return (num_inner_quotes % 2) == 0;
}

bool Tokenizer::IsCurrentNewline() const {
  return cur_char() == '\n';
}

void Tokenizer::AdvanceToNextToken() {
  while (!at_end() && IsWhitespace(cur_char()))
    Advance();
}

Token::Type Tokenizer::ClassifyCurrent() const {
  char next_char = cur_char();
  if (IsDigit(next_char))
    return Token::kInt;
  if (next_char == '"' ||
      (ascii_toupper(next_char) == 'L' && cur_ + 1 < input_.size() &&
       input_[cur_ + 1] == '"'))
    return Token::kString;

  if (IsIdentifierFirstChar(next_char))
    return Token::kIdentifier;

  if (next_char == ',')
    return Token::kComma;
  if (next_char == '{')
    return Token::kBeginBlock;
  if (next_char == '}')
    return Token::kEndBlock;

  if (next_char == '+')
    return Token::kPlus;
  if (next_char == '-')
    return Token::kMinus;
  if (next_char == '|')
    return Token::kPipe;
  if (next_char == '&')
    return Token::kAmp;
  if (next_char == '~')
    return Token::kTilde;
  if (next_char == '(')
    return Token::kLeftParen;
  if (next_char == ')')
    return Token::kRightParen;

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
      } while (!at_end() && IsDigitContinuingChar(cur_char()));
      break;

    case Token::kString: {
      char initial = cur_char();
      if (ascii_toupper(initial) == 'L') {
        Advance();
        initial = cur_char();
      }
      Advance();  // Advance past initial "
      for (;;) {
        if (at_end()) {
          err_ = "Unterminated string literal.";
          break;
        }
        if (FindStringTerminator(initial)) {
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
    case Token::kBeginBlock:
    case Token::kEndBlock:
    case Token::kPlus:
    case Token::kMinus:
    case Token::kPipe:
    case Token::kAmp:
    case Token::kTilde:
    case Token::kLeftParen:
    case Token::kRightParen:
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

//////////////////////////////////////////////////////////////////////////////
// AST

class LanguageResource;
class CursorResource;
class BitmapResource;
class IconResource;
class MenuResource;
class DialogResource;
class StringtableResource;
class AcceleratorsResource;
class RcdataResource;
class VersioninfoResource;
class DlgincludeResource;
class HtmlResource;
class UserDefinedResource;

class Visitor {
 public:
  virtual bool VisitLanguageResource(const LanguageResource* r) = 0;
  virtual bool VisitCursorResource(const CursorResource* r) = 0;
  virtual bool VisitBitmapResource(const BitmapResource* r) = 0;
  virtual bool VisitIconResource(const IconResource* r) = 0;
  virtual bool VisitMenuResource(const MenuResource* r) = 0;
  virtual bool VisitDialogResource(const DialogResource* r) = 0;
  virtual bool VisitStringtableResource(const StringtableResource* r) = 0;
  virtual bool VisitAcceleratorsResource(const AcceleratorsResource* r) = 0;
  virtual bool VisitRcdataResource(const RcdataResource* r) = 0;
  virtual bool VisitVersioninfoResource(const VersioninfoResource* r) = 0;
  virtual bool VisitDlgincludeResource(const DlgincludeResource* r) = 0;
  virtual bool VisitHtmlResource(const HtmlResource* r) = 0;
  virtual bool VisitUserDefinedResource(const UserDefinedResource* r) = 0;

 protected:
  ~Visitor() {}
};

// Every resource's type or name can be a uint16_t or an utf-16 string.
// The uint16_t looks like (0xffff value) when serialized, the utf-16 string
// is just a \0-terminated utf-16le string.  This class represents that concept.
// (Also used in a few other places, e.g. DIALOG's CLASS, MENU, etc).
class IntOrStringName {
 public:
  static IntOrStringName MakeInt(uint16_t val) {
    IntOrStringName r{0xffff, val};
    return r;
  }

  // This takes an UTF16-encoded string.
  static IntOrStringName MakeStringUTF16(const C16string& val) {
    IntOrStringName r;
    for (size_t j = 0; j < val.size(); ++j)
      r.data_.push_back(val[j]);
    r.data_.push_back(0);  // \0-terminate.
    return r;
  }

  // Like MakeStringUTF16 but also converts to upper case.
  static IntOrStringName MakeUpperStringUTF16(const C16string& val) {
    IntOrStringName r;
    for (size_t j = 0; j < val.size(); ++j)
      r.data_.push_back(ascii_toupper(val[j]));
    r.data_.push_back(0);  // \0-terminate.
    return r;
  }

  static IntOrStringName MakeEmpty() {
    IntOrStringName r{0};  // Note: Picks std::initializer_list ctor with 1 elt
    return r;
  }

  bool is_empty() const { return data_[0] == 0; }
  size_t serialized_size() const { return data_.size() * 2; }
  bool write(FILE* f) const {
    return fwrite(data_.data(), 2, data_.size(), f) == data_.size();
  }

 private:
  IntOrStringName() {}
  IntOrStringName(std::initializer_list<uint16_t> init) : data_(init) {}
  std::vector<uint16_t> data_;
};

// Root of AST class hierarchy.
class Resource {
 public:
  virtual ~Resource() {}

  Resource(IntOrStringName name) : name_(name) {}
  virtual bool Visit(Visitor* v) const = 0;

  const IntOrStringName& name() const { return name_; }

 private:
  IntOrStringName name_;
};

class LanguageResource final : public Resource {
 public:
  struct Language {
    uint8_t language;
    uint8_t sublanguage;
    uint16_t as_uint16() const { return language | sublanguage << 10; }
    bool operator<(const Language& rhs) const {
      return std::tie(language, sublanguage) <
             std::tie(rhs.language, rhs.sublanguage);
    }
  };

  // LANGUAGE doesn't get emitted as its own entry to the .res file, instead
  // it modifies the language field in all the other emitted resources.  So
  // it doesn't have an id itself.
  LanguageResource(uint8_t language, uint8_t sublanguage)
      : Resource(IntOrStringName::MakeEmpty()),
        language_{language, sublanguage} {}

  bool Visit(Visitor* v) const override {
    return v->VisitLanguageResource(this);
  }

  Language language() const { return language_; }

 private:
  Language language_;
};

class FileResource : public Resource {
 public:
  FileResource(IntOrStringName name, std::experimental::string_view path)
      : Resource(name), path_(path) {}

  std::experimental::string_view path() const { return path_; }

 private:
  std::experimental::string_view path_;
};

class CursorResource final : public FileResource {
 public:
  CursorResource(IntOrStringName name, std::experimental::string_view path)
      : FileResource(name, path) {}

  bool Visit(Visitor* v) const override { return v->VisitCursorResource(this); }
};

class BitmapResource final : public FileResource {
 public:
  BitmapResource(IntOrStringName name, std::experimental::string_view path)
      : FileResource(name, path) {}

  bool Visit(Visitor* v) const override { return v->VisitBitmapResource(this); }
};

class IconResource final : public FileResource {
 public:
  IconResource(IntOrStringName name, std::experimental::string_view path)
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

class MenuResource final : public Resource {
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

  MenuResource(IntOrStringName name, SubmenuEntryData entries)
      : Resource(name), entries_(std::move(entries)) {}


  bool Visit(Visitor* v) const override { return v->VisitMenuResource(this); }

  SubmenuEntryData entries_;
};

class DialogResource final : public Resource {
 public:
  enum DialogKind { kDialog, kDialogEx };

  struct FontInfo {
    uint16_t size;
    std::experimental::string_view name;
    // The rest only set if kind == kDialogEx:
    uint16_t weight;
    uint8_t italic;
    uint8_t charset;
  };

  struct Control {
    Control()  // FIXME: remove, probably
        : help_id(0),
          style(0),
          exstyle(0),
          x(0),
          y(0),
          w(0),
          h(0),
          id(0),
          clazz(IntOrStringName::MakeEmpty()),
          text(IntOrStringName::MakeStringUTF16(C16string())) {}

    uint32_t help_id;  // only set if kind == kDialogEx

    uint32_t style;
    uint32_t exstyle;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    uint32_t id;  // 32 bit in DIALOGEX, 16 bit in DIALOG
    IntOrStringName clazz;

    // For controls that have no text, an empty string (\0) is written to .res.
    IntOrStringName text;

    // There's another uint16_t field here that seems to be always 0.

    uint16_t serialized_size(bool is_last, DialogKind kind) const {
      uint16_t size = 2*4 + 5*2; // style, exstyle, x, y, w, h, id
      size += clazz.serialized_size();
      size += text.serialized_size();
      size += 2;  // FIXME: Mystery 0 field after the control text.
      if (kind == kDialogEx)
        size += 4 + 2;  // help_id and 32-bit id.
      if (!is_last && size % 4)
        size += 2;  // pad to uint32_t
      return size;
    }
  };

  DialogResource(IntOrStringName name,
                 DialogKind kind,
                 uint16_t x,
                 uint16_t y,
                 uint16_t w,
                 uint16_t h,
                 uint32_t help_id,
                 std::experimental::string_view caption,
                 IntOrStringName clazz,
                 uint32_t exstyle,
                 std::experimental::optional<FontInfo> font,
                 IntOrStringName menu,
                 std::experimental::optional<uint32_t> style,
                 std::vector<Control> controls)
      : Resource(name),
        kind(kind),
        x(x),
        y(y),
        w(w),
        h(h),
        help_id(help_id),
        caption(caption),
        clazz(std::move(clazz)),
        exstyle(exstyle),
        font(std::move(font)),
        menu(std::move(menu)),
        style(style),
        controls(std::move(controls)) {}

  bool Visit(Visitor* v) const override { return v->VisitDialogResource(this); }

  DialogKind kind;

  uint16_t x, y, w, h;
  uint32_t help_id;  // only set if kind == kDialogEx

  // Empty if not set. rc.exe also writes no caption for `CAPTION ""`.
  std::experimental::string_view caption;

  // Empty if not set. rc.exe also writes a 0 class for `CLASS ""`.
  IntOrStringName clazz;

  uint32_t exstyle;

  std::experimental::optional<FontInfo> font;

  // This is only empty as default value: Weirdly, for string names, rc
  // includes the quotes, so `MENU ""` produces `""` (with quotes) in the
  // output.
  IntOrStringName menu;

  std::experimental::optional<uint32_t> style;

  std::vector<Control> controls;
};

class StringtableResource final : public Resource {
 public:
  struct Entry {
    uint16_t id;
    std::experimental::string_view value;
  };

  StringtableResource(Entry* entries, size_t num_entries)
      // STRINGTABLEs have no names.
      : Resource(IntOrStringName::MakeInt(0)),
        entries_(entries, entries + num_entries) {}

  bool Visit(Visitor* v) const override {
    return v->VisitStringtableResource(this);
  }

  typedef std::vector<Entry>::const_iterator const_iterator;
  const_iterator begin() const { return entries_.begin(); }
  const_iterator end() const { return entries_.end(); }

 private:
  std::vector<Entry> entries_;
};

enum AcceleratorFlags {
  kAccelVIRTKEY = 1,
  kAccelNOINVERT = 2,
  kAccelSHIFT = 4,
  kAccelCONTROL = 8,
  kAccelALT = 0x10,
  kAccelLASTITEM = 0x80,
};

class AcceleratorsResource final : public Resource {
 public:
  struct Accelerator {
    uint16_t flags;  // Combination of AcceleratorFlags.
    uint16_t key;
    uint16_t id;
    uint16_t pad;  // Seems to be always 0.
  };

  AcceleratorsResource(IntOrStringName name,
                       std::vector<Accelerator> accelerators)
      : Resource(name), accelerators_(std::move(accelerators)) {}

  bool Visit(Visitor* v) const override {
    return v->VisitAcceleratorsResource(this);
  }

  const std::vector<Accelerator>& accelerators() const { return accelerators_; }

 private:
  std::vector<Accelerator> accelerators_;
};

// Either refers to data read from a file, or to data from an inline data
// block/
class FileOrDataResource : public Resource {
 public:
  enum Type { kFile, kData };

  FileOrDataResource(IntOrStringName name,
                     Type type,
                     std::experimental::string_view path,
                     std::vector<uint8_t> data)
      : Resource(name), type_(type), path_(path), data_(std::move(data)) {}

  std::experimental::string_view path() const { return path_; }

  Type type() const { return type_; }
  const std::vector<uint8_t>& data() const { return data_; }

 private:
  Type type_;
  std::experimental::string_view path_;
  std::vector<uint8_t> data_;
};

class RcdataResource final : public FileOrDataResource {
 public:
  RcdataResource(IntOrStringName name, std::experimental::string_view path)
      : FileOrDataResource(name, kFile, path, std::vector<uint8_t>()) {}
  RcdataResource(IntOrStringName name, std::vector<uint8_t> data)
      : FileOrDataResource(name, kData, "", std::move(data)) {}

  bool Visit(Visitor* v) const override { return v->VisitRcdataResource(this); }
};

class VersioninfoResource final : public Resource {
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

  struct InfoData {
    enum Type { kValue, kBlock };
    explicit InfoData(Type type) : type_(type) {}
    Type type_;
  };
  struct Entry {
    Entry(std::experimental::string_view key, std::unique_ptr<InfoData> data)
        : key(key), data(std::move(data)) {}
    std::experimental::string_view key;
    std::unique_ptr<InfoData> data;
  };
  struct ValueData : public InfoData {
    explicit ValueData(uint16_t value_size,
                       bool is_text,
                       std::vector<uint8_t> value)
        : InfoData(kValue),
          value_size(value_size),
          is_text(is_text),
          value(std::move(value)) {}
    // For string VALUEs, this is the number of char16_ts in the string,
    // including the terminating \0. For int VALUEs, this is the byte size of
    // all values. For VALUEs that combine int and string VALUEs, this is the
    // sum of the string value_sizes for the string parts and the int
    // value_sizes for the int parts.  In other words, for mixed VALUEs, this
    // field is completely useless -- rc probably didn't intend to accept
    // mixed VALUEs.
    uint16_t value_size;
    // If this is true, value contains (0-terminated) utf-16 string data,
    // else it contains arbitrary binary data.
    bool is_text;
    std::vector<uint8_t> value;
  };
  struct BlockData : public InfoData {
    BlockData() : InfoData(kBlock) {}
    explicit BlockData(std::vector<std::unique_ptr<Entry>> values)
        : InfoData(kBlock), values(std::move(values)) {}
    std::vector<std::unique_ptr<Entry>> values;
  };

  VersioninfoResource(IntOrStringName name,
                      const FixedInfo& info,
                      BlockData block)
      : Resource(name), fixed_info_(info), block_(std::move(block)) {}

  bool Visit(Visitor* v) const override {
    return v->VisitVersioninfoResource(this);
  }

  FixedInfo fixed_info_;
  BlockData block_;
};

class DlgincludeResource final : public Resource {
 public:
  DlgincludeResource(IntOrStringName name, std::experimental::string_view data)
      : Resource(name), data_(data) {}

  bool Visit(Visitor* v) const override {
    return v->VisitDlgincludeResource(this);
  }

  std::experimental::string_view data() const { return data_; }

 private:
  std::experimental::string_view data_;
};

class HtmlResource final : public FileOrDataResource {
 public:
  HtmlResource(IntOrStringName name, std::experimental::string_view path)
      : FileOrDataResource(name, kFile, path, std::vector<uint8_t>()) {}
  HtmlResource(IntOrStringName name, std::vector<uint8_t> data)
      : FileOrDataResource(name, kData, "", std::move(data)) {}

  bool Visit(Visitor* v) const override { return v->VisitHtmlResource(this); }
};

class UserDefinedResource final : public FileOrDataResource {
 public:
  UserDefinedResource(IntOrStringName type,
                      IntOrStringName name,
                      std::experimental::string_view path)
      : FileOrDataResource(name, kFile, path, std::vector<uint8_t>()),
        type_(type) {}
  UserDefinedResource(IntOrStringName type,
                      IntOrStringName name,
                      std::vector<uint8_t> data)
      : FileOrDataResource(name, kData, "", std::move(data)), type_(type) {}

  bool Visit(Visitor* v) const override {
    return v->VisitUserDefinedResource(this);
  }

  const IntOrStringName& type() const { return type_; }

 private:
  IntOrStringName type_;
};

class FileBlock {
 public:
  std::vector<std::unique_ptr<Resource>> res_;
};

//////////////////////////////////////////////////////////////////////////////
// Internal encoding management.

// Stores the encoding of internal byte strings.  If this is kEncodingUTF8,
// then byte strings are known-valid UTF8 in memory.  If it's
// kEncodingUnknown, then it's in the encoding we got from the file, subject
// to `#pragma code_page()` and crazy things like that -- in that case we just
// give up on values >= 128.
enum InternalEncoding { kEncodingUnknown, kEncodingUTF8 };

bool ToUTF16(C16string* utf16, std::experimental::string_view in,
             InternalEncoding encoding, std::string* err_) {
  if (encoding == kEncodingUTF8) {
    // InternalEncoding is only set to kEncodingUTF8 when we know that all data
    // is valid UTF-8, so this conversion here should never fail.
#if defined(__linux__) && GCC_VERSION < 50100
    std::vector<Char16> buffer(in.size());
    const UTF8* src_start = (UTF8*)in.data();
    UTF16* dst_start = (UTF16*)buffer.data();
    ConversionResult r = ConvertUTF8toUTF16(
        &src_start, (UTF8*)in.data() + in.size(),
        &dst_start, (UTF16*)buffer.data() + buffer.size(),
        strictConversion);
    assert(r == conversionOK); (void)r;
    *utf16 = C16string(buffer.data(), dst_start - (UTF16*)buffer.data());
#else
    std::wstring_convert<std::codecvt_utf8_utf16<Char16>, Char16> convert;
    *utf16 = convert.from_bytes(in.to_string());
#endif
  } else {
    for (size_t j = 0; j < in.size(); ++j) {
      if (in[j] & 0x80) {
        *err_ = "only 7-bit characters supported in non-unicode files";
        return false;
      }
      utf16->push_back(in[j]);
    }
  }
  return true;
}

// String literals can contain escape characters ("foo\tbar"). Most strings
// don't contain these, and in that case a string_view not including the
// start and end quotes can represent the string contents efficiently.
// If there _are_ escape characters, the string_view needs to refer to memory
// containing an actual <tab> character instead of '\' 't'.  This class
// stores those interpreted strings.
class StringStorage {
 public:
  std::experimental::string_view StringContents(
      std::experimental::string_view s);
 private:
  // FIXME: Just std::vector<std::string> might be fine too, but at least
  // with MSVC, vector seems to copy instead of moving strings when it grows,
  // which is bad since the string_views vended by this class refer to fixed
  // memory locations.
  std::vector<std::unique_ptr<std::string>> storage_;
};

std::experimental::string_view StringStorage::StringContents(
      std::experimental::string_view s) {
  // The literal includes quotes, strip them.
  size_t start_skip_count = ascii_toupper(s[0]) == 'L' ? 2 : 1;
  s = s.substr(start_skip_count, s.size() - (start_skip_count + 1));
  if (s.find('"') == s.npos && s.find('\\') == s.npos)
    return s;
  std::unique_ptr<std::string> stored(new std::string);
  stored->reserve(s.size());
  // There are two phases:
  // 1. Collapse "" to "
  // 2. Process \ escapes
  // Do both in one pass.
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c != '\\' || i + 1 == s.size()) {
      stored->push_back(c);
      if (c == '"') {  // "" -> "
        assert(s[i + 1] == '"');
        ++i;
      }
      continue;
    }
    // Handles \-escapes.
    c = s[++i];
    if (c == 'x') {
      c = 0;
      int nibble;
      if (i + 1 < s.size() && hexchar(s[i + 1], &nibble)) {
        c = nibble;
        ++i;
      }
      if (i + 1 < s.size() && hexchar(s[i + 1], &nibble)) {
        c = (c << 4) | nibble;
        ++i;
      }
      // FIXME: In L"" strings, \x takes four nibbles instead of two.
      if (i + 1 < s.size() || c)  // \0 is only added if not at end of string.
        stored->push_back(c);
      continue;
    }
    if (c >= '0' && c <= '7') {
      c = c - '0';
      for (int j = 0;
           j < 2 && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '7';
           ++j, ++i) {
          c = (c << 3) | (s[i + 1] - '0');
      }
      // FIXME: L"" strings?
      if (i + 1 < s.size() || c)  // \0 is only added if not at end of string.
        stored->push_back(c);
      continue;
    }
    switch (c) {
      case 'a': c = '\b'; break;  // Yes, \a in .rc files maps to \b.
      case 'n': c = '\n'; break;
      case 'r': c = '\r'; break;
      case 't': c = '\t'; break;
      case '0': c = '\0'; break;
      case '\\': c = '\\'; break;
      case '"': c = '"'; assert(s[i + 1] == '"'); ++i; break;
      default: stored->push_back('\\');
    }
    stored->push_back(c);
  }
  storage_.push_back(std::move(stored));
  return *storage_.back();
}

//////////////////////////////////////////////////////////////////////////////
// Parser

class Parser {
 public:
  static std::unique_ptr<FileBlock> Parse(std::vector<Token> tokens,
                                          InternalEncoding encoding,
                                          StringStorage* string_storage,
                                          std::string* err);

 private:
  Parser(std::vector<Token> tokens, InternalEncoding encoding,
         StringStorage* string_storage);

  // Parses an expression matching
  //    expr ::= unary_expr {binary_op unary_expr}
  // where
  //    binary_op = "+" "-" "|" "&"
  //    unary_op = "-" "~"  // FIXME: +?
  //    unary_expr ::= unary_op unary_expr | primary_expr
  //    primary_expr ::= int | "(" expr ")"
  // All operators have the same precedence and are evaluated left-to-right,
  // except for parentheses which are evaluated first.
  // FIXME: consider building and storing an AST for the full expression and
  // calling Eval() on the root expression node in the serializer instead.
  bool EvalIntExpression(uint32_t* out, bool* is_32 = nullptr);
  bool EvalIntExpressionUnary(uint32_t* out, bool* is_32);
  bool EvalIntExpressionPrimary(uint32_t* out, bool* is_32);

  // Takes a kString token and returns a string_view for the interpreted
  // contents of the string ("a" -> a, "a""b" -> a"b, "\n" -> ASCII 0xA, etc).
  std::experimental::string_view StringContents(const Token& tok);

  bool ParseVersioninfoData(std::vector<uint8_t>* data,
                            uint16_t* value_size,
                            bool* is_text);
  bool ParseRawData(std::vector<uint8_t>* data);

  std::unique_ptr<LanguageResource> ParseLanguage();
  void MaybeParseMenuOptions(uint16_t* style);
  std::unique_ptr<MenuResource::SubmenuEntryData> ParseMenuBlock();
  std::unique_ptr<MenuResource> ParseMenu(IntOrStringName name);
  bool ParseDialogControl(DialogResource::Control* control,
                          DialogResource::DialogKind dialog_kind);
  std::unique_ptr<DialogResource> ParseDialog(
      IntOrStringName name,
      DialogResource::DialogKind dialog_kind);
  std::unique_ptr<StringtableResource> ParseStringtable();
  bool ParseAccelerator(AcceleratorsResource::Accelerator* accelerator);
  std::unique_ptr<AcceleratorsResource> ParseAccelerators(IntOrStringName name);
  std::unique_ptr<VersioninfoResource::BlockData> ParseVersioninfoBlock();
  std::unique_ptr<VersioninfoResource> ParseVersioninfo(IntOrStringName name);
  std::unique_ptr<Resource> ParseResource();
  std::unique_ptr<FileBlock> ParseFile(std::string* err);

  // If |error_message| is non-nullptr, sets err_ if expected token isn't found.
  bool Is(Token::Type type, const char* error_message = nullptr);
  // If |error_message| is non-nullptr, sets err_ if expected token isn't found.
  bool Match(Token::Type type, const char* error_message = nullptr);
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
  InternalEncoding encoding_;
  StringStorage* string_storage_;
  size_t cur_;
};

// static
std::unique_ptr<FileBlock> Parser::Parse(std::vector<Token> tokens,
                                         InternalEncoding encoding,
                                         StringStorage* string_storage,
                                         std::string* err) {
  Parser p(std::move(tokens), encoding, string_storage);
  return p.ParseFile(err);
}

Parser::Parser(std::vector<Token> tokens, InternalEncoding encoding,
               StringStorage* string_storage)
    : tokens_(std::move(tokens)), encoding_(encoding),
      string_storage_(string_storage), cur_(0) {}

bool Parser::Is(Token::Type type, const char* error_message) {
  if (at_end())
    return false;
  bool is_match = cur_token().type() == type;
  if (!is_match && error_message) {
    err_ = error_message + std::string(", got ") +
           cur_or_last_token().value_.to_string();
  }
  return is_match;
}

bool Parser::Match(Token::Type type, const char* error_message) {
  if (!Is(type, error_message))
    return false;
  Consume();
  return true;
}

const Token& Parser::Consume() {
  return tokens_[cur_++];
}

bool Parser::EvalIntExpression(uint32_t* out, bool* is_32) {
  if (!EvalIntExpressionUnary(out, is_32))
    return false;

  while (Is(Token::kPlus) || Is(Token::kMinus) || Is(Token::kPipe) ||
      Is(Token::kAmp)) {
    Token::Type op = Consume().type_;
    uint32_t rhs;
    if (!EvalIntExpressionUnary(&rhs, is_32))
      return false;
    switch (op) {
      case Token::kPlus:  *out = *out + rhs; break;
      case Token::kMinus: *out = *out - rhs; break;
      case Token::kPipe:  *out = *out | rhs; break;
      case Token::kAmp:   *out = *out & rhs; break;
      default: abort();  // Unreachable.
    }
  }

  return true;
}

bool Parser::EvalIntExpressionUnary(uint32_t* out, bool* is_32) {
  if (Match(Token::kMinus)) {
    if (!EvalIntExpressionUnary(out, is_32))
      return false;
    *out = -*out;
    return true;
  }
  if (Match(Token::kTilde)) {
    if (!EvalIntExpressionUnary(out, is_32))
      return false;
    *out = ~*out;
    return true;
  }
  return EvalIntExpressionPrimary(out, is_32);
}

bool Parser::EvalIntExpressionPrimary(uint32_t* out, bool* is_32) {
  if (Match(Token::kLeftParen))
    return EvalIntExpression(out, is_32) &&
           Match(Token::kRightParen, "expected )");
  if (!Is(Token::kInt, "expected int, +, ~, or ("))
    return false;
  const Token& t = Consume();
  *out = t.IntValue(is_32);
  return true;
}

std::experimental::string_view Parser::StringContents(const Token& tok) {
  assert(tok.type() == Token::kString);
  return string_storage_->StringContents(tok.value_);
}

static bool EndsVersioninfoData(const Token& t) {
  return t.type() == Token::kEndBlock ||
         (t.type() == Token::kIdentifier &&
          (IsEqualAsciiUppercase(t.value_, "VALUE") ||
           IsEqualAsciiUppercase(t.value_, "BLOCK")));
}

bool Parser::ParseVersioninfoData(std::vector<uint8_t>* data,
                                  uint16_t* value_size,
                                  bool* is_text) {
  bool is_prev_string = false;
  bool is_right_after_comma = false;
  while (!at_end() && !EndsVersioninfoData(cur_token())) {
    if (Match(Token::kComma)) {
      is_right_after_comma = true;
      is_prev_string = false;
      continue;
    }
    if (!Is(Token::kInt) && !Is(Token::kString)) {
      err_ = "expected int or string, got " +
             cur_or_last_token().value_.to_string();
      return false;
    }
    bool is_text_token = Is(Token::kString);
    if (is_text_token) {
      std::experimental::string_view value_val = StringContents(Consume());

      if (is_prev_string) {
        // Overwrite \0 from immediately preceding string.
        data->pop_back();
        data->pop_back();
      }
      C16string value_val_utf16;
      if (!ToUTF16(&value_val_utf16, value_val, encoding_, &err_))
        return false;
      for (size_t j = 0; j < value_val_utf16.size(); ++j) {
        // FIXME: This is gross and assumes little-endian-ness.
        data->push_back(value_val_utf16[j] & 0xFF);
        data->push_back(value_val_utf16[j] >> 8);
      }
      data->push_back(0);  // \0-terminate.
      data->push_back(0);
      *value_size += value_val_utf16.size();
      if (is_right_after_comma)
        *value_size += 1;
      is_prev_string = true;
    } else {
      is_prev_string = false;
      // FIXME: The Is(Token::kInt) should probably be IsIntExprStart
      // (int "-" "~" "("); currently this rejects "..., ~0"
      uint32_t value_num;
      bool is_32 = false;
      if (!EvalIntExpression(&value_num, &is_32))
        return false;
      *is_text = false;
      data->push_back(value_num & 0xFF);
      data->push_back((value_num >> 8) & 0xFF);
      *value_size += 2;
      if (is_32) {
        data->push_back((value_num >> 16) & 0xFF);
        data->push_back(value_num >> 24);
        *value_size += 2;
      }
    }
    is_right_after_comma = false;
  }
  return true;
}

// FIXME: share code with ParseVersioninfoData()?
bool Parser::ParseRawData(std::vector<uint8_t>* data) {
  while (!at_end() && !Is(Token::kEndBlock)) {
    if (!Is(Token::kInt) && !Is(Token::kString)) {
      err_ = "expected int or string, got " +
             cur_or_last_token().value_.to_string();
      return false;
    }
    bool is_text_token = Is(Token::kString);
    if (is_text_token) {
      std::experimental::string_view value_val = StringContents(Consume());

      // FIXME: 1-byte strings for "asdf", 2-byte for L"asdf".
      for (size_t j = 0; j < value_val.size(); ++j) {
        // FIXME: Real UTF16 support.
        data->push_back(value_val[j]);
      }
      // No implicit \0-termination in raw blocks.
    } else {
      // FIXME: The Is(Token::kInt) should probably be IsIntExprStart
      // (int "-" "~" "("); currently this rejects "..., ~0"
      uint32_t value_num;
      bool is_32 = false;
      if (!EvalIntExpression(&value_num, &is_32))
        return false;
      data->push_back(value_num & 0xFF);
      data->push_back((value_num >> 8) & 0xFF);
      if (is_32) {
        data->push_back((value_num >> 16) & 0xFF);
        data->push_back(value_num >> 24);
      }
    }
    while (Match(Token::kComma)) {
      // Ignore arbitrary many trailing commas after each value.
    }
  }
  return Match(Token::kEndBlock, "expected END or }");
}

std::unique_ptr<LanguageResource> Parser::ParseLanguage() {
  if (!Is(Token::kInt, "expected int"))
    return std::unique_ptr<LanguageResource>();
  uint8_t language = Consume().IntValue();
  if (!Match(Token::kComma, "expected comma") ||
      !Is(Token::kInt, "expected int"))
    return std::unique_ptr<LanguageResource>();
  uint8_t sub_language = Consume().IntValue();
  return std::make_unique<LanguageResource>(language, sub_language);
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
  if (!Match(Token::kBeginBlock, "expected BEGIN or {"))
    return std::unique_ptr<MenuResource::SubmenuEntryData>();

  std::unique_ptr<MenuResource::SubmenuEntryData> entries(
      new MenuResource::SubmenuEntryData);
  while (!at_end() && cur_token().type() != Token::kEndBlock) {
    if (!Is(Token::kIdentifier) ||
        (!IsEqualAsciiUppercase(cur_token().value_, "MENUITEM") &&
         !IsEqualAsciiUppercase(cur_token().value_, "POPUP"))) {
      err_ = "expected MENUITEM or POPUP, got " +
             cur_or_last_token().value_.to_string();
      return std::unique_ptr<MenuResource::SubmenuEntryData>();
    }
    bool is_item = IsEqualAsciiUppercase(cur_token().value_ , "MENUITEM");
    Consume();  // Eat MENUITEM or POPUP.
    if (!Is(Token::kString, "expected string"))
      return std::unique_ptr<MenuResource::SubmenuEntryData>();
    std::experimental::string_view name_val = StringContents(Consume());

    std::unique_ptr<MenuResource::EntryData> entry_data;
    uint16_t style = 0;
    if (is_item) {
      if (!Match(Token::kComma, "expected comma"))
        return std::unique_ptr<MenuResource::SubmenuEntryData>();
      if (!Is(Token::kInt, "expected int"))
        return std::unique_ptr<MenuResource::SubmenuEntryData>();
      uint16_t id_num = Consume().IntValue();

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
  if (!Match(Token::kEndBlock, "expected END or }"))
    return std::unique_ptr<MenuResource::SubmenuEntryData>();
  if (entries->subentries.empty()) {
    err_ = "empty menus are not allowed";
    return std::unique_ptr<MenuResource::SubmenuEntryData>();
  }
  return entries;
}


std::unique_ptr<MenuResource> Parser::ParseMenu(IntOrStringName name) {
  std::unique_ptr<MenuResource::SubmenuEntryData> entries = ParseMenuBlock();
  if (!entries)
    return std::unique_ptr<MenuResource>();
  return std::make_unique<MenuResource>(name, std::move(*entries.get()));
}

static void ClassAndStyleForControl(std::experimental::string_view type,
                                    uint16_t* control_class,
                                    uint32_t* default_style) {
  // https://msdn.microsoft.com/en-us/library/windows/desktop/ms644997(v=vs.85).aspx
  const uint16_t kButton = 0x80;
  const uint16_t kEdit = 0x81;
  const uint16_t kStatic = 0x82;
  const uint16_t kListBox = 0x83;
  const uint16_t kScrollBar = 0x84;
  const uint16_t kComboBox = 0x85;

  if (IsEqualAsciiUppercase(type, "AUTO3STATE")) {
    *default_style = 0x50010006;
    *control_class = kButton;
  } else if (IsEqualAsciiUppercase(type, "AUTOCHECKBOX")) {
    *default_style = 0x50010003;
    *control_class = kButton;
  } else if (IsEqualAsciiUppercase(type, "COMBOBOX")) {
    *default_style = 0x50000000;
    *control_class = kComboBox;
  } else if (IsEqualAsciiUppercase(type, "CTEXT")) {
    *default_style = 0x50020001;
    *control_class = kStatic;
  } else if (IsEqualAsciiUppercase(type, "DEFPUSHBUTTON")) {
    *default_style = 0x50010001;
    *control_class = kButton;
  } else if (IsEqualAsciiUppercase(type, "EDITTEXT")) {
    *default_style = 0x50810000;
    *control_class = kEdit;
  } else if (IsEqualAsciiUppercase(type, "GROUPBOX")) {
    *default_style = 0x50000007;
    *control_class = kButton;
  } else if (IsEqualAsciiUppercase(type, "HEDIT")) {
    *default_style = 0x50810000;
    *control_class = kButton;
  } else if (IsEqualAsciiUppercase(type, "IEDIT")) {
    *default_style = 0x50810000;
    *control_class = kButton;
  } else if (IsEqualAsciiUppercase(type, "ICON")) {
    *default_style = 0x50000003;
    *control_class = kStatic;
  } else if (IsEqualAsciiUppercase(type, "LISTBOX")) {
    *default_style = 0x50800001;
    *control_class = kListBox;
  } else if (IsEqualAsciiUppercase(type, "LTEXT")) {
    *default_style = 0x50020000;
    *control_class = kStatic;
  } else if (IsEqualAsciiUppercase(type, "PUSHBOX")) {
    *default_style = 0x5001000a;
    *control_class = kButton;
  } else if (IsEqualAsciiUppercase(type, "PUSHBUTTON")) {
    *default_style = 0x50010000;
    *control_class = kButton;
  } else if (IsEqualAsciiUppercase(type, "RADIOBUTTON")) {
    *default_style = 0x50000004;
    *control_class = kButton;
  } else if (IsEqualAsciiUppercase(type, "RTEXT")) {
    *default_style = 0x50020002;
    *control_class = kStatic;
  } else if (IsEqualAsciiUppercase(type, "SCROLLBAR")) {
    *default_style = 0x50000000;
    *control_class = kScrollBar;
  } else if (IsEqualAsciiUppercase(type, "STATE3")) {
    *default_style = 0x50010005;
    *control_class = kButton;
  }
}

bool Parser::ParseDialogControl(DialogResource::Control* control,
                                DialogResource::DialogKind dialog_kind) {
  // https://msdn.microsoft.com/en-us/library/windows/desktop/aa380902(v=vs.85).aspx
  if (!Is(Token::kIdentifier, "expected identifier"))
    return false;
  const Token& type = Consume();
  if (CaseInsensitiveStringSet{{
          "AUTO3STATE", "AUTOCHECKBOX", "COMBOBOX", "CONTROL", "CTEXT",
          "DEFPUSHBUTTON", "EDITTEXT", "GROUPBOX", "HEDIT", "IEDIT", "ICON",
          "LISTBOX", "LTEXT", "PUSHBOX", "PUSHBUTTON", "RADIOBUTTON", "RTEXT",
          "SCROLLBAR", "STATE3"},
          40, CaseInsensitiveHash, IsEqualAsciiUppercase}
          .count(type.value_) == 0) {
    err_ = "unknown control type " + type.value_.to_string();
    return false;
  }

  bool wants_text = CaseInsensitiveStringSet{{
      "COMBOBOX", "EDITTEXT", "HEDIT", "IEDIT", "LISTBOX", "SCROLLBAR"},
      12, CaseInsensitiveHash, IsEqualAsciiUppercase}.count(type.value_) == 0;
  if (wants_text) {
    // FIXME: rc.exe also accepts identifiers (bare `adsf`, no quotes).
    if (!Is(Token::kString) && !Is(Token::kInt)) {
      err_ = "expected string or int, got " +
             cur_or_last_token().value_.to_string();
      return false;
    }
    const Token& text = Consume();
    C16string text_val_utf16;
    if (text.type() == Token::kString) {
      std::experimental::string_view text_val = StringContents(text);
      if (!ToUTF16(&text_val_utf16, text_val, encoding_, &err_))
        return false;
    }
    control->text = text.type() == Token::kInt
                        ? IntOrStringName::MakeInt(text.IntValue())
                        : IntOrStringName::MakeStringUTF16(text_val_utf16);
    if (!Match(Token::kComma, "expected comma"))
      return false;
  }

  const Token* control_type = nullptr;
  if (IsEqualAsciiUppercase(type.value_, "CONTROL")) {
    // Special: Has id, class, style, so id below will actually be style not id
    // for this type only.
    if (!EvalIntExpression(&control->id))
      return false;

    if (!Match(Token::kComma, "expected comma") ||
        // FIXME: class can be either string or int
        !Is(Token::kString, "expected string"))
      return false;
    control_type = &Consume();
    if (!Match(Token::kComma, "expected comma"))
      return false;
  }

  uint32_t id;
  if (!EvalIntExpression(&id))
    return false;
  uint32_t rect[4];
  for (int i = 0; i < 4; ++i)
    if (!Match(Token::kComma, "expected comma") || !EvalIntExpression(&rect[i]))
      return false;

  if (IsEqualAsciiUppercase(type.value_, "CONTROL"))
    control->style = id;
  else
    control->id = id;
  control->x = rect[0];
  control->y = rect[1];
  control->w = rect[2];
  control->h = rect[3];

  // https://msdn.microsoft.com/en-us/library/windows/desktop/ms644997(v=vs.85).aspx
  const uint16_t kButton = 0x80;
  const uint16_t kEdit = 0x81;
  const uint16_t kStatic = 0x82;
  const uint16_t kListBox = 0x83;
  const uint16_t kScrollBar = 0x84;
  const uint16_t kComboBox = 0x85;

  uint16_t control_class = 0;
  uint32_t default_style = 0;
  if (IsEqualAsciiUppercase(type.value_, "CONTROL")) {
    default_style = 0x50000000;
    std::experimental::string_view type_val = StringContents(*control_type);
    if (IsEqualAsciiUppercase(type_val, "BUTTON"))
      control->clazz = IntOrStringName::MakeInt(kButton);
    else if (IsEqualAsciiUppercase(type_val, "EDIT"))
      control->clazz = IntOrStringName::MakeInt(kEdit);
    else if (IsEqualAsciiUppercase(type_val, "STATIC"))
      control->clazz = IntOrStringName::MakeInt(kStatic);
    else if (IsEqualAsciiUppercase(type_val, "LISTBOX"))
      control->clazz = IntOrStringName::MakeInt(kListBox);
    else if (IsEqualAsciiUppercase(type_val, "SCROLLBAR"))
      control->clazz = IntOrStringName::MakeInt(kScrollBar);
    else if (IsEqualAsciiUppercase(type_val, "COMBOBOX"))
      control->clazz = IntOrStringName::MakeInt(kComboBox);
    else {
      C16string type_val_utf16;
      if (!ToUTF16(&type_val_utf16, type_val, encoding_, &err_))
        return false;
      control->clazz = IntOrStringName::MakeStringUTF16(type_val_utf16);
    }
  } else {
    ClassAndStyleForControl(type.value_, &control_class, &default_style);
    control->clazz = IntOrStringName::MakeInt(control_class);
  }

  control->style |= default_style;

  // `, style` is only accepted for non-CONTROL controls. CONTROL only has
  // a trailing `, exstyle`, since style precedes `x` there already.
  if (!control_type && Match(Token::kComma)) {
    uint32_t parsed_style;
    if (!EvalIntExpression(&parsed_style))
      return false;
    control->style |= parsed_style;
  }
  // FIXME: The Is(Token::kInt) should really be IsIntExprStart
  // (int "-" "~" "("); currently this wrongly rejects "..., ~0"
  if (Match(Token::kComma) && Is(Token::kInt))
    if (!EvalIntExpression(&control->exstyle))
      return false;
  if (dialog_kind == DialogResource::kDialogEx && Match(Token::kComma) &&
      Is(Token::kInt))
    control->help_id = Consume().IntValue();
  return true;
}

std::unique_ptr<DialogResource> Parser::ParseDialog(
    IntOrStringName name,
    DialogResource::DialogKind dialog_kind) {
  // Parse attributes of dialog itself.
  uint16_t rect[4];
  for (int i = 0; i < 4; ++i) {
    if (i > 0)
      Match(Token::kComma);  // Eat optional commas.
    if (!Is(Token::kInt, "expected int"))
      return std::unique_ptr<DialogResource>();
    rect[i] = Consume().IntValue();
  }

  // DIALOGEX can have an optional helpID after the dialog rect.
  uint32_t help_id = 0;
  if (dialog_kind == DialogResource::kDialogEx && Match(Token::kComma)) {
    if (!Is(Token::kInt, "expected int"))
      return std::unique_ptr<DialogResource>();
    help_id = Consume().IntValue();
  }

  std::experimental::string_view caption_val;
  IntOrStringName clazz = IntOrStringName::MakeEmpty();
  uint32_t exstyle = 0;
  std::experimental::optional<DialogResource::FontInfo> font;
  IntOrStringName menu = IntOrStringName::MakeEmpty();
  std::experimental::optional<uint32_t> style;
  while (!at_end() && cur_token().type() != Token::kBeginBlock) {
    if (!Is(Token::kIdentifier, "expected identifier, BEGIN or {"))
      return std::unique_ptr<DialogResource>();
    const Token& tok = Consume();
    if (IsEqualAsciiUppercase(tok.value_, "CAPTION")) {
      if (!Is(Token::kString, "expected string"))
        return std::unique_ptr<DialogResource>();
      caption_val = StringContents(Consume());
    }
    else if (IsEqualAsciiUppercase(tok.value_, "CLASS")) {
      if (!Is(Token::kString) && !Is(Token::kInt)) {
        err_ = "expected string or int, got " +
               cur_or_last_token().value_.to_string();
        return std::unique_ptr<DialogResource>();
      }
      const Token& clazz_tok = Consume();
      if (clazz_tok.type() == Token::kString) {
        std::experimental::string_view clazz_val = StringContents(clazz_tok);
        C16string clazz_val_utf16;
        if (!ToUTF16(&clazz_val_utf16, clazz_val, encoding_, &err_))
          return std::unique_ptr<DialogResource>();
        clazz = IntOrStringName::MakeStringUTF16(clazz_val_utf16);
      } else {
        uint16_t clazz_val = clazz_tok.IntValue();
        clazz = IntOrStringName::MakeInt(clazz_val);
      }
    }
    else if (IsEqualAsciiUppercase(tok.value_, "EXSTYLE")) {
      if (!EvalIntExpression(&exstyle))
        return std::unique_ptr<DialogResource>();
    }
    else if (IsEqualAsciiUppercase(tok.value_, "FONT")) {
      DialogResource::FontInfo info;
      if (!Is(Token::kInt, "expected int"))
        return std::unique_ptr<DialogResource>();
      info.size = Consume().IntValue();
      if (!Match(Token::kComma, "expected comma"))
        return std::unique_ptr<DialogResource>();
      if (!Is(Token::kString, "expected string"))
        return std::unique_ptr<DialogResource>();
      info.name = StringContents(Consume());

      // DIALOGEX can have optional font weight, italic, encoding flags.
      if (dialog_kind == DialogResource::kDialogEx) {
        uint16_t vals[3] = {0, 0, 1};
        for (int i = 0; i < 3 && Match(Token::kComma); ++i) {
          if (!Is(Token::kInt, "expected int"))
            return std::unique_ptr<DialogResource>();
          vals[i] = Consume().IntValue();
        }
        info.weight = vals[0];
        info.italic = vals[1];
        info.charset = vals[2];
      }

      font = info;
    }
    else if (IsEqualAsciiUppercase(tok.value_, "MENU")) {
      if (!Is(Token::kString) && !Is(Token::kInt)) {
        err_ = "expected string or int, got " +
               cur_or_last_token().value_.to_string();
        return std::unique_ptr<DialogResource>();
      }
      const Token& menu_tok = Consume();
      if (menu_tok.type() == Token::kString) {
        // Do NOT strip the quotes here, rc.exe includes them too.
        C16string menu_utf16;
        if (!ToUTF16(&menu_utf16, menu_tok.value_, encoding_, &err_))
          return std::unique_ptr<DialogResource>();
        menu = IntOrStringName::MakeStringUTF16(menu_utf16);
      } else {
        uint16_t menu_val = menu_tok.IntValue();
        menu = IntOrStringName::MakeInt(menu_val);
      }
    }
    else if (IsEqualAsciiUppercase(tok.value_, "STYLE")) {
      uint32_t style_val;
      if (!EvalIntExpression(&style_val))
        return std::unique_ptr<DialogResource>();
      style = style_val;
    } else {
      err_ = "unknown DIALOG attribute " + tok.value_.to_string();
      return std::unique_ptr<DialogResource>();
    }
  }

  // Parse resources block.
  if (!Match(Token::kBeginBlock, "expected BEGIN of {"))
    return std::unique_ptr<DialogResource>();
  std::vector<DialogResource::Control> controls;
  while (!at_end() && cur_token().type() != Token::kEndBlock) {
    DialogResource::Control control;
    if (!ParseDialogControl(&control, dialog_kind))
      return std::unique_ptr<DialogResource>();
    controls.push_back(control);
  }
  if (!Match(Token::kEndBlock, "exptected END or }"))
    return std::unique_ptr<DialogResource>();

  return std::make_unique<DialogResource>(
      name, dialog_kind, rect[0], rect[1], rect[2], rect[3], help_id,
      caption_val, std::move(clazz), exstyle, std::move(font), std::move(menu),
      style, std::move(controls));
}

std::unique_ptr<StringtableResource> Parser::ParseStringtable() {
  if (!Match(Token::kBeginBlock, "expected BEGIN or {"))
    return std::unique_ptr<StringtableResource>();
  std::vector<StringtableResource::Entry> entries;
  while (!at_end() && cur_token().type() != Token::kEndBlock) {
    if (!Is(Token::kInt, "expected int"))
      return std::unique_ptr<StringtableResource>();
    uint16_t key_num = Consume().IntValue();
    Match(Token::kComma);  // Eat optional comma between key and value.

    if (!Is(Token::kString, "expected string"))
      return std::unique_ptr<StringtableResource>();
    std::experimental::string_view str_val = StringContents(Consume());
    entries.push_back(StringtableResource::Entry{key_num, str_val});
  }
  if (!Match(Token::kEndBlock, "expected END or }"))
    return std::unique_ptr<StringtableResource>();
  return std::make_unique<StringtableResource>(entries.data(), entries.size());
}

bool Parser::ParseAccelerator(AcceleratorsResource::Accelerator* accelerator) {
  if (!Is(Token::kString) && !Is(Token::kInt)) {
    err_ =
        "expected string or int, got " + cur_or_last_token().value_.to_string();
    return false;
  }
  const Token& name = Consume();
  if (!Match(Token::kComma, "expected comma"))
    return false;
  if (!Is(Token::kInt, "expected int"))
    return false;
  uint16_t id_num = Consume().IntValue();

  uint16_t flags = 0;
  while (Match(Token::kComma)) {
    if (!Is(Token::kIdentifier, "expected identifier"))
      return false;
    const Token& flag = Consume();
    if (IsEqualAsciiUppercase(flag.value_, "ASCII"))
      ;
    else if (IsEqualAsciiUppercase(flag.value_, "VIRTKEY"))
      flags |= kAccelVIRTKEY;
    else if (IsEqualAsciiUppercase(flag.value_, "NOINVERT"))
      flags |= kAccelNOINVERT;
    else if (IsEqualAsciiUppercase(flag.value_, "SHIFT"))
      flags |= kAccelSHIFT;
    else if (IsEqualAsciiUppercase(flag.value_, "CONTROL"))
      flags |= kAccelCONTROL;
    else if (IsEqualAsciiUppercase(flag.value_, "ALT"))
      flags |= kAccelALT;
    else {
      err_ = "unknown flag " + flag.value_.to_string();
      return false;
    }
  }

  accelerator->flags = flags;
  accelerator->key = 0;
  if (name.type() == Token::kInt) {
    accelerator->key = name.IntValue();
  } else {
    // name was checked to be kInt or kString above, so it's a kString here.
    std::experimental::string_view name_val = StringContents(name);
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
    if (flags & (kAccelSHIFT | kAccelVIRTKEY))
      accelerator->key = ascii_toupper(accelerator->key);
  }
  accelerator->id = id_num;
  accelerator->pad = 0;
  return true;
}

std::unique_ptr<AcceleratorsResource> Parser::ParseAccelerators(
    IntOrStringName name) {
  if (!Match(Token::kBeginBlock, "expected BEGIN or {"))
    return std::unique_ptr<AcceleratorsResource>();

  std::vector<AcceleratorsResource::Accelerator> entries;
  while (!at_end() && cur_token().type() != Token::kEndBlock) {
    AcceleratorsResource::Accelerator accelerator;
    if (!ParseAccelerator(&accelerator))
      return std::unique_ptr<AcceleratorsResource>();
    entries.push_back(accelerator);
  }
  if (!Match(Token::kEndBlock, "expected END or }"))
    return std::unique_ptr<AcceleratorsResource>();
  return std::make_unique<AcceleratorsResource>(name, std::move(entries));
}

std::unique_ptr<VersioninfoResource::BlockData>
Parser::ParseVersioninfoBlock() {
  if (!Match(Token::kBeginBlock, "expected BEGIN or {"))
    return std::unique_ptr<VersioninfoResource::BlockData>();

  std::unique_ptr<VersioninfoResource::BlockData> block(
      new VersioninfoResource::BlockData);
  while (!at_end() && cur_token().type() != Token::kEndBlock) {
    if (!Is(Token::kIdentifier) ||
        (!IsEqualAsciiUppercase(cur_token().value_, "BLOCK") &&
         !IsEqualAsciiUppercase(cur_token().value_, "VALUE"))) {
      err_ = "expected BLOCK or VALUE, got " +
             cur_or_last_token().value_.to_string();
      return std::unique_ptr<VersioninfoResource::BlockData>();
    }
    bool is_value = IsEqualAsciiUppercase(cur_token().value_, "VALUE");
    Consume();
    if (!Is(Token::kString, "expected string"))
      return std::unique_ptr<VersioninfoResource::BlockData>();
    std::experimental::string_view name_val = StringContents(Consume());

    std::unique_ptr<VersioninfoResource::InfoData> info_data;
    if (is_value) {
      std::vector<uint8_t> val;
      uint16_t value_size = 0;
      bool is_text = true;
      if (!ParseVersioninfoData(&val, &value_size, &is_text))
        return std::unique_ptr<VersioninfoResource::BlockData>();
      info_data.reset(new VersioninfoResource::ValueData(value_size, is_text,
                                                         std::move(val)));
    } else {
      std::unique_ptr<VersioninfoResource::BlockData> block =
          ParseVersioninfoBlock();
      if (!block)
        return std::unique_ptr<VersioninfoResource::BlockData>();
      info_data = std::move(block);
    }

    block->values.push_back(std::unique_ptr<VersioninfoResource::Entry>(
        new VersioninfoResource::Entry(name_val, std::move(info_data))));
  }
  if (!Match(Token::kEndBlock, "expected END or }"))
    return std::unique_ptr<VersioninfoResource::BlockData>();
  return block;
}

std::unique_ptr<VersioninfoResource> Parser::ParseVersioninfo(
    IntOrStringName name) {
  // Parse fixed info.
  VersioninfoResource::FixedInfo fixed_info = {};
  CaseInsensitiveStringMap<uint32_t*> fields{{
    {"FILEFLAGSMASK", &fixed_info.fileflags_mask},
    {"FILEFLAGS", &fixed_info.fileflags},
    {"FILEOS", &fixed_info.fileos},
    {"FILETYPE", &fixed_info.filetype},
    {"FILESUBTYPE", &fixed_info.filesubtype},
  }, 10, &CaseInsensitiveHash, IsEqualAsciiUppercase};
  while (!at_end() && cur_token().type() != Token::kBeginBlock) {
    if (!Is(Token::kIdentifier, "expected identifier, BEGIN or {"))
      return std::unique_ptr<VersioninfoResource>();
    const Token& name = Consume();
    uint32_t val_num;
    if (!EvalIntExpression(&val_num))
      return std::unique_ptr<VersioninfoResource>();
    if (IsEqualAsciiUppercase(name.value_, "FILEVERSION") ||
        IsEqualAsciiUppercase(name.value_, "PRODUCTVERSION")) {
      uint32_t val_nums[4] = { val_num };
      for (int i = 0; i < 3 && Match(Token::kComma); ++i)
        if (!EvalIntExpression(&val_nums[i + 1]))
          return std::unique_ptr<VersioninfoResource>();
      if (IsEqualAsciiUppercase(name.value_, "FILEVERSION")) {
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
  std::unique_ptr<VersioninfoResource::BlockData> block =
      ParseVersioninfoBlock();
  if (!block)
    return std::unique_ptr<VersioninfoResource>();
  return std::make_unique<VersioninfoResource>(name, fixed_info,
                                               std::move(*block.get()));
}

std::unique_ptr<Resource> Parser::ParseResource() {
  // FIXME: `"hi ho" {}` is parsed by Microsoft rc as a user-defined resource
  // with id `"hi` and type `ho"` -- so their tokenizer is context-dependent.
  // Maybe just check if `id` or `name` below contain namespace and if so,
  // reject them.

  const Token& id = Consume();  // Either int or ident.

  if (at_end()) {
    err_ = "expected resource name";
    return std::unique_ptr<Resource>();
  }

  // Normally, name first.
  //
  // Exceptions:
  // - LANGUAGE
  // - STRINGTABLE
  if (id.type() == Token::kIdentifier) {
    if (IsEqualAsciiUppercase(id.value_, "LANGUAGE"))
      return ParseLanguage();
    if (IsEqualAsciiUppercase(id.value_, "STRINGTABLE"))
      return ParseStringtable();
  }

  // FIXME: rc.exe allows unquoted names like `foo.bmp`
  if (id.type() != Token::kInt && id.type() != Token::kString &&
      id.type() != Token::kIdentifier) {
    err_ = "expected int, string, or identifier, got " +
           cur_or_last_token().value_.to_string();
    return std::unique_ptr<Resource>();
  }
  C16string id_utf16;
  // FIXME: "", \a, L"" handling?
  if (id.type() != Token::kInt &&
      !ToUTF16(&id_utf16, id.value_, encoding_, &err_))  // Do NOT strip quotes
    return std::unique_ptr<Resource>();
  // Fun fact: rc.exe silently truncates to 16 bit as well.
  // FIXME: consider warning on non-int IDs, that seems to be unintentional
  // quite often.
  IntOrStringName name =
      id.type() == Token::kInt
          ? IntOrStringName::MakeInt(id.IntValue())
          : IntOrStringName::MakeUpperStringUTF16(id_utf16);

  const Token& type = Consume();
  if (at_end()) {
    err_ = "expected resource type";
    return std::unique_ptr<Resource>();
  }

  // FIXME: skip Common Resource Attributes if needed:
  // https://msdn.microsoft.com/en-us/library/windows/desktop/aa380908(v=vs.85).aspx
  // They have been ignored since the 16-bit days, so hopefully they no longer
  // exist in practice.

  // FIXME: rc.exe also allows using well-known ints instead of text most of
  // the time. For example, a block with type 4 (kRT_MENU) is parsed the same
  // as a MENU (or MENUEX) block.  However, type 3 (kRT_ICON) seems to be
  // slightly different from an ICON, it complains that the input .ico file
  // isn't quite right.

  // Types with custom parsers.
  if (type.type_ == Token::kIdentifier &&
      IsEqualAsciiUppercase(type.value_, "MENU"))
    return ParseMenu(name);
  if (type.type_ == Token::kIdentifier &&
      IsEqualAsciiUppercase(type.value_, "MENUEX")) {
    err_ = "MENUEX not implemented yet";  // FIXME
    return std::unique_ptr<Resource>();
  }
  if (type.type_ == Token::kIdentifier &&
      IsEqualAsciiUppercase(type.value_, "DIALOG"))
    return ParseDialog(name, DialogResource::kDialog);
  if (type.type_ == Token::kIdentifier &&
      IsEqualAsciiUppercase(type.value_, "DIALOGEX"))
    return ParseDialog(name, DialogResource::kDialogEx);
  if (type.type_ == Token::kIdentifier &&
      IsEqualAsciiUppercase(type.value_, "ACCELERATORS"))
    return ParseAccelerators(name);
  if (type.type_ == Token::kIdentifier &&
      IsEqualAsciiUppercase(type.value_, "MESSAGETABLE")) {
    err_ = "MESSAGETABLE not implemented yet";  // FIXME
    return std::unique_ptr<Resource>();
  }
  if (type.type_ == Token::kIdentifier &&
      IsEqualAsciiUppercase(type.value_, "VERSIONINFO"))
    return ParseVersioninfo(name);

  // Unsupported types.
  if (IsEqualAsciiUppercase(type.value_, "FONTDIR")) {
    err_ = "FONTDIR not implemented yet";  // FIXME
    return std::unique_ptr<Resource>();
  }
  if (IsEqualAsciiUppercase(type.value_, "FONT")) {
    err_ = "FONT not implemented yet";  // FIXME
    // If this gets implemented: rc requires numeric names for FONTs.
    return std::unique_ptr<Resource>();
  }
  if (IsEqualAsciiUppercase(type.value_, "PLUGPLAY")) {
    err_ = "PLUGPLAY not implemented";
    return std::unique_ptr<Resource>();
  }
  if (IsEqualAsciiUppercase(type.value_, "VXD")) {
    err_ = "VXD not implemented";
    return std::unique_ptr<Resource>();
  }
  if (IsEqualAsciiUppercase(type.value_, "ANICURSOR")) {
    err_ = "ANICURSOR not implemented yet";  // FIXME
    return std::unique_ptr<Resource>();
  }
  if (IsEqualAsciiUppercase(type.value_, "ANIICON")) {
    err_ = "ANIICON not implemented yet";  // FIXME
    return std::unique_ptr<Resource>();
  }

  // Types always taking a string parameter.
  bool needs_string = IsEqualAsciiUppercase(type.value_, "CURSOR") ||
                      IsEqualAsciiUppercase(type.value_, "BITMAP") ||
                      IsEqualAsciiUppercase(type.value_, "ICON") ||
                      IsEqualAsciiUppercase(type.value_, "DLGINCLUDE");
  if (needs_string) {
    if (!Is(Token::kString, "expected string"))
      return std::unique_ptr<DialogResource>();
    const Token& data = Consume();
    std::experimental::string_view str_val = StringContents(data);

    if (IsEqualAsciiUppercase(type.value_, "CURSOR"))
      return std::make_unique<CursorResource>(name, str_val);
    if (IsEqualAsciiUppercase(type.value_, "BITMAP"))
      return std::make_unique<BitmapResource>(name, str_val);
    if (IsEqualAsciiUppercase(type.value_, "ICON"))
      return std::make_unique<IconResource>(name, str_val);
    if (IsEqualAsciiUppercase(type.value_, "DLGINCLUDE"))
      return std::make_unique<DlgincludeResource>(name, str_val);
  }

  // The remaining types can either take a string filename or a BEGIN END
  // block containing inline data.
  const Token& data = Consume();
  if (data.type_ == Token::kBeginBlock) {
    std::vector<uint8_t> raw_data;
    if (!ParseRawData(&raw_data))
      return std::unique_ptr<Resource>();

    if (IsEqualAsciiUppercase(type.value_, "RCDATA"))
      return std::make_unique<RcdataResource>(name, std::move(raw_data));
    if (IsEqualAsciiUppercase(type.value_, "HTML"))
      return std::make_unique<HtmlResource>(name, std::move(raw_data));

    // Not a known resource type, so it's a User-Defined Resource.
    if (type.type_ == Token::kIdentifier || type.type_ == Token::kInt ||
        type.type_ == Token::kString) {
      C16string type_utf16;
      // Do NOT strip quotes.
      // FIXME: "", \a, L"" handling?
      if (type.type() != Token::kInt &&
          !ToUTF16(&type_utf16, type.value_, encoding_, &err_))
        return std::unique_ptr<Resource>();
      IntOrStringName type_name =
          type.type() == Token::kInt
             ? IntOrStringName::MakeInt(type.IntValue())
             : IntOrStringName::MakeUpperStringUTF16(type_utf16);
      return std::make_unique<UserDefinedResource>(type_name, name,
                                                   std::move(raw_data));
    }
  }

  if (type.type_ == Token::kIdentifier && data.type_ == Token::kString) {
    std::experimental::string_view str_val = StringContents(data);
    if (IsEqualAsciiUppercase(type.value_, "RCDATA"))
      return std::make_unique<RcdataResource>(name, str_val);
    if (IsEqualAsciiUppercase(type.value_, "HTML"))
      return std::make_unique<HtmlResource>(name, str_val);
  }

  // Not a known resource type, so it's a custom User-Defined Resource.
  if ((type.type_ == Token::kIdentifier || type.type_ == Token::kInt ||
       type.type_ == Token::kString) &&
      data.type_ == Token::kString) {
    C16string type_utf16;
    // Do NOT strip quotes.
    // FIXME: "", \a, L"" handling?
    if (type.type() != Token::kInt &&
        !ToUTF16(&type_utf16, type.value_, encoding_, &err_))
      return std::unique_ptr<Resource>();

    std::experimental::string_view str_val = StringContents(data);
    IntOrStringName type_name = type.type() == Token::kInt
                                    ? IntOrStringName::MakeInt(type.IntValue())
                                    : IntOrStringName::MakeUpperStringUTF16(
                                          type_utf16);
    return std::make_unique<UserDefinedResource>(type_name, name, str_val);
  }

  err_ = "unknown resource, type " + type.value_.to_string();
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
  kRT_STRING = 6,  // For STRINGTABLE.
  kRT_FONTDIR = 7,
  kRT_FONT = 8,
  kRT_ACCELERATOR = 9,
  kRT_RCDATA = 10,
  kRT_MESSAGETABLE = 11,
  kRT_GROUP_CURSOR = 12,
  kRT_GROUP_ICON = 14,
  kRT_VERSION = 16,  // For VERSIONINFO, not VERSION
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

// The format of .res files is documented at
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms648007(v=vs.85).aspx
class SerializationVisitor : public Visitor {
 public:
  // FIXME: rc.exe gets the default language from the system while this
  // hardcodes US English. Neither seems like a great default.
  SerializationVisitor(
      FILE* f, const std::vector<std::string>& include_dirs,
      bool show_includes, InternalEncoding encoding, std::string* err)
      : out_(f),
        include_dirs_(include_dirs),
        show_includes_(show_includes),
        err_(err),
        next_icon_id_(1),
        cur_language_{9, 1},
        encoding_(encoding) {}

  void WriteResHeader(
      uint32_t data_size,
      IntOrStringName type,
      IntOrStringName name,
      uint16_t memory_flags = 0x1030,
      std::experimental::optional<uint16_t> language =
          std::experimental::optional<uint16_t>());
  bool WriteFileOrDataResource(
      IntOrStringName type, const FileOrDataResource* r);

  bool VisitLanguageResource(const LanguageResource* r) override;
  bool VisitCursorResource(const CursorResource* r) override;
  bool VisitBitmapResource(const BitmapResource* r) override;
  bool VisitIconResource(const IconResource* r) override;
  bool VisitMenuResource(const MenuResource* r) override;
  bool VisitDialogResource(const DialogResource* r) override;
  bool VisitStringtableResource(const StringtableResource* r) override;
  bool VisitAcceleratorsResource(const AcceleratorsResource* r) override;
  bool VisitRcdataResource(const RcdataResource* r) override;
  bool VisitVersioninfoResource(const VersioninfoResource* r) override;
  bool VisitDlgincludeResource(const DlgincludeResource* r) override;
  bool VisitHtmlResource(const HtmlResource* r) override;
  bool VisitUserDefinedResource(const UserDefinedResource* r) override;

  bool WriteStringtables();

 private:
  FILE* OpenFile(const char* path);

  enum GroupType { kIcon = 1, kCursor = 2 };
  bool WriteIconOrCursorGroup(const FileResource* r, GroupType type);

  FILE* out_;
  const std::vector<std::string>& include_dirs_;
  bool show_includes_;
  std::string* err_;
  int next_icon_id_;
  LanguageResource::Language cur_language_;
  InternalEncoding encoding_;

  // Id of first string in bundle, and language of bundle.
  typedef std::pair<uint16_t, LanguageResource::Language> BundleKey;
  struct StringBundle {
    StringBundle(BundleKey key) : key(key) {
      std::fill_n(strings, 16, nullptr);
    }
    BundleKey key;
    const std::experimental::string_view* strings[16];
  };
  bool EmitOneStringBundle(const StringBundle& bundle);
  std::list<StringBundle> string_bundles_;
  std::map<BundleKey, std::list<StringBundle>::iterator> bundle_index;
};

struct FClose {
  FClose(FILE* f) : f_(f) {}
  ~FClose() { if (f_) fclose(f_); }
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

std::string Join(const std::string dir, const char* path) {
  if (dir.empty())
    return path;

  bool path_is_absolute;
#if !defined(_WIN32)
  path_is_absolute = path[0] == '/';
#else
  path_is_absolute = !PathIsRelative(path);
#endif
  if (path_is_absolute)
    return path;

  return dir + "/" + path;
}

FILE* SerializationVisitor::OpenFile(const char* path) {
  FILE* f = NULL;
  std::string path_storage;
  for (const std::string& dir : include_dirs_) {
    path_storage = Join(dir, path);
    FILE* nf = fopen(path_storage.c_str(), "rb");
    if (nf) {
      f = nf;
      path = path_storage.c_str();
      break;
    }
    // rc.exe only keeps searching if the file doesn't exist; if it exists
    // but is e.g. not readable, it fails. Match that.
    if (errno != ENOENT)
      break;
  }
  if (!f) {
    *err_ = std::string("failed to open ") + path;
    return NULL;
  }
  if (show_includes_) {
#if !defined(_WIN32)
    char full_path[PATH_MAX];
    realpath(path, full_path);
#else
    char full_path[_MAX_PATH];
    GetFullPathName(path, sizeof(full_path), full_path, NULL);
#endif
    printf("Note: including file: %s\n", full_path);
  }
  return f;
}

void SerializationVisitor::WriteResHeader(
    uint32_t data_size,
    IntOrStringName type,
    IntOrStringName name,
    uint16_t memory_flags,
    std::experimental::optional<uint16_t> language) {
  uint32_t header_size = 0x18 + type.serialized_size() + name.serialized_size();
  bool pad = false;
  if (header_size % 4) {
    header_size += 2;
    pad = true;
  }
  write_little_long(out_, data_size);
  write_little_long(out_, header_size);
  type.write(out_);
  name.write(out_);
  if (pad)  // pad to uint32_t after type and name.
    write_little_short(out_, 0);
  write_little_long(out_, 0);              // data version
  write_little_short(out_, memory_flags);  // memory flags XXX
  uint16_t lang = cur_language_.as_uint16();
  if (language)
    lang = *language;
  write_little_short(out_, lang);
  write_little_long(out_, 0);              // version XXX
  write_little_long(out_, 0);              // characteristics
}

bool SerializationVisitor::WriteFileOrDataResource(
    IntOrStringName type, const FileOrDataResource* r) {
  size_t size;
  FILE* f = NULL;
  if (r->type() == FileOrDataResource::kFile) {
    f = OpenFile(r->path().to_string().c_str());
    if (!f)
      return false;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
  } else {
    size = r->data().size();
  }
  FClose closer(f);

  WriteResHeader(size, type, r->name(), 0x30);

  if (r->type() == FileOrDataResource::kFile) {
    fseek(f, 0, SEEK_SET);
    copy(out_, f, size);
  } else
    fwrite(r->data().data(), 1, size, out_);
  uint8_t padding = ((4 - (size & 3)) & 3);  // DWORD-align.
  fwrite("\0\0", 1, padding, out_);

  return true;
}

bool SerializationVisitor::WriteIconOrCursorGroup(const FileResource* r,
                                                  GroupType type) {
  // An .ico file usually contains several bitmaps. In a .res file, one
  //kRT_ICON is written per bitmap, and they're tied together with one
  //kRT_GROUP_ICON.
  // .ico format: https://msdn.microsoft.com/en-us/library/ms997538.aspx

  FILE* f = OpenFile(r->path().to_string().c_str());
  if (!f)
    return false;
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
    // Cursors are prepended by their hotspot.
    WriteResHeader(
        entry.data_size + (type == kCursor ? 4 : 0),
        IntOrStringName::MakeInt(type == kIcon ? kRT_ICON : kRT_CURSOR),
        IntOrStringName::MakeInt(next_icon_id_), 0x1010);

    if (type == kCursor) {
      write_little_short(out_, entry.num_planes);  // hotspot_x
      write_little_short(out_, entry.bpp);         // hotspot_y
    }

    fseek(f, entry.data_offset, SEEK_SET);
    copy(out_, f, entry.data_size);
    if (type == kCursor)
      entry.data_size += 4;

    entry.id = next_icon_id_++;

    uint8_t padding = (4 - (entry.data_size & 3)) & 3;  // DWORD-align.
    fwrite("\0\0", 1, padding, out_);
  }

  // Write final kRT_GROUP_ICON resource.
  uint16_t group_size = 6 + dir.count * 14;   // 1 IconDir + n IconEntry
  uint8_t group_padding = ((4 - (group_size & 3)) & 3);  // DWORD-align.
  WriteResHeader(group_size,
                 IntOrStringName::MakeInt(type == kIcon ? kRT_GROUP_ICON
                                                        : kRT_GROUP_CURSOR),
                 r->name());

  write_little_short(out_, dir.reserved);
  write_little_short(out_, dir.type);
  write_little_short(out_, dir.count);
  for (IconEntry& entry : entries) {
    if (type == kIcon) {
      fputc(entry.width, out_);
      fputc(entry.height, out_);
      fputc(entry.num_colors, out_);
      fputc(entry.reserved, out_);
      // Some files have num_planes set to 0, but rc.exe still writes 1 here.
      write_little_short(out_, 1);
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

bool SerializationVisitor::VisitLanguageResource(const LanguageResource* r) {
  cur_language_ = r->language();
  return true;
}

bool SerializationVisitor::VisitCursorResource(const CursorResource* r) {
  return WriteIconOrCursorGroup(r, kCursor);
}

bool SerializationVisitor::VisitBitmapResource(const BitmapResource* r) {
  FILE* f = OpenFile(r->path().to_string().c_str());
  if (!f)
    return false;
  FClose closer(f);
  fseek(f, 0, SEEK_END);
  size_t size = ftell(f);
  // https://support.microsoft.com/en-us/kb/67883
  // "NOTE: The BITMAPFILEHEADER structure is NOT present in the packed DIB;
  //  however, it is present in a DIB read from disk."
  const int kBitmapFileHeaderSize = 14;
  size -= kBitmapFileHeaderSize;

  // XXX padding?
  WriteResHeader(size, IntOrStringName::MakeInt(kRT_BITMAP), r->name(), 0x30);

  fseek(f, kBitmapFileHeaderSize, SEEK_SET);
  copy(out_, f, size);

  return true;
}

bool SerializationVisitor::VisitIconResource(const IconResource* r) {
  return WriteIconOrCursorGroup(r, kIcon);
}

bool CountMenu(const MenuResource::SubmenuEntryData& submenu,
               InternalEncoding encoding,
               std::string* err,
               size_t* num_submenus,
               size_t* num_items,
               size_t* total_string_length) {
  for (const auto& item : submenu.subentries) {
    C16string name_utf16;
    if (!ToUTF16(&name_utf16, item->name, encoding, err))
      return false;
    *total_string_length += name_utf16.size() + 1;
    if (item->style & kMenuPOPUP) {
      ++*num_submenus;
      if (!CountMenu(static_cast<MenuResource::SubmenuEntryData&>(*item->data),
                     encoding, err, num_submenus, num_items,
                     total_string_length))
        return false;
    } else {
      ++*num_items;
    }
  }
  return true;
}

bool WriteMenu(FILE* out, InternalEncoding encoding, std::string* err,
               const MenuResource::SubmenuEntryData& submenu) {
  for (const auto& item : submenu.subentries) {
    uint16_t style = item->style;
    if (&item == &submenu.subentries.back())
      style |= kMenuLASTITEM;
    write_little_short(out, style);
    if (!(item->style & kMenuPOPUP)) {
      write_little_short(
          out, static_cast<MenuResource::ItemEntryData&>(*item->data).id);
    }
    C16string name_utf16;
    if (!ToUTF16(&name_utf16, item->name, encoding, err))
      return false;
    for (size_t j = 0; j < name_utf16.size(); ++j)
      write_little_short(out, name_utf16[j]);
    write_little_short(out, 0);  // \0-terminate.
    if (item->style & kMenuPOPUP) {
      if (!WriteMenu(out, encoding, err,
                     static_cast<MenuResource::SubmenuEntryData&>(*item->data)))
        return false;
    }
  }
  return true;
}

bool SerializationVisitor::VisitMenuResource(const MenuResource* r) {
  size_t num_submenus = 0, num_items = 0, total_string_length = 0;
  if (!CountMenu(r->entries_, encoding_, err_,
                 &num_submenus, &num_items, &total_string_length))
    return false;

  size_t size = 4 + (num_submenus + 2*num_items + total_string_length) * 2;

  WriteResHeader(size, IntOrStringName::MakeInt(kRT_MENU), r->name());

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
  if (!WriteMenu(out_, encoding_, err_, r->entries_))
    return false;
  if (size % 4) // Pad to dword.
    write_little_short(out_, 0);
  return true;
}

bool SerializationVisitor::VisitDialogResource(const DialogResource* r) {
  size_t fixed_size = r->kind == DialogResource::kDialogEx ? 0x1a : 0x12;
  fixed_size += r->clazz.serialized_size();

  C16string caption_utf16;
  if (!ToUTF16(&caption_utf16, r->caption, encoding_, err_))
    return false;
  fixed_size += (caption_utf16.size() + 1) * 2;

  C16string fontname_utf16;
  if (r->font) {
    fixed_size += 2;  // 1 uint16_t font size.
    if (r->kind == DialogResource::kDialogEx)
      fixed_size += 4;  // uint16_t font weight, 2 uint8_t italic, charset

    if (!ToUTF16(&fontname_utf16, r->font->name, encoding_, err_))
      return false;
    fixed_size += 2 * (fontname_utf16.size() + 1);
  }
  fixed_size += r->menu.serialized_size();

  size_t size = fixed_size;
  if (r->controls.size() && fixed_size % 4)
    size += 2;  // pad to uint32_t after header.

  for (const auto& c : r->controls)
    size += c.serialized_size(&c == &r->controls.back(), r->kind);

  WriteResHeader(size, IntOrStringName::MakeInt(kRT_DIALOG), r->name());

  // DIALOGEX seems to have a pretty different layout :-/
  struct DialogData {
    // 0x40 if FONT, 0x8880<<16 default, 0x40<<16 if CAPTION;
    // STYLE overwrites
    uint32_t style;
    uint32_t exstyle;

    uint16_t num_controls;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    // If there's a MENU, it's here instead of menu  (either 0xffff 0x2a, or
    // 0-terminated utf-16 text; pad). If there isn't, then menu is 0.
    uint16_t menu;
    // If there's a CLASS, it's here instead of clazz  (either 0xffff 0x2a, or
    // 0-terminated utf-16 text; pad). If there isn't, then clazz is 0.
    uint16_t clazz;
    // If there's a CAPTION, it's here instead of caption  (always
    // 0-terminated utf-16 text; pad). If there isn't, then caption is 0.
    uint16_t caption;
    // If there's a FONT, it trails this. uint16 size, weight, uint8_t italic,
    // charset, \0-term utf-16 name , name, pad)
  };

  struct DialogExData {
    uint16_t unk1;  // always 1?
    uint16_t unk2;  // always 0xffff?
    uint32_t help_id;
      // (0xffff help_id kind of seems like the help id could be an
      // IntOrStringName, but rc.exe rejects string helpIDs.)
    uint32_t exstyle;
    uint32_t style;

    // The rest is identical, except that FONT at the end has more stuff:
    uint16_t num_controls;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;

    // If there's a MENU, it's here instead of menu  (either 0xffff 0x2a, or
    // 0-terminated utf-16 text; pad). If there isn't, then menu is 0.
    uint16_t menu;
    // If there's a CLASS, it's here instead of clazz  (either 0xffff 0x2a, or
    // 0-terminated utf-16 text; pad). If there isn't, then clazz is 0.
    uint16_t clazz;
    // If there's a CAPTION, it's here instead of caption  (always
    // 0-terminated utf-16 text; pad). If there isn't, then caption is 0.
    uint16_t caption;
    // If there's a FONT, it trails this. uint16 size,
    // \0-term utf-16 name , name, pad)
  };

  uint32_t style = 0x80880000;
  if (!r->caption.empty())
    style |= 0x400000;
  if (r->font)
    style |= 0x40;
  if (r->style)
    style = *r->style;

  if (r->kind == DialogResource::kDialogEx) {
    write_little_short(out_, 1);  // FIXME
    write_little_short(out_, 0xffff);  // FIXME
    write_little_long(out_, r->help_id);
    write_little_long(out_, r->exstyle);
    write_little_long(out_, style);
  } else {
    write_little_long(out_, style);
    write_little_long(out_, r->exstyle);
  }

  write_little_short(out_, r->controls.size());
  write_little_short(out_, r->x);
  write_little_short(out_, r->y);
  write_little_short(out_, r->w);
  write_little_short(out_, r->h);
  r->menu.write(out_);
  r->clazz.write(out_);
  for (size_t j = 0; j < caption_utf16.size(); ++j)
    write_little_short(out_, caption_utf16[j]);
  write_little_short(out_, 0);  // \0-terminate caption.
  if (r->font) {
    write_little_short(out_, r->font->size);
    if (r->kind == DialogResource::kDialogEx) {
      write_little_short(out_, r->font->weight);
      fputc(r->font->italic, out_);
      fputc(r->font->charset, out_);
    }
    for (size_t j = 0; j < fontname_utf16.size(); ++j)
      write_little_short(out_, fontname_utf16[j]);
    write_little_short(out_, 0);
  }

  if (fixed_size % 4) {
    // Pad to dword after header.
    write_little_short(out_, 0);
  }

  // Write dialog controls.
  for (const auto& c : r->controls) {
    if (r->kind == DialogResource::kDialogEx) {
      write_little_long(out_, c.help_id);
      write_little_long(out_, c.exstyle);
      write_little_long(out_, c.style);
    } else {
      write_little_long(out_, c.style);
      write_little_long(out_, c.exstyle);
    }
    write_little_short(out_, c.x);
    write_little_short(out_, c.y);
    write_little_short(out_, c.w);
    write_little_short(out_, c.h);
    if (r->kind == DialogResource::kDialogEx)
      write_little_long(out_, c.id);
    else
      write_little_short(out_, c.id);
    c.clazz.write(out_);
    c.text.write(out_);

    // Huh, looks like there's another 0 uint16_t after the name!
    // FIXME: figure out what this is and if it's ever non-0. Doesn't look like
    // padding since it's there (followed by a padding 0) even if the control
    // ends on a uint32_t boundary.
    write_little_short(out_, 0);

    // In DIALOGEX, the bigger id shifts everything by 2 bytes.
    if ((c.clazz.serialized_size() + c.text.serialized_size()) % 4 !=
        2*int(r->kind == DialogResource::kDialogEx))
      write_little_short(out_, 0);  // pad, but see FIXME above
  }

  return true;
}

bool SerializationVisitor::VisitStringtableResource(
    const StringtableResource* r) {
  // Microsoft rc seems to create blocks in the order they appear in the
  // input file.
  for (auto& str : *r) {
    BundleKey key{str.id & ~0xF, cur_language_};
    auto it = bundle_index.find(key);
    StringBundle* bundle;
    if (it == bundle_index.end()) {
      string_bundles_.push_back(StringBundle(key));
      bundle_index[key] = std::prev(string_bundles_.end());
      bundle = &string_bundles_.back();
    } else {
      bundle = &*it->second;
    }
    if (bundle->strings[str.id - key.first]) {
      *err_ = "duplicate string table key " + std::to_string(str.id);
      return false;
    }
    bundle->strings[str.id - key.first] = &str.value;
  }
  return true;
}

bool SerializationVisitor::VisitAcceleratorsResource(
    const AcceleratorsResource* r) {
  size_t size = r->accelerators().size() * 8;
  WriteResHeader(size, IntOrStringName::MakeInt(kRT_ACCELERATOR), r->name(),
                 0x30);
  for (const auto& accelerator : r->accelerators()) {
    uint16_t flags = accelerator.flags;
    if (&accelerator == &r->accelerators().back())
      flags |= kAccelLASTITEM;
    write_little_short(out_, flags);
    write_little_short(out_, accelerator.key);
    write_little_short(out_, accelerator.id);
    write_little_short(out_, accelerator.pad);
  }
  return true;
}

bool SerializationVisitor::VisitRcdataResource(const RcdataResource* r) {
  return WriteFileOrDataResource(IntOrStringName::MakeInt(kRT_RCDATA), r);
}

bool CountBlock(
    const VersioninfoResource::BlockData& block,
    InternalEncoding encoding,
    std::string* err,
    uint16_t* total_size,
    std::unordered_map<VersioninfoResource::Entry*, uint16_t>* sizes) {
  for (const auto& value : block.values) {
    C16string key_utf16;
    if (!ToUTF16(&key_utf16, value->key, encoding, err))
      return false;
    uint16_t size =
        3 * sizeof(uint16_t) + sizeof(char16_t) * (key_utf16.size() + 1);
    if (size % 4)
      size += 2;  // Pad to 4 bytes after name.
    if (value->data->type_ == VersioninfoResource::InfoData::kValue) {
      size += static_cast<VersioninfoResource::ValueData&>(*value->data)
                  .value.size();
    } else {
      // Children are already padded to 4 bytes.
      if (!CountBlock(
              static_cast<VersioninfoResource::BlockData&>(*value->data),
              encoding, err, &size, sizes))
        return false;
    }
    (*sizes)[value.get()] = size;

    // The padding after the object isn't include in that object's size.
    // Padding after the last object isn't included in the block's size.
    if (size % 4 && &value != &block.values.back())
      size += 2;  // Pad to 4 bytes after value too.
    *total_size += size;
  }
  return true;
}

bool WriteBlock(
    FILE* out,
    InternalEncoding encoding,
    std::string* err,
    const VersioninfoResource::BlockData& block,
    const std::unordered_map<VersioninfoResource::Entry*, uint16_t> sizes) {
  for (const auto& value : block.values) {
    uint16_t value_size = 0;
    const std::vector<uint8_t>* data;
    bool is_text = true;
    if (value->data->type_ == VersioninfoResource::InfoData::kValue) {
      const VersioninfoResource::ValueData& value_data =
          static_cast<VersioninfoResource::ValueData&>(*value->data);
      value_size = value_data.value_size;
      is_text = value_data.is_text;
      data = &value_data.value;
    }

    // Every block / value there seems to start with a 3-uint16_t header:
    // - uint16_t total_size (including header)
    // - value_size (in char16_ts, only for VALUE, 0 for BLOCKs, including \0)
    // - uint16_t is 1 for text values and blocks, 0 for int values
    // Followed by node data, followed by optional padding to uint32_t boundary
    uint16_t size = sizes.find(value.get())->second;
    write_little_short(out, size);
    write_little_short(out, value_size);
    write_little_short(out, is_text ? 1 : 0);

    C16string key_utf16;
    if (!ToUTF16(&key_utf16, value->key, encoding, err))
      return false;
    for (size_t j = 0; j < key_utf16.size(); ++j)
      write_little_short(out, key_utf16[j]);
    write_little_short(out, 0);  // \0-terminate.
    if (value->key.size() % 2)
      write_little_short(out, 0);  // padding to dword after 3 uint16_t and key

    if (value->data->type_ == VersioninfoResource::InfoData::kValue) {
      fwrite(data->data(), 1, data->size(), out);
      if (data->size() % 4)
        write_little_short(out, 0);  // pad to dword after value
    } else {
      if (!WriteBlock(
               out, encoding, err,
               static_cast<VersioninfoResource::BlockData&>(*value->data),
               sizes))
        return false;
    }
  }
  return true;
}

bool SerializationVisitor::VisitVersioninfoResource(
    const VersioninfoResource* r) {
  std::unordered_map<VersioninfoResource::Entry*, uint16_t> sizes;
  uint16_t block_size = 0;
  if (!CountBlock(r->block_, encoding_, err_, &block_size, &sizes))
    return false;

  const size_t kFixedInfoSize = 0x5c;
  size_t size = kFixedInfoSize + block_size;
  WriteResHeader(size, IntOrStringName::MakeInt(kRT_VERSION), r->name(), 0x30);

  // The fixed info block seems to always start with the same fixed bytes:
  write_little_short(out_, size);
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

  // Write variable block.
  return WriteBlock(out_, encoding_, err_, r->block_, sizes);
}

bool SerializationVisitor::VisitDlgincludeResource(
    const DlgincludeResource* r) {
  size_t size = r->data().size() + 1; // include trailing \0
  WriteResHeader(size, IntOrStringName::MakeInt(kRT_DLGINCLUDE), r->name());
  // data() is a string_view and might not be \0-terminated, so write \0
  // separately.
  fwrite(r->data().data(), 1, r->data().size(), out_);
  fputc('\0', out_);
  uint8_t padding = ((4 - (size & 3)) & 3);  // DWORD-align.
  fwrite("\0\0", 1, padding, out_);
  return true;
}

bool SerializationVisitor::VisitHtmlResource(const HtmlResource* r) {
  return WriteFileOrDataResource(IntOrStringName::MakeInt(kRT_HTML), r);
}

bool SerializationVisitor::VisitUserDefinedResource(
    const UserDefinedResource* r) {
  return WriteFileOrDataResource(r->type(), r);
}

bool SerializationVisitor::EmitOneStringBundle(const StringBundle& bundle) {
  // Each string is written as uint16_t length, followed by string data without
  // a trailing \0.
  // FIXME: rc.exe /n null-terminates strings in string table, have option
  // for that.

  C16string utf16[16];
  for (int i = 0; i < 16; ++i) {
    if (!bundle.strings[i])
      continue;
    if (!ToUTF16(&utf16[i], *bundle.strings[i], encoding_, err_))
      return false;
  }

  size_t size = 0;
  for (int i = 0; i < 16; ++i)
    size += 2 * (1 + (bundle.strings[i] ? utf16[i].size() : 0));

  WriteResHeader(size, IntOrStringName::MakeInt(kRT_STRING),
                 IntOrStringName::MakeInt((bundle.key.first / 16) + 1), 0x1030,
                 bundle.key.second.as_uint16());
  for (int i = 0; i < 16; ++i) {
    if (!bundle.strings[i]) {
      fwrite("\0", 1, 2, out_);
      continue;
    }
    write_little_short(out_, utf16[i].size());
    for (size_t j = 0; j < utf16[i].size(); ++j)
      write_little_short(out_, utf16[i][j]);
  }
  uint8_t padding = ((4 - (size & 3)) & 3);  // DWORD-align.
  fwrite("\0\0", 1, padding, out_);

  return true;
}

bool SerializationVisitor::WriteStringtables() {
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

  for (const StringBundle& bundle : string_bundles_)
    if (!EmitOneStringBundle(bundle))
      return false;
  string_bundles_.clear();

  return true;
}

bool WriteRes(const FileBlock& file,
              const std::string& out,
              const std::vector<std::string>& include_dirs,
              bool show_includes,
              InternalEncoding encoding,
              std::string* err) {
  FILE* f = fopen(out.c_str(), "wb");
  if (!f) {
    *err = "failed to open " + out;
    return false;
  }
  FClose closer(f);

  SerializationVisitor serializer(
      f, include_dirs, show_includes, encoding, err);

  // First write the "this is not the ancient 16-bit format" header.
  serializer.WriteResHeader(0, IntOrStringName::MakeInt(0),
                            IntOrStringName::MakeInt(0), 0, 0);

  for (size_t i = 0; i < file.res_.size(); ++i) {
    const Resource* res = file.res_[i].get();
    if (!res->Visit(&serializer))
      return false;
  }
  return serializer.WriteStringtables();
}

//////////////////////////////////////////////////////////////////////////////
// Driver

int main(int argc, char* argv[]) {
#if defined(_MSC_VER)
  _setmode(_fileno(stdin), _O_BINARY);
#endif

  std::string output = "out.res";
  std::vector<std::string> includes;
  // MS rc.exe's search order for includes:
  // 1. next to the input .rc
  // 2. cwd
  // 3. in all /I args (relative to cwd), in order
  // 4. in directories in %INCLUDE% (FIXME: not implemented; also a bit silly),
  //    if /x isn't passed (/x makes MS rc not look in %INCLUDE%)
  includes.push_back("");
  std::string cd;

  bool show_includes = false;
  bool input_is_utf8 = false;
  while (argc > 1 && (argv[1][0] == '/' || argv[1][0] == '-')) {
    if (strncmp(argv[1], "/I", 2) == 0 || strncmp(argv[1], "-I", 2) == 0) {
      // /I flags are relative to original cwd, not to where input .rc is.
      includes.push_back(argv[1] + 2);
    } else if (strncmp(argv[1], "/fo", 3) == 0) {
      output = std::string(argv[1] + 3);
    } else if (strncmp(argv[1], "/cd", 3) == 0) {
      cd = std::string(argv[1] + 3);
    } else if (strcmp(argv[1], "/showIncludes") == 0) {
      show_includes = true;
    } else if (strcmp(argv[1], "/utf-8") == 0) {
      // rc.exe doesn't support utf-8, so require an explicit flag for that.
      input_is_utf8 = true;
    } else if (strcmp(argv[1], "--") == 0) {
      --argc;
      ++argv;
      break;
    } else {
      fprintf(stderr, "rc: unrecognized option `%s'\n", argv[1]);
      return 1;
    }
    --argc;
    ++argv;
  }

  // Put directory input .rc is in at front of search path.
  if (!cd.empty())
    includes.insert(includes.begin(), cd);

  std::istreambuf_iterator<char> begin(std::cin), end;
  std::string s(begin, end);

  InternalEncoding encoding = kEncodingUnknown;

  if (input_is_utf8) {
    // Validate that the input if valid utf-8.
    if (!isLegalUTF8String((UTF8*)s.data(), (UTF8*)s.data() + s.size())) {
      fprintf(stderr, "rc: input is not valid utf-8\n");
      return 1;
    }
    encoding = kEncodingUTF8;
  }
  // Convert from UTF-16LE to UTF-8 if input is UTF-16LE.
  // Checking for a 0 byte is a hack: valid .rc files can start with a
  // character that needs both bytes of a UFT-16 unit.
  // Microsoft cl.exe silently produces a .res file with nothing but the
  // 32-bit header when handed UTF-16BE input (with or without BOM).
  else if (s.size() >= 2 &&
      ((uint8_t(s[0]) == 0xff && uint8_t(s[1]) == 0xfe) || s[1] == '\0')) {
    // Make sure s.data() ends in two \0 bytes, i.e. one \0 Char16.
    s += std::string("\0", 1);
#if defined(__linux__) && GCC_VERSION < 50100
    std::vector<uint8_t> buffer(s.size() * 6);
    const UTF16* src_start = (UTF16*)s.data();
    const UTF16* src_end = src_start + s.size()/2;
    UTF8* dst_start = (UTF8*)buffer.data();
    ConversionResult r = ConvertUTF16toUTF8(
        &src_start, src_end,
        &dst_start, (UTF8*)buffer.data() + buffer.size(),
        strictConversion);
    if (r != conversionOK) {
      fprintf(stderr, "rc: tried to decode input as UTF-16LE and failed\n");
      return 1;
    }
    s = std::string((char*)buffer.data(), dst_start - (UTF8*)buffer.data());
#else
    // wstring_convert throws on error, or returns a fixed string handed to
    // its ctor if present. Pass an invalid UTF-8 string as error string,
    // then we can be sure if we get that back it must have been returned as
    // error string.
    std::wstring_convert<
        std::codecvt_utf8_utf16<Char16, 0x10ffff, std::little_endian>,
        Char16>
        convert("\x80");
    s = convert.to_bytes(reinterpret_cast<const Char16*>(s.data()));
    if (s == "\x80") {
      fprintf(stderr, "rc: tried to decode input as UTF-16LE and failed\n");
      return 1;
    }
#endif
    encoding = kEncodingUTF8;
    // FIXME:
    // Tests for:
    // - utf-16le with non-BMP
    // - utf-16 with invalid bytes in the middle
  }

  std::string err;
  std::vector<Token> tokens = Tokenizer::Tokenize(s, &err);
  if (tokens.empty() && !err.empty()) {
    fprintf(stderr, "rc: %s\n", err.c_str());
    return 1;
  }
  StringStorage string_storage;
  std::unique_ptr<FileBlock> file =
      Parser::Parse(std::move(tokens), encoding, &string_storage, &err);
  if (!file) {
    fprintf(stderr, "rc: %s\n", err.c_str());
    return 1;
  }
  if (!WriteRes(*file.get(), output, includes, show_includes, encoding, &err)) {
    fprintf(stderr, "rc: %s\n", err.c_str());
    return 1;
  }
}
