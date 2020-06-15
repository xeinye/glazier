#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>

#include "arg.h"
#include "wm.h"

#define LEN(x) (sizeof(x)/sizeof(x[0]))

struct xatom {
	char *name;
	xcb_atom_t atom;
};

struct geometry {
	uint32_t x, y, w, h, b;
};

enum EWMH_TYPES {
	IGNORE,
	NORMAL,
	POPUP,
};

enum {
	_NET_SUPPORTED,
	_NET_CLIENT_LIST,
	_NET_CLIENT_LIST_STACKING,
	_NET_SUPPORTING_WM_CHECK,
	_NET_ACTIVE_WINDOW,
	_NET_WM_STATE,
	_NET_WM_STATE_FULLSCREEN,
	_NET_WM_WINDOW_TYPE,
	_NET_WM_WINDOW_TYPE_DESKTOP,
	_NET_WM_WINDOW_TYPE_DOCK,
	_NET_WM_WINDOW_TYPE_TOOLBAR,
	_NET_WM_WINDOW_TYPE_MENU,
	_NET_WM_WINDOW_TYPE_UTILITY,
	_NET_WM_WINDOW_TYPE_SPLASH,
	_NET_WM_WINDOW_TYPE_DIALOG,
	_NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
	_NET_WM_WINDOW_TYPE_POPUP_MENU,
	_NET_WM_WINDOW_TYPE_TOOLTIP,
	_NET_WM_WINDOW_TYPE_NOTIFICATION,
	_NET_WM_WINDOW_TYPE_COMBO,
	_NET_WM_WINDOW_TYPE_DND,
	_NET_WM_WINDOW_TYPE_NORMAL,
};

static void usage(const char *);
static int ewmh_init();
static int ewmh_wipe();
static int ewmh_supported();
static int ewmh_supportingwmcheck();
static int ewmh_activewindow(xcb_window_t);
static int ewmh_clientlist();
static int ewmh_type(xcb_window_t);
static int ewmh_message(xcb_client_message_event_t *);
static int ewmh_fullscreen(xcb_window_t, int);

xcb_connection_t *conn;
xcb_screen_t     *scrn;
xcb_window_t      ewmhwid; /* _NET_SUPPORTING_WM_CHECK target window */

struct xatom ewmh[] = {
	[_NET_SUPPORTED]                    = { .name = "_NET_SUPPORTED"                    },
	[_NET_CLIENT_LIST]                  = { .name = "_NET_CLIENT_LIST"                  },
	[_NET_CLIENT_LIST_STACKING]         = { .name = "_NET_CLIENT_LIST_STACKING"         },
	[_NET_SUPPORTING_WM_CHECK]          = { .name = "_NET_SUPPORTING_WM_CHECK"          },
	[_NET_ACTIVE_WINDOW]                = { .name = "_NET_ACTIVE_WINDOW"                },
	[_NET_WM_STATE]                     = { .name = "_NET_WM_STATE"                     },
	[_NET_WM_STATE_FULLSCREEN]          = { .name = "_NET_WM_STATE_FULLSCREEN"          },
	[_NET_WM_WINDOW_TYPE]               = { .name = "_NET_WM_WINDOW_TYPE"               },
	[_NET_WM_WINDOW_TYPE_DESKTOP]       = { .name = "_NET_WM_WINDOW_TYPE_DESKTOP"       },
	[_NET_WM_WINDOW_TYPE_DOCK]          = { .name = "_NET_WM_WINDOW_TYPE_DOCK"          },
	[_NET_WM_WINDOW_TYPE_TOOLBAR]       = { .name = "_NET_WM_WINDOW_TYPE_TOOLBAR"       },
	[_NET_WM_WINDOW_TYPE_MENU]          = { .name = "_NET_WM_WINDOW_TYPE_MENU"          },
	[_NET_WM_WINDOW_TYPE_UTILITY]       = { .name = "_NET_WM_WINDOW_TYPE_UTILITY"       },
	[_NET_WM_WINDOW_TYPE_SPLASH]        = { .name = "_NET_WM_WINDOW_TYPE_SPLASH"        },
	[_NET_WM_WINDOW_TYPE_DIALOG]        = { .name = "_NET_WM_WINDOW_TYPE_DIALOG"        },
	[_NET_WM_WINDOW_TYPE_DROPDOWN_MENU] = { .name = "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU" },
	[_NET_WM_WINDOW_TYPE_POPUP_MENU]    = { .name = "_NET_WM_WINDOW_TYPE_POPUP_MENU"    },
	[_NET_WM_WINDOW_TYPE_TOOLTIP]       = { .name = "_NET_WM_WINDOW_TYPE_TOOLTIP"       },
	[_NET_WM_WINDOW_TYPE_NOTIFICATION]  = { .name = "_NET_WM_WINDOW_TYPE_NOTIFICATION"  },
	[_NET_WM_WINDOW_TYPE_COMBO]         = { .name = "_NET_WM_WINDOW_TYPE_COMBO"         },
	[_NET_WM_WINDOW_TYPE_DND]           = { .name = "_NET_WM_WINDOW_TYPE_DND"           },
	[_NET_WM_WINDOW_TYPE_NORMAL]        = { .name = "_NET_WM_WINDOW_TYPE_NORMAL"        },
};

