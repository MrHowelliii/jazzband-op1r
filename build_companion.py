"""Bundle the HTML companion, its local core and the WebAssembly band into a standalone HTML file.
Run `make web` in jazzband-op1r first when the plugin or web/ changed (it regenerates jazzband-web.js)."""
from pathlib import Path

base = Path(__file__).resolve().parent
output = source = (base / 'Jazz Band.html').read_text()
for name in ('companion-core.js', 'jazzband-web.js', 'vendor/qrcode.js'):
    code = (base / name).read_text()
    assert '</script' not in code.lower(), name
    tag = f'<script src="{name}"></script>'
    assert tag in output, name
    output = output.replace(tag, '<script>\n' + code + '\n</script>')
assert output != source
path = base / 'Jazz Band Companion.html'
path.write_text(output)
print(path)
