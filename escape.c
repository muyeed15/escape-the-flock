#include <unistd.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <termios.h>

#define STATE_FILE "game_state.tmp"
#define SNAKE_SLEEP 2
#define WOLF_SLEEP 1
#define MAX_ROWS 20
#define MAX_COLS 20
#define MAX_STATE_BUF 4096
#define MAX_DISPLAY_BUF 16384

/* utilities */

int my_strlen(const char *s)
{
	int i = 0;
	while (s[i]) i++;
	return i;
}

int my_isdigit(char c)
{
	return c >= '0' && c <= '9';
}

int my_atoi(const char *s)
{
	int n = 0;
	while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
	return n;
}

void my_itoa(int n, char *buf, int *pos)
{
	char tmp[12];
	int i = 0;
	if (n == 0) {
		buf[(*pos)++] = '0';
		return;
	}
	while (n > 0) {
		tmp[i++] = '0' + (n % 10);
		n /= 10;
	}
	while (i > 0) buf[(*pos)++] = tmp[--i];
}

void buf_append_char(char *buf, int *pos, char c)
{
	buf[(*pos)++] = c;
}

void buf_append_str(char *buf, int *pos, const char *s)
{
	while (*s) buf[(*pos)++] = *s++;
}

void buf_append_int(char *buf, int *pos, int n)
{
	my_itoa(n, buf, pos);
}

/* map */

int **allocate_map(int rows, int cols)
{
	int **map = (int **)malloc(sizeof(int *) * rows);
	if (!map) return 0;
	for (int i = 0; i < rows; i++) {
		map[i] = (int *)malloc(sizeof(int) * cols);
		if (!map[i]) {
			for (int j = 0; j < i; j++) free(map[j]);
			free(map);
			return 0;
		}
	}
	return map;
}

void free_map(int **map, int rows)
{
	if (!map) return;
	for (int i = 0; i < rows; i++) free(map[i]);
	free(map);
}

/* file io */

int read_file_to_buffer(int fd, char *buf, int max_size)
{
	int total = 0, n;
	while (total < max_size - 1) {
		n = read(fd, buf + total, max_size - 1 - total);
		if (n <= 0) break;
		total += n;
	}
	buf[total] = '\0';
	return total;
}

int parse_map_buffer(char *buf, int ***map_ptr, int *rows_ptr, int *cols_ptr, int *state_ptr)
{
	int pos = 0;
	while (buf[pos] == ' ' || buf[pos] == '\n' || buf[pos] == '\r') pos++;

	*state_ptr = my_atoi(buf + pos);
	while (buf[pos] && buf[pos] != '\n') pos++;
	if (buf[pos] == '\n') pos++;

	while (buf[pos] == ' ' || buf[pos] == '\n' || buf[pos] == '\r') pos++;
	*rows_ptr = my_atoi(buf + pos);
	while (buf[pos] && buf[pos] >= '0' && buf[pos] <= '9') pos++;
	while (buf[pos] == ' ' || buf[pos] == '\r') pos++;
	*cols_ptr = my_atoi(buf + pos);
	while (buf[pos] && buf[pos] != '\n') pos++;
	if (buf[pos] == '\n') pos++;

	int **map = allocate_map(*rows_ptr, *cols_ptr);
	if (!map) return 0;
	*map_ptr = map;

	for (int i = 0; i < *rows_ptr; i++) {
		for (int j = 0; j < *cols_ptr; j++) {
			while (buf[pos] == ' ' || buf[pos] == '\r' || buf[pos] == '\n') pos++;
			map[i][j] = my_atoi(buf + pos);
			while (buf[pos] && buf[pos] >= '0' && buf[pos] <= '9') pos++;
		}
	}
	return 1;
}

void build_state_string(int **map, int rows, int cols, int state, char *buf, int *len)
{
	*len = 0;
	buf_append_int(buf, len, state);
	buf_append_char(buf, len, '\n');
	buf_append_int(buf, len, rows);
	buf_append_char(buf, len, ' ');
	buf_append_int(buf, len, cols);
	buf_append_char(buf, len, '\n');
	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < cols; j++) {
			buf_append_int(buf, len, map[i][j]);
			if (j < cols - 1) buf_append_char(buf, len, ' ');
		}
		buf_append_char(buf, len, '\n');
	}
	buf[*len] = '\0';
}

