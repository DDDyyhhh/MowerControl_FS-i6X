#!/usr/bin/env python3
# Mach-O assembler emits ltmp labels even for empty disabled translation units.
import re,subprocess,sys
r=subprocess.run(['/usr/bin/nm',*sys.argv[1:]],capture_output=True,text=True)
if '--defined-only' in sys.argv:
 sys.stdout.write(''.join(line+'\n' for line in r.stdout.splitlines() if not re.search(r'\bltmp[0-9]+$',line)))
else:
 sys.stdout.write(r.stdout)
sys.stderr.write(r.stderr)
sys.exit(r.returncode)
