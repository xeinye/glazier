#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>

#include "arg.h"
#include "wm.h"
#include "config.h"

#define LEN(x) (sizeof(x)/sizeof(x[0]))
#define XEV(x) (evname[(x)->response_type & ~0x80])

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
static int cb_mapreq(xcb_generic_event_t *);
static int cb_destroy(xcb_generic_event_t *);
static int cb_mouse_press(xcb_generic_event_t *);
static int cb_mouse_release(xcb_generic_event_t *);
static int cb_motion(xcb_generic_event_t *);
static int cb_enter(xcb_generic_event_t *);
static int cb_configure(xcb_generic_event_t *);
static int cb_configreq(xcb_generic_event_t *);

int verbose = 0;
xcb_connection_t *conn;
xcb_screen_t     *scrn;
xcb_window_t      curwid;
struct cursor_t   cursor;

static const char *evname[] = {
	[0] = "EVENT_ERROR",
	[XCB_CREATE_NOTIFY] = "CREATE_NOTIFY",
	[XCB_DESTROY_NOTIFY] = "DESTROY_NOTIFY",
	[XCB_BUTTON_PRESS] = "BUTTON_PRESS",
	[XCB_BUTTON_RELEASE] = "BUTTON_RELEASE",
	[XCB_MOTION_NOTIFY] = "MOTION_NOTIFY",
	[XCB_ENTER_NOTIFY] = "ENTER_NOTIFY",
	[XCB_CONFIGURE_NOTIFY] = "CONFIGURE_NOTIFY",
	[XCB_KEY_PRESS] = "KEY_PRESS",
	[XCB_FOCUS_IN] = "FOCUS_IN",
	[XCB_KEYMAP_NOTIFY] = "KEYMAP_NOTIFY",
	[XCB_EXPOSE] = "EXPOSE",
	[XCB_GRAPHICS_EXPOSURE] = "GRAPHICS_EXPOSURE",
	[XCB_NO_EXPOSURE] = "NO_EXPOSURE",
	[XCB_VISIBILITY_NOTIFY] = "VISIBILITY_NOTIFY",
	[XCB_UNMAP_NOTIFY] = "UNMAP_NOTIFY",
	[XCB_MAP_NOTIFY] = "MAP_NOTIFY",
	[XCB_MAP_REQUEST] = "MAP_REQUEST",
	[XCB_REPARENT_NOTIFY] = "REPARENT_NOTIFY",
	[XCB_CONFIGURE_REQUEST] = "CONFIGURE_REQUEST",
	[XCB_GRAVITY_NOTIFY] = "GRAVITY_NOTIFY",
	[XCB_RESIZE_REQUEST] = "RESIZE_REQUEST",
	[XCB_CIRCULATE_NOTIFY] = "CIRCULATE_NOTIFY",
	[XCB_PROPERTY_NOTIFY] = "PROPERTY_NOTIFY",
	[XCB_SELECTION_CLEAR] = "SELECTION_CLEAR",
	[XCB_SELECTION_REQUEST] = "SELECTION_REQUEST",
	[XCB_SELECTION_NOTIFY] = "SELECTION_NOTIFY",
	[XCB_COLORMAP_NOTIFY] = "COLORMAP_NOTIFY",
	[XCB_CLIENT_MESSAGE] = "CLIENT_MESSAGE",
	[XCB_MAPPING_NOTIFY] = "MAPPING_NOTIFY"
};

static const struct ev_callback_t cb[] = {
	/* event,             function */
	{ XCB_MAP_REQUEST,    cb_mapreq },
	{ XCB_DESTROY_NOTIFY, cb_destroy },
	{ XCB_BUTTON_PRESS,   cb_mouse_press },
	{ XCB_BUTTON_RELEASE, cb_mouse_release },
	{ XCB_MOTION_NOTIFY,  cb_motion },
	{ XCB_ENTER_NOTIFY,   cb_enter },
	{ XCB_CONFIGURE_NOTIFY, cb_configure },
	{ XCB_CONFIGURE_REQUEST, cb_configreq },
};

void
usage(char *name)
{
	fprintf(stderr, "usage: %s [-vh]\n", name);
}

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

	xcb_reparent_window(conn, child, parent, 0, titlebar);
	xcb_map_window(conn, parent);

	return parent;
}

