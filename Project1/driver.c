typedef unsigned short color_t;

void init_graphics();
void clear_screen();
void exit_graphics();
void draw_pixel(int x, int y, color_t color);
void draw_rect(int x, int y, int width, int height, color_t color);
void fill_rect(int x, int y, int width, int height, color_t color);
char getkey();

int main()
{
	char key;
	init_graphics();
	clear_screen();
	int x  = (600-20)/2;
	int y = (400-20)/2;
	draw_text(240, 50, "Matthew", 60000);
	do
	{
		key = getkey();
		if(key == 'w')
			y-=5;
		if(key == 's')
			y+=5;
		if(key == 'd')
			x+=5;
		if(key == 'a')
			x-=5;
		draw_rect(x, y, 20, 20, 20);
		sleep_ms(10);
	}
	while(key != 'q');
	clear_screen();
	exit_graphics();
	return 0;
}
