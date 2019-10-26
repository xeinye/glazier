#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>

#include "arg.h"
#include "wm.h"

#define LEN(x) (sizeof(x)/sizeof(x[0]))
#define XEV(x) (evname[(x)->response_type & ~0x80])
#define MIN(x,y) ((x)>(y)?(y):(x))
#define MAX(x,y) ((x)>(y)?(x):(y))

struct ev_callback_t {
	uint32_t type;
	int (*handle)(xcb_generic_event_t *);
};

struct cursor_t {
	int x;
	int y;
	int b;
};

enum {
	XHAIR_DFLT,
	XHAIR_MOVE,
	XHAIR_SIZE,
	XHAIR_TELE,
};

#include "config.h"

void usage(char *);
static int takeover();
static int adopt(xcb_window_t);
static int inflate(xcb_window_t, int);
static int outline(xcb_drawable_t, int, int, int, int, int);
static int ev_callback(xcb_generic_event_t *);

/* XCB events callbacks */
static int cb_default(xcb_generic_event_t *);
static int cb_create(xcb_generic_event_t *);
static int cb_mapreq(xcb_generic_event_t *);
static int cb_mouse_press(xcb_generic_event_t *);
static int cb_mouse_release(xcb_generic_event_t *);
static int cb_motion(xcb_generic_event_t *);
static int cb_enter(xcb_generic_event_t *);
static int cb_focus(xcb_generic_event_t *);
static int cb_configreq(xcb_generic_event_t *);

int verbose = 0;
xcb_connection_t *conn;
xcb_screen_t     *scrn;
xcb_window_t      curwid;
struct cursor_t   cursor;

static const char *evname[] = {
	[0]                     = "EVENT_ERROR",
	[XCB_CREATE_NOTIFY]     = "CREATE_NOTIFY",
	[XCB_DESTROY_NOTIFY]    = "DESTROY_NOTIFY",
	[XCB_BUTTON_PRESS]      = "BUTTON_PRESS",
	[XCB_BUTTON_RELEASE]    = "BUTTON_RELEASE",
	[XCB_MOTION_NOTIFY]     = "MOTION_NOTIFY",
	[XCB_ENTER_NOTIFY]      = "ENTER_NOTIFY",
	[XCB_CONFIGURE_NOTIFY]  = "CONFIGURE_NOTIFY",
	[XCB_KEY_PRESS]         = "KEY_PRESS",
	[XCB_FOCUS_IN]          = "FOCUS_IN",
	[XCB_FOCUS_OUT]         = "FOCUS_OUT",
	[XCB_KEYMAP_NOTIFY]     = "KEYMAP_NOTIFY",
	[XCB_EXPOSE]            = "EXPOSE",
	[XCB_GRAPHICS_EXPOSURE] = "GRAPHICS_EXPOSURE",
	[XCB_NO_EXPOSURE]       = "NO_EXPOSURE",
	[XCB_VISIBILITY_NOTIFY] = "VISIBILITY_NOTIFY",
	[XCB_UNMAP_NOTIFY]      = "UNMAP_NOTIFY",
	[XCB_MAP_NOTIFY]        = "MAP_NOTIFY",
	[XCB_MAP_REQUEST]       = "MAP_REQUEST",
	[XCB_REPARENT_NOTIFY]   = "REPARENT_NOTIFY",
	[XCB_CONFIGURE_REQUEST] = "CONFIGURE_REQUEST",
	[XCB_GRAVITY_NOTIFY]    = "GRAVITY_NOTIFY",
	[XCB_RESIZE_REQUEST]    = "RESIZE_REQUEST",
	[XCB_CIRCULATE_NOTIFY]  = "CIRCULATE_NOTIFY",
	[XCB_PROPERTY_NOTIFY]   = "PROPERTY_NOTIFY",
	[XCB_SELECTION_CLEAR]   = "SELECTION_CLEAR",
	[XCB_SELECTION_REQUEST] = "SELECTION_REQUEST",
	[XCB_SELECTION_NOTIFY]  = "SELECTION_NOTIFY",
	[XCB_COLORMAP_NOTIFY]   = "COLORMAP_NOTIFY",
	[XCB_CLIENT_MESSAGE]    = "CLIENT_MESSAGE",
	[XCB_MAPPING_NOTIFY]    = "MAPPING_NOTIFY"
};

