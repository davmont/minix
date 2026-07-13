/*	MINIX: forwarder for wayland's two spellings of the config header.
 *
 * meson drops config.h in the build root, so wayland's sources reach it two
 * different ways: wayland-shm.c says #include "config.h" and wayland-os.c says
 * #include "../config.h".  With -I<minix>/include the first resolves to
 * include/config.h and the second to this file.  Keep the answers in one place
 * and forward.
 */
#include "include/config.h"
