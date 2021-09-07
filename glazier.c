#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_image.h>
#include <xcb/randr.h>

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
	int x, y, b;
	int mode;
};

enum {
	XHAIR_DFLT,
	XHAIR_MOVE,
	XHAIR_SIZE,
	XHAIR_TELE,
};

enum {
	GRAB_NONE = 0,
	GRAB_MOVE,
	GRAB_SIZE,
	GRAB_TELE,
};

#include "config.h"

void usage(char *);
static int takeover();
static int adopt(xcb_window_t);
static uint32_t backpixel(xcb_window_t);
static int paint(xcb_window_t);
static int inflate(xcb_window_t, int);
static int outline(xcb_drawable_t, int, int, int, int);
static int ev_callback(xcb_generic_event_t *);

/* XRandR specific functions */
static int crossedge(xcb_window_t);
static int snaptoedge(xcb_window_t);

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
static int cb_configure(xcb_generic_event_t *);

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
	{ XCB_CONFIGURE_NOTIFY,  cb_configure },
};

void
usage(char *name)
{
	fprintf(stderr, "usage: %s [-vh]\n", name);
}

/*
 * Every window that shouldn't be ignored (override_redirect) is adoped
 * by the WM when it is created, or when the WM is started.
 * When a window is created, it is centered on the cursor, before it
 * gets mapped on screen. Windows that are already visible are not moved.
 * Some events are also registered by the WM for these windows.
 */
int
adopt(xcb_window_t wid)
{
	if (wm_is_ignored(wid))
		return -1;

	return wm_reg_window_event(wid, XCB_EVENT_MASK_ENTER_WINDOW
		| XCB_EVENT_MASK_FOCUS_CHANGE
		| XCB_EVENT_MASK_STRUCTURE_NOTIFY);
}

/*
 * Return the color of the pixel in one of the window corners.
 * Each corner is tested in a clockwise fashion until an uncovered region
 * is found. When such pixel is found, the color is returned.
 * If no color is found a default of border_color is returned.
 */
uint32_t
backpixel(xcb_window_t wid)
{
	int w, h;
	uint32_t color;
	xcb_image_t *px;

	w = wm_get_attribute(wid, ATTR_W);
	h = wm_get_attribute(wid, ATTR_H);

	px = xcb_image_get(conn, wid, 0, 0, 1, 1, 0xffffffff, XCB_IMAGE_FORMAT_Z_PIXMAP);
	if (px) color = xcb_image_get_pixel(px, 0, 0);

	if (!color) {
		px = xcb_image_get(conn, wid, w - 1, 0, 1, 1, 0xffffffff, XCB_IMAGE_FORMAT_Z_PIXMAP);
		if (px) color = xcb_image_get_pixel(px, 0, 0);
	}

	if (!color) {
		px = xcb_image_get(conn, wid, 0, h - 1, 1, 1, 0xffffffff, XCB_IMAGE_FORMAT_Z_PIXMAP);
		if (px) color = xcb_image_get_pixel(px, 0, 0);
	}

	if (!color) {
		px = xcb_image_get(conn, wid, w, h, 1, 1, 0xffffffff, XCB_IMAGE_FORMAT_Z_PIXMAP);
		if (px) color = xcb_image_get_pixel(px, 0, 0);
	}

	return color ? color : border_color;
}

/*
 * Paint double borders around the window. The background is taken from
 * the window content via backpixel(), and the border line is drawn on
 * top of it using the colors defined in config.h.
 *
 * Note: drawing on the borders require specifying regions from position
 * the top-left corner of the window itself. Drawing on the border pixmap
 * is done by drawing outside the window, and then wrapping over to the
 * left side. For example, assuming a window of 200x100, with a 10px
 * border, drawing a 5px square in the top left of the border means drawing
 * a 5x5 rectangle at position 210,110. The area does not wrap around
 * indefinitely though, so drawing a rectangle of 10x10 or 200x10 at
 * position 210,110 would have the same effect: draw a 10x10 square in
 * the top right. uugh…
 */