static const struct ev_callback_t cb[] = {
	/* event,                function */
	{ XCB_CREATE_NOTIFY,     cb_create },
	{ XCB_MAP_REQUEST,       cb_mapreq },
	{ XCB_BUTTON_PRESS,      cb_mouse_press },
	{ XCB_BUTTON_RELEASE,    cb_mouse_release },
	{ XCB_MOTION_NOTIFY,     cb_motion },
	{ XCB_ENTER_NOTIFY,      cb_enter },
	{ XCB_FOCUS_IN,          cb_focus },
	{ XCB_FOCUS_OUT,         cb_focus },
	{ XCB_CONFIGURE_REQUEST, cb_configreq },
};

void
usage(char *name)
{
	fprintf(stderr, "usage: %s [-vh]\n", name);
}

static int
adopt(xcb_window_t wid)
{
	int x, y, w, h;

	if (!wm_is_mapped(wid)) {
		w = wm_get_attribute(wid, ATTR_W);
		h = wm_get_attribute(wid, ATTR_H);
		wm_get_cursor(0, scrn->root, &x, &y);
		wm_move(wid, ABSOLUTE, x - w/2, y - h/2);
	}

	wm_reg_window_event(wid, XCB_EVENT_MASK_ENTER_WINDOW
		| XCB_EVENT_MASK_FOCUS_CHANGE
		| XCB_EVENT_MASK_STRUCTURE_NOTIFY);

	return 0;
}

static int
inflate(xcb_window_t wid, int step)
{
	int x, y, w, h;

	x = wm_get_attribute(wid, ATTR_X) - step/2;
	y = wm_get_attribute(wid, ATTR_Y) - step/2;
	w = wm_get_attribute(wid, ATTR_W) + step;
	h = wm_get_attribute(wid, ATTR_H) + step;

	wm_teleport(wid, x, y, w, h);
	wm_restack(wid, XCB_STACK_MODE_ABOVE);

	return 0;
}

static int
takeover()
{
	int i, n;
	xcb_window_t *orphans, wid;

	n = wm_get_windows(scrn->root, &orphans);

	for (i = 0; i < n; i++) {
		wid = orphans[i];
		if (wm_is_ignored(wid))
			continue;

		if (verbose)
			fprintf(stderr, "Adopting 0x%08x\n", wid);

		adopt(wid);
		if (wm_is_mapped(wid))
			wm_set_border(border, border_color, wid);
	}


	return n;
}

