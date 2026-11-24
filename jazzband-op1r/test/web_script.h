/* the scripted session, shared word for word with tests/web.test.cjs (C and JS read the same lines) */
jbw_set(10, 10); jbw_set(11, 9); jbw_set(12, 8); jbw_set(13, 7);
jbw_play(); run(1500); cp("play");
jbw_key(2); run(1000); cp("key");
jbw_set(1, 1); jbw_set(4, 1); run(2500); cp("loop");
jbw_song(2, -1, 0); run(1500); cp("queued");
jbw_set(7, 2); jbw_set(8, 2); jbw_tempo(200); jbw_set(6, 2); run(1500); cp("bossa");
jbw_finish(); run(4000); cp("finish");
jbw_song(1, -1, 0); jbw_set(7, 1); jbw_set(9, 1); jbw_bar(2, 0); run(3000); cp("waltz");
jbw_bar(0, 1); run(800); cp("jump");
jbw_level(0, 0); jbw_mute(0); jbw_stop(); run(300); cp("stop");
