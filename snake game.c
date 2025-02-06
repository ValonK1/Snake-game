#define _XOPEN_SOURCE_EXTENDED // Must be first.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <locale.h>
#include <ncursesw/curses.h> // Should be last.

// Adam: Tick-based game (so trophies can be generated at time intervals).
#define TICKS_PER_SECOND 50
static const int USECS_PER_TICK = 1000000 / TICKS_PER_SECOND;
static const int TICKS_PER_MOVE_MAX = TICKS_PER_SECOND / 4;
static const int TICKS_PER_MOVE_MIN = 3;

// Adam: Constants for game state.
#define PLAYING 0
#define LOSS -1
#define WIN 1

// Adam: Data holder for screen coordinates.
struct Coord {
  int r;
  int c;
};

// Adam: Snake data tracking.
static int snake_len = 0;
static int snake_head_ptr = 0;
static int snake_win_len = 0;
static int snake_dir = KEY_RIGHT;
static int snake_prev_dir = KEY_RIGHT;
static struct Coord *snake_elements;

// Note: default color may be -1 on some systems. Ours is 0.
#define COLOR_DEFAULT 0
#define COLOR_SNAKE 1
#define COLOR_TROPHY 2

// Adam: Draw pit.
void draw_border();

// Adam: Set up a new snake.
void reset_snake();

// Adam: Main game loop.
void run_game();

// Adam: Length-based speed.
int get_ticks_per_move();

// Val: Generate a new trophy.
void generate_trophy(struct Coord *trophy, int *ticks_till_new_trophy);

// Val: Read user input.
int read_input();

// Val: Collision check and update next head.
int update_next_head(int input, struct Coord *next_head);

// Adam: Consume trophy and grow snake.
int award_trophy(struct Coord *next_head, struct Coord *trophy);

// Adam: Finalize move and draw snake with new head.
void draw_snake(struct Coord *head);

// Val: Print game finish status.
void print_finish(int end);

// Adam: Debug/extra feedback.
void feedback(char *content);

// Adam: Main method.
int main() {
  // UTF-8 character usage via http://dillingers.com/blog/2014/08/10/ncursesw-and-unicode/.
  // Set locale (so as to use UTF-8 characters).
  setlocale(LC_ALL, "en_US.UTF-8");

  // Set up screen.
  initscr();
  curs_set(FALSE);
  noecho();

  // Set up colors.
  use_default_colors();
  start_color();

  init_pair(COLOR_SNAKE, COLOR_BLACK, COLOR_GREEN);
  init_pair(COLOR_TROPHY, COLOR_BLACK, COLOR_YELLOW);

  // Val: Set up for game input.
  nodelay(stdscr, TRUE);
  keypad(stdscr, TRUE);

  run_game();

  // Val: Clean up for normal input post-game.
  nodelay(stdscr, FALSE);
  flushinp();

  // Pause to display screen at the end.
  int pressed;
  while ((pressed = getch()) == KEY_UP || pressed == KEY_DOWN || pressed == KEY_LEFT || pressed == KEY_RIGHT) {
    // Require a non-arrow key to quit (so you can't accidentally skip the end).
  }

  // Clean up after ourselves.
  free(snake_elements);
  endwin();

  return 0;
}

// Adam: Draw border around pit.
void draw_border() {
  // Draw border around snake pit.
  box(stdscr, 0, 0);

  // Add a label.
  move(0, 1);
  printw("Snake-2.0");
}

