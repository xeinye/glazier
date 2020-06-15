/* define crosshairs used for various operations */
char *xhair[] = {
	[XHAIR_MOVE] = "hand1",
	[XHAIR_SIZE] = "lr_angle",
	[XHAIR_TELE] = "tcross",
	[XHAIR_DFLT] = "left_ptr",
};

/* key that must be pressed to register mouse events */
int modifier = XCB_MOD_MASK_4;

/* window borders and titlebar */
int border = 2;
int border_color = 0x666666;
int border_color_active = 0xcc6666;

/* move/resize step amound in pixels */
int move_step = 16;
