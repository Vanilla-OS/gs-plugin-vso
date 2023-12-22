all:
	meson build
	meson compile -C build

clean:
	rm -rf build

reconfigure:
	meson --reconfigure build
	meson compile -C build