// Adam: Set up a new snake.
void reset_snake() {
  snake_len = 3;
  snake_head_ptr = -1; // First draw will increment.

  // Length of half of the perimeter means user wins the game.
  int new_win_len = LINES + COLS;

  // If win condition increased and snake elements were allocated, we need to re-allocate for more space.
  if (new_win_len > snake_win_len && snake_elements != NULL) {
    free(snake_elements);
    snake_elements = NULL;
  }

  snake_win_len = new_win_len;

  size_t elements_size = sizeof(struct Coord) * snake_win_len;

  // Allocate array for snake elements if necessary.
  if (snake_elements == NULL) {
    snake_elements = malloc(elements_size);
  }

  // Reset snake elements to 0.
  memset(snake_elements, 0, elements_size);

  // Initialize head in the middle of the field.
  struct Coord head;
  head.r = LINES / 2;
  head.c = COLS / 2;

  // Random starting direction.
  // Seed pseduorandom generator with current time.
  srand(time(NULL));
  // Use higher bits (higher bits supposedly have more even distribution).
  snake_dir = KEY_DOWN + ((rand() >> 12) & 0x3);
  snake_prev_dir = snake_dir;

  // Finalize and draw snake.
  draw_snake(&head);
  /*char len_info[8];
  sprintf(len_info, "win: %d", snake_win_len);
  feedback(len_info);*/
}

// Adam: Finalize move and draw snake with new head.
void draw_snake(struct Coord *head) {
  // Update head pointer.
  snake_head_ptr = (snake_head_ptr + 1) % snake_len;

  int discard_c = snake_elements[snake_head_ptr].c;
  int discard_r = snake_elements[snake_head_ptr].r;

  if (discard_c != 0 && discard_r != 0) {
    // Snake element has been drawn and must be erased.
    move(discard_r, discard_c);
    addch(' ');
  }

  snake_elements[snake_head_ptr] = *head;

  // Move cursor to head location.
  move(snake_elements[snake_head_ptr].r, snake_elements[snake_head_ptr].c);

  // Change to writing snake color.
  attrset(COLOR_PAIR(COLOR_SNAKE));

  // Write head.
  switch (snake_dir) {
    case KEY_UP:
      addwstr(L"\u2809"); // ⠉
      break;
    case KEY_DOWN:
      addwstr(L"\u28C0"); // ⣀
      break;
    case KEY_LEFT:
      addwstr(L"\u2806"); // ⠆
      break;
    case KEY_RIGHT:
      addwstr(L"\u2830"); // ⠰
      break;
    default:
      addch('@');
  }

  // If snake is larger than just a head, we can draw the tail and "neck."
  if (snake_len >= 2) {
    // "Neck" first because we want the tail to clobber it for length 2.
    int ptr = (snake_head_ptr - 1 + snake_len) % snake_len;

    if (snake_elements[ptr].r != 0 && snake_elements[ptr].c != 0) {
      move(snake_elements[ptr].r, snake_elements[ptr].c);

      if (snake_prev_dir == snake_dir) {
        if (snake_dir == KEY_UP || snake_dir == KEY_DOWN) {
          addwstr(L"\u2551"); // ║
        } else {
          addwstr(L"\u2550"); // ═
        }
      } else if (snake_prev_dir == KEY_RIGHT && snake_dir == KEY_UP
          || snake_prev_dir == KEY_DOWN && snake_dir == KEY_LEFT) {
        addwstr(L"\u255D"); // ╝
      } else if (snake_prev_dir == KEY_LEFT && snake_dir == KEY_UP
          || snake_prev_dir == KEY_DOWN && snake_dir == KEY_RIGHT) {
        addwstr(L"\u255A"); // ╚
      } else if (snake_prev_dir == KEY_RIGHT && snake_dir == KEY_DOWN
          || snake_prev_dir == KEY_UP && snake_dir == KEY_LEFT) {
        addwstr(L"\u2557"); // ╗
      } else /*if (snake_prev_dir == KEY_LEFT && snake_dir == KEY_DOWN
          || snake_prev_dir == KEY_UP && snake_dir == KEY_RIGHT)*/ { // Final case, fall through.
        addwstr(L"\u2554"); // ╔
      }
    }

    // Tail tip.
    ptr = (snake_head_ptr + 1) % snake_len;
    int prev_ptr = (snake_head_ptr + 2) % snake_len;
    struct Coord tail = snake_elements[ptr];
    struct Coord tail_prev = snake_elements[prev_ptr];
  
    if (tail.r != 0 && tail.c != 0
        && tail_prev.r != 0 && tail_prev.c != 0) {
      move(tail.r, tail.c);
      int dr = tail.r - tail_prev.r;
      if (dr > 0) {
        // Moving up
        addwstr(L"\u255C"); // ╜
      } else if (dr < 0) {
        // Moving down
        addwstr(L"\u2553"); // ╓
      } else if (tail.c - tail_prev.c > 0) {
        // Moving left
        addwstr(L"\u2555"); // ╕
      } else {
        // Moving right
        addwstr(L"\u2558"); // ╘
      }
    }
  }

  // Done drawing snake bits, reset color.
  attrset(COLOR_PAIR(COLOR_DEFAULT));

  // Refresh screen once drawn.
  refresh();
}

