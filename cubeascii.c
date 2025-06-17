#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>

// === CONFIGURATION ===

// Screen dimensions and visual settings
#define MAP_WIDTH 16
#define MAP_HEIGHT 16
#define SCREEN_WIDTH  90
#define SCREEN_HEIGHT 50

// Colors
#define RED_1 "\033[48;2;255;50;50m"
#define RED_2 "\033[48;2;200;30;30m"
#define RED_3 "\033[48;2;150;20;20m"
#define RED_4 "\033[48;2;100;10;10m"
#define RED_5 "\033[48;2;60;5;5m"
#define SKY_BG "\033[48;2;135;206;250m"
#define FLOOR_BG "\033[48;2;50;50;50m"
#define RESET "\033[0m"

#define WALL_COLOR "\033[48;2;80;80;80m"
#define FLOOR_COLOR "\033[48;2;30;30;30m"
#define PLAYER_COLOR "\033[48;2;255;0;0m"
#define PIXEL_CHAR "  "
//ALTERNATE PIXEL_CHAR : ░ ▒, ▓,

// mm chars (futur implementation)
#define DIR_CHAR_UP "▀▀"
#define DIR_CHAR_DOWN  "▄▄"
#define DIR_CHAR_LEFT "█ "
#define DIR_CHAR_RIGHT " █"
#define BORDER_COLOR "\033[48;2;0;0;0m"
#define BORDER_CHAR  "  "

// Map of the scene, each Char or block/tile is described as a "cell" in subsequent comments.
char *map[] = 
{
	"1111111111111111",
	"1100000000000001",
	"1101010101010101",
	"1100000000000001",
	"1000000000001101",
	"1011110001111101",
	"1001100000111101",
	"1001000100100001",
	"1111000000100111",
	"1110111110001111",
	"1010001000001111",
	"1011100001111001",
	"1000011000000001",
	"1011001000000001",
	"100110000P000001",
	"1111111111111111",
};

// The player's state in the world,
// position, direction, and field of view (FOV) projection plane.
typedef struct {
	float x;		// Player position on the map
	float y;
	
	float	dirX;	// Direction vector :
	float	dirY;	// Represents where the player is looking.
					// Coordinates are "inverted" compared to carthesian coordinates (screen convention).
					// Example: (0, -1) = facing up (north) on the grid

    float planeX;	// 2D Camera plane vector (horizontal component)
    float planeY;	// 2D Camera plane vector (vertical component)

	/*
		Vectors describe a direction in map space, Vector AB describes the direction from point A to B,
		and is noted as two variables that are the coordinates of B in a new geometrical space where A is the origin.
		(This is not a lesson on what vectors are. For that, visit : https://en.wikipedia.org/wiki/Euclidean_vector)

		The camera plane is a vector perpendicular to the player's direction vector (dirX, dirY).
		It represents the "screen plane" in 2D space, i.e., what the player sees stretched across the field of view.

		For example, if the player is facing north (dirX = 0, dirY = -1),
		then the camera plane might be horizontal: (planeX = 0.70, planeY = 0).

		The LENGTH (or magnitude) of this vector controls the field of view (FOV).
		A longer plane means a wider FOV; typical values are around 0.66

		Rays are generated across the screen from left to right.
		For each screen column, a value cameraX ∈ [-1, +1] is used to interpolate across the camera plane:

			rayDirX = dirX + planeX * cameraX;
			rayDirY = dirY + planeY * cameraX;

		This means:
			- When cameraX = -1 → ray points toward the far left edge of the screen.
			- When cameraX =  0 → ray points straight ahead (aligned with direction vector).
			- When cameraX = +1 → ray points toward the far right edge.

		This interpolation generates one ray for each column of pixels on the screen, 
		spreading them across the field of view and simulating perspective.
		This creates the illusion of depth using simple 2D math.
		(i just explained 2D raycasting.)

		To summarize :
			- The direction vector points where the player is looking.
			- The camera plane vector defines how wide the player can see (FOV).
			- Ray directions are computed by combining both, scaled by cameraX.

		here's a small visual representation :
			<-------  camera plane (planeX, planeY)
				^
				|						keep in mind that vectors don't actually exist in the scene, 
				|   dir (0, -1)			they dont have a "starting point".
				P   player position		They represent a DIRECTION, and have a LENGTH.
		*/

} t_player;

