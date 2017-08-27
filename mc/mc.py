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

tokens = (
  'COMMENT',
  )

t_COMMENT = ';[^\n]*'

def t_newline(t):
  r'\n+'
  return None  # Ignore newlines.

lexer = lex.lex()
in_data = sys.stdin.read()
if in_data.startswith(codecs.BOM_UTF16_LE):
  in_data = in_data[len(codecs.BOM_UTF16_LE):]
  in_data = in_data.decode('utf-16le')
lexer.input(in_data)

for tok in iter(lex.token, None):
  print repr(tok.type), repr(tok.value)