xcb_window_t
get_frame(xcb_window_t wid)
{
	xcb_window_t frame = scrn->root;
	xcb_query_tree_cookie_t c;
	xcb_query_tree_reply_t *r;

	for (;;) {
		c = xcb_query_tree(conn, wid);
		r = xcb_query_tree_reply(conn, c, NULL);
		if (r == NULL)
			return scrn->root;

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
	if (verbose && XEV(ev)) {
		fprintf(stderr, "%s not handled\n", XEV(ev));
	} else if (verbose) {
		fprintf(stderr, "EVENT %d not handled\n", ev->response_type);
	}

	return 0;
}

static int
cb_mapreq(xcb_generic_event_t *ev)
{
	int x, y, w, h;
	static xcb_window_t frame;
	xcb_map_request_event_t *e;

	e = (xcb_map_request_event_t *)ev;

	/* avoid infinite loops when creating frame window */
	if (frame == e->window || get_frame(e->window) != scrn->root)
		return 0;

	if (verbose)
		fprintf(stderr, "%s: 0x%08x\n", XEV(e), e->window);

	frame = frame_window(e->window);

	w = wm_get_attribute(frame, ATTR_W);
	h = wm_get_attribute(frame, ATTR_H);
	wm_get_cursor(0, scrn->root, &x, &y);

	wm_set_border(border, titlebar_color, frame);
	wm_move(frame, ABSOLUTE, x - w/2, y - h/2);
	xcb_map_window(conn, e->window);
	wm_set_focus(e->window);

	return 0;
}

static int
cb_destroy(xcb_generic_event_t *ev)
{
	xcb_create_notify_event_t *e;

	e = (xcb_create_notify_event_t *)ev;
	if (verbose)
		fprintf(stderr, "%s 0x%08x 0x%08x\n", XEV(e), e->window, e->parent);

	xcb_destroy_window(conn, e->parent);

	return 0;
}

static int
cb_mouse_press(xcb_generic_event_t *ev)
{
	int x, y, w, h;
	xcb_cursor_t p;
	static xcb_timestamp_t lasttime = 0;
	xcb_cursor_context_t *cx;
	xcb_grab_pointer_cookie_t c;
	xcb_grab_pointer_reply_t *r;
	xcb_button_press_event_t *e;

	e = (xcb_button_press_event_t *)ev;

	/* ignore some motion events if they happen too often */
	if (e->time - lasttime < 32)
		return 0;

	if (verbose)
		fprintf(stderr, "%s 0x%08x\n", XEV(e), e->event);

	if (xcb_cursor_context_new(conn, scrn, &cx) < 0) {
		fprintf(stderr, "cannot instantiate cursor\n");
		exit(1);
	}

	wm_restack(e->event, XCB_STACK_MODE_ABOVE);

	cursor.x = e->root_x - wm_get_attribute(e->event, ATTR_X);
	cursor.y = e->root_y - wm_get_attribute(e->event, ATTR_Y);
	cursor.b = e->detail;
	lasttime = e->time;

	switch(e->detail) {
	case 1:
		curwid = e->event;
		p = xcb_cursor_load_cursor(cx, XHAIR_MOVE);
		break;
	case 2:
		xcb_kill_client(conn, get_child(e->event));
		break;
	case 3:
		curwid = e->event;
		p = xcb_cursor_load_cursor(cx, XHAIR_SIZE);
		break;
	case 4:
		x = wm_get_attribute(e->event, ATTR_X) - move_step/2;
		y = wm_get_attribute(e->event, ATTR_Y);
		w = wm_get_attribute(e->event, ATTR_W) + move_step;
		h = wm_get_attribute(e->event, ATTR_H) + move_step;
		wm_teleport(e->event, x, y, w, h);
		wm_teleport(get_child(e->event), 0, titlebar, w, h - titlebar);
		break;
	case 5:
		x = wm_get_attribute(e->event, ATTR_X) + move_step/2;
		y = wm_get_attribute(e->event, ATTR_Y);
		w = wm_get_attribute(e->event, ATTR_W) - move_step;
		h = wm_get_attribute(e->event, ATTR_H) - move_step;
		wm_teleport(e->event, x, y, w, h);
		wm_teleport(get_child(e->event), 0, titlebar, w, h - titlebar);
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
		fprintf(stderr, "%s 0x%08x\n", XEV(e), e->child);

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
	static xcb_timestamp_t lasttime = 0;
	xcb_motion_notify_event_t *e;

	e = (xcb_motion_notify_event_t *)ev;

	/* ignore some motion events if they happen too often */
	if (e->time - lasttime < 32 || curwid == scrn->root)
		return 0;

	if (verbose)
		fprintf(stderr, "%s 0x%08x %d,%d\n", XEV(e), e->event, e->root_x, e->root_y);

	x = e->root_x;
	y = e->root_y;
	lasttime = e->time;

	switch (cursor.b) {
	case 1:
		x -= cursor.x;
		y -= cursor.y;
		wm_move(curwid, ABSOLUTE, x, y);
		break;
	case 3:
		wm_resize(curwid, ABSOLUTE, x, y);
		x = wm_get_attribute(curwid, ATTR_W);
		y = wm_get_attribute(curwid, ATTR_H);
		wm_resize(get_child(curwid), ABSOLUTE, x, y);
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
		fprintf(stderr, "%s 0x%08x\n", XEV(e), e->event);

	if (wm_get_windows(e->event, &child) == 1) {
		wm_set_focus(child[0]);
		free(child);
	} else {
		wm_set_focus(e->event);
	}

	return 0;
}

static int
cb_configure(xcb_generic_event_t *ev)
{
	int x, y;
	xcb_window_t frame;
	xcb_configure_notify_event_t *e;

	e = (xcb_configure_notify_event_t *)ev;

	if (e->window == e->event || e->event == scrn->root || e->override_redirect || curwid != scrn->root)
		return 0;

	frame = get_frame(e->window);
	if (frame == scrn->root)
		return 0;

	if (verbose)
		fprintf(stderr, "%s 0x%08x 0x%08x:%dx%d+%d+%d\n",
			XEV(e), frame, e->window,
			e->width, e->height,
			e->x, e->y);

	x = wm_get_attribute(frame, ATTR_X);
	y = wm_get_attribute(frame, ATTR_Y);
	wm_teleport(e->window, 0, titlebar, e->width, e->height);
	wm_teleport(get_frame(e->window), x + e->x, y + e->y - titlebar, e->width, e->height + titlebar);

	return 0;
}

static int
cb_configreq(xcb_generic_event_t *ev)
{
	xcb_configure_request_event_t *e;

	e = (xcb_configure_request_event_t *)ev;

	if (verbose)
		fprintf(stderr, "%s 0x%08x 0x%08x:%dx%d+%d+%d\n",
			XEV(e), e->parent, e->window,
			e->width, e->height,
			e->x, e->y);

	wm_teleport(e->window, e->x, e->y, e->width, e->height);

	return 0;
}

static int
ev_callback(xcb_generic_event_t *ev)
{
	uint8_t i;
	uint32_t type;

	if (!ev)
		return -1;

	type = ev->response_type & ~0x80;
	for (i=0; i<LEN(cb); i++)
		if (type == cb[i].type)
			return cb[i].handle(ev);

	return cb_default(ev);
}

int
main (int argc, char *argv[])
{
	int mask;
	char *argv0;
	xcb_generic_event_t *ev = NULL;

	ARGBEGIN {
	case 'v':
		verbose++;
		break;
	case 'h':
		usage(argv0);
		return 0;
		break; /* NOTREACHED */
	default:
		usage(argv0);
		return -1;
		break; /* NOTREACHED */
	} ARGEND;

	wm_init_xcb();
	wm_get_screen();

	curwid = scrn->root;

	/* needed to get notified of windows creation */
	mask = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
		| XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

	if (!wm_reg_event(scrn->root, mask)) {
		if (verbose)
			fprintf(stderr, "Cannot redirect root window event.\n");
		return -1;
	}

	for (;;) {
		xcb_flush(conn);
		ev = xcb_wait_for_event(conn);
		if (!ev)
			break;

		ev_callback(ev);
		free(ev);
	}

	wm_kill_xcb();

	return 0;
}