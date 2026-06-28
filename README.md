# escape-the-flock

Escape the flock: a multi-process terminal game powered by raw system calls (fork, flock, nanosleep) and ANSI escape codes.

## Build

```sh
make
```

## Run

```sh
./escape <map_file>
```

Example:

```sh
./escape map.txt
```

## Map File Format

```
<game_state>
<rows> <cols>
<cell_data...>
```

| Line | Description |
|------|-------------|
| 1 | Game state (0 = ongoing, 1 = win, 2 = wolf kill, 3 = snake kill) |
| 2 | Playable area dimensions (max 20x20) |
| 3+ | Grid of integers separated by spaces |

### Cell Values

| Value | Object | Display |
|-------|--------|---------|
| 0 | Empty | space |
| 1 | Wall | space with white background |
| 2 | Player | `P` |
| 3 | Goal | `G` |
| 4 | Snake | `~` |
| 5 | Wolf | `W` |

## Controls

```
Press w to move UP
Press s to move DOWN
Press a to move LEFT
Press d to move RIGHT
```

Input is non-canonical; keys take effect immediately.

## Game Rules

- Reach the Goal `G` to win
- If the Snake `~` or Wolf `W` reaches the Player `P`, you lose
- Player cannot walk through Walls, map borders, or Enemies
- Enemies cannot walk through Walls, map borders, the Goal, or each other

### Enemy Behavior

| Enemy | Movement | Speed | Directions |
|-------|----------|-------|-------------|
| Snake | Random | 2 sec | 8 (all directions) |
| Wolf | Random | 1 sec | 4 (cardinal) |

- Direction is decided randomly using `rand()`
- When the Player is one move away, the Enemy attacks directly instead

## Architecture

Three processes communicate via a shared state file:

| Process | Role |
|---------|------|
| Parent | User input and Player movement, display |
| Child 1 | Snake movement (every 2s) |
| Child 2 | Wolf movement (every 1s) |

Synchronization via `flock()` (exclusive lock). Only one process accesses the state file at a time.

### System Calls Used

`open` `close` `read` `write` `fork` `wait` `flock` `nanosleep` `lseek` `ftruncate`

### Allowed Library Functions

`malloc` `free` `srand` `rand` `tcgetattr` `tcsetattr`

## Cleanup

```sh
make clean
```