int
paint(xcb_window_t wid)
{
	int val[2], w, h, d, b, i;
	xcb_pixmap_t px;
	xcb_gcontext_t gc;

	w = wm_get_attribute(wid, ATTR_W);
	h = wm_get_attribute(wid, ATTR_H);
	d = wm_get_attribute(wid, ATTR_D);
	b = wm_get_attribute(wid, ATTR_B);
	i = inner_border;

	if (i > b)
		return -1;

	px = xcb_generate_id(conn);
	gc = xcb_generate_id(conn);

	val[0] = backpixel(wid);
	xcb_create_gc(conn, gc, wid, XCB_GC_FOREGROUND, val);
	xcb_create_pixmap(conn, d, px, wid, w + 2*b, h + 2*b);

	/* background color */
	xcb_rectangle_t bg = { 0, 0, w + 2*b, h + 2*b };

	xcb_poly_fill_rectangle(conn, px, gc, 1, &bg);

	/* abandon all hopes already */
	xcb_rectangle_t r[] = {
		{w+(b-i)/2,0,i,h+(b+i)/2},             /* right */
		{w+b+(b-i)/2,0,i,h+(b+i)/2},           /* left */
		{0,h+(b-i)/2,w+(b-i)/2+i,i},           /* bottom; bottom-right */
		{0,h+b+(b-i)/2,w+(b+i)/2,i},           /* top; top-right */
		{w+b+(b-i)/2,h+b+(b-i)/2,i+(b-i/2),i}, /* top-left corner; top-part */
		{w+b+(b-i)/2,h+b+(b-i)/2,i,i+(b-i/2)}, /* top-left corner; left-part */
		{w+b+(b-i)/2,h+(b-i)/2,i+(b-i)/2,i},   /* top-right corner; right-part */
		{w+(b-i)/2,h+b+(b-i)/2,i,i+(b-i)/2}    /* bottom-left corner; bottom-part */
	};

	val[0] = (wid == wm_get_focus()) ? border_color_active : border_color;
	xcb_change_gc(conn, gc, XCB_GC_FOREGROUND, val);
	xcb_poly_fill_rectangle(conn, px, gc, 8, r);

	xcb_change_window_attributes(conn, wid, XCB_CW_BORDER_PIXMAP, &px);

	xcb_free_pixmap(conn, px);
	xcb_free_gc(conn, gc);

	return 0;
}

/*
 * Inflating a window will grow it both vertically and horizontally in
 * all 4 directions, thus making it look like it is inflating.
 * The window can be "deflated" by providing a negative `step` value.
 */
int
inflate(xcb_window_t wid, int step)
{
	int x, y, w, h;

	x = wm_get_attribute(wid, ATTR_X) - step/2;
	y = wm_get_attribute(wid, ATTR_Y) - step/2;
	w = wm_get_attribute(wid, ATTR_W) + step;
	h = wm_get_attribute(wid, ATTR_H) + step;

	return wm_teleport(wid, x, y, w, h);
}

/*
 * When the WM is started, it will take control of the existing windows.
 * This means registering events on them and setting the borders if they
 * are mapped. This function is only supposed to run once at startup,
 * as the callback functions will take control of new windows
 */
int
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
			paint(wid);
	}

	wid = wm_get_focus();
	if (wid != scrn->root) {
		curwid = wid;
		paint(wid);
	}

	return n;
}

/*
 * Draws a rectangle selection on the screen.
 * The trick here is to invert the color on the selection, so that
 * redrawing the same rectangle will "clear" it.
 * This function is used to dynamically draw a region for moving/resizing
 * a window using the cursor. As such, we need to make sure that whenever
 * we draw a rectangle, we clear out the last drawn one by redrawing
 * the latest coordinates again, so we have to save them from one call to
 * the other.
 */
int
outline(xcb_drawable_t wid, int x, int y, int w, int h)
{
	int mask, val[3];
	static int X = 0, Y = 0, W = 0, H = 0;
	xcb_gcontext_t gc;
	xcb_rectangle_t r;

	gc = xcb_generate_id(conn);
	mask = XCB_GC_FUNCTION | XCB_GC_SUBWINDOW_MODE | XCB_GC_GRAPHICS_EXPOSURES;
	val[0] = XCB_GX_INVERT;
	val[1] = XCB_SUBWINDOW_MODE_INCLUDE_INFERIORS,
	val[2] = 0;
	xcb_create_gc(conn, gc, wid, mask, val);

	/* redraw last rectangle to clear it */
	r.x = X;
	r.y = Y;
	r.width = W;
	r.height = H;
	xcb_poly_rectangle(conn, wid, gc, 1, &r);

	/* draw rectangle and save its coordinates for later removal */
	X = r.x = x;
	Y = r.y = y;
	W = r.width = w;
	H = r.height = h;
	xcb_poly_rectangle(conn, wid, gc, 1, &r);

	return 0;
}

/*
 * Callback used for all events that are not explicitely registered.
 * This is not at all necessary, and used for debugging purposes.
 */
