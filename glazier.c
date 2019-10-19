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
static int cb_motion(xcb_generic_event_t *);

int verbose = 1;
xcb_connection_t *conn;
xcb_screen_t     *scrn;
xcb_window_t      wid;
int button = 0;

static const struct ev_callback_t cb[] = {
	/* event,             function */
	{ XCB_CREATE_NOTIFY,  cb_create },
	{ XCB_BUTTON_PRESS,   cb_mouse_press },
	{ XCB_BUTTON_RELEASE, cb_mouse_release },
	{ XCB_MOTION_NOTIFY,  cb_motion },
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
	int x, y, w, h;
	xcb_cursor_t p;
	xcb_cursor_context_t *cx;
	xcb_grab_pointer_cookie_t c;
	xcb_grab_pointer_reply_t *r;
	xcb_button_press_event_t *e;

	e = (xcb_button_press_event_t *)ev;
	if (verbose)
		fprintf(stderr, "mouse_press: 0x%08x\n", e->child);

	/* set window id globally for move/reshape */
	wid = e->child;
	button = e->detail;

	if (xcb_cursor_context_new(conn, scrn, &cx) < 0) {
		fprintf(stderr, "cannot instantiate cursor\n");
		exit(1);
	}

	switch(button) {
	case 1:
		p = xcb_cursor_load_cursor(cx, XHAIR_MOVE);
		break;
	case 3:
		p = xcb_cursor_load_cursor(cx, XHAIR_SIZE);
		break;
	case 4:
		x = wm_get_attribute(wid, ATTR_X) - move_step/2;
		y = wm_get_attribute(wid, ATTR_Y) - move_step/2;
		w = wm_get_attribute(wid, ATTR_W) + move_step;
		h = wm_get_attribute(wid, ATTR_H) + move_step;
		wm_teleport(wid, x, y, w, h);
		break;
	case 5:
		x = wm_get_attribute(wid, ATTR_X) + move_step/2;
		y = wm_get_attribute(wid, ATTR_Y) + move_step/2;
		w = wm_get_attribute(wid, ATTR_W) - move_step;
		h = wm_get_attribute(wid, ATTR_H) - move_step;
		wm_teleport(wid, x, y, w, h);
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
	xcb_cursor_context_free(cx);

	return 0;
}

static int
cb_mouse_release(xcb_generic_event_t *ev)
{
	xcb_cursor_t p;
	xcb_cursor_context_t *cx;
	xcb_button_release_event_t *e;

	e = (xcb_button_release_event_t *)ev;
	if (verbose)
		fprintf(stderr, "mouse_release: 0x%08x\n", e->child);

	if (xcb_cursor_context_new(conn, scrn, &cx) < 0) {
		fprintf(stderr, "cannot instantiate cursor\n");
		exit(1);
	}

	p = xcb_cursor_load_cursor(cx, XHAIR_DFLT);
	xcb_change_window_attributes(conn, e->child, XCB_CW_CURSOR, &p);
	xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);

	xcb_flush(conn);
	xcb_cursor_context_free(cx);
	wid = scrn->root;

	return 0;
}

static int
cb_motion(xcb_generic_event_t *ev)
{
	int x,y;
	xcb_motion_notify_event_t *e;

	e = (xcb_motion_notify_event_t *)ev;
	if (verbose)
		fprintf(stderr, "motion: 0x%08x\n", e->child);

	wm_get_cursor(0, wid, &x, &y);

	switch (button) {
	case 1:
		wm_move(wid, ABSOLUTE, x, y);
		break;
	case 3:
		wm_resize(wid, ABSOLUTE, x, y);
		break;
	}

	wm_set_border(border, border_color, wid);

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

	wid = scrn->root;

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