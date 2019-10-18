#include <stdio.h>
#include <xcb/xcb.h>

#include "wm.h"
#include "config.h"

#define LEN(x) (sizeof(x)/sizeof(x[0]))
#define NO_EVENT 0

struct ev_callback_t {
	uint32_t type;
	int (*handle)(xcb_generic_event_t *);
};

static int ev_callback(xcb_generic_event_t *);

/* XCB events callbacks */
static int cb_default(xcb_generic_event_t *);
static int cb_create(xcb_generic_event_t *);
static int cb_mouse_press(xcb_generic_event_t *);
static int cb_mouse_release(xcb_generic_event_t *);

int verbose = 1;
xcb_connection_t *conn;
xcb_screen_t     *scrn;

static const struct ev_callback_t cb[] = {
	/* event,             function */
	{ XCB_CREATE_NOTIFY,  cb_create },
	{ XCB_BUTTON_PRESS,   cb_mouse_press },
	{ XCB_BUTTON_RELEASE, cb_mouse_release },
	{ NO_EVENT,           cb_default },
};

static int
cb_default(xcb_generic_event_t *ev)
{
	if (verbose)
		fprintf(stderr, "event not handled: %d\n", ev->response_type);

	return 0;
}

static int
cb_create(xcb_generic_event_t *ev)
{
	uint32_t val[2];
	xcb_create_notify_event_t *e;

	e = (xcb_create_notify_event_t *)ev;

	if (verbose)
		fprintf(stderr, "create: 0x%08x\n", e->window);

	wm_set_border(border, border_color, e->window);
	wm_set_focus(e->window);

	return 0;
}

static int
cb_mouse_press(xcb_generic_event_t *ev)
{
	xcb_button_press_event_t *e;

	e = (xcb_button_press_event_t *)ev;
	if (verbose)
		fprintf(stderr, "mouse_press: 0x%08x\n", e->child);

	return 0;
}

static int
cb_mouse_release(xcb_generic_event_t *ev)
{
	xcb_button_release_event_t *e;

	e = (xcb_button_release_event_t *)ev;
	if (verbose)
		fprintf(stderr, "mouse_release: 0x%08x\n", e->child);
	return 0;
}

static int
ev_callback(xcb_generic_event_t *ev)
{
	uint8_t i;
	uint32_t type;

	type = ev->response_type & ~0x80;
	for (i=0; i<LEN(cb); i++)
		if (type == cb[i].type)
			return cb[i].handle(ev);

	return 1;
}

int
main (int argc, char *argv[])
{
	xcb_generic_event_t *ev = NULL;

	wm_init_xcb();
	wm_get_screen();

	/* grab mouse clicks for window movement */
	xcb_grab_button(conn, 1, scrn->root,
		XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
		XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE,
		XCB_BUTTON_INDEX_ANY, MODMASK);

	/* needed to get notified of windows creation */
	wm_reg_event(scrn->root, XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY);
	xcb_flush(conn);

	for (;;) {
		if (!(ev = xcb_wait_for_event(conn)))
			break;

		ev_callback(ev);
	}

	wm_kill_xcb();

	return 0;
}