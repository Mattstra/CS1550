#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/unistd.h>
#include <sys/select.h>
#include <time.h>
#include <linux/fb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include "iso_font.h"

typedef unsigned short color_t;
unsigned short *mapMem;

//structs for screen info
struct fb_var_screeninfo fbvar;
struct fb_fix_screeninfo fbfix;

void init_graphics()
{
	//open graphics device
	int gfd = open("/dev/fb0", O_RDWR);

	//get screen info
	ioctl(gfd, FBIOGET_VSCREENINFO, &fbvar);
	ioctl(gfd, FBIOGET_FSCREENINFO, &fbfix);
	unsigned int size = fbvar.xres_virtual * fbfix.line_length;
	mapMem = (unsigned short *)mmap(NULL, size, PROT_WRITE, MAP_SHARED, gfd, 0);

	//terminal struct
	struct termios terminal;
	ioctl(STDIN_FILENO, TCGETS, &terminal);
	terminal.c_lflag &= ~(ICANON|ECHO);	//disable canonical and echo modes
	ioctl(STDIN_FILENO, TCSETS, &terminal);	

	
}

void exit_graphics()
{
	//undo changes to canonical and echo modes
	struct termios terminal;
	ioctl(STDIN_FILENO, TCGETS, &terminal);
        terminal.c_lflag |= (ICANON|ECHO);     //enable canonical and echo modes
        ioctl(STDIN_FILENO, TCSETS, &terminal);
	
}

void clear_screen()
{
	write(1, "\033[2J", 8);
}

char getkey()
{
	//see if there was any input and return it

	//timeout struct
	struct timeval tval;

	fd_set fdVal;
	FD_ZERO(&fdVal);
	FD_SET(0, &fdVal);
	
	//wait up to 5 seconds
	tval.tv_sec = 5;
	tval.tv_usec = 0;
	int sfd = select(STDIN_FILENO+1, &fdVal, NULL, NULL, &tval);

	char input;

	if(sfd > 0)
		read(0, &input, sizeof(input));
	return input;	
}

void sleep_ms(long ms)
{
	//timespec struct
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = ms * 1000000;	//milliseconds times 1 million

	nanosleep(&ts, NULL);
}

void draw_pixel(int x, int y, color_t color)
{
	//makes sure that the (x,y) coordinate is not out of bounds
	if((x < 0 || x >= fbvar.xres_virtual) || (y < 0 || y >= fbvar.yres_virtual))
	{
		return;
	}

	else
	{
		//draw pixel at coordinate x,y in framebuffer
		unsigned int xcord = x;
		unsigned int ycord = (fbfix.line_length /2) * y;
		//offset of pixel in framebuffer
		unsigned short *pixel = mapMem + xcord + ycord;
		*pixel = color;
	}
}
//draw a outline of a rectangle with the specified color
void draw_rect(int x1, int y1, int width, int height, color_t color)
{
	//changing x and y coordinate of the pixel being drawn
	int xcord;
	int ycord;

	for(xcord = x1; xcord < x1+width; xcord++)
	{
		for(ycord = y1; ycord < y1+height; ycord++)
		{
			//coordinates to be drawn are from (x1, y1) to 
			//(x1+width-1, y1)
			if(xcord == x1 || xcord == x1+width-1)
				draw_pixel(xcord, ycord, color);
			if(ycord == y1 || ycord == y1+height-1)
				draw_pixel(xcord, ycord, color); 
		}
	}
}

void fill_rect(int x1, int y1, int width, int height, color_t color)
{
	int xcord;
	int ycord;
	for(xcord = x1; xcord < x1+width; xcord++)
	{
		for(ycord = y1; ycord < y1+height; ycord++)
			draw_pixel(xcord, ycord, color);
	}

}

void draw_char(int x, int y, color_t color, int ascii)
{
	int i;
	int j;
	int bit;
	for(i = 0; i < 16; i++)
	{
		//go through each bit of the 16 rows and draw a pixel if
		//the bit is a 1
		for(j = 0; j < 16; j++)
		{
			bit = (iso_font[(ascii * 16) +i] & (1<<j));
			//right shift to value off last bit
			bit = bit >> j;
			if(bit == 1)
				//letter is sideways unless I switch x 
				//and y
				draw_pixel(y+j, x+i, color);
		}
	}
}

void draw_text(int x, int y, const char* text, color_t color)
{
	int i = 0;
	while(text[i] != '\0')
	{
		int ascii = text[i];
		draw_char(x, y, color, ascii);
		i++;
		//add width of letters to x coordinate
		// y is used since x and y have to be switched
		y += 8;
	}
}
