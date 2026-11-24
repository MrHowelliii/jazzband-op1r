CC = cc
HOST_FLAGS = -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
INCLUDES = -Imock -Iplugins/jazzband -DJB_HOST_OP1_TEMPO

.PHONY: test preview test-songbook test-styles test-usb test-packs test-setlists test-form test-all web test-web

# the companion's play-along: this plugin compiled to WebAssembly (clang, wasm32, no libc), bundled for the page
WASM_FLAGS = --target=wasm32 -mcpu=mvp -O2 -ffreestanding -nostdlib -fno-builtin -Wall -Wno-unused-function \
             -Wl,--no-entry -Wl,--export-dynamic -Wl,--initial-memory=2097152 -Wl,--strip-all
out/jazzband.wasm: web/jb_wasm.c web/op1_plugin.h plugins/jazzband/jazzband.c plugins/jazzband/jb_tables.h
	mkdir -p out
	clang $(WASM_FLAGS) -Iweb -Imock -Iplugins/jazzband web/jb_wasm.c -o $@
web: out/jazzband.wasm web/jbw-glue.js
	python3 web/bundle.py out/jazzband.wasm web/jbw-glue.js ../jazzband-web.js
# the same session natively (clang: C leaves argument evaluation order open, and gcc draws a few random numbers in
# another order than clang) and through the page's glue in Node: they must match sample for sample
test-web: web
	clang -O1 -g -fsanitize=undefined -fsanitize-trap=all -DJBW_NATIVE -Iweb -Imock -Iplugins/jazzband test/web_test.c -lm -o out/web_test
	node ../tests/web.test.cjs

out/host_test_sanitized: test/host_test.c plugins/jazzband/jazzband.c plugins/jazzband/jb_tables.h mock/op1_plugin.h mock/seq/seq_text.h
	mkdir -p out
	$(CC) $(HOST_FLAGS) $(INCLUDES) test/host_test.c -lm -o $@

test: out/host_test_sanitized
	./out/host_test_sanitized 1 140

preview: test
	python3 render_frames.py out/frames.txt out/navigation-preview.png 'stopped,song browser,waltz (3/4),players'

test-setlists:
	mkdir -p out
	$(CC) $(HOST_FLAGS) $(INCLUDES) test/setlist_test.c -lm -o out/setlist_test
	./out/setlist_test
	python3 render_frames.py out/setlist_frames.txt out/setlist-preview.png || true

test-form:
	mkdir -p out
	$(CC) $(HOST_FLAGS) $(INCLUDES) test/form_test.c -lm -o out/form_test
	./out/form_test
	python3 render_frames.py out/form_frames.txt out/form-preview.png || true

test-songbook:
	node ../tests/companion.test.cjs
	mkdir -p out
	$(CC) $(HOST_FLAGS) $(INCLUDES) test/songbook_test.c -lm -o out/songbook_test
	./out/songbook_test ../tests/songbook-test.raw ../tests/songbook-edited-test.raw ../tests/songbook-list-test.raw
	python3 render_frames.py out/browser_frames.txt out/browser-preview.png || true

test-styles:
	mkdir -p out
	$(CC) $(HOST_FLAGS) $(INCLUDES) test/styles_test.c -lm -o out/styles_test
	./out/styles_test

test-usb: test-songbook
	$(CC) $(HOST_FLAGS) $(INCLUDES) -Imock/op1r-loader test/usb_songs_test.c -lm -o out/usb_songs_test
	./out/usb_songs_test ../tests/jbsongs-test.op1p ../tests/songbook-test.raw

test-packs:
	mkdir -p out
	$(CC) $(HOST_FLAGS) $(INCLUDES) -Imock/op1r-loader test/packs_test.c -lm -o out/packs_test
	./out/packs_test packs/out
	python3 render_frames.py out/packs_frames.txt out/packs-preview.png || true

test-all: test test-web test-styles test-songbook test-usb test-packs test-setlists test-form
