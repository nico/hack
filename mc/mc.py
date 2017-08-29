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
  # Also, comments betwen Language= and . get included in theo output.
  # Use a conditional lexer (4.19) (or don't use ply) to get that right.
  'MESSAGEID',
  'SEVERITY',
  'FACILITY',
  'SYMBOLICNAME',
  'LANGUAGE',
  #'OUTPUTBASE' # Already present in header section.

  'message',  # Must be last.
  )

# FIXME: "Case is ignored when comparing against keyword names."
t_COMMENT = ';[^\n]*'
name = r'\s*=\s*[a-zA-Z_][a-zA-Z_0-9]+'
t_MESSAGEIDTYPEDEF = 'MessageIdTypedef' + name
parenlist = r'\s*=\s*\([^)]*\)'
t_SEVERITYNAMES = 'SeverityNames' + parenlist
t_FACILITYNAMES = 'FacilityNames' + parenlist
t_LANGUAGENAMES = 'LanguageNames' + parenlist
t_OUTPUTBASE = r'OutputBase\s*=\s*(?:10|16)'

# The value for MessageId is optional.
t_MESSAGEID = 'MessageId\s*=(?:\s*\+?(?:0[xX][0-9a-fA-F]+|[0-9]+))?'
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

def parse_list(s):
  s = s[s.index('(')+1:s.rindex(')')]
  return [(name, int(num, 0), symbol) for name, num, symbol in
                re.findall(r'\s*([^=\s]+)\s*=\s*([^:\s]+)\s*(?::\s*(\S*))?', s)]

# outputs:
out_header = ''  # generated .h file
out_rc = ''      # generated .rc file
out_bins = {}    # .bin files referenced by .rc file
HEADER, MESSAGE = range(2)
state = HEADER
symbolic_name = None
message_id = 0  # FIXME: Make this per-facility
message_id_typedef = ''
language = 'English'
severity = 0
facility = 0
messages = {}
for tok in iter(lex.token, None):
  if tok.type == 'COMMENT':
    # FIXME: These aren't output as they're encountered, instead all comments
    # in the header block and before each message block are collected and then
    # output all together in front of the next output chunk.
    out_header += tok.value[1:].replace('\r', '') + '\n'  # Strip leading ';'
    continue

  if state == HEADER:
    # FIXME: FACILITYNAMES, SEVERITYNAMES aren't printed in the order in the
    # .mc file, instead they're first collected and then printed once the header
    # block is done.
    if tok.type == 'MESSAGEIDTYPEDEF':
      message_id_typedef = '(%s)' % tok.value.split('=', 1)[1].strip()
      continue
    if tok.type == 'SEVERITYNAMES':
      severity_names = parse_list(tok.value)
      for _, num, symbol in severity_names:
        if symbol:
          out_header += '#define %s 0x%x\n' % (symbol, num)
      # FIXME: amend existing severity_names instead, warn on collisions
      continue
    if tok.type == 'FACILITYNAMES':
      facility_names = parse_list(tok.value)
      for _, num, symbol in facility_names:
        if symbol:
          out_header += '#define %s 0x%x\n' % (symbol, num)
      # FIXME: amend existing facility_names instead, warn on collisions
      continue
    if tok.type == 'LANGUAGENAMES':
      language_names = parse_list(tok.value)
      for _, _, sym in language_names:
        assert sym
      # FIXME: amend existing language_names instead, warn on collisions
      continue
    if tok.type == 'OUTPUTBASE':
      continue # XXX
    assert tok.type == 'MESSAGEID'  # This starts a message definition.
    state = MESSAGE
    # Fall through.
  if state == MESSAGE:
    if tok.type == 'MESSAGEIDTYPEDEF':
      message_id_typedef = '(%s)' % tok.value.split('=', 1)[1].strip()
      continue
    if tok.type == 'MESSAGEID':
      symbolic_name = None
      value = tok.value.split('=', 1)[1].strip()
      if value.startswith('+'):
        value = value[1:].strip()
        pass # XXX
      elif value:
        message_id = int(value, 0)
      else:
        pass # XXX
      continue
    if tok.type == 'SEVERITY':
      sev = tok.value.split('=', 1)[1].strip()
      for name, num, _ in severity_names:
        if name == sev:
          severity = num
          break
      else:
        assert False, 'unknown severity %s' % sev
      continue
    if tok.type == 'FACILITY':
      fac = tok.value.split('=', 1)[1].strip()
      for name, num, _ in facility_names:
        if name == fac:
          facility = num
          break
      else:
        assert False, 'unknown facility %s' % fac
      continue
    if tok.type == 'SYMBOLICNAME':
      symbolic_name = tok.value.split('=', 1)[1].strip()
      continue
    if tok.type == 'OUTPUTBASE':
      continue # XXX
    if tok.type == 'LANGUAGE':
      language = tok.value.split('=', 1)[1].strip()
      continue
    if tok.type == 'message':
      mid = (severity << 30) | (facility << 16) | message_id
      messages.setdefault(language, {})[mid] = tok.value[:tok.value.rfind('.')]
      if symbolic_name:
        out_header += '#define %s (%s0x%xL)\n' % (
                          symbolic_name, message_id_typedef, mid)
        symbolic_name = None
      continue


out_rc = ''
for _, num, symbol in language_names:
  out_rc += 'LANGUAGE 0x%x,0x%x\r\n' % (num & 0xff, num >> 10)
  out_rc += '1 11 "%s.bin"\r\n' % symbol

# Output .bin format seems to have this format:
# * uint32_t num_ranges
# * num_ranges many triples of uint32_t, looking like starts and stops of
#   MessageId ranges followed by either offset of that range's data in the file
# * data packets, each consisting of an uint16_t containing the size of this
#   packet, another uint16_t that seems to be always just "1", followed by
#   an utf16-le zero-terminated string with the message text.  Data packets
#   can be followed by an additional uint16_t that's 0 to pad the packet size
#   to an uint32_t boundary.  The size of the packing is included in the
#   packet's size.
for lang, lang_messages in messages.iteritems():
  for name, _, symbol in language_names:
    if name == lang:
      outname = symbol + '.bin'
      break
  else:
    assert False
  ids = sorted(lang_messages.keys())
  ranges = [(ids[0],ids[0])]
  for i in ids[1:]:
    if i == ranges[-1][1] + 1:
      ranges[-1][1] = i
    else:
      ranges.append((i,i))
  with open(outname, 'wb') as binout:
    import struct
    binout.write(struct.pack('<I', len(ranges)))
    data_offset = 4 + len(ranges) * 3 * 4
    lengths = {}
    # Write headers.
    for range_start, range_end in ranges:
      binout.write(struct.pack('<III', range_start, range_end, data_offset))
      length = 0
      for message in range(range_start, range_end + 1):
        length += 2 + 2 + 2 * (len(lang_messages[message]) + 1)
        if length % 4 == 2:
          length += 2
      lengths[range_start] = length
      data_offset += length
    # Write message data.
    for message_id in ids:
      message = lang_messages[message_id]
      message_len = 2 + 2 + 2 * (len(message) + 1)
      need_pad = message_len % 4 == 2
      if need_pad:
        message_len += 2
      binout.write(struct.pack('<HH', message_len, 1))
      for c in message:
        binout.write(struct.pack('<H', ord(c)))
      binout.write(struct.pack('<H', 0))
      if need_pad:
        binout.write(struct.pack('<H', 0))

print out_header
print out_rc