// Adam: Main game loop.
void run_game() {
  // Set up for a new round.
  clear();
  draw_border();
  reset_snake();

  // Put win condition on screen.
  char *winconf = "Win: %d/%d";
  char wincon[20];
  sprintf(wincon, winconf, snake_len, snake_win_len);
  feedback(wincon);

  int game_state = PLAYING;
  int input = snake_dir;
  struct Coord next_head;
  struct Coord trophy;
  int ticks_till_new_trophy = 0;

  int ticks_per_move = get_ticks_per_move();
  int ticks_since_move = ticks_per_move;
  while (1) {
    // Tick counters for movement and trophy generation.
    ++ticks_since_move;
    --ticks_till_new_trophy;

    // Tick snake if it is supposed to move.
    if (ticks_since_move >= ticks_per_move) {
      // Reset ticks since snake moved.
      ticks_since_move = 0;

      // Read user input.
      input = read_input();

      // Prepare to move snake.
      game_state = update_next_head(input, &next_head);

      // If game is over, don't wait until next tick.
      if (game_state != PLAYING) {
        break;
      }

      // If new head will consume trophy, award it.
      if (award_trophy(&next_head, &trophy)) {
        // Update speed for new length.
        ticks_per_move = get_ticks_per_move();
        // Prepare to draw new trophy next tick.
        ticks_till_new_trophy = -1;

        // Update win condition status.
        sprintf(wincon, winconf, snake_len, snake_win_len);
        feedback(wincon);
      }

      // Draw new head.
      draw_snake(&next_head);

      // Check for a win after head has moved so trophy isn't sitting there "unconsumed" on win.
      if (snake_len >= snake_win_len) {
        game_state = WIN;
        break;
      }
    }

    // Handle trophy generation.
    // Snake moves before trophy is regenerated so that ties
    // (snake tries to eat trophy the tick it expires)
    // go to the player, which feels less frustrating.
    if (ticks_till_new_trophy <= 0) {
      generate_trophy(&trophy, &ticks_till_new_trophy);
    }

    // Wait until next game tick.
    usleep(USECS_PER_TICK);
  }

  // Print win/loss state.
  print_finish(game_state);
}

// Adam: Length-based speed.
int get_ticks_per_move() {
  double delta = TICKS_PER_MOVE_MAX - TICKS_PER_MOVE_MIN + 1;
  double ratio = snake_len;
  ratio /= snake_win_len;

  return TICKS_PER_MOVE_MAX - (delta * ratio);
}

