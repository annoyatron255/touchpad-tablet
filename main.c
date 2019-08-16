#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <linux/input.h>

#include <libevdev-1.0/libevdev/libevdev.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

enum scaling_mode {
	FULL,
	ASPECT_FILL_X,
	ASPECT_FILL_Y,
	NONE
};

int debug_output = false;

int handle_event(Display *dpy, Window target_window, const struct input_event *ev,
	         const struct input_absinfo *x_absinfo,
	         const struct input_absinfo *y_absinfo,
	         const struct input_absinfo *pressure_absinfo,
	         int click_threshold, int release_threshold, int movement_threshold,
		 int scaling_mode) {
	static int abs_x;
	static int abs_y;
	static int abs_pressure;
	XWindowAttributes target_attr;

	if (ev->type == EV_SYN) {
		if (abs_pressure >= movement_threshold) {
			if (XGetWindowAttributes(dpy, target_window, &target_attr) == 0) {
				fprintf(stderr, "ERROR: Failed to get window attributes\n");
				exit(1);
			}
			if (debug_output)
				printf("DEBUG: target_window width: %d height: %d\n",
				       target_attr.width, target_attr.height);

			int x, y;

			switch (scaling_mode) {
				case FULL:
					x = abs_x * target_attr.width / x_absinfo->maximum;
					y = abs_y * target_attr.height / y_absinfo->maximum;
					break;
				case ASPECT_FILL_X:
					x = abs_x * target_attr.width / x_absinfo->maximum;
					y = abs_y * target_attr.width / x_absinfo->maximum;
					break;
				case ASPECT_FILL_Y:
					x = abs_x * target_attr.height / y_absinfo->maximum;
					y = abs_y * target_attr.height / y_absinfo->maximum;
					break;
				case NONE:
					x = abs_x;
					y = abs_y;
					break;
				default:
					fprintf(stderr, "ERROR: Invalid scaling mode: %d\n",
					        scaling_mode);
					exit(1);
			}

			XWarpPointer(dpy, None, target_window, 0, 0, 0, 0, x, y);
		}

		if (abs_pressure >= click_threshold)
			XTestFakeButtonEvent(dpy, 1, true, 0);
		else if (abs_pressure <= release_threshold)
			XTestFakeButtonEvent(dpy, 1, false, 0);

		XFlush(dpy);
	} else if (ev->type != EV_ABS)
		return 0;

	switch (ev->code) {
		case ABS_X:
			if (ev->value != 0) // Spurious jumps to zero only on ABS_X(?)
				abs_x = ev->value;
			break;
		case ABS_Y:
			abs_y = ev->value;
			break;
		case ABS_PRESSURE:
			abs_pressure = ev->value;
			break;
	}
	return 0;
}