static int
outline(xcb_drawable_t wid, int x, int y, int w, int h, int clear)
{
	int mask, val[3];
	static int X = 0, Y = 0, W = 0, H = 0;
	xcb_gcontext_t gc;
	xcb_rectangle_t r;

	gc = xcb_generate_id(conn);
	mask = XCB_GC_FUNCTION | XCB_GC_LINE_WIDTH | XCB_GC_SUBWINDOW_MODE;
	val[0] = XCB_GX_INVERT;
	val[1] = 0;
	val[2] = XCB_SUBWINDOW_MODE_INCLUDE_INFERIORS;
	xcb_create_gc(conn, gc, wid, mask, val);

	/* redraw last rectangle to clear it */
	r.x = X;
	r.y = Y;
	r.width = W;
	r.height = H;
	xcb_poly_rectangle(conn, wid, gc, 1, &r);

	if (clear) {
		X = Y = W = H = 0;
		return 0;
	}

	/* draw rectangle and save its coordinates for later removal */
	X = r.x = x;
	Y = r.y = y;
	W = r.width = w;
	H = r.height = h;
	xcb_poly_rectangle(conn, wid, gc, 1, &r);
	
	return 0;
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
cb_create(xcb_generic_event_t *ev)
{
	xcb_create_notify_event_t *e;

	e = (xcb_create_notify_event_t *)ev;

	if (e->override_redirect)
		return 0;

	if (verbose)
		fprintf(stderr, "%s 0x%08x\n", XEV(e), e->window);

	adopt(e->window);

	return 0;
}

static int
cb_mapreq(xcb_generic_event_t *ev)
{
	xcb_map_request_event_t *e;

	e = (xcb_map_request_event_t *)ev;

	if (verbose)
		fprintf(stderr, "%s 0x%08x\n", XEV(e), e->window);

	wm_remap(e->window, MAP);
	wm_set_border(border, border_color, e->window);
	wm_set_focus(e->window);

	return 0;
}

static int
cb_mouse_press(xcb_generic_event_t *ev)
{
	int mask;
	static xcb_timestamp_t lasttime = 0;
	xcb_button_press_event_t *e;

	e = (xcb_button_press_event_t *)ev;

	/* ignore some motion events if they happen too often */
	if (e->time - lasttime < 8)
		return 0;

	if (verbose)
		fprintf(stderr, "%s 0x%08x %d\n", XEV(e), e->event, e->detail);

	cursor.x = e->root_x - wm_get_attribute(e->child, ATTR_X);
	cursor.y = e->root_y - wm_get_attribute(e->child, ATTR_Y);
	cursor.b = e->detail;
	lasttime = e->time;

	mask = XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION;

	switch(e->detail) {
	case 1:
		curwid = e->child;
		wm_reg_cursor_event(scrn->root, mask, xhair[XHAIR_MOVE]);
		break;
	case 2:
		cursor.x = e->root_x;
		cursor.y = e->root_y;
		wm_reg_cursor_event(scrn->root, mask, xhair[XHAIR_TELE]);
		break;
	case 3:
		curwid = e->child;
		wm_reg_cursor_event(scrn->root, mask, xhair[XHAIR_SIZE]);
		break;
	case 4:
		inflate(e->child, move_step);
		wm_restack(e->child, XCB_STACK_MODE_ABOVE);
		cursor.b = 0;
		break;
	case 5:
		inflate(e->child, - move_step);
		wm_restack(e->child, XCB_STACK_MODE_ABOVE);
		cursor.b = 0;
		break;
	default:
		return 1;
	}

	return 0;
}

static int
cb_mouse_release(xcb_generic_event_t *ev)
{
	int x, y, w, h;
	xcb_cursor_t p;
	xcb_cursor_context_t *cx;
	xcb_button_release_event_t *e;

	e = (xcb_button_release_event_t *)ev;
	if (verbose)
		fprintf(stderr, "%s 0x%08x %d\n", XEV(e), e->event, e->detail);

	if (xcb_cursor_context_new(conn, scrn, &cx) < 0) {
		fprintf(stderr, "cannot instantiate cursor\n");
		exit(1);
	}

	p = xcb_cursor_load_cursor(cx, xhair[XHAIR_DFLT]);
	xcb_change_window_attributes(conn, e->event, XCB_CW_CURSOR, &p);
	xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);

	xcb_cursor_context_free(cx);

	switch (e->detail) {
	case 1:
		w = wm_get_attribute(curwid, ATTR_W);
		h = wm_get_attribute(curwid, ATTR_H);
		wm_teleport(curwid, e->root_x - cursor.x, e->root_y - cursor.y, w, h);
		break;
	case 2:
		x = MIN(e->root_x,cursor.x);
		y = MIN(e->root_y,cursor.y);
		w = MAX(e->root_x,cursor.x) - x;
		h = MAX(e->root_y,cursor.y) - y;
		wm_teleport(curwid, x, y, w, h);
		break;
	case 3:
		x = wm_get_attribute(curwid, ATTR_X);
		y = wm_get_attribute(curwid, ATTR_Y);
		wm_teleport(curwid, x, y, e->root_x - x, e->root_y - y);
		break;
	}

	cursor.x = 0;
	cursor.y = 0;
	cursor.b = 0;

	wm_restack(curwid, XCB_STACK_MODE_ABOVE);

	/* clear last drawn rectangle to avoid leaving artefacts */
	outline(scrn->root, 0, 0, 0, 0, 1);

	return 0;
}

