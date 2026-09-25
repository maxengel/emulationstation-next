#!/usr/bin/env python3
# Every command the interface builds from a credential a player typed hands
# it to the shell quoted (fork #198).
#
#   python3 tests/credential-quoting.py [es-source-root]
#
# setrootpass takes the device password, wifictl a network's name and key,
# and cloud_setup --set-syncpath the cloud folder; spliced in bare, a space
# cuts the value and $ ; & or a quote are the shell's, so the device stores
# something other than what was typed, or runs it. #198 named three
# setrootpass callers and its fix quoted two: the third, FINISH RESTORE
# PROCESS > DEVICE PASSWORD, stayed bare until 2026-09-25; the CLOUD FOLDER
# sat in double quotes, which leave $ and a quote to the shell, until the
# same day's trace (#273). So the check reads the source rather than a list
# of sites.
#
# A call site is a string literal that starts (after an optional
# "timeout N " and "/usr/bin/") with one of CREDENTIAL_COMMANDS and is
# followed by `+`. Every operand spliced into that statement must be a call
# (Utils::String::shellQuote(...), std::string(...)) or a literal: a bare
# identifier anywhere in the statement is a value handed to the shell as
# typed, whatever the first operand looks like. Exit 0 when every site
# quotes, 1 when one does not (printed as file:line with the bare names), 2
# when the scan finds no setrootpass site at all -- a scan that finds
# nothing to check has not passed.

import os
import re
import sys

CREDENTIAL_COMMANDS = ("setrootpass", "wifictl connect", "wifictl enable", "wifictl join", "wifictl forget",
                       "cloud_setup --set-syncpath", "--connect", "--host --port")
# A site inside a WIN32-only region is code no ROCKNIX build compiles (fork
# #275: the netplay command has a Windows twin beside the ROCKNIX one); the
# check reads the region markers and leaves those sites alone.
WIN32_IF = re.compile(r'^\s*#\s*(?:if\s+(?:defined\s*\(\s*)?WIN32|ifdef\s+WIN32)\b')
PP_IF = re.compile(r'^\s*#\s*if(?:def|ndef)?\b')
PP_ELSE = re.compile(r'^\s*#\s*(?:else|elif)\b')
PP_ENDIF = re.compile(r'^\s*#\s*endif\b')


def win32_only_lines(text):
    """Line numbers (1-based) inside the taken branch of a #if WIN32 block."""
    out, stack = set(), []   # stack entries: True while inside a WIN32 branch
    for n, line in enumerate(text.split("\n"), 1):
        if PP_IF.match(line):
            stack.append(bool(WIN32_IF.match(line)))
        elif PP_ELSE.match(line) and stack:
            stack[-1] = False
        elif PP_ENDIF.match(line) and stack:
            stack.pop()
        elif any(stack):
            out.add(n)
    return out
SITE = re.compile(r'"((?:timeout \d+ )?(?:/usr/bin/)?(?:%s)) (?:\\")?"\s*\+\s*' % "|".join(re.escape(c) for c in CREDENTIAL_COMMANDS))
STATEMENT_END = re.compile(r';\s*\n')
OPERAND = re.compile(r'\+\s*([A-Za-z_][\w:]*)\s*(\(?)')


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sites, bare = 0, []
    for sub in ("es-app/src", "es-core/src"):
        for dp, _dirs, files in os.walk(os.path.join(root, sub)):
            for f in files:
                if not f.endswith((".cpp", ".h")):
                    continue
                path = os.path.join(dp, f)
                text = open(path, encoding="utf-8", errors="replace").read()
                skip = win32_only_lines(text)
                for m in SITE.finditer(text):
                    if text.count("\n", 0, m.start()) + 1 in skip:
                        continue
                    sites += 1
                    end = STATEMENT_END.search(text, m.end())
                    statement = text[m.start():end.end() if end else len(text)]
                    line = text.count("\n", 0, m.start()) + 1
                    bad = [o.group(1) for o in OPERAND.finditer(statement) if not o.group(2)]
                    if bad:
                        bare.append("%s:%d  %s (bare: %s)" % (os.path.relpath(path, root), line, m.group(1), ", ".join(bad)))
    setrootpass = sum(1 for _ in re.finditer(r'"setrootpass "', "\n".join(
        open(os.path.join(dp, f), encoding="utf-8", errors="replace").read()
        for sub in ("es-app/src", "es-core/src") for dp, _d, fs in os.walk(os.path.join(root, sub))
        for f in fs if f.endswith((".cpp", ".h")))))
    if setrootpass == 0:
        print("credential-quoting: no setrootpass call site found under %s -- the scan is broken, not clean" % root)
        return 2
    for b in bare:
        print("BARE  " + b)
    print("credential-quoting: %d credential call site(s), %d quoted, %d bare" % (sites, sites - len(bare), len(bare)))
    return 1 if bare else 0


if __name__ == "__main__":
    sys.exit(main())