void
usage(const char *name)
{
	fprintf(stderr, "usage: %s [-h]\n", name);
}

void
cleanup()
{
	printf("cleaning up\n");
	ewmh_wipe();
	wm_kill_xcb();
}

int
ewmh_init()
{
	uint32_t i, n;
	xcb_window_t *w;

	for (i = 0; i < LEN(ewmh); i++)
		ewmh[i].atom = wm_add_atom(ewmh[i].name, strlen(ewmh[i].name));

	/* monitor focus events on existing windows */
	n = wm_get_windows(scrn->root, &w);
	for (i = 0; i < n; i++)
		wm_reg_window_event(w[i], XCB_EVENT_MASK_FOCUS_CHANGE);

	ewmh_supported();
	ewmh_supportingwmcheck();
	ewmh_clientlist();

	return 0;
}

int
ewmh_wipe()
{
	xcb_delete_property(conn, scrn->root, ewmh[_NET_SUPPORTED].atom);
	xcb_delete_property(conn, scrn->root, ewmh[_NET_CLIENT_LIST].atom);
	xcb_delete_property(conn, scrn->root, ewmh[_NET_CLIENT_LIST_STACKING].atom);
	xcb_delete_property(conn, scrn->root, ewmh[_NET_ACTIVE_WINDOW].atom);
	xcb_delete_property(conn, scrn->root, ewmh[_NET_SUPPORTING_WM_CHECK].atom);
	xcb_destroy_window(conn, ewmhwid);

	xcb_flush(conn);

	return 0;
}

int
ewmh_supported()
{
	uint32_t i;
	xcb_atom_t supported[LEN(ewmh)];

	for (i = 0; i < LEN(ewmh); i++)
		supported[i] = ewmh[i].atom;

	return wm_set_atom(scrn->root, ewmh[_NET_SUPPORTED].atom, XCB_ATOM_ATOM, i, &supported);
}

int
ewmh_supportingwmcheck()
{
	int val = 1;

	ewmhwid = xcb_generate_id(conn);

	/* dummyest window ever. */
	xcb_create_window(conn,
		XCB_COPY_FROM_PARENT, ewmhwid, scrn->root,
		0, 0, 1, 1, 0,                     /* x, y, w, h, border */
		XCB_WINDOW_CLASS_INPUT_ONLY,       /* no need for output */
		scrn->root_visual,                 /* visual */
		XCB_CW_OVERRIDE_REDIRECT, &val);   /* have the WM ignore us */

	wm_set_atom(scrn->root, ewmh[_NET_SUPPORTING_WM_CHECK].atom, XCB_ATOM_WINDOW, 1, &ewmhwid);
	wm_set_atom(ewmhwid, ewmh[_NET_SUPPORTING_WM_CHECK].atom, XCB_ATOM_WINDOW, 1, &ewmhwid);

	return 0;
}

