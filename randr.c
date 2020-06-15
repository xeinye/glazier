#include <xcb/xcb.h>
#include <xcb/randr.h>

#include "randr.h"

extern xcb_connection_t *conn;
extern xcb_screen_t *scrn;

/*
 * Return the geometry of the monitor at the given coordinates
 */
int
randr_geometry(int x, int y, struct geometry_t *g)
{
	xcb_randr_get_monitors_cookie_t c;
	xcb_randr_get_monitors_reply_t *r;
	xcb_randr_monitor_info_iterator_t i;

	/* get_active: ignore inactive monitors */
	c = xcb_randr_get_monitors(conn, scrn->root, 1);
	r = xcb_randr_get_monitors_reply(conn, c, NULL);
	i = xcb_randr_get_monitors_monitors_iterator(r);

	while (g && i.rem > 0) {
		if (x >= i.data->x
		 && y >= i.data->y
		 && x <= i.data->width + i.data->x
		 && y <= i.data->height + i.data->y) {
			g->x = i.data->x;
			g->y = i.data->y;
			g->w = i.data->width;
			g->h = i.data->height;
			return 0;
		}
		xcb_randr_monitor_info_next(&i);
	}

	return -1;
}