static int
cb_motion(xcb_generic_event_t *ev)
{
	int x, y, w, h;
	static xcb_timestamp_t lasttime = 0;
	xcb_motion_notify_event_t *e;

	e = (xcb_motion_notify_event_t *)ev;

	/* ignore some motion events if they happen too often */
	if (e->time - lasttime < 32)
		return 0;

	if (curwid == scrn->root)
		return -1;

	if (verbose)
		fprintf(stderr, "%s 0x%08x %d,%d\n", XEV(e), curwid, e->root_x, e->root_y);

	lasttime = e->time;

	switch (e->state & (XCB_BUTTON_MASK_1|XCB_BUTTON_MASK_2|XCB_BUTTON_MASK_3)) {
	case XCB_BUTTON_MASK_1:
		x = e->root_x - cursor.x;
		y = e->root_y - cursor.y;
		w = wm_get_attribute(curwid, ATTR_W);
		h = wm_get_attribute(curwid, ATTR_H);
		outline(scrn->root, x, y, w, h, 0);
		break;
	case XCB_BUTTON_MASK_2:
		if (cursor.x > e->root_x) {
			x = e->root_x;
			w = cursor.x - x;
		} else {
			x = cursor.x;
			w = e->root_x - x;
		}
		if (cursor.y > e->root_y) {
			y = e->root_y;
			h = cursor.y - y;
		} else {
			y = cursor.y;
			h = e->root_y - y;
		}
		outline(scrn->root, x, y, w, h, 0);
		break;
	case XCB_BUTTON_MASK_3:
		x = wm_get_attribute(curwid, ATTR_X);
		y = wm_get_attribute(curwid, ATTR_Y);
		w = e->root_x - x;
		h = e->root_y - y;
		outline(scrn->root, x, y, w, h, 0);
		break;
	default:
		return -1;
	}


	return 0;
}

static int
cb_enter(xcb_generic_event_t *ev)
{
	xcb_enter_notify_event_t *e;

	e = (xcb_enter_notify_event_t *)ev;

	if (cursor.b)
		return 0;

	if (verbose)
		fprintf(stderr, "%s 0x%08x\n", XEV(e), e->event);

	wm_set_focus(e->event);

	return 0;
}

static int
cb_focus(xcb_generic_event_t *ev)
{
	xcb_focus_in_event_t *e;

	e = (xcb_focus_in_event_t *)ev;

	if (verbose)
		fprintf(stderr, "%s 0x%08x\n", XEV(e), e->event);

	switch(e->response_type & ~0x80) {
	case XCB_FOCUS_IN:
		wm_set_border(-1, border_color_active, e->event);
		break;
	case XCB_FOCUS_OUT:
		wm_set_border(-1, border_color, e->event);
		break;
	}

	return 0;
}

static int
cb_configreq(xcb_generic_event_t *ev)
{
	int x, y, w, h, b;
	xcb_configure_request_event_t *e;

	e = (xcb_configure_request_event_t *)ev;

	if (verbose)
		fprintf(stderr, "%s 0x%08x 0x%08x:%dx%d+%d+%d\n",
			XEV(e), e->parent, e->window,
			e->width, e->height,
			e->x, e->y);

	x = wm_get_attribute(e->window, ATTR_X);
	y = wm_get_attribute(e->window, ATTR_Y);
	w = wm_get_attribute(e->window, ATTR_W);
	h = wm_get_attribute(e->window, ATTR_H);
	b = wm_get_attribute(e->window, ATTR_B);
	if (e->border_width != b)
		wm_set_border(e->border_width, -1, e->window);

	if (e->x != x || e->y != y || e->width != w || e->height != h)
		wm_teleport(e->window, e->x, e->y, e->width, e->height);

	wm_restack(e->window, e->stack_mode);

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

	if (!wm_reg_window_event(scrn->root, mask)) {
		fprintf(stderr, "Cannot redirect root window event.\n");
		return -1;
	}

	xcb_grab_button(conn, 0, scrn->root, XCB_EVENT_MASK_BUTTON_PRESS,
		XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, scrn->root,
		XCB_NONE, XCB_BUTTON_INDEX_ANY, modifier);

	takeover();

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