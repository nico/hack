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
  # Header section keywords:
  'MESSAGEIDTYPEDEF',
  'SEVERITYNAMES',
  'FACILITYNAMES',
  'LANGUAGENAMES',
  'OUTPUTBASE',
  # Message section keywords:
  # "The MessageId statement marks the beginning of the message definition"
  # The docs claim that Language is optional, but in practice mc.exe seems to
  # treat it at the end of the header section of a message and treats everything
  # after it as message text (up to the "." line). If Language isn't present,
  # mc.exe errors out.
  # FIXME: In particular, putting e.g. SymbolicName= after the Language= line
  # includes the SymbolicName= line literally in the generated .bin file.
  # Use a conditional lexer (4.19) (or don't use ply) to get that right.
  'MESSAGEID',
  'SEVERITY',
  'FACILITY',
  'SYMBOLICNAME',
  'LANGUAGE',
  #'OUTPUTBASE' # Already present in header section.

  'message',  # Must be last.
  )

t_COMMENT = ';[^\n]*'
name = r'\s*=\s*[a-zA-Z_][a-zA-Z_0-9]+'
t_MESSAGEIDTYPEDEF = 'MessageIdTypedef' + name
parenlist = r'\s*=\s*\([^)]*\)'
t_SEVERITYNAMES = 'SeverityNames' + parenlist
t_FACILITYNAMES = 'FacilityNames' + parenlist
t_LANGUAGENAMES = 'LanguageNames' + parenlist
t_OUTPUTBASE = r'OutputBase\s*=\s*(?:10|16)'

t_MESSAGEID = 'MessageId\s*=\s*\+?(?:0[xX][0-9a-fA-F]+|[0-9]+)'
t_SEVERITY = 'Severity' + name
t_FACILITY = 'Facility' + name
t_SYMBOLICNAME = 'SymbolicName' + name
t_LANGUAGE = 'Language' + name

t_message = r'.*\r?\n\.\r?\n'
t_ignore = '[ \t]*'

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