int
cb_default(xcb_generic_event_t *ev)
{
	if (verbose < 2)
		return 0;

	if (XEV(ev)) {
		fprintf(stderr, "%s not handled\n", XEV(ev));
	} else {
		fprintf(stderr, "EVENT %d not handled\n", ev->response_type);
	}

	return 0;
}

/*
 * XCB_CREATE_NOTIFY is the first event triggered by new windows, and
 * is used to prepare the window for use by the WM.
 * The attribute `override_redirect` allow windows to specify that they
 * shouldn't be handled by the WM.
 */
int
cb_create(xcb_generic_event_t *ev)
{
	int x, y, w, h;
	xcb_randr_monitor_info_t *m;
	xcb_create_notify_event_t *e;

	e = (xcb_create_notify_event_t *)ev;

	if (e->override_redirect)
		return 0;

	if (verbose)
		fprintf(stderr, "%s 0x%08x\n", XEV(e), e->window);

	x = wm_get_attribute(e->window, ATTR_X);
	y = wm_get_attribute(e->window, ATTR_Y);

	if (!wm_is_mapped(e->window) && !x && !y) {
		wm_get_cursor(0, scrn->root, &x, &y);

		/* move window under the cursor */
		if ((m = wm_get_monitor(wm_find_monitor(x, y)))) {
			w = wm_get_attribute(e->window, ATTR_W);
			h = wm_get_attribute(e->window, ATTR_H);
			x = MAX(m->x, x - w/2);
			y = MAX(m->y, y - h/2);

			wm_teleport(e->window, x, y, w, h);
		}
	}

	adopt(e->window);

	return 0;
}

/*
 * XCB_MAP_REQUEST is triggered by a window that wants to be mapped on
 * screen. This is then the responsibility of the WM to map it on screen
 * and eventually decorate it. This event require that the WM register
 * XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT on the root window to intercept
 * map requests.
 */
int
cb_mapreq(xcb_generic_event_t *ev)
{
	xcb_map_request_event_t *e;

	e = (xcb_map_request_event_t *)ev;

	if (verbose)
		fprintf(stderr, "%s 0x%08x\n", XEV(e), e->window);

	wm_remap(e->window, MAP);
	wm_set_border(border, 0, e->window);
	wm_set_focus(e->window);
	paint(e->window);

	/* prevent window to pop outside the screen */
	if (crossedge(e->window))
		snaptoedge(e->window);

	return 0;
}

/*
 * The WM grabs XCB_BUTTON_PRESS events when the modifier is held.
 * Once pressed, we'll grab the pointer entirely (without modifiers)
 * and wait for motion/release events.
 * The special mouse buttons 4/5 (scroll up/down) are treated especially,
 * as they do not trigger any "release" event.
 *
 * This function must also save the window ID where the mouse press
 * occured so we know which window to move/resize, even if the focus
 * changes to another window.
 * For similar reasons, we must save the cursor position.
 */
int
cb_mouse_press(xcb_generic_event_t *ev)
{
	int mask;
	static xcb_timestamp_t lasttime = 0;
	xcb_button_press_event_t *e;
	xcb_window_t wid;

	e = (xcb_button_press_event_t *)ev;

	/* ignore some motion events if they happen too often */
	if (e->time - lasttime < 8)
		return -1;

	wid = e->child ? e->child : e->event;

	if (verbose)
		fprintf(stderr, "%s 0x%08x %d\n", XEV(e), wid, e->detail);

	cursor.x = e->root_x - wm_get_attribute(wid, ATTR_X);
	cursor.y = e->root_y - wm_get_attribute(wid, ATTR_Y);
	cursor.b = e->detail;
	lasttime = e->time;

	mask = XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION;

	switch(e->detail) {
	case 1:
		curwid = wid;
		cursor.mode = GRAB_MOVE;
		wm_reg_cursor_event(scrn->root, mask, xhair[XHAIR_MOVE]);
		break;
	case 2:
		/* teleport acts on the last focused window */
		cursor.x = e->root_x;
		cursor.y = e->root_y;
		cursor.mode = GRAB_TELE;
		wm_reg_cursor_event(scrn->root, mask, xhair[XHAIR_TELE]);
		break;
	case 3:
		curwid = wid;
		cursor.mode = GRAB_SIZE;
		wm_reg_cursor_event(scrn->root, mask, xhair[XHAIR_SIZE]);
		break;
	case 4:
		inflate(wid, move_step);
		wm_restack(e->child, XCB_STACK_MODE_ABOVE);
		break;
	case 5:
		inflate(wid, - move_step);
		wm_restack(wid, XCB_STACK_MODE_ABOVE);
		break;
	default:
		return -1;
	}

	return 0;
}