int
ewmh_activewindow(xcb_window_t wid)
{
	wm_set_atom(scrn->root, ewmh[_NET_ACTIVE_WINDOW].atom, XCB_ATOM_WINDOW, 1, &wid);
	return 0;
}

int
ewmh_clientlist()
{
	uint32_t i, c, n;
	xcb_window_t *w, *l;

	n = wm_get_windows(scrn->root, &w);

	l = calloc(n, sizeof(*w));

	for (i=0, c=0; i<n; i++) {
		if (ewmh_type(w[i]) != NORMAL) {
			xcb_change_window_attributes(conn, w[i], XCB_CW_OVERRIDE_REDIRECT, &(int[]){1});
		}
		if (!wm_is_listable(w[i], 0))
			l[c++] = w[i];
	}

	free(w);

	wm_set_atom(scrn->root, ewmh[_NET_CLIENT_LIST].atom, XCB_ATOM_WINDOW, c, l);
	wm_set_atom(scrn->root, ewmh[_NET_CLIENT_LIST_STACKING].atom, XCB_ATOM_WINDOW, c, l);

	free(l);

	return 0;
}

int
ewmh_type(xcb_window_t window)
{
	unsigned long n;
	int type = NORMAL; /* treat non-ewmh as normal windows */
	xcb_atom_t *atoms;

	atoms = wm_get_atom(window, ewmh[_NET_WM_WINDOW_TYPE].atom, XCB_ATOM_ATOM, &n);

	if (!atoms)
		return NORMAL;

	/*
	 * as per the EWMH spec, when multiple types are
	 * applicable, they must be listed from the most to least
	 * important.
	 * To do so, we cycle through them in reverse order, changing
	 * the window type everytime a known type is encountered.
	 * Some toolkits like to use toolkit-specific atoms as their
	 * first value for more appropriate categorization. This function
	 * only deals with standard EWMH atoms.
	 */
	while (n --> 0) {
		if (atoms[n] == ewmh[_NET_WM_WINDOW_TYPE_DESKTOP].atom
		 || atoms[n] == ewmh[_NET_WM_WINDOW_TYPE_DOCK].atom
		 || atoms[n] == ewmh[_NET_WM_WINDOW_TYPE_TOOLBAR].atom
		 || atoms[n] == ewmh[_NET_WM_WINDOW_TYPE_POPUP_MENU].atom
		 || atoms[n] == ewmh[_NET_WM_WINDOW_TYPE_COMBO].atom
		 || atoms[n] == ewmh[_NET_WM_WINDOW_TYPE_DND].atom)
			type = IGNORE;

		if (atoms[n] == ewmh[_NET_WM_WINDOW_TYPE_MENU].atom
		 || atoms[n] == ewmh[_NET_WM_WINDOW_TYPE_SPLASH].atom
		 || atoms[n] == ewmh[_NET_WM_WINDOW_TYPE_DROPDOWN_MENU].atom
		 || atoms[n] == ewmh[_NET_WM_WINDOW_TYPE_TOOLTIP].atom
		 || atoms[n] == ewmh[_NET_WM_WINDOW_TYPE_NOTIFICATION].atom)
			type = POPUP;

		if (atoms[n] == ewmh[_NET_WM_WINDOW_TYPE_DIALOG].atom
		 || atoms[n] == ewmh[_NET_WM_WINDOW_TYPE_UTILITY].atom
		 || atoms[n] == ewmh[_NET_WM_WINDOW_TYPE_NORMAL].atom)
			type = NORMAL;
	}

	return type;
}

int
ewmh_message(xcb_client_message_event_t *ev)
{
	/* ignore all other messages */
	if (ev->type != ewmh[_NET_WM_STATE].atom)
		return -1;

	if (ev->data.data32[1] == ewmh[_NET_WM_STATE_FULLSCREEN].atom
	 || ev->data.data32[2] == ewmh[_NET_WM_STATE_FULLSCREEN].atom) {
		ewmh_fullscreen(ev->window, ev->data.data32[0]);
		return 0;
	}

	return 1;
}