int read_state_from_file(int fd, int **map, int rows, int cols, int *state)
{
	lseek(fd, 0, SEEK_SET);
	char buf[MAX_STATE_BUF];
	int n = read(fd, buf, MAX_STATE_BUF - 1);
	if (n <= 0) return -1;
	buf[n] = '\0';

	int pos = 0;
	while (buf[pos] == ' ' || buf[pos] == '\n' || buf[pos] == '\r') pos++;
	*state = my_atoi(buf + pos);
	while (buf[pos] && buf[pos] != '\n') pos++;
	if (buf[pos] == '\n') pos++;

	/* skip the rows cols line */
	while (buf[pos] == ' ' || buf[pos] == '\n' || buf[pos] == '\r') pos++;
	while (buf[pos] && buf[pos] >= '0' && buf[pos] <= '9') pos++;
	while (buf[pos] == ' ' || buf[pos] == '\r' || buf[pos] == '\n') pos++;
	while (buf[pos] && buf[pos] >= '0' && buf[pos] <= '9') pos++;
	while (buf[pos] && buf[pos] != '\n') pos++;
	if (buf[pos] == '\n') pos++;

	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < cols; j++) {
			while (buf[pos] == ' ' || buf[pos] == '\r' || buf[pos] == '\n') pos++;
			map[i][j] = my_atoi(buf + pos);
			while (buf[pos] && buf[pos] >= '0' && buf[pos] <= '9') pos++;
		}
	}
	return 0;
}

int write_state_to_file(int fd, int **map, int rows, int cols, int state)
{
	char buf[MAX_STATE_BUF];
	int len;
	build_state_string(map, rows, cols, state, buf, &len);
	lseek(fd, 0, SEEK_SET);
	if (write(fd, buf, len) != len) return -1;
	ftruncate(fd, len);
	return 0;
}

/* display */

void set_noncanonical(struct termios *saved)
{
	struct termios newt;
	tcgetattr(0, saved);
	newt = *saved;
	newt.c_lflag &= ~(ICANON | ECHO);
	newt.c_cc[VMIN] = 0;
	newt.c_cc[VTIME] = 10;
	tcsetattr(0, TCSANOW, &newt);
}

void restore_terminal(struct termios *saved)
{
	tcsetattr(0, TCSANOW, saved);
}

void clear_and_home(char *buf, int *pos)
{
	buf_append_str(buf, pos, "\033[H\033[2J");
}

void build_display(int **map, int rows, int cols, char *buf, int *len)
{
	*len = 0;
	clear_and_home(buf, len);
	buf_append_char(buf, len, '\n');

	/* top border */
	for (int j = 0; j < cols + 2; j++)
		buf_append_char(buf, len, '*');
	buf_append_char(buf, len, '\n');

	for (int i = 0; i < rows; i++) {
		buf_append_char(buf, len, '*');
		for (int j = 0; j < cols; j++) {
			switch (map[i][j]) {
			case 0:
				buf_append_char(buf, len, ' ');
				break;
			case 1:
				buf_append_str(buf, len, "\033[47m \033[0m");
				break;
			case 2:
				buf_append_char(buf, len, 'P');
				break;
			case 3:
				buf_append_char(buf, len, 'G');
				break;
			case 4:
				buf_append_char(buf, len, '~');
				break;
			case 5:
				buf_append_char(buf, len, 'W');
				break;
			default:
				buf_append_char(buf, len, ' ');
				break;
			}
		}
		buf_append_char(buf, len, '*');
		buf_append_char(buf, len, '\n');
	}

	/* bottom border */
	for (int j = 0; j < cols + 2; j++)
		buf_append_char(buf, len, '*');
	buf_append_char(buf, len, '\n');

	buf_append_str(buf, len, "Press w to move UP\n");
	buf_append_str(buf, len, "Press s to move DOWN\n");
	buf_append_str(buf, len, "Press a to move LEFT\n");
	buf_append_str(buf, len, "Press d to move RIGHT\n");
}

void display_map(int **map, int rows, int cols)
{
	char buf[MAX_DISPLAY_BUF];
	int len;
	build_display(map, rows, cols, buf, &len);
	write(1, buf, len);
}

