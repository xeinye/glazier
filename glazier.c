#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>

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
	xcb_create_notify_event_t *e;

	e = (xcb_create_notify_event_t *)ev;
	if (verbose)
		fprintf(stderr, "create: 0x%08x\n", e->window);

	wm_reg_event(e->window, XCB_EVENT_MASK_ENTER_WINDOW|XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY);
	wm_set_border(border, border_color, e->window);
	wm_set_focus(e->window);

	xcb_flush(conn);

	return 0;
}

static int
cb_mouse_press(xcb_generic_event_t *ev)
{
	xcb_cursor_t p;
	xcb_cursor_context_t *cx;
	xcb_grab_pointer_cookie_t c;
	xcb_grab_pointer_reply_t *r;
	xcb_button_press_event_t *e;

	e = (xcb_button_press_event_t *)ev;
	if (verbose)
		fprintf(stderr, "mouse_press: 0x%08x\n", e->child);

	if (xcb_cursor_context_new(conn, scrn, &cx) < 0) {
		fprintf(stderr, "cannont instantiate cursor\n");
		exit(1);
	}

	switch(e->detail) {
	case 1:
		p = xcb_cursor_load_cursor(cx, XHAIR_MOVE);
		break;
	case 3:
		p = xcb_cursor_load_cursor(cx, XHAIR_SIZE);
		break;
	default:
		return 1;
	}

	/* grab pointer and watch motion events */
	c = xcb_grab_pointer(conn, 0, scrn->root,
		XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_BUTTON_RELEASE |
		XCB_EVENT_MASK_BUTTON_MOTION,
		XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
		XCB_NONE, p, XCB_CURRENT_TIME);

	r = xcb_grab_pointer_reply(conn, c, NULL);
	if (!r || r->status != XCB_GRAB_STATUS_SUCCESS) {
		fprintf(stderr, "cannot grab pointer\n");
		return 1;
	}

	xcb_flush(conn);

	return 0;
}

static int
cb_mouse_release(xcb_generic_event_t *ev)
{
	xcb_cursor_t p;
	xcb_cursor_context_t *cx;
	xcb_grab_pointer_cookie_t c;
	xcb_grab_pointer_reply_t *r;
	xcb_button_release_event_t *e;

	e = (xcb_button_release_event_t *)ev;
	if (verbose)
		fprintf(stderr, "mouse_release: 0x%08x\n", e->child);

	xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
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