"""Reads the action engine's text banks (UKTxt.Dat, USATxt.Dat, ... from the disc's filesys archives), as
Txt_LoadLanguage (0x6d250) and Txt_BindLabel (0x6d460) do.

File layout: dword dataSize, dataSize bytes of NUL-terminated strings, 1-4 bytes of padding (4 - dataSize % 4,
so 4 when already aligned), dword numEntries, then numEntries - 1 dword string offsets (entry 0 is "", entry 1
starts at offset 0; the first dword read is entry 1's), the last of which doubles as numFixups, then numFixups
dword fixups. A text label (Action_TranslatedText) is (bank << 24) | index; its entry is index + fixup[bank].
Entry 0 means "a string from the heap" (a buffer the caller fills in), and an entry past the end reads
"Invalid Text Label".

  python text_bank.py UKTxt.Dat                  # every label: 0x0100000a  Records ...
  python text_bank.py UKTxt.Dat 0x0100000a 0x1b6 # just those
"""
import struct
import sys


class TextBank:
    def __init__(self, data):
        size = struct.unpack_from('<I', data, 0)[0]
        self.data = data[4:4 + size]
        p = 4 + size + (4 - (size & 3))
        self.num_entries = struct.unpack_from('<I', data, p)[0]
        p += 8
        offsets = [0]
        for _ in range(2, self.num_entries):
            offsets.append(struct.unpack_from('<I', data, p)[0])
            p += 4
        num_fixups = struct.unpack_from('<I', data, p)[0]
        p += 4
        self.fixups = list(struct.unpack_from('<%dI' % num_fixups, data, p))
        self.strings = [''] + [self._cstr(o) for o in offsets]

    def _cstr(self, off):
        end = self.data.index(b'\0', off)
        return self.data[off:end].decode('cp1252')

    def entry(self, label):
        bank = label >> 24
        if bank >= len(self.fixups):
            return None
        return (label & 0xffffff) + self.fixups[bank]

    def get(self, label):
        """the string Txt_BindLabel returns for a label (None for a heap string or an invalid label)"""
        if label == 0xffffffff:
            return None
        e = self.entry(label)
        if e is None or e >= self.num_entries:
            return 'Invalid Text Label'
        if e == 0:
            return None
        return self.strings[e]

    def labels(self):
        """every (label, string) the bank holds, a label per entry (the lowest bank that reaches it)"""
        seen = {}
        for bank, fix in enumerate(self.fixups):
            end = self.fixups[bank + 1] if bank + 1 < len(self.fixups) else self.num_entries
            for e in range(max(fix, 1), end):
                seen.setdefault(e, (bank << 24) | (e - fix))
        return [(seen[e], self.strings[e]) for e in sorted(seen)]


if __name__ == '__main__':
    tb = TextBank(open(sys.argv[1], 'rb').read())
    if len(sys.argv) > 2:
        for a in sys.argv[2:]:
            lab = int(a, 0)
            print('0x%08x  %r' % (lab, tb.get(lab)))
    else:
        print('# %d entries, fixups %s' % (tb.num_entries, ' '.join(hex(f) for f in tb.fixups)))
        for lab, s in tb.labels():
            print('0x%08x  %s' % (lab, s.replace('\n', '\\n')))