int main (int argc, char **argv) {
	int click_threshold = 50;
	int release_threshold = 20;
	int movement_threshold = 0;
	static int scaling_mode = ASPECT_FILL_X;
	Window target_window = 0;

	while (1) {
		static struct option long_options[] = {
			{"debug", no_argument, &debug_output, 1},
			{"scale-full", no_argument, &scaling_mode, FULL},
			{"scale-aspect-fill-x", no_argument, &scaling_mode, ASPECT_FILL_X},
			{"scale-aspect-fill-y", no_argument, &scaling_mode, ASPECT_FILL_Y},
			{"scale-none", no_argument, &scaling_mode, NONE},
			{"click-threshold", required_argument, 0, 'c'},
			{"release-threshold", required_argument, 0, 'r'},
			{"movement-threshold", required_argument, 0, 'm'},
			{"window", required_argument, 0, 'w'},
			{"help", no_argument, 0, 'h'},
			{0, 0, 0, 0}
		};

		int option_index = 0;

		int c = getopt_long(argc, argv, "c:r:m:w:h", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
			case 0:
				break;
			case 'c':
				click_threshold = atoi(optarg);
				break;
			case 'r':
				release_threshold = atoi(optarg);
				break;
			case 'm':
				movement_threshold = atoi(optarg);
				break;
			case 'w':
				target_window = atoi(optarg);
				break;
			case 'h':
				printf("Usage: touchpad-tablet [OPTION] [DEVICE]\n"
				       "Use touchpad as psuedo-tablet on a window.\n"
				       "\n"
				       "Mandatory arguments to long options are mandatory for short options too.\n"
				       "      --debug                       print debugging messages\n"
				       "      --scale-full                  set scaling to stretch to fill on\n"
				       "                                      both x and y axises\n"
				       "      --scale-aspect-fill-x         set scaling to stretch to fill on\n"
				       "                                      x axis and to maintain aspect ratio\n"
				       "      --scale-aspect-fill-y         set scaling to stretch to fill on\n"
				       "                                      y axis and to maintain aspect ratio\n"
				       "      --scale-none                  disable scaling and use touchpad\n"
				       "                                      values directly\n"
				       "  -c, --click-threshold=INTEGER     set pressure threshold to trigger a\n"
				       "                                      mouse down event\n"
				       "  -r, --release-threshold=INTEGER   set pressure threshold to end a mouse\n"
				       "                                      down event\n"
				       "  -m, --movement-threshold=INTEGER  set pressure threshold to allow\n"
				       "                                      movement of cursor\n"
				       "  -w, --window=WINDOWID             window id to bind to. defaults to\n"
				       "                                      root window\n"
				       "  -h, --help                        display this help and exit\n"
				       "\n"
				       "Source: <https://github.com/annoyatron255/touchpad-tablet>\n");
				return 0;
			case '?':
				// Error message printed by getopts_long
				return 1;
			default:
				abort();
		}
	}

	struct libevdev *dev = NULL;
	int fd;
	int rc = 1;

	if (optind < argc)
		fd = open(argv[optind], O_RDONLY);//|O_NONBLOCK);
	else
		fd = open("/dev/input/event15", O_RDONLY);//|O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "ERROR: Failed to open device: %s\n", strerror(errno));
		return 1;
	}

	rc = libevdev_new_from_fd(fd, &dev);
	if (rc < 0) {
		fprintf(stderr, "ERROR: Failed to init libevdev: %s\n", strerror(-rc));
		return 1;
	}

	printf("Input device name: \"%s\"\n", libevdev_get_name(dev));
	printf("Input device ID: bus %#x vendor %#x product %#x\n",
	       libevdev_get_id_bustype(dev),
	       libevdev_get_id_vendor(dev),
	       libevdev_get_id_product(dev));

	if (!libevdev_has_event_code(dev, EV_ABS, ABS_X) ||
	    !libevdev_has_event_code(dev, EV_ABS, ABS_Y) ||
	    !libevdev_has_event_code(dev, EV_ABS, ABS_PRESSURE)) {
		fprintf(stderr, "ERROR: This device does not support absolute coordinates and pressure\n");
		return 1;
	}

	const struct input_absinfo *x, *y, *pressure;
	x = libevdev_get_abs_info(dev, ABS_X);
	y = libevdev_get_abs_info(dev, ABS_Y);
	pressure = libevdev_get_abs_info(dev, ABS_PRESSURE);

	if (debug_output) {
		printf("DEBUG: ABS_X min: %d max: %d\n", x->minimum, x->maximum);
		printf("DEBUG: ABS_Y min: %d max: %d\n", y->minimum, y->maximum);
		printf("DEBUG: ABS_PRESSURE min: %d max: %d\n", pressure->minimum, pressure->maximum);
	}

	if (click_threshold < pressure->minimum ||
	    click_threshold > pressure->maximum ||
	    release_threshold < pressure->minimum ||
	    release_threshold > pressure->maximum) {
		fprintf(stderr, "ERROR: Threshold(s) out of range\n");
		return 1;
	}

	if (click_threshold < release_threshold)
		fprintf(stderr, "WARNING: click threshold less than release threshold\n");

	Display *dpy;
	Window root_window;

	dpy = XOpenDisplay(0);
	root_window = XRootWindow(dpy, 0);
	if (debug_output)
		printf("DEBUG: Root window id: %x\n", root_window);
	XSelectInput(dpy, root_window, KeyReleaseMask);

	if (!target_window)
		target_window = root_window;

	do {
		struct input_event ev;
		rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL|LIBEVDEV_READ_FLAG_BLOCKING, &ev);
		if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
			handle_event(dpy, target_window, &ev, x, y, pressure, click_threshold,
			             release_threshold, movement_threshold, scaling_mode);
		}
	} while (rc == 1 || rc == 0 || rc == -EAGAIN);
}
