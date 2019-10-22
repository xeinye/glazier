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

struct cursor_t {
	int x;
	int y;
	int b;
};

static int ev_callback(xcb_generic_event_t *);

/* XCB events callbacks */
static int cb_default(xcb_generic_event_t *);
static int cb_create(xcb_generic_event_t *);
static int cb_destroy(xcb_generic_event_t *);
static int cb_mouse_press(xcb_generic_event_t *);
static int cb_mouse_release(xcb_generic_event_t *);
static int cb_motion(xcb_generic_event_t *);
static int cb_enter(xcb_generic_event_t *);
static int cb_configure(xcb_generic_event_t *);

int verbose = 1;
xcb_connection_t *conn;
xcb_screen_t     *scrn;
xcb_window_t      curwid;
struct cursor_t   cursor;

static const struct ev_callback_t cb[] = {
	/* event,             function */
	{ XCB_CREATE_NOTIFY,  cb_create },
	{ XCB_DESTROY_NOTIFY, cb_destroy },
	{ XCB_BUTTON_PRESS,   cb_mouse_press },
	{ XCB_BUTTON_RELEASE, cb_mouse_release },
	{ XCB_MOTION_NOTIFY,  cb_motion },
	{ XCB_ENTER_NOTIFY,   cb_enter },
	{ NO_EVENT,           cb_default },
};

xcb_window_t
frame_window(xcb_window_t child)
{
	int b,x,y,w,h,val[2];
	xcb_window_t parent;

	val[0] = titlebar_color;

	b = 0;
	x = wm_get_attribute(child, ATTR_X);
	y = wm_get_attribute(child, ATTR_Y) - titlebar;
	w = wm_get_attribute(child, ATTR_W);
	h = wm_get_attribute(child, ATTR_H) + titlebar;

	parent = xcb_generate_id(conn);
	val[0] = titlebar_color;
	val[1] =  XCB_EVENT_MASK_EXPOSURE
		| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
		| XCB_EVENT_MASK_ENTER_WINDOW
		| XCB_EVENT_MASK_BUTTON_PRESS
		| XCB_EVENT_MASK_BUTTON_RELEASE
		| XCB_EVENT_MASK_BUTTON_MOTION;

	xcb_create_window(conn, scrn->root_depth, parent, scrn->root,
		x, y, w, h, b, XCB_WINDOW_CLASS_INPUT_OUTPUT, scrn->root_visual,
		XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, val);

	if (verbose)
		fprintf(stderr, "frame: 0x%08x\n", parent);

	xcb_reparent_window(conn, child, parent, 0, titlebar);
	xcb_map_window(conn, parent);

	return parent;
}

xcb_window_t
get_frame(xcb_window_t wid)
{
	xcb_window_t frame = wid;
	xcb_query_tree_cookie_t c;
	xcb_query_tree_reply_t *r;

	for (;;) {
		c = xcb_query_tree(conn, wid);
		r = xcb_query_tree_reply(conn, c, NULL);
		if (r == NULL)
			return -1;

		wid = r->parent;
		free(r);

		if (wid == scrn->root)
			break;

		frame = wid;
	}

	return frame;
}

xcb_window_t
get_child(xcb_window_t wid)
{
	xcb_window_t child, *children;

	if (wm_get_windows(wid, &children) != 1)
		return -1;

	child = children[0];
	free(children);

	return child;
}

static int
cb_default(xcb_generic_event_t *ev)
{
	if (verbose)
		fprintf(stderr, "event received: %d\n", ev->response_type);

	return 0;
}

static int
cb_create(xcb_generic_event_t *ev)
{
	int x, y, w, h;
	static xcb_window_t frame;
	xcb_create_notify_event_t *e;

	e = (xcb_create_notify_event_t *)ev;
	if (e->override_redirect) {
		return 0;
	}

	/* avoid infinite loops when creating frame window */
	if (frame == e->window)
		return 0;

	if (verbose)
		fprintf(stderr, "create: 0x%08x\n", e->window);

	frame = frame_window(e->window);

	wm_get_cursor(0, scrn->root, &x, &y);
	w = wm_get_attribute(frame, ATTR_W);
	h = wm_get_attribute(frame, ATTR_H);
	wm_move(frame, ABSOLUTE, x - w/2, y - h/2);
	wm_reg_event(e->window, XCB_EVENT_MASK_STRUCTURE_NOTIFY);
	wm_set_focus(e->window);

	return 0;
}

