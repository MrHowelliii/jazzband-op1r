CC = cc
HOST_FLAGS = -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
INCLUDES = -Imock -Iplugins/jazzband -DJB_HOST_OP1_TEMPO

.PHONY: test preview test-songbook test-styles test-usb test-packs test-setlists test-form test-all

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

test-all: test test-styles test-songbook test-usb test-packs test-setlists test-form