// Represents a single ray cast from the player's position,
// used to calculate wall collisions and rendering information for one vertical column.
typedef struct {
	float cameraX;		// X-coordinate in camera space:
						// Ranges from -1 (left side of screen) to +1 (right side).
						// Used to interpolate between the left/right extremes of the FOV.

	float rayDirX;		// Direction of the ray (X axis)
	float rayDirY;		// Direction of the ray (Y axis)
						// This is calculated by combining the player's direction
						// with the camera plane scaled by cameraX.

	int mapX;			// Current cell the ray is in on the X axis
	int mapY;			// Current cell the ray is in on the Y axis

	float deltaDistX;	// Distance the ray travels between successive vertical gridlines.
						// Computed as: deltaDistX = abs(1 / rayDirX)
						// It represents how far the ray must move to go from one x-side to the next.
	float deltaDistY;	// deltaDistY Same but for horizontal gridlines (y-axis)

	/*
		deltaDistX and deltaDistY are constant for each rays.
		Computed as:
			deltaDistX = fabs(1 / rayDirX);
			deltaDistX = fabs(1 / rayDirY);

		Some explanations :

		A vertical gridline is located at integer x-values (e.g., x = 1, 2, 3...).
		So when a ray crosses from one vertical gridline to the next, it has moved exactly 1 unit in X.
		But how far has it traveled in the 2D world, diagonally?

		Here's a simple ray :
		    |
    		|               *
    		|           *
    		|       *
    		|   *
    		+----------------→ x axis
		
		Imagine a triangle formed by the ray's path:
			- Horizontal side	= 1 (fixed distance between vertical lines)
			- Vertical side		= changes with the ray's angle (rayDirY / rayDirX)
			- Hypotenuse		= actual distance the ray travels to reach the next vertical line

		From Pythagoras:
			Ray advances 1 unit along the X axis. We calculate the necessary Y distance 
			that corresponds to this horizontal step.
			We're determining the diagonal distance the ray travels to cross one full grid square in the X direction :
				deltaDistX	= sqrt(1 + (rayDirY / rayDirX) * (rayDirY / rayDirX));

			Rewrite as a single square root by combining the fraction under one root for clarity.
			deltaDistX	= sqrt((rayDirX * rayDirX + rayDirY * rayDirY) / (rayDirX * rayDirX));

			Split the square root into numerator and denominator, taking absolute value of the denominator 
			to ensure positive distance.
				deltaDistX	= sqrt(rayDirX * rayDirX + rayDirY * rayDirY) / fabs(rayDirX);

			Recognize the numerator as the magnitude (length) of the ray direction vector.
			deltaDistX	= |rayDir| / fabs(rayDirX);


		Since rayDir is normalized (it's not, but it's treated proportionally across rays, eh) :
			|rayDir| = 1

		So : deltaDistX = 1 / fabs(rayDirX)

		The same applies to deltaDistY:
			deltaDistY = 1 / fabs(rayDirY)

		These values are used in the DDA algorithm to step through the grid,
		deciding whether the ray hits the next vertical or horizontal gridline first.
	*/

	float sideDistX;	// Distance the ray has to travel from its starting position
						// to the next x-side (vertical grid line)
	float sideDistY;	// sideDistY Same as above but for y-side (horizontal grid line)

	int stepX;			// Direction to step in X (either +1 or -1)
	int stepY;			// Direction to step in Y (either +1 or -1)

	int hit;			// Flag: 0 = no wall hit yet, 1 = wall has been hit
	int side;			// Whether the wall was hit from the X (0) or Y (1) side.
						// Used for shading and perspective correction.

	float perpWallDist;	// Corrected perpendicular distance to the wall from the player.
						// This is what ultimatly determines how tall the wall slice is.
						// Avoids weird rendering effect when ray is not straight-on.
} t_ray;

// Buffers for each screen column.
int			draw_start[SCREEN_WIDTH];
int			draw_end[SCREEN_WIDTH];
const char	*wallColor[SCREEN_WIDTH];

// Get appropriate red shade based on distance, closer is brighter.
const char *get_shade(float dist)
{
	if (dist < 1.5)
		return RED_1;
	if (dist < 3.0)
		return RED_2;
	if (dist < 5.0)
		return RED_3;
	if (dist < 7.0)
		return RED_4;
	return RED_5;
}

// Clears terminal.
void clear_screen()
{
	printf("\033[H\033[J");
}

// Initializes all ray parameters for a single screen column.
// Computes the ray direction based on the camera plane and player view direction.
// Sets the initial map tile the ray is in, and calculates the fixed distances (deltaDistX/Y)
// between x/y-side intersections. Handles division-by-zero by using a very large float.
void init_ray(t_ray *ray, int column, t_player *player)
{
	ray->cameraX = 2 * column / (float)SCREEN_WIDTH - 1;
	ray->rayDirX = player->dirX + player->planeX * ray->cameraX;
	ray->rayDirY = player->dirY + player->planeY * ray->cameraX;
	ray->mapX = (int)player->x;
	ray->mapY = (int)player->y;
	ray->deltaDistX = 0;
	ray->deltaDistY = 0;

	if (ray->rayDirX != 0) {
		ray->deltaDistX = fabs(1 / ray->rayDirX);
	} else {
		ray->deltaDistX = 1e30;
	}
	if (ray->rayDirY != 0) {
		ray->deltaDistY = fabs(1 / ray->rayDirY);
	} else {
		ray->deltaDistY = 1e30;
	}
	ray->hit = 0;
}