void display_game_over(int **map, int rows, int cols, int state)
{
	char buf[MAX_DISPLAY_BUF];
	int len = 0;
	clear_and_home(buf, &len);
	buf_append_char(buf, &len, '\n');

	for (int j = 0; j < cols + 2; j++)
		buf_append_char(buf, &len, '*');
	buf_append_char(buf, &len, '\n');

	for (int i = 0; i < rows; i++) {
		buf_append_char(buf, &len, '*');
		for (int j = 0; j < cols; j++) {
			switch (map[i][j]) {
			case 0: buf_append_char(buf, &len, ' '); break;
			case 1: buf_append_str(buf, &len, "\033[47m \033[0m"); break;
			case 2: buf_append_char(buf, &len, 'P'); break;
			case 3: buf_append_char(buf, &len, 'G'); break;
			case 4: buf_append_char(buf, &len, '~'); break;
			case 5: buf_append_char(buf, &len, 'W'); break;
			default: buf_append_char(buf, &len, ' '); break;
			}
		}
		buf_append_char(buf, &len, '*');
		buf_append_char(buf, &len, '\n');
	}

	for (int j = 0; j < cols + 2; j++)
		buf_append_char(buf, &len, '*');
	buf_append_char(buf, &len, '\n');

	buf_append_str(buf, &len, "Press w to move UP\n");
	buf_append_str(buf, &len, "Press s to move DOWN\n");
	buf_append_str(buf, &len, "Press a to move LEFT\n");
	buf_append_str(buf, &len, "Press d to move RIGHT\n");

	if (state == 1)
		buf_append_str(buf, &len, "Player wins!!\n");
	else if (state == 2)
		buf_append_str(buf, &len, "Wolf bites the player!!\n");
	else if (state == 3)
		buf_append_str(buf, &len, "Snake bites the player!!\n");

	buf_append_str(buf, &len, "Press any key to quit\n");

	write(1, buf, len);

	{
		struct termios oldt, newt;
		tcgetattr(0, &oldt);
		newt = oldt;
		newt.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(0, TCSANOW, &newt);
		char dummy;
		read(0, &dummy, 1);
		tcsetattr(0, TCSANOW, &oldt);
	}
}

/* movement */

int find_position(int **map, int rows, int cols, int target, int *r, int *c)
{
	for (int i = 0; i < rows; i++)
		for (int j = 0; j < cols; j++)
			if (map[i][j] == target) {
				*r = i;
				*c = j;
				return 0;
			}
	return -1;
}

int valid_cell_type(int cell, int is_player)
{
	if (cell == 1) return 0;
	if (is_player)
		return (cell != 4 && cell != 5);
	else
		return (cell == 0);
}

int is_valid_move(int **map, int rows, int cols, int r, int c, int is_player)
{
	if (r < 0 || r >= rows || c < 0 || c >= cols) return 0;
	return valid_cell_type(map[r][c], is_player);
}

int is_adjacent_snake(int sr, int sc, int pr, int pc)
{
	int dr = sr - pr;
	int dc = sc - pc;
	return (dr >= -1 && dr <= 1 && dc >= -1 && dc <= 1);
}

int is_adjacent_wolf(int wr, int wc, int pr, int pc)
{
	return ((wr == pr && (wc == pc - 1 || wc == pc + 1)) ||
	        (wc == pc && (wr == pr - 1 || wr == pr + 1)));
}

/* enemy */

void get_snake_dirs(int *drs, int *dcs)
{
	drs[0] = -1; dcs[0] = -1;
	drs[1] = -1; dcs[1] = 0;
	drs[2] = -1; dcs[2] = 1;
	drs[3] = 0;  dcs[3] = -1;
	drs[4] = 0;  dcs[4] = 1;
	drs[5] = 1;  dcs[5] = -1;
	drs[6] = 1;  dcs[6] = 0;
	drs[7] = 1;  dcs[7] = 1;
}

void get_wolf_dirs(int *drs, int *dcs)
{
	drs[0] = -1; dcs[0] = 0;
	drs[1] = 1;  dcs[1] = 0;
	drs[2] = 0;  dcs[2] = -1;
	drs[3] = 0;  dcs[3] = 1;
}