/*
 * When XCB_BUTTON_RELEASE is triggered, this will "commit" any
 * move/resize initiated on a previous mouse press.
 * This will also ungrab the mouse pointer.
 */
int
cb_mouse_release(xcb_generic_event_t *ev)
{
	int x, y, w, h;
	xcb_cursor_t p;
	xcb_cursor_context_t *cx;
	xcb_button_release_event_t *e;

	e = (xcb_button_release_event_t *)ev;
	if (verbose)
		fprintf(stderr, "%s 0x%08x %d\n", XEV(e), e->event, e->detail);

	/* only respond to release events for the current grab mode */
	if (cursor.mode != GRAB_NONE && e->detail != cursor.b)
		return -1;

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
	cursor.mode = GRAB_NONE;

	wm_restack(curwid, XCB_STACK_MODE_ABOVE);

	/* clear last drawn rectangle to avoid leaving artefacts */
	outline(scrn->root, 0, 0, 0, 0);
	xcb_clear_area(conn, 0, scrn->root, 0, 0, 0, 0);

	w = wm_get_attribute(curwid, ATTR_W);
	h = wm_get_attribute(curwid, ATTR_H);
	xcb_clear_area(conn, 1, curwid, 0, 0, w, h);
	paint(curwid);

	return 0;
}

/*
 * When the pointer is grabbed, every move triggers a XCB_MOTION_NOTIFY.
 * Events are reported for every single move by 1 pixel.
 *
 * This can spam a huge lot of events, and treating them all can be
 * resource hungry and make the interface feels laggy.
 * To get around this, we must ignore some of these events. This is done
 * by using the `time` attribute, and only processing new events every
 * X milliseconds.
 *
 * This callback is different from the others because it does not uses
 * the ID of the window that reported the event, but an ID previously
 * saved in cb_mouse_press().
 * This makes sense as we want to move the last window we clicked on,
 * and not the window we are moving over.
 */
int
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
		fprintf(stderr, "%s 0x%08x %d,%d\n", XEV(e), curwid,
			e->root_x, e->root_y);

	lasttime = e->time;

	switch (e->state & (XCB_BUTTON_MASK_1|XCB_BUTTON_MASK_2|XCB_BUTTON_MASK_3)) {
	case XCB_BUTTON_MASK_1:
		x = e->root_x - cursor.x;
		y = e->root_y - cursor.y;
		w = wm_get_attribute(curwid, ATTR_W);
		h = wm_get_attribute(curwid, ATTR_H);
		outline(scrn->root, x, y, w, h);
		break;
	case XCB_BUTTON_MASK_2:
		x = MIN(cursor.x, e->root_x);
		y = MIN(cursor.y, e->root_y);
		w = MAX(cursor.x - e->root_x, e->root_x - cursor.x);
		h = MAX(cursor.y - e->root_y, e->root_y - cursor.y);
		outline(scrn->root, x, y, w, h);
		break;
	case XCB_BUTTON_MASK_3:
		x = wm_get_attribute(curwid, ATTR_X);
		y = wm_get_attribute(curwid, ATTR_Y);
		w = e->root_x - x;
		h = e->root_y - y;
		outline(scrn->root, x, y, w, h);
		break;
	default:
		return -1;
	}

	return 0;
}

/*
 * Each time the pointer moves from one window to another, an
 * XCB_ENTER_NOTIFY event is fired. This is used to switch input focus
 * between windows to follow where the pointer is.
 */
int
cb_enter(xcb_generic_event_t *ev)
{
	xcb_enter_notify_event_t *e;

	e = (xcb_enter_notify_event_t *)ev;

	if (wm_is_ignored(e->event))
		return 0;

	if (cursor.mode != GRAB_NONE)
		return 0;

	if (verbose)
		fprintf(stderr, "%s 0x%08x\n", XEV(e), e->event);

	return wm_set_focus(e->event);
}

/*
 * Whenever the input focus change from one window to another, both an
 * XCB_FOCUS_OUT and XCB_FOCUS_IN are fired.
 * This is the occasion to change the border color to represent focus.
 */
int
cb_focus(xcb_generic_event_t *ev)
{
	xcb_focus_in_event_t *e;

	e = (xcb_focus_in_event_t *)ev;

	if (verbose)
		fprintf(stderr, "%s 0x%08x\n", XEV(e), e->event);

	switch(e->response_type & ~0x80) {
	case XCB_FOCUS_IN:
		curwid = e->event;
		return paint(e->event);
		break; /* NOTREACHED */
	case XCB_FOCUS_OUT:
		return paint(e->event);
		break; /* NOTREACHED */
	}

	return -1;
}