// Based on the ray's direction, this function determines which direction (+/-) to step
// along the X and Y axes. Also calculates the initial distance from the player's position
// to the first X or Y side of the current map square.
void compute_initial_steps(t_ray *ray, t_player *player)
{
	if (ray->rayDirX < 0)
	{
		ray->stepX = -1;
		ray->sideDistX = (player->x - ray->mapX) * ray->deltaDistX;
	}
	else
	{
		ray->stepX = 1;
		ray->sideDistX = (ray->mapX + 1.0 - player->x) * ray->deltaDistX;
	}
	if (ray->rayDirY < 0)
	{
		ray->stepY = -1;
		ray->sideDistY = (player->y - ray->mapY) * ray->deltaDistY;
	}
	else
	{
		ray->stepY = 1;
		ray->sideDistY = (ray->mapY + 1.0 - player->y) * ray->deltaDistY;
	}
}

// Executes the DDA loop to find where the ray hits a wall on the map.
// At each step, it advances the ray to the next tile in either X or Y direction,
// based on which side is closer. It stops when a wall (a '1' cell) is hit.
// Finally, computes the perpendicular distance from the player to the wall.
// (https://en.wikipedia.org/wiki/Digital_differential_analyzer_(graphics_algorithm))
void perform_dda(t_ray *ray)
{
	while (!ray->hit)
	{
		if (ray->sideDistX < ray->sideDistY)
		{
			ray->sideDistX += ray->deltaDistX;
			ray->mapX += ray->stepX;
			ray->side = 0;
		}
		else
		{
			ray->sideDistY += ray->deltaDistY;
			ray->mapY += ray->stepY;
			ray->side = 1;
		}
		if (map[ray->mapY][ray->mapX] == '1')
			ray->hit = 1;
	}
	if (ray->side == 0)
		ray->perpWallDist = ray->sideDistX - ray->deltaDistX;
	else
		ray->perpWallDist = ray->sideDistY -ray->deltaDistY;
}

// Computes the vertical line (or "slice") on the screen to draw the wall columns,
// based on how far the wall is from the player.
// Stores the start and end pixel rows for drawing, and selects a "shaded" color for walls
void compute_wall_slice(t_ray *ray, int x)
{
	int lineHeight;
	int start;
	int end;

	lineHeight = (int)(SCREEN_HEIGHT / ray->perpWallDist);
	start = -lineHeight / 2 + SCREEN_HEIGHT / 2;
	end = lineHeight / 2 + SCREEN_HEIGHT / 2;
	if (start < 0)
		start = 0;
	if (end >= SCREEN_HEIGHT)
		end = SCREEN_HEIGHT - 1;
	draw_start[x] = start;
	draw_end[x] = end;
	wallColor[x] = get_shade(ray->perpWallDist);
}

// The main render loop.
// For each vertical column on the screen, a ray is cast, DDA is performed,
// and a wall slice is computed and stored.
// Then the screen is drawn line by line, by printing each pixel column with
// sky, wall, or floor color accordingly.
// draw_start[x] is top of the wall slice in column x
// draw_end[x] is bottom of the wall slice in column x
void render(t_player player)
{
	int x, y;
	t_ray ray;

	x = 0;
	clear_screen();
	while (x < SCREEN_WIDTH)
	{
		init_ray(&ray, x, &player);
		compute_initial_steps(&ray, &player);
		perform_dda(&ray);
		compute_wall_slice(&ray, x);
		x++;
	}
	y = 0;
	while (y < SCREEN_HEIGHT)
	{
		x = 0;
		while (x < SCREEN_WIDTH)
		{
			if (y < draw_start[x])
				printf(SKY_BG PIXEL_CHAR RESET);
			else if (y <= draw_end[x])
				printf("%s" PIXEL_CHAR RESET, wallColor[x]);
			else
				printf(FLOOR_BG PIXEL_CHAR RESET);
			x++;
		}
		printf("\n");
		y++;
	}
}