// Val: Generate a new trophy.
void generate_trophy(struct Coord *trophy, int *ticks_till_new_trophy) {
  // Magic value -1: Don't erase old trophy. Used for initial draw and when awarded.
  if (*ticks_till_new_trophy != -1) {
    // Erase old trophy.
    mvaddch(trophy->r, trophy->c, ' ');
  }

  // Find an unoccupied space for new trophy.
  while (1) {
    // Generate coordinates 1 inclusive to LINES|COLS - 1 exclusive.
    int r = rand() % (LINES - 2) + 1;
    int c = rand() % (COLS - 2) + 1;

    int no_collide = 1;

    // Check if location is in body.
    for (int i = 0; i < snake_len; ++i) {
      if (snake_elements[i].r == r && snake_elements[i].c == c) {
        no_collide = 0;
        break;
      }
    }

    if (no_collide) {
      trophy->r = r;
      trophy->c = c;
      break;
    }
  }

  // Generate value and draw trophy.
  int value = (rand() % 9) + 1;

  attrset(COLOR_PAIR(COLOR_TROPHY)); // Adam: Colors!
  mvaddch(trophy->r, trophy->c, '0' + value);
  attrset(COLOR_PAIR(COLOR_DEFAULT)); // Adam: Colors!

  refresh();

  // Set up expiration.
  // 1-9 seconds, so we want to generate a number between 0-8 seconds inclusive.
  int range = TICKS_PER_SECOND * 8 + 1;
  *ticks_till_new_trophy = TICKS_PER_SECOND + (rand() % range);
}

// Adam: Consume trophy and grow snake.
int award_trophy(struct Coord *head, struct Coord *trophy) {
    if (head->r != trophy->r || head->c != trophy->c) {
      return FALSE;
    }
    // Get value of trophy.
    cchar_t trophy_val;
    mvin_wch(trophy->r, trophy->c, &trophy_val);
    wchar_t trophy_char;
    attr_t attributes;
    short color;

    getcchar(&trophy_val, &trophy_char, &attributes, &color, 0);
    int value = trophy_char - '0';
    // Add length for trophy.
    snake_len += value;

    // Our array is only snake_win_len, don't exceed.
    if (snake_len > snake_win_len) {
      value = snake_win_len - snake_len + value;
      snake_len = snake_win_len;
    }

    // Move "gap" to the current end of our circular array.
    for (int index = snake_len - 1; index > snake_head_ptr + value; --index) {
      int prev = index - value;
      snake_elements[index].r = snake_elements[prev].r;
      snake_elements[index].c = snake_elements[prev].c;
      snake_elements[prev].r = 0;
      snake_elements[prev].c = 0;
    }

    return TRUE;
}

// Val: Read user input.
int read_input() {
  // Try to clear buffer to most recent input.
  int input = snake_dir;
  int temp;
  for (int i = 0; i < 10; ++i) {
    temp = getch();

    // If there's no more input to read, buffer is clear.
    if (temp == -1) {
      break;
    }

    // Otherwise use latest buffered input.
    input = temp;
  }

  // Flush input in case there were over 10 keys pressed.
  flushinp();

  return input;
}

// Val: Collision check and update next head.
int update_next_head(int input, struct Coord *next_head) {
  // Copy current head.
  *next_head = snake_elements[snake_head_ptr];

  // Update previous direction.
  snake_prev_dir = snake_dir;
  snake_dir = input;
  switch (input) {
    case KEY_UP:
    case KEY_DOWN:
    case KEY_LEFT:
    case KEY_RIGHT:
      break;
    // Cheat codes: win/lose. Capitals only, you have to mean it.
    case 'W':
      feedback("You cheated!");
      return WIN;
    case 'L':
      feedback("You cheated!");
      return LOSS;
    default:
      // Keep old snake direction.
      snake_dir = snake_prev_dir;
      break;
  }

  // Move new head and check for inverted movement direction.
  switch (snake_dir) {
    case KEY_UP:
      if (snake_prev_dir == KEY_DOWN) {
        feedback("You can't go backwards!");
        return LOSS;
      }
      next_head->r -= 1;
      break;
    case KEY_DOWN:
      if (snake_prev_dir == KEY_UP) {
        feedback("You can't go backwards!");
        return LOSS;
      }
      next_head->r += 1;
      break;
    case KEY_LEFT:
      if (snake_prev_dir == KEY_RIGHT) {
        feedback("You can't go backwards!");
        return LOSS;
      }
      next_head->c -= 1;
      break;
    case KEY_RIGHT:
      if (snake_prev_dir == KEY_LEFT) {
        feedback("You can't go backwards!");
        return LOSS;
      }
    default:
      next_head->c += 1;
      break;
  }

  // Collision checks for pit bounds:
  if (next_head->r <= 0 || next_head->r >= LINES - 1 || next_head->c <= 0 || next_head->c >= COLS - 1) {
    feedback("You ran into the edge of the pit!");
    return LOSS;
  }

  // Collision checks for snake elements:
  // Skip last element (head + 1) because it will vacate its spot as head moves.
  // Adam: Optimization: only need to check every other element.
  // If snake length is even, check odds. If snake length is odd, check evens.
  for (int i = 2 + ~(snake_len & 0x1); i < snake_len; i += 2) {
    int index = (snake_head_ptr + i) % snake_len;
    if (snake_elements[index].r == next_head->r && snake_elements[index].c == next_head->c) {
      feedback("You hit yourself!");
      return LOSS;
    }
  }

  return 0;
}

