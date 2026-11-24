"""Bundle the HTML companion and its local core into a standalone HTML file."""
from pathlib import Path

base = Path(__file__).resolve().parent
source = (base / 'Jazz Band.html').read_text()
core = (base / 'companion-core.js').read_text()
assert '</script' not in core.lower()
output = source.replace('<script src="companion-core.js"></script>', '<script>\n' + core + '\n</script>')
assert output != source
path = base / 'Jazz Band Companion.html'
path.write_text(output)
print(path)