/* move enemy, return 1 if attacked */
int move_enemy_random(int **map, int rows, int cols, int *er, int *ec,
                      int enemy_type, int pr, int pc)
{
	int drs[8], dcs[8];
	int ndirs;
	if (enemy_type == 4) {
		get_snake_dirs(drs, dcs);
		ndirs = 8;
	} else {
		get_wolf_dirs(drs, dcs);
		ndirs = 4;
	}

	/* attack if adjacent */
	int adjacent = (enemy_type == 4)
		? is_adjacent_snake(*er, *ec, pr, pc)
		: is_adjacent_wolf(*er, *ec, pr, pc);

	if (adjacent) {
		map[*er][*ec] = 0;
		map[pr][pc] = enemy_type;
		*er = pr;
		*ec = pc;
		return 1;
	}

	/* collect valid moves */
	int valid_dr[8], valid_dc[8];
	int nvalid = 0;
	for (int d = 0; d < ndirs; d++) {
		int nr = *er + drs[d];
		int nc = *ec + dcs[d];
		if (is_valid_move(map, rows, cols, nr, nc, 0))
			valid_dr[nvalid] = drs[d], valid_dc[nvalid++] = dcs[d];
	}

	if (nvalid == 0) return 0;

	/* pick random */
	int pick = rand() % nvalid;
	int nr = *er + valid_dr[pick];
	int nc = *ec + valid_dc[pick];

	map[*er][*ec] = 0;
	map[nr][nc] = enemy_type;
	*er = nr;
	*ec = nc;
	return 0;
}

/* processes */

void snake_process(const char *state_file, int rows, int cols)
{
	int **map = allocate_map(rows, cols);
	if (!map) _exit(1);

	int fd = open(state_file, O_RDWR);
	if (fd < 0) { free_map(map, rows); _exit(1); }

	srand(time(0) ^ (getpid() << 16));

	int pr = -1, pc = -1, sr = -1, sc = -1;
	struct timespec ts = { SNAKE_SLEEP, 0 };

	while (1) {
		nanosleep(&ts, 0);

		flock(fd, LOCK_EX);

		int state;
		if (read_state_from_file(fd, map, rows, cols, &state) < 0) {
			flock(fd, LOCK_UN);
			break;
		}

		if (state != 0) {
			flock(fd, LOCK_UN);
			break;
		}

		if (find_position(map, rows, cols, 2, &pr, &pc) != 0 ||
		    find_position(map, rows, cols, 4, &sr, &sc) != 0) {
			flock(fd, LOCK_UN);
			break;
		}

		int attacked = move_enemy_random(map, rows, cols, &sr, &sc, 4, pr, pc);

		int new_state = 0;
		if (attacked) new_state = 3;

		write_state_to_file(fd, map, rows, cols, new_state);
		flock(fd, LOCK_UN);

		if (new_state != 0) break;
	}

	free_map(map, rows);
	close(fd);
	_exit(0);
}

void wolf_process(const char *state_file, int rows, int cols)
{
	int **map = allocate_map(rows, cols);
	if (!map) _exit(1);

	int fd = open(state_file, O_RDWR);
	if (fd < 0) { free_map(map, rows); _exit(1); }

	srand(time(0) ^ (getpid() << 16));

	int pr = -1, pc = -1, wr = -1, wc = -1;
	struct timespec ts = { WOLF_SLEEP, 0 };

	while (1) {
		nanosleep(&ts, 0);

		flock(fd, LOCK_EX);

		int state;
		if (read_state_from_file(fd, map, rows, cols, &state) < 0) {
			flock(fd, LOCK_UN);
			break;
		}

		if (state != 0) {
			flock(fd, LOCK_UN);
			break;
		}

		if (find_position(map, rows, cols, 2, &pr, &pc) != 0 ||
		    find_position(map, rows, cols, 5, &wr, &wc) != 0) {
			flock(fd, LOCK_UN);
			break;
		}

		int attacked = move_enemy_random(map, rows, cols, &wr, &wc, 5, pr, pc);

		int new_state = 0;
		if (attacked) new_state = 2;

		write_state_to_file(fd, map, rows, cols, new_state);
		flock(fd, LOCK_UN);

		if (new_state != 0) break;
	}

	free_map(map, rows);
	close(fd);
	_exit(0);
}

/* main */

