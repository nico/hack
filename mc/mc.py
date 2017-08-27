import codecs
import os
import re
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
  'COMMENT',
  'DOT',
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

literals = ':=()'
t_COMMENT = ';[^\n]*'
t_identifier = '[a-zA-Z_][a-zA-Z_0-9]*'
t_number = '0[xX][0-9a-fA-F]+|[0-9]+'
t_ignore = r'[ \t]*'

# Make token rules for all the tokens starting with 'kw':
for t in tokens:
  if t.startswith('kw'):
    globals()['t_' + t] = t[len('kw'):]

def t_DOT(t):
  # Must be a function because functions get matched first, and t_newline
  # is a function and must be matched after this.
  r'\r?\n\.\r?\n'
  return t

def t_newline(t):
  r'(?:\r?\n)+'
  t.lexer.lineno += len(t.value)

lexer = lex.lex(reflags=re.UNICODE)
in_data = sys.stdin.read()
if in_data.startswith(codecs.BOM_UTF16_LE):
  in_data = in_data[len(codecs.BOM_UTF16_LE):]
  in_data = in_data.decode('utf-16le')
lexer.input(in_data)

for tok in iter(lex.token, None):
  print repr(tok.type), repr(tok.value)