int
ewmh_fullscreen(xcb_window_t wid, int state)
{
	size_t n;
	int isfullscreen;
	xcb_atom_t *atom, orig;
	struct geometry g, *d;

	atom = wm_get_atom(wid, ewmh[_NET_WM_STATE].atom, XCB_ATOM_ATOM, &n);
	orig = wm_add_atom("ORIGINAL_SIZE", strlen("ORIGINAL_SIZE"));

	isfullscreen = (atom && *atom == ewmh[_NET_WM_STATE_FULLSCREEN].atom);

	switch (state) {
	case -1:
		return isfullscreen;
		break; /* NOTREACHED */

	case 0: /* _NET_WM_STATE_REMOVE */
		wm_set_atom(wid, ewmh[_NET_WM_STATE].atom, XCB_ATOM_ATOM, 0, NULL);
		d = wm_get_atom(wid, orig, XCB_ATOM_CARDINAL, &n);
		if (!d || !n) return -1;
		wm_set_border(d->b, -1, wid);
		wm_teleport(wid, d->x, d->y, d->w, d->h);
		xcb_delete_property(conn, wid, orig);
		break;

	case 1: /* _NET_WM_STATE_ADD */
		/* save current window geometry */
		g.x = wm_get_attribute(wid, ATTR_X);
		g.y = wm_get_attribute(wid, ATTR_Y);
		g.w = wm_get_attribute(wid, ATTR_W);
		g.h = wm_get_attribute(wid, ATTR_H);
		g.b = wm_get_attribute(wid, ATTR_B);
		wm_set_atom(wid, orig, XCB_ATOM_CARDINAL, 5, &g);

		/* move window fullscreen */
		g.w = wm_get_attribute(scrn->root, ATTR_W);
		g.h = wm_get_attribute(scrn->root, ATTR_H);
		wm_set_border(0, -1, wid);
		wm_teleport(wid, 0, 0, g.w, g.h);
		wm_set_atom(wid, ewmh[_NET_WM_STATE].atom, XCB_ATOM_ATOM, 1, &ewmh[_NET_WM_STATE_FULLSCREEN].atom);
		break;

	case 2: /* _NET_WM_STATE_TOGGLE */
		printf("0x%08x !fullscreen\n", wid);
		ewmh_fullscreen(wid, !isfullscreen);
		break;
	}

	return 0;
}

int
main (int argc, char *argv[])
{
	int mask;
	char *argv0;
	xcb_generic_event_t *ev = NULL;

	ARGBEGIN {
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
	ewmh_init();

	signal(SIGINT,  cleanup);
	signal(SIGTERM, cleanup);

	/* needed to get notified of windows creation */
	mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;

	if (!wm_reg_window_event(scrn->root, mask))
		return -1;

	for (;;) {
		xcb_flush(conn);
		ev = xcb_wait_for_event(conn);
		if (!ev)
			break;

		switch (ev->response_type & ~0x80) {

		case XCB_CREATE_NOTIFY:
		        wm_reg_window_event(((xcb_create_notify_event_t *)ev)->window, XCB_EVENT_MASK_FOCUS_CHANGE);
			/* FALLTHROUGH */
		case XCB_DESTROY_NOTIFY:
			ewmh_clientlist();
			break;

		case XCB_CLIENT_MESSAGE:
			ewmh_message((xcb_client_message_event_t *)ev);
			break;

		case XCB_FOCUS_IN:
			ewmh_activewindow(((xcb_focus_in_event_t *)ev)->event);
			break;
		case XCB_MAP_NOTIFY:
			if (ewmh_type(((xcb_map_notify_event_t *)ev)->window) == POPUP)
				wm_restack(((xcb_map_notify_event_t *)ev)->window, XCB_STACK_MODE_ABOVE);
		}
		free(ev);
	}

	return -1;
}