// Adam: Debug/extra feedback.
void feedback(char *content) {
  // Debug messages: center of bottom edge of pit
  int center_shift = (COLS / 2) - (strlen(content) / 2);
  move(LINES - 1, center_shift);
  addstr(content);
  refresh();
}

// Val: Print game finish status.
void print_finish(int state) {
  int center_r = LINES / 2;
  int center_c = COLS / 2;

  if (LINES < 6) {
    char *content;
    if (state == WIN) {
      content = "You win!";
    } else {
      content = "You lose.";
    }
    center_c -= 4;

    move(center_r, center_c);
    addstr(content);
    refresh();
    return;
  }

  // More than 6 lines tall, art!
  center_r -= 3;
  center_c -= 19;
  if (state == WIN) {
    /*
    __   __                     _       _ 
    \ \ / /                    (_)     | |
     \ V /___  _   _  __      ___ _ __ | |
      \ // _ \| | | | \ \ /\ / / | '_ \| |
      | | (_) | |_| |  \ V  V /| | | | |_|
      \_/\___/ \__,_|   \_/\_/ |_|_| |_(_)
    */
    mvaddstr(  center_r, center_c, "__   __                     _       _ ");
    mvaddstr(++center_r, center_c, "\\ \\ / /                    (_)     | |");
    mvaddstr(++center_r, center_c, " \\ V /___  _   _  __      ___ _ __ | |");
    mvaddstr(++center_r, center_c, "  \\ // _ \\| | | | \\ \\ /\\ / / | '_ \\| |");
    mvaddstr(++center_r, center_c, "  | | (_) | |_| |  \\ V  V /| | | | |_|");
    mvaddstr(++center_r, center_c, "  \\_/\\___/ \\__,_|   \\_/\\_/ |_|_| |_(_)");
  } else {
    /*
    __   __            _                  
    \ \ / /           | |                 
     \ V /___  _   _  | | ___  ___  ___   
      \ // _ \| | | | | |/ _ \/ __|/ _ \  
      | | (_) | |_| | | | (_) \__ \  __/_ 
      \_/\___/ \__,_| |_|\___/|___/\___(_)
    */
    mvaddstr(  center_r, center_c, "__   __            _");
    mvaddstr(++center_r, center_c, "\\ \\ / /           | |");
    mvaddstr(++center_r, center_c, " \\ V /___  _   _  | | ___  ___  ___");
    mvaddstr(++center_r, center_c, "  \\ // _ \\| | | | | |/ _ \\/ __|/ _ \\");
    mvaddstr(++center_r, center_c, "  | | (_) | |_| | | | (_) \\__ \\  __/_ ");
    mvaddstr(++center_r, center_c, "  \\_/\\___/ \\__,_| |_|\\___/|___/\\___(_)");
  }
  refresh();
}
