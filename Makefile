all:
	meson build
	meson compile -C build

all-arm64:
	meson build --cross-file aarch64-linux-gnu.txt
	meson compile -C build

clean:
	rm -rf build

reconfigure:
	meson --reconfigure build
	meson compile -C build