int main(int argc, char *argv[])
{
	if (argc != 2) {
		char *msg = "Usage: ./escape <filename>\n";
		write(2, msg, my_strlen(msg));
		return 1;
	}

	/* read map file */
	int orig_fd = open(argv[1], O_RDONLY);
	if (orig_fd < 0) {
		char *msg = "Error: Cannot open map file.\n";
		write(2, msg, my_strlen(msg));
		return 1;
	}

	char buf[MAX_STATE_BUF];
	if (read_file_to_buffer(orig_fd, buf, MAX_STATE_BUF) <= 0) {
		char *msg = "Error: Cannot read map file.\n";
		write(2, msg, my_strlen(msg));
		close(orig_fd);
		return 1;
	}
	close(orig_fd);

	int **map;
	int rows, cols, state;
	if (!parse_map_buffer(buf, &map, &rows, &cols, &state)) {
		char *msg = "Error: Failed to parse map.\n";
		write(2, msg, my_strlen(msg));
		return 1;
	}

	/* create state file */
	int state_fd = open(STATE_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (state_fd < 0) {
		char *msg = "Error: Cannot create state file.\n";
		write(2, msg, my_strlen(msg));
		free_map(map, rows);
		return 1;
	}
	write_state_to_file(state_fd, map, rows, cols, 0);
	close(state_fd);

	/* show map */
	display_map(map, rows, cols);

	/* fork snake */
	pid_t snake_pid = fork();
	if (snake_pid < 0) {
		char *msg = "Error: fork() failed.\n";
		write(2, msg, my_strlen(msg));
		free_map(map, rows);
		return 1;
	}
	if (snake_pid == 0) {
		free_map(map, rows);
		snake_process(STATE_FILE, rows, cols);
	}

	/* fork wolf */
	pid_t wolf_pid = fork();
	if (wolf_pid < 0) {
		char *msg = "Error: fork() failed.\n";
		write(2, msg, my_strlen(msg));
		free_map(map, rows);
		wait(0);
		return 1;
	}
	if (wolf_pid == 0) {
		free_map(map, rows);
		wolf_process(STATE_FILE, rows, cols);
	}

	/* parent: player */
	struct termios saved;
	set_noncanonical(&saved);

	int state_fd_parent = open(STATE_FILE, O_RDWR);
	if (state_fd_parent < 0) {
		restore_terminal(&saved);
		free_map(map, rows);
		wait(0);
		wait(0);
		return 1;
	}

	int game_over = 0;
	int final_state = 0;

	while (!game_over) {
		char ch;
		int n = read(0, &ch, 1);

		flock(state_fd_parent, LOCK_EX);

		int cur_state;
		if (read_state_from_file(state_fd_parent, map, rows, cols, &cur_state) < 0) {
			flock(state_fd_parent, LOCK_UN);
			break;
		}

		if (cur_state != 0) {
			final_state = cur_state;
			game_over = 1;
			flock(state_fd_parent, LOCK_UN);
			break;
		}

		if (n <= 0) {
			flock(state_fd_parent, LOCK_UN);
			display_map(map, rows, cols);
			{
				struct timespec ts = { 0, 200000000 };
				nanosleep(&ts, 0);
			}
			continue;
		}

		int dr = 0, dc = 0;
		switch (ch) {
		case 'w': case 'W': dr = -1; break;
		case 'a': case 'A': dc = -1; break;
		case 's': case 'S': dr = 1;  break;
		case 'd': case 'D': dc = 1;  break;
		}

		if (dr != 0 || dc != 0) {
			int pr, pc;
			if (find_position(map, rows, cols, 2, &pr, &pc) == 0) {
				int nr = pr + dr;
				int nc = pc + dc;

				if (is_valid_move(map, rows, cols, nr, nc, 1)) {
					if (map[nr][nc] == 3) {
						map[pr][pc] = 0;
						map[nr][nc] = 2;
						final_state = 1;
						game_over = 1;
					} else {
						map[pr][pc] = 0;
						map[nr][nc] = 2;
					}
				}
			}
		}

		write_state_to_file(state_fd_parent, map, rows, cols, final_state);
		flock(state_fd_parent, LOCK_UN);

		if (!game_over) display_map(map, rows, cols);
	}

	restore_terminal(&saved);

	/* game over screen */
	if (game_over && final_state != 0)
		display_game_over(map, rows, cols, final_state);

	close(state_fd_parent);

	/* wait children */
	wait(0);
	wait(0);

	free_map(map, rows);
	return 0;
}