static int
cb_destroy(xcb_generic_event_t *ev)
{
	xcb_create_notify_event_t *e;

	e = (xcb_create_notify_event_t *)ev;
	if (verbose)
		fprintf(stderr, "destroy: 0x%08x (0x%08x)\n", e->window, e->parent);

	xcb_destroy_window(conn, e->parent);

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
		fprintf(stderr, "mouse_press: 0x%08x\n", e->event);

	/* set window id globally for move/reshape */
	curwid = e->event;

	if (xcb_cursor_context_new(conn, scrn, &cx) < 0) {
		fprintf(stderr, "cannot instantiate cursor\n");
		exit(1);
	}

	wm_restack(curwid, XCB_STACK_MODE_ABOVE);

	cursor.x = e->root_x - wm_get_attribute(curwid, ATTR_X);
	cursor.y = e->root_y - wm_get_attribute(curwid, ATTR_Y);
	cursor.b = e->detail;

	switch(e->detail) {
	case 1:
		p = xcb_cursor_load_cursor(cx, XHAIR_MOVE);
		break;
	case 3:
		p = xcb_cursor_load_cursor(cx, XHAIR_SIZE);
		break;
	case 4:
		x = wm_get_attribute(curwid, ATTR_X) - move_step/2;
		y = wm_get_attribute(curwid, ATTR_Y) - move_step/2;
		w = wm_get_attribute(curwid, ATTR_W) + move_step;
		h = wm_get_attribute(curwid, ATTR_H) + move_step;
		wm_teleport(curwid, x, y, w, h);
		break;
	case 5:
		x = wm_get_attribute(curwid, ATTR_X) + move_step/2;
		y = wm_get_attribute(curwid, ATTR_Y) + move_step/2;
		w = wm_get_attribute(curwid, ATTR_W) - move_step;
		h = wm_get_attribute(curwid, ATTR_H) - move_step;
		wm_teleport(curwid, x, y, w, h);
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

	xcb_cursor_context_free(cx);

	cursor.x = 0;
	cursor.y = 0;
	cursor.b = 0;
	curwid = scrn->root;

	return 0;
}

static int
cb_motion(xcb_generic_event_t *ev)
{
	int x, y;
	static int last_time = 0;
	xcb_motion_notify_event_t *e;

	e = (xcb_motion_notify_event_t *)ev;

	/* ignore some motion events if they happen too often */
	if (e->time - last_time < 32 || curwid == scrn->root)
		return 0;

	if (verbose)
		fprintf(stderr, "motion: 0x%08x (%d,%d)\n", e->event, e->root_x, e->root_y);

	x = e->root_x;
	y = e->root_y;
	last_time = e->time;

	switch (cursor.b) {
	case 1:
		x -= cursor.x;
		y -= cursor.y;
		wm_move(curwid, ABSOLUTE, x, y);
		break;
	case 3:
		wm_resize(get_child(curwid), ABSOLUTE, x, y - titlebar);
		wm_resize(curwid, ABSOLUTE, x, y);
		break;
	}

	return 0;
}

static int
cb_enter(xcb_generic_event_t *ev)
{
	xcb_window_t *child;
	xcb_enter_notify_event_t *e;

	e = (xcb_enter_notify_event_t *)ev;

	if (verbose)
		fprintf(stderr, "enter: 0x%08x\n", e->event);

	if (wm_get_windows(e->event, &child) == 1) {
		wm_set_focus(child[0]);
		free(child);
	} else {
		wm_set_focus(e->event);
	}

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

	curwid = scrn->root;

	/* needed to get notified of windows creation */
	wm_reg_event(scrn->root, XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY);
	xcb_flush(conn);

	for (;;) {
		ev = xcb_wait_for_event(conn);
		if (!ev)
			break;

		ev_callback(ev);
		xcb_flush(conn);
		free(ev);
	}

	wm_kill_xcb();

	return 0;
}