/*
 * XCB_CONFIGURE_REQUEST is triggered by every window that wants to
 * change its attributes like size, stacking order or border.
 * These must now be handled by the WM because of the
 * XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT registration.
 */
int
cb_configreq(xcb_generic_event_t *ev)
{
	int x, y, w, h;
	xcb_configure_request_event_t *e;

	e = (xcb_configure_request_event_t *)ev;

	if (verbose)
		fprintf(stderr, "%s 0x%08x 0x%08x:%dx%d+%d+%d\n",
			XEV(e), e->parent, e->window,
			e->width, e->height,
			e->x, e->y);

	if (e->value_mask &
		( XCB_CONFIG_WINDOW_X
		| XCB_CONFIG_WINDOW_Y
		| XCB_CONFIG_WINDOW_WIDTH
		| XCB_CONFIG_WINDOW_HEIGHT)) {
		x = wm_get_attribute(e->window, ATTR_X);
		y = wm_get_attribute(e->window, ATTR_Y);
		w = wm_get_attribute(e->window, ATTR_W);
		h = wm_get_attribute(e->window, ATTR_H);

		if (e->value_mask & XCB_CONFIG_WINDOW_X) x = e->x;
		if (e->value_mask & XCB_CONFIG_WINDOW_Y) y = e->y;
		if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH)  w = e->width;
		if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) h = e->height;

		wm_teleport(e->window, x, y, w, h);

		/* redraw border pixmap after move/resize */
		paint(e->window);
	}

	if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
		wm_set_border(e->border_width, border_color, e->window);

	if (e->value_mask & XCB_CONFIG_WINDOW_STACK_MODE)
		wm_restack(e->window, e->stack_mode);

	return 0;
}

int
cb_configure(xcb_generic_event_t *ev)
{
	xcb_configure_notify_event_t *e;

	e = (xcb_configure_notify_event_t *)ev;

	if (verbose)
		fprintf(stderr, "%s 0x%08x %dx%d+%d+%d\n",
			XEV(e), e->window,
			e->width, e->height,
			e->x, e->y);

	/* update screen size when root window's size change */
	if (e->window == scrn->root) {
		scrn->width_in_pixels = e->width;
		scrn->height_in_pixels = e->height;
	}

	return 0;
}

/*
 * This functions uses the ev_callback_t structure to call out a specific
 * callback function for each EVENT fired.
 */
int
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

/*
 * Returns 1 is the given window's geometry crosses the monitor's edge,
 * and 0 otherwise
 */
int
crossedge(xcb_window_t wid)
{
	int r = 0;
	int x, y, w, h, b;
	xcb_randr_monitor_info_t *m;

	b = wm_get_attribute(wid, ATTR_B);
	x = wm_get_attribute(wid, ATTR_X);
	y = wm_get_attribute(wid, ATTR_Y);
	w = wm_get_attribute(wid, ATTR_W);
	h = wm_get_attribute(wid, ATTR_H);
	m = wm_get_monitor(wm_find_monitor(x, y));

	if (!m)
		return -1;

	if ((x + w + 2*b > m->x + m->width)
	 || (y + h + 2*b > m->y + m->height))
		r = 1;

	free(m);
	return r;
}

/*
 * Moves a window so that its border doesn't cross the monitor's edge
 */
int
snaptoedge(xcb_window_t wid)
{
	int x, y, w, h, b;
	xcb_randr_monitor_info_t *m;

	b = wm_get_attribute(wid, ATTR_B);
	x = wm_get_attribute(wid, ATTR_X);
	y = wm_get_attribute(wid, ATTR_Y);
	w = wm_get_attribute(wid, ATTR_W);
	h = wm_get_attribute(wid, ATTR_H);
	m = wm_get_monitor(wm_find_monitor(x, y));

	if (!m)
		return -1;

	if (w + 2*b > m->width)  w = m->width - 2*b;
	if (h + 2*b > m->height) h = m->height - 2*b;

	if (x + w + 2*b > m->x + m->width) x = MAX(m->x, m->x + m->width - w - 2*b);
	if (y + h + 2*b > m->y + m->height) y = MAX(m->y, m->y + m->height - h - 2*b);

	wm_teleport(wid, x, y, w, h);

	return 0;
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
	mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY
		| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
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

	return wm_kill_xcb();
}