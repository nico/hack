import codecs
import os
import sys
try:
  from ply import lex
except ImportError:
  # ply not available on system, try falling back to a chrome checkout.
  # FIXME: just check in ply in local third_party.
  sys.path.insert(0, os.path.join('/src/chrome/src/third_party'))
  from ply import lex

# https://msdn.microsoft.com/en-us/library/windows/desktop/dd996906(v=vs.85).aspx
tokens = (
  'COLON',
  'COMMENT',
  'DOT',
  'EQUAL',
  'LPAREN',
  'RPAREN',
  'identifier',
  'number',
  # Header section keywords:
  'kwFacilityNames',
  'kwLanguageNames',
  'kwMessageIdTypedef',
  'kwOutputBase',
  'kwSeverityNames',
  # Message section keywords:
  'kwFacility',
  'kwLanguage',
  'kwMessageId',
  #'kwOutputBase' # Already present in header section.
  'kwSeverity',
  'kwSymbolicName',
  )

t_COLON = ':'
t_COMMENT = ';[^\n]*'
t_DOT = '^.$'
t_EQUAL = '='
t_LPAREN = r'\('
t_RPAREN = r'\)'
t_identifier = '[a-zA-Z_][a-zA-Z_0-9]*'
t_number = '0[xX][0-9a-fA-F]+|[0-9]+'
# Make token rules for all the tokens starting with 'kw':
for t in tokens:
  if t.startswith('kw'):
    globals()['t_' + t] = t[len('kw'):]

def t_whitespace(t):
  r'(?:(?:\r?\n)|[ \t])+'
  return None  # Ignore newlines.

lexer = lex.lex()
in_data = sys.stdin.read()
if in_data.startswith(codecs.BOM_UTF16_LE):
  in_data = in_data[len(codecs.BOM_UTF16_LE):]
  in_data = in_data.decode('utf-16le')
lexer.input(in_data)

for tok in iter(lex.token, None):
  print repr(tok.type), repr(tok.value)
