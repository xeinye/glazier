#include <stdio.h>
#include <xcb/xcb.h>

#include "wm.h"

#define LEN(x) (sizeof(x)/sizeof(x[0]))

struct ev_callback_t {
	uint32_t type;
	int (*handle)(xcb_generic_event_t *);
};

static int cb_create(xcb_generic_event_t *);
static int ev_callback(xcb_generic_event_t *);
static void ev_loop();

xcb_connection_t *conn;
xcb_screen_t     *scrn;

static const struct ev_callback_t cb[] = {
	/* event,                 function */
	{  XCB_CREATE_NOTIFY,     cb_create },
};

static int
cb_create(xcb_generic_event_t *ev)
{
	xcb_create_notify_event_t *e = (xcb_create_notify_event_t *)ev;

	printf("%d:%d\n", e->response_type, e->window);
	return 0;
}

static int
ev_callback(xcb_generic_event_t *ev)
{
	uint8_t i;
	uint32_t type;

	type = ev->response_type & ~0x80;
	for (i=0; i<LEN(cb); i++)
		if (type == cb->type)
			return cb->handle(ev);

	return 1;
}

static void
ev_loop()
{
	xcb_generic_event_t *ev = NULL;

	/* needed to get notified of windows creation */
	wm_reg_event(scrn->root, XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY);
	xcb_flush(conn);

	for (;;) {
		ev = xcb_wait_for_event(conn);
		ev_callback(ev);
	}
}

int
main (int argc, char *argv[])
{
	wm_init_xcb();
	wm_get_screen();
	ev_loop();
	wm_kill_xcb();

	return 0;
}