// Returns 1 if a key has been pressed (non-blocking), 0 otherwise.
// Temporarily puts terminal in non-canonical, non-blocking mode,
// checks for input, then restores terminal state.
int kbhit(void)
{
	struct termios oldt;
	struct termios newt;
	int ch;
	int oldf;

	tcgetattr(0, &oldt);
	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(0, TCSANOW, &newt);
	oldf = fcntl(0, F_GETFL, 0);
	fcntl(0, F_SETFL, oldf | O_NONBLOCK);
	ch = getchar();
	tcsetattr(0, TCSANOW, &oldt);
	fcntl(0, F_SETFL, oldf);
	if (ch != EOF)
		return (ungetc(ch, stdin), 1);
	return (0);
}

// Minimal getch implementation
// Reads a single character from stdin without waiting for Enter
// and without echoing the input to the terminal.
// Temporarily sets the terminal to non-canonical, no-echo mode.
char getch()
{
	char buf;
	struct termios old = {0};

	buf = 0;
	if (tcgetattr(0, &old) < 0) 
		perror("tcsetattr()");
	old.c_lflag &= ~ICANON;
	old.c_lflag &= ~ECHO;
	tcsetattr(0, TCSANOW, &old);
	read(0, &buf, 1);
	old.c_lflag |= ICANON;
	old.c_lflag |= ECHO;
	tcsetattr(0, TCSADRAIN, &old);
	return (buf);
}

//moves the player.
//?
void move_player(t_player *p, char key)
{
	float moveSpeed;
	float rotSpeed;
	float newX;
	float newY;
	float oldDirX;
	float oldPlaneX;

	moveSpeed = 0.1;
	rotSpeed = 0.05;
	if (key == 'w') //move forward
	{
		newX = p->x + p->dirX * moveSpeed;
		newY = p->y + p->dirY * moveSpeed;
		if (map[(int)newY][(int)p->x] != '1') p->y = newY;
		if (map[(int)p->y][(int)newX] != '1') p->x = newX;
	}
	if (key == 's') // move backward
	{
		newX = p->x - p->dirX * moveSpeed;
		newY = p->y - p->dirY * moveSpeed;
		if (map[(int)newY][(int)p->x] != '1') p->y = newY;
		if (map[(int)p->y][(int)newX] != '1') p->x = newX;
	}
	if (key == 'a') // strafe left
	{
		newX = p->x - p->planeX * moveSpeed;
		newY = p->y - p->planeY * moveSpeed;
		if (map[(int)newY][(int)p->x] != '1') p->y = newY;
		if (map[(int)p->y][(int)newX] != '1') p->x = newX;
	}
	if (key == 'd') // strafe right
	{
		newX = p->x + p->planeX * moveSpeed;
		newY = p->y + p->planeY * moveSpeed;
		if (map[(int)newY][(int)p->x] != '1') p->y = newY;
		if (map[(int)p->y][(int)newX] != '1') p->x = newX;
	}
	if (key == 'e') // rotate left
	{
		oldDirX = p->dirX;
		p->dirX = p->dirX * cos(rotSpeed) - p->dirY * sin(rotSpeed);
		p->dirY = oldDirX * sin(rotSpeed) + p->dirY * cos(rotSpeed);
		oldPlaneX = p->planeX;
		p->planeX = p->planeX * cos(rotSpeed) - p->planeY * sin(rotSpeed);
		p->planeY = oldPlaneX * sin(rotSpeed) + p->planeY * cos(rotSpeed);
	}
	if (key == 'q') // rotate right
	{
		oldDirX = p->dirX;
		p->dirX = p->dirX * cos(-rotSpeed) - p->dirY * sin(-rotSpeed);
		p->dirY = oldDirX * sin(-rotSpeed) + p->dirY * cos(-rotSpeed);
		oldPlaneX = p->planeX;
		p->planeX = p->planeX * cos(-rotSpeed) - p->planeY * sin(-rotSpeed);
		p->planeY = oldPlaneX * sin(-rotSpeed) + p->planeY * cos(-rotSpeed);
	}
}

void print_tab(char **t)
{
	while(*t)
		printf("%s\n",*t++);
}

int main()
{
	t_player player;
	char c;
	int y;
	int x;

	y = 0;
	while (y < MAP_HEIGHT)
	{
		x = 0;
		while (x < MAP_WIDTH)
		{
			if (map[y][x] == 'P')
			{
				player.x = x + 0.5;
				player.y = y + 0.5;
				player.dirX = 0;
				player.dirY = -1;
				player.planeX = 0.66;
				player.planeY = 0;
			}
			x++;
		}
		y++;
	}
	while (1)
	{
		render(player);
		if (kbhit())
		{
			c = getch();
			if (c == 27) break;
			move_player(&player, c);
		}
		usleep(15000);
	}
	return 0;
}
