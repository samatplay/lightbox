/* FUTURE IDEAS:

All lit LEDs "fall" into a pile on one edge
All lit LEDs are sucked into a central point or axis
Scroll mode to/from an axis instead of an edge
Tracer mode with bands of LEDs instead of points
Tracers that enter from opposite directions and collide in an "explosion"
Tracers that enter from opposite directions and collide in a white point that bursts into several tracers
Matrix mode - continuous tracers from one direction that leave behind fixed pixels (digital rain)
Fireworks - field goes to black, tracers enter from edges and "explode" into colored pixels that fade out
Tron lightcycles

*/

/*
  CHANGELOG

  20141028
    Added wave and undulate modes
  20140912
    Changed timing system to use global timers for minor/major effects instead of separate timers for each effect with percent chances for each effect
    Added 2048 mode
  20140910
    Changed timings on blinkenlietz, starfield and Game Of Life
  20140710
    Added starfield and blinkenlietz
    Changed tracers to bounce off of lit LEDs when running along the edge of the field
    Tweaked start times of all modes
*/


/*
  TO FADE TO A NEW COLOR INCREMENTALLY:
    MULTIPLIER = (ELAPSED_TIME <= TOTAL_TIME) ? ((ELAPSED_TIME * 1.0) / TOTAL_TIME) : 1.0
    NEW_COLOR = START_COLOR + ((TARGET_COLOR - START_COLOR) * MULTIPLIER)
*/

/*
 * Wiring:
 *   DC negative = LED string BLUE + GND among digital pins + GND among power pins
 *   DC positive = LED string RED + Vin among power pins
 *   PIN 13 = LED string GREEN
 *   PIN 11 = LED string YELLOW
 */

#include <SPI.h>

#define LED_DDR  DDRB
#define LED_PORT PORTB
#define LED_PIN  _BV(PORTB5)

#define MINVAL(X,Y)             (((X) < (Y)) ? (X) : (Y))
#define MAXVAL(X,Y)             (((X) > (Y)) ? (X) : (Y))
#define CLEAR_BIT(VAR,I)        VAR[(I) / 8] ^= (1 << ((I) % 8))
#define SET_BIT(VAR,I)          VAR[(I) / 8] |= (1 << ((I) % 8))
#define IF_BIT(VAR,I)           (VAR[(I) / 8] & (1 << ((I) % 8)))

#define CHOOSE3(A,B,C)          ((random(9) < 3) ? (A) : ((random(10) < 5) ? (B) : (C)))
#define IS_BLACK(LED)           (((LED)[0] == 0) && ((LED)[1] == 0) && ((LED)[2] == 0))
#define COPY_COLOR(DEST,SRC)    ({ DEST[0] = SRC[0]; DEST[1] = SRC[1]; DEST[2] = SRC[2]; })
#define SET_COLOR(DEST,R,G,B)   ({ DEST[0] = R; DEST[1] = G; DEST[2] = B; })
#define SET_BLACK(DEST)         SET_COLOR(DEST, 0, 0, 0)
#define FADE_RGB(DEST,START_R,START_G,START_B,END_R,END_G,END_B,TOTAL_TIME,ELAPSED) \
                                ({ float tmp_fade_mult = ((ELAPSED) > 0) ? (((ELAPSED) <= (TOTAL_TIME)) ? (((ELAPSED) * 1.0) / (TOTAL_TIME)) : 1.0) : 0.0; \
                                   (DEST)[0] = (START_R) + (((END_R) - (START_R)) * tmp_fade_mult); \
                                   (DEST)[1] = (START_G) + (((END_G) - (START_G)) * tmp_fade_mult); \
                                   (DEST)[2] = (START_B) + (((END_B) - (START_B)) * tmp_fade_mult); })
#define FADE_UP(DEST,END_COLOR,TOTAL_TIME,ELAPSED) \
                                FADE_RGB((DEST), 0, 0, 0, (END_COLOR)[0], (END_COLOR)[1], (END_COLOR)[2], (TOTAL_TIME), (ELAPSED))
#define FADE_BLACK(DEST,START_COLOR,TOTAL_TIME,ELAPSED) \
                                FADE_RGB((DEST), (START_COLOR)[0], (START_COLOR)[1], (START_COLOR)[2], 0, 0, 0, (TOTAL_TIME), (ELAPSED))
#define FADE_COLOR(DEST,START_COLOR,END_COLOR,TOTAL_TIME,ELAPSED) \
                                FADE_RGB((DEST), (START_COLOR)[0], (START_COLOR)[1], (START_COLOR)[2], (END_COLOR)[0], (END_COLOR)[1], (END_COLOR)[2], (TOTAL_TIME), (ELAPSED))

#define NUM_LED                 25
#define INVALID_LED             (NUM_LED + 1)
#define LEDS_PER_COL_TALL       ((NUM_ROWS + 1) / 2)
#define LEDS_PER_COL_SHORT      (LEDS_PER_COL_TALL - 1)
#define NUM_NEIGHBORS           8
#define INVALID_DIRECTION       (NUM_NEIGHBORS + 1)

#if (NUM_LED == 98)

#define NUM_COLS                13
#define NUM_ROWS                15

#elif (NUM_LED == 25)

#define NUM_COLS                7
#define NUM_ROWS                7

#endif

#define OPPOSITE_DIRECTION(X)   ((X >= (NUM_NEIGHBORS / 2)) ? (X - (NUM_NEIGHBORS / 2)) : (X + (NUM_NEIGHBORS / 2)))

// Deliberate pause at the end of each loop() function.  Most
// of the modes move so slowly there's no reason to run the
// CPU at max speed.  This also determines the max framerate.
#define LOOP_DELAY_MSECS        50

/*
 * The following modes may be used within the mode[] array, and always within the
 * mode_field or next_time[] variables
 */
// Simulates each LED blinking on or off randomly like a box
// full of old fashioned incandescent Christmas tree "blinker"
// bulbs.
#define MODE_XMAS               0
// Simulates Conway's "Game of Life". Not a strict implementation, but pretty.
#define MODE_LIFE               1
// Rapidly scrolls the entire field
#define MODE_SCROLL             2
// Sends a moving dot across the field with a fading tail
#define MODE_TRACER             3
// Shows a solid field of color with white stars that blink
#define MODE_STARFIELD          4
// Shows a black field with colored lietz that blink
#define MODE_BLINKENLIETZ       5
// Plays a game of 2048 (look it up)
#define MODE_TWENTY48           6
// One or more waves of color spread out across the field
#define MODE_WAVE               7
// One or more waves of undulating color spread out across the field
#define MODE_UNDULATE           8
// The total number of modes listed above
#define NUM_MODE                9

/*
 * The following percent chances control whether an effect will start when it's
 * time for a major effect.  The total of all the percents should add up to 100.
 */
// Game of Life
#define LIFE_CHANCE_PERCENT             8
// Scroll
#define SCROLL_CHANCE_PERCENT           15
// Starfield
#define STARFIELD_CHANCE_PERCENT        10
// Blinkenlietz
#define BLINKENLIETZ_CHANCE_PERCENT     25
// Game of 2048
#define TWENTY48_CHANCE_PERCENT         7
// Wave of colors
#define WAVE_CHANCE_PERCENT             20
// Undulating field
#define UNDULATE_CHANCE_PERCENT         15


/*
 * The following values are used in the next_time[] array.
 */
// Minor effects are relatively small and quick, should only take a few seconds at most
#define TIME_MINOR              0
// Major effects clear the entire field and may run for a number of minutes
#define TIME_MAJOR              1
// The total number of times listed above
#define NUM_TIMES               2

// The range of time to wait before starting a minor effect
#define TIME_MINOR_MIN_MSECS    2000
#define TIME_MINOR_MAX_MSECS    20000
// The range of time to wait before starting a major effect
#define TIME_MAJOR_MIN_MSECS    90000
#define TIME_MAJOR_MAX_MSECS    450000

/********************************************************************************
 ********************************************************************************
 ********************************************************************************
 *** FLICKER CONSTANTS
 ********************************************************************************
 ********************************************************************************
 ********************************************************************************/
// The delay between on/off states when an LED flickers
#define FLICKER_DELAY_MSECS     10
// Minimum number of times an LED flickers
#define FLICKER_MIN             3
// Maximum number of times an LED flickers
#define FLICKER_MAX             5

/********************************************************************************
 ********************************************************************************
 ********************************************************************************
 *** FADEMOVE CONSTANTS
 ********************************************************************************
 ********************************************************************************
 ********************************************************************************/
// The number of LEDs to fade from one location to another
#define FADEMOVE_MIN                    1
#define FADEMOVE_MAX                    5
// The range of time for an LED to turn on by fading from another location
#define FADEMOVE_MIN_MSECS              400
#define FADEMOVE_MAX_MSECS              1000

/********************************************************************************
 ********************************************************************************
 ********************************************************************************
 *** FADESAME CONSTANTS
 ********************************************************************************
 ********************************************************************************
 ********************************************************************************/
// The range of time for an LED to fade to a new color instead of turning on or off
#define FADESAME_MIN_MSECS              400
#define FADESAME_MAX_MSECS              1000


/********************************************************************************
 ********************************************************************************
 ********************************************************************************
 *** XMAS BULB CONSTANTS
 ********************************************************************************
 ********************************************************************************
 ********************************************************************************/
// When blinking like bulbs, this is the maximum number of
// lit LEDs.
#define XMAS_ON_MAX             ((byte)(NUM_LED / 3))
// Chance of an LED flickering on instead of just coming on solid
#define XMAS_FLICKER_ON_PERCENT 15
// Chance of an LED flickering off instead of just going black
#define XMAS_FLICKER_OFF_PERCENT        15

// Chance an LED will move or change instead of turning on or off
#define XMAS_CHANGE_PERCENT     20
// Chance an LED will move from an existing location by fading instead of turning on
#define XMAS_FADEMOVE_PERCENT   60
// Chance an LED will fade to a new color instead of turning off
#define XMAS_FADESAME_PERCENT   40


/********************************************************************************
 ********************************************************************************
 ********************************************************************************
 *** GAME OF LIFE CONSTANTS
 ********************************************************************************
 ********************************************************************************
 ********************************************************************************/
// Do not start a Game unless this number of LEDs are lit
#define LIFE_LED_MIN                    ((byte)(NUM_LED / 6.5))
// Postpone starting a major effect by a number in this range if a Game can't start when scheduled
#define LIFE_TIME_POSTPONE_MIN_MSECS    60000
#define LIFE_TIME_POSTPONE_MAX_MSECS    90000
// Once a Game has started, delay ending the Game by a number in this range
#define LIFE_TIME_END_MIN_MSECS         90000
#define LIFE_TIME_END_MAX_MSECS         120000
// Time between generations
#define LIFE_GENERATION_MSECS           3000
// Time a cell will live even when starved or overcrowded
#define LIFE_LIFESPAN                   3
// Living LEDs die if they have a number of neighbors in this range
#define LIFE_STARVATION_MIN             0
#define LIFE_STARVATION_MAX             1
// Dead LEDs are born if they have a number of neighbors in this range
#define LIFE_BIRTH_MIN                  2
#define LIFE_BIRTH_MAX                  2
// LEDs have this percent chance of being born, if they fall within the range above
#define LIFE_BIRTH_CHANCE               25
// Living LEDs die if they have a number of neighbors in this range
#define LIFE_OVERCROWDING_MIN           3
#define LIFE_OVERCROWDING_MAX           8
/*
 * The following submodes are only used within the mode[] array, never for the
 * mode_field or next_time[] variables
 */
// Modes for each LED during the Game.  The ALIVE states must be sequential
#define LIFE_LEDMODE_BIRTHING   10
#define LIFE_LEDMODE_ALIVE      11
#define LIFE_LEDMODE_DYING      (LIFE_LEDMODE_ALIVE + LIFE_LIFESPAN)
#define LIFE_LEDMODE_DEAD       (LIFE_LEDMODE_ALIVE + LIFE_LIFESPAN + 1)


/********************************************************************************
 ********************************************************************************
 ********************************************************************************
 *** SCROLL CONSTANTS
 ********************************************************************************
 ********************************************************************************
 ********************************************************************************/
// Delay starting a scroll by a number in this range
#define SCROLL_TIME_MIN_MSECS           200000
#define SCROLL_TIME_MAX_MSECS           400000
// Scroll the LEDs in a number of directions in this range
#define SCROLL_DIRECTION_MIN            1
#define SCROLL_DIRECTION_MAX            3
// Scroll the LEDs by a distance in this range per direction
#define SCROLL_DISTANCE_MIN             5
#define SCROLL_DISTANCE_MAX             20
// Delay each incremental movement during a scroll by this amount of time
#define SCROLL_MOVEMENT_DELAY_MSECS     20


/********************************************************************************
 ********************************************************************************
 ********************************************************************************
 *** TRACER CONSTANTS
 ********************************************************************************
 ********************************************************************************
 ********************************************************************************/
// Percent chance a tracer will start when it's time for a minor effect
#define TRACER_CHANCE_PERCENT           7
// Do not start more than this number of tracers simultaneously
#define TRACER_MAX_ACTIVE               3
#define INVALID_TRACER                  (TRACER_MAX_ACTIVE + 1)
// Each tracer may "bounce" off the edges a number of times in this range
// Must be a difference of no more than 15
#define TRACER_BOUNCE_MIN               2
#define TRACER_BOUNCE_MAX               5
// A tracer moving along an edge has this percent chance of randomly changing to
// a new direction when it hits a lit LED instead of going over it.  Changing
// direction this way does not count against its max number of bounces.
#define TRACER_REFLECT_PERCENT          34
// though the edge had a "rough spot" that caused a reflection)
// Each tracer's tail extends a number of LEDs in this range
// Must be a difference of no more than 15
#define TRACER_LENGTH_MIN               4
#define TRACER_LENGTH_MAX               8
// Time each LED takes to brighten; this also determines the tracer's speed
// Max must be small enough to store in a byte
#define TRACER_BRIGHTEN_MSECS_MIN       200
#define TRACER_BRIGHTEN_MSECS_MAX       1000


/********************************************************************************
 ********************************************************************************
 ********************************************************************************
 *** STARFIELD CONSTANTS
 ********************************************************************************
 ********************************************************************************
 ********************************************************************************/
// Once a field has started, delay ending by a number in this range
#define STARFIELD_TIME_END_MIN_MSECS    90000
#define STARFIELD_TIME_END_MAX_MSECS    120000
// The maximum brightness of any RGB value in the field's background color
#define STARFIELD_COLOR_MAX             2
// The amount of time to fade from the start state to the field background color
#define STARFIELD_TIME_FIELD_FADE_MSECS 3000
// Delay starting a new star for this range of time
#define STARFIELD_TIME_STAR_MIN_MSECS   200
#define STARFIELD_TIME_STAR_MAX_MSECS   1000
// The amount of time a star needs to reach max brightness
#define STARFIELD_TIME_BRIGHT_MIN_MSECS 100
#define STARFIELD_TIME_BRIGHT_MAX_MSECS 600
// The amount of time a star needs to fade to the background
#define STARFIELD_TIME_DIM_MIN_MSECS    700
#define STARFIELD_TIME_DIM_MAX_MSECS    2000
// The number of LEDs to turn on after the starfield ends
#define STARFIELD_END_LED_MAX   ((byte)(NUM_LED / 7))
// When the field ends, some LEDs will brighten by this RGB amount
#define STARFIELD_END_RGB_MIN           96
#define STARFIELD_END_RGB_MAX           255


/********************************************************************************
 ********************************************************************************
 ********************************************************************************
 *** BLINKENLIETZ CONSTANTS
 ********************************************************************************
 ********************************************************************************
 ********************************************************************************/
// Once a field has started, delay ending by a number in this range
#define BLINKENLIETZ_TIME_END_MIN_MSECS    90000
#define BLINKENLIETZ_TIME_END_MAX_MSECS    120000
// The amount of time to fade from the start state to the field background color
#define BLINKENLIETZ_TIME_FIELD_FADE_MSECS 3000
// Delay starting a new star for this range of time
#define BLINKENLIETZ_TIME_STAR_MIN_MSECS   200
#define BLINKENLIETZ_TIME_STAR_MAX_MSECS   1000
// The amount of time a star needs to reach max brightness
#define BLINKENLIETZ_TIME_BRIGHT_MIN_MSECS 100
#define BLINKENLIETZ_TIME_BRIGHT_MAX_MSECS 600
// The amount of time a star needs to fade to the background
#define BLINKENLIETZ_TIME_DIM_MIN_MSECS    700
#define BLINKENLIETZ_TIME_DIM_MAX_MSECS    2000


/********************************************************************************
 ********************************************************************************
 ********************************************************************************
 *** STARFIELD AND BLINKENLIETZ CONSTANTS
 ********************************************************************************
 ********************************************************************************
 ********************************************************************************/
// The maximum number of active stars
#define STARLIETZ_MAX_STARS             ((byte)(NUM_LED / 10))


/********************************************************************************
 ********************************************************************************
 ********************************************************************************
 *** 2048 CONSTANTS
 ********************************************************************************
 ********************************************************************************
 ********************************************************************************/
// Once a game has started, delay ending by a number in this range
#define TWENTY48_TIME_END_MIN_MSECS             90000
#define TWENTY48_TIME_END_MAX_MSECS             120000
// The amount of time the field takes to fade to black when the game starts
#define TWENTY48_TIME_FIELD_FADE_MSECS          3000
// Start a game with this many lit LEDs
#define TWENTY48_START_LEDS                     ((NUM_LED == 25) ? 2 : 4)
// Add this many LEDs per turn
#define TWENTY48_TURN_LEDS                      ((NUM_LED == 25) ? 1 : 2)
// Time to wait between moves
#define TWENTY48_TIME_MOVE_MSECS                800
// Time to wait between sliding moves
#define TWENTY48_TIME_STEP_MSECS                100
// The current state of the game
#define TWENTY48_MODE_IDLE                      0
#define TWENTY48_MODE_MOVING                    1
// The maximum number of color values a tile can advance through
#define TWENTY48_MAX_COLORS                     12
// The percent chance a new tile will have a value of 2 instead of 1
#define TWENTY48_NEW_TILE_DOUBLE_PERCENT        25


/********************************************************************************
 ********************************************************************************
 ********************************************************************************
 *** WAVE CONSTANTS
 ********************************************************************************
 ********************************************************************************
 ********************************************************************************/
// The number of colors in each color wave
#define WAVE_MIN_COLORS         1
#define WAVE_MAX_COLORS         7
// Invalid number for a wave
#define WAVE_INVALID            (WAVE_MAX_COLORS + 2)
// The range of time each LED needs to brighten to full intensity
#define WAVE_MIN_BRIGHT_MSECS   750
#define WAVE_MAX_BRIGHT_MSECS   2000
// The range of time each LED should hold at full intensity before fading
#define WAVE_MIN_HOLD_MSECS     500
#define WAVE_MAX_HOLD_MSECS     5000
// The range of time to delay propagation of color from a full-bright LED to a neighbor
#define WAVE_MIN_DELAY_MSECS    0
#define WAVE_MAX_DELAY_MSECS    2000
// Chance one of the wave colors will be black
#define WAVE_BLACK_PERCENT      50


/********************************************************************************
 ********************************************************************************
 ********************************************************************************
 *** WAVE CONSTANTS
 ********************************************************************************
 ********************************************************************************
 ********************************************************************************/
// The number of colors in undulating wave
#define UNDULATE_MIN_COLORS     1
#define UNDULATE_MAX_COLORS     4
// Invalid number for a color
#define UNDULATE_INVALID_COLOR  (UNDULATE_MAX_COLORS + 2)
// The range of time each LED takes to brighten to full intensity - constant for all colors
#define UNDULATE_MIN_UP_MSECS   1000
#define UNDULATE_MAX_UP_MSECS   4000
// The range of time each LED takes to fade to black - constant for all colors
#define UNDULATE_MIN_DOWN_MSECS 1000
#define UNDULATE_MAX_DOWN_MSECS 4000
// The range of time to delay spreading the initial color from a full-bright LED to a neighbor
#define UNDULATE_MIN_DELAY_MSECS        500
#define UNDULATE_MAX_DELAY_MSECS        3000
// The range of up/down blinks for each LED - constant for all colors
#define UNDULATE_MIN_UPDOWN     5
#define UNDULATE_MAX_UPDOWN     10
// Additional delay to apply to each LED each time it becomes black - hides the edge of the wave after the entire field is undulating
#define UNDULATE_MIN_BLACK_DELAY_MSECS  500
#define UNDULATE_MAX_BLACK_DELAY_MSECS  1500


/*
 * GLOBAL VARIABLES
 *
 * The Arduino Uno only has 2K for all of these and the stack.
 * GCC will not catch overflows, it will compile them and the
 * Arduino will crash and reboot when any accesses are made
 * beyond 2K.  When that happens, the field will abruptly
 * reset into a starting (random) pattern.  If it's crashing
 * on init, the field will rapidly flicker through random
 * patterns.
 */
// The current color of each LED
byte color[NUM_LED][3];
// Stores the current state of the entire field
byte mode_field;
// Stores the current state of each LED
byte mode[NUM_LED];
// Stores the last choices made by select_color()
byte last_color_choice;
// Stores the time of the last loop()
unsigned long last_time;
// Stores the time the next action should start
unsigned long next_time[NUM_TIMES];

// Per-mode variables, stored in a union to save space.
// Each mode must ensure it does not use this union while
// another mode is active.
union
  {

  // MODE_XMAS
  struct
    {
    byte saved_color[3];
    } xmas;

  // MODE_LIFE
  struct
    {
    // Saved color of each LED
    byte saved_color[NUM_LED][3];
    // Color of each generation
    byte color[LIFE_LIFESPAN + 1][3];
    // Time to start next generation
    unsigned long next_time;
    } life;

  // MODE_TRACER
  struct
    {
    // Number of bounces per tracer - low nibble is number of bounces minus TRACER_BOUNCE_MIN, high nibble is tail length minus TRACER_LENGTH_MIN
    byte tail_bounces[TRACER_MAX_ACTIVE];
    // Color of each tracer
    byte color[TRACER_MAX_ACTIVE][3];
    // Time to brighten each leading LED - also determines speed of movement
    byte bright_msecs[TRACER_MAX_ACTIVE];
    // Direction and bounce count of each tracer - LSN is direction, MSN is bounce count
    byte count_direction[TRACER_MAX_ACTIVE];
    // LEDs in each tracer; element [i][0] is always the leading LED
    // INVALID_LED = LED is off the edge of the field, later LEDs may still be darkening
    byte location[TRACER_MAX_ACTIVE][TRACER_LENGTH_MAX + 1];
    // Time the most recent LED began brightening
    // 0 = tracer is not active
    unsigned long start_time[TRACER_MAX_ACTIVE];
    // Original color of the brightening LED
    byte start_color[TRACER_MAX_ACTIVE][3];
    } tracer;

  // MODE_SCROLL
  struct
    {
    // Saved color of each LED
    byte saved_color[NUM_LED][3];
    } scroll;

  // MODE_STARFIELD and MODE_BLINKENLIETZ
  struct
    {
    // Base color of the field
    byte base_color[3];
    // Saved color of each LED
    byte saved_color[NUM_LED][3];
    // Next time a star will begin
    unsigned long next_time;
    // Time the entire field will end
    unsigned long end_time;
    // The location of each active star
    byte location[STARLIETZ_MAX_STARS];
    // The total number of active stars
    byte num_active;
    // The time each active star began brightening or dimming
    unsigned long start_time[STARLIETZ_MAX_STARS];
    // The amount of time each star will spend brightening or dimming
    unsigned long change_time[STARLIETZ_MAX_STARS];
    // Whether each star is brightening or dimming
    boolean updown[STARLIETZ_MAX_STARS];
    // Color of each star
    byte color[STARLIETZ_MAX_STARS][3];
    } starlietz;

  // MODE_TWENTY48
  struct
    {
    // State of play
    byte mode;
    // Direction of motion
    byte direction;
    // Saved color of each LED
    byte saved_color[NUM_LED][3];
    // Time game will end
    unsigned long end_time;
    // Time of next move
    unsigned long next_time;
    // Value of each LED
    byte value[NUM_LED];
    // Color of each numeric value
    byte color[TWENTY48_MAX_COLORS][3];
    // If each LED has already been combined with another in this move
    boolean combined[NUM_LED];
    } twenty48;

  // MODE_WAVE
  struct
    {
    // Time needed to brighten an LED to full intensity (per color)
    unsigned long bright_time;
    // Time to hold an LED at full intensity (per color)
    unsigned long wave_time;
    // Number of waves of color
    byte num_colors;
    // Color of each LED
    byte color_num[NUM_LED];
    // Color of each wave
    byte colors[WAVE_MAX_COLORS + 1][3];
    // Original color of each LED
    byte saved_color[NUM_LED][3];
    // Time each LED began brightening (per color)
    unsigned long start_time[NUM_LED];
    } wave;

  // MODE_UNDULATE
  struct
    {
    // Number of waves of color
    byte num_colors;
    // The color each LED is current using
    byte color_num[NUM_LED];
    // The color of each wave
    byte color[UNDULATE_MAX_COLORS][3];
    // The original color of each LED before the waves start
    byte saved_color[NUM_LED][3];
    // The number of blinks each LED has made within the current color
    byte updown[NUM_LED];
    // The number of blinks each LED should make before moving to the next color
    byte num_updown;
    // The time each LED began its brightening or fading, future times indicate a pause
    unsigned long start_time[NUM_LED];
    // The amount of time each LED needs to brighten to full intensity
    unsigned long up_time;
    // The amount of time each LED needs to fade to black
    unsigned long down_time;
    } undulate;

  } permode;


/*
 * Redraws the entire field
 */
void redraw()
  {
  word i;

  // Completely redraws the field by passing each LED's color in sequence
  for (i = 0; i < (NUM_LED * 3); i++)
    for (SPDR = color[i / 3][i % 3]; !(SPSR & _BV(SPIF)); );

  delay(1);
  }

/*
 * This function returns the indexes of each LED's neighboring LEDs, based on NUM_COLS and NUM_ROWS.
 * Given a layout like this one, NUM_LED is 98, NUM_COLS is 13 and NUM_ROWS is 15.
        0               15              30              45              60              75              90

                14              29              44              59              74              89

        1               16              31              46              61              76              91

                13              28              43              58              73              88

        2               17              32              47              62              77              92

                12              27              42              57              72              87

        3               18              33              48              63              78              93

                11              26              41              56              71              86

        4               19              34              49              64              79              94

                10              25              40              55              70              85

        5               20              35              50              65              80              95

                9               24              39              54              69              84

        6               21              36              51              66              81              96

                8               23              38              53              68              83

        7               22              37              52              67              82              97
 *
 * Given a layout like this one, NUM_LED is 25, NUM_COLS is 7 and NUM_ROWS is 7
        0               7               14              21

                6               13              20

        1               8               15              22

                5               12              19

        2               9               16              23

                4               11              18

        3               10              17              24
 *
 * Neighbor indexes and direction are given in clockwise order, starting at the top:
                0
             7     1
          6           2
             5     3
                4
 * INVALID_LED is invalid (edge of field).
 */
byte get_row(byte index)
  {
  return(((index % NUM_ROWS) < LEDS_PER_COL_TALL) ? ((index % NUM_ROWS) * 2) : (NUM_ROWS - ((((index % NUM_ROWS) * 2) + 1) - NUM_ROWS)));
  }
byte get_col(byte index)
  {
  return(((index % NUM_ROWS) < LEDS_PER_COL_TALL) ? ((index / NUM_ROWS) * 2) : (((index / NUM_ROWS) * 2) + 1));
  }
byte get_neighbor(byte index, byte direction)
  {
  byte return_value;
  byte tmp_row;
  byte tmp_col;

  return_value = INVALID_LED;

  tmp_col = get_col(index);
  tmp_row = get_row(index);

  switch (direction)
    {
    case 0:
      if (tmp_row > 1)
        return_value = index + (((index % NUM_ROWS) < LEDS_PER_COL_TALL) ? -1 : 1);

      break;
    case 1:
      if ((tmp_row > 0) &&
          (tmp_col < (NUM_COLS - 1)))
        return_value = index + (((index % NUM_ROWS) < LEDS_PER_COL_TALL) ? (NUM_ROWS - tmp_row) : tmp_row);

      break;
    case 2:
      if (tmp_col < (NUM_COLS - 2))
        return_value = index + NUM_ROWS;

      break;
    case 3:
      if ((tmp_row < (NUM_ROWS - 1)) &&
          (tmp_col < (NUM_COLS - 1)))
        return_value = index + (((index % NUM_ROWS) < LEDS_PER_COL_TALL) ? ((NUM_ROWS - tmp_row) - 1) : (tmp_row + 1));

      break;
    case 4:
      if (tmp_row < (NUM_ROWS - 2))
        return_value = index + (((index % NUM_ROWS) < LEDS_PER_COL_TALL) ? 1 : -1);

      break;
    case 5:
      if ((tmp_row < (NUM_ROWS - 1)) &&
          (tmp_col > 0))
        return_value = index - (((index % NUM_ROWS) < LEDS_PER_COL_TALL) ? (tmp_row + 1) : ((NUM_ROWS - tmp_row) - 1));

      break;
    case 6:
      if (tmp_col > 1)
        return_value = index - NUM_ROWS;

      break;
    case 7:
      if ((tmp_row > 0) &&
          (tmp_col > 0))
        return_value = index - (((index % NUM_ROWS) < LEDS_PER_COL_TALL) ? tmp_row : (NUM_ROWS - tmp_row));

      break;
    }

  return(return_value);
  }

boolean is_edge(byte index)
  {
  boolean return_value;
  byte i;

  return_value = 0;

  if ((get_neighbor(index, 1) == INVALID_LED) ||
      (get_neighbor(index, 3) == INVALID_LED) ||
      (get_neighbor(index, 5) == INVALID_LED) ||
      (get_neighbor(index, 7) == INVALID_LED))
    return_value = 1;

  return(return_value);
  }

void select_color(byte *target_color)
  {
  byte tmp_color[3];

  tmp_color[0] = color[0][0];
  tmp_color[1] = color[0][1];
  tmp_color[2] = color[0][2];
  select_color((byte)0);
  target_color[0] = color[0][0];
  target_color[1] = color[0][1];
  target_color[2] = color[0][2];
  color[0][0] = tmp_color[0];
  color[0][1] = tmp_color[1];
  color[0][2] = tmp_color[2];

  return;
  }

void select_color(byte index)
  {
  byte i;
  byte tmp_num;
  byte choice;

  // The Arduino's PRNG isn't very good and without
  // these checks it has a bad habit of picking the
  // same colors many times in a row -- not very
  // interesting.
  i = 0;
  do
    {
    switch (random(9))
      {
      case 0:
      case 1:
      case 2:
        tmp_num = 0;
        break;
      case 3:
      case 4:
      case 5:
        tmp_num = 1;
        break;
      case 6:
      case 7:
        tmp_num = 2;
        break;
      case 8:
        tmp_num = 3;
        break;
      }

    i++;
    }
  while ((((last_color_choice & 0x30) >> 4) == tmp_num) &&
         (i < 255));
  choice = tmp_num << 4;

  i = 0;
  do
    {
    tmp_num = random(3);
    i++;
    }
  while ((((last_color_choice & 0x0C) >> 2) == tmp_num) &&
         (i < 255));
  choice |= (tmp_num & 0x03) << 2;

  i = 0;
  do
    {
    tmp_num = random(3);
    i++;
    }
  while ((((last_color_choice & 0x03) == tmp_num) ||
          (((choice & 0x0C) >> 2) == tmp_num)) &&
         (i < 255));
  choice |= (tmp_num & 0x03);

  // This function weights the colors towards choosing a bright component with
  // dim secondary components so the results are more likely to be distinguishable
  // colors.  Choosing totally randomly tends to constantly produce results that are
  // bright shades of pastels, difficult to tell apart and not very pretty.
  i = 0;
  do
    {
    switch ((choice & 0x30) >> 4)
      {
      case 0:
        // choose a primary color at one of three brightnesses
        color[index][0] = (((choice & 0x0C) >> 2) == 0) ? CHOOSE3(64, 128, 255) : 0;
        color[index][1] = (((choice & 0x0C) >> 2) == 1) ? CHOOSE3(64, 128, 255) : 0;
        color[index][2] = (((choice & 0x0C) >> 2) == 2) ? CHOOSE3(64, 128, 255) : 0;

        break;
      case 1:
        // choose a secondary color at one of three brightnesses
        tmp_num = CHOOSE3(64, 128, 255);
        color[index][0] = ((((choice & 0x0C) >> 2) == 0) || ((choice & 0x03) == 0)) ? tmp_num : 0;
        color[index][1] = ((((choice & 0x0C) >> 2) == 1) || ((choice & 0x03) == 1)) ? tmp_num : 0;
        color[index][2] = ((((choice & 0x0C) >> 2) == 2) || ((choice & 0x03) == 2)) ? tmp_num : 0;

        break;
      case 2:
        // choose a color with two components at one of three brightnesses
        // and the third rather dim
        tmp_num = CHOOSE3(64, 128, 255);
        color[index][0] = ((((choice & 0x0C) >> 2) == 0) || ((choice & 0x03) == 0)) ? tmp_num : random(64);
        color[index][1] = ((((choice & 0x0C) >> 2) == 1) || ((choice & 0x03) == 1)) ? tmp_num : random(64);
        color[index][2] = ((((choice & 0x0C) >> 2) == 2) || ((choice & 0x03) == 2)) ? tmp_num : random(64);

        break;
      case 3:
        // choose a color with a primary component at one of two brightnesses
        // and the other two components dimmer
        color[index][0] = (((choice & 0x0C) >> 2) == 0) ? CHOOSE3(128, 255, 255) : random(64);
        color[index][1] = (((choice & 0x0C) >> 2) == 1) ? CHOOSE3(128, 255, 255) : random(64);
        color[index][2] = (((choice & 0x0C) >> 2) == 2) ? CHOOSE3(128, 255, 255) : random(64);

        break;
      }

    i++;
    }
  while (((MAXVAL(MAXVAL(color[index][0], color[index][1]), color[index][2]) - MINVAL(MINVAL(color[index][0], color[index][1]), color[index][2])) < 127) &&
         (i < 255));

  // The LEDs don't do "light yellow" well -- it just comes out as white,
  // which isn't very pretty.  This forces a white color back into yellow
  // territory.
  if ((MAXVAL(color[index][0], color[index][1]) > 127) &&
      (abs(color[index][0] - color[index][1]) < 96))
    color[index][2] = 0;

  last_color_choice = choice;

  return;
  }

/*
 * This function sets everything up from scratch.  This function is obviously
 * called from setup() but may also be called from loop() if the time value
 * returned by millis() has overflowed (happens every 57 days or so).
 */
void reinit()
  {
  byte i;

  for (i = 0; i < NUM_LED; i++)
    {
    color[i][0] = 0;
    color[i][1] = 0;
    color[i][2] = 0;

    mode[i] = MODE_XMAS;
    }

  mode_field = MODE_XMAS;

  for (i = 0; i < (XMAS_ON_MAX / 2); i++)
    select_color(random(NUM_LED));

  redraw();

  last_time = millis() - 1;

  reset_next_time(last_time);

  return;
  }

void setup()
  {
  int tmp_time;
  byte i;
  char *compile_time = __TIME__;

  LED_DDR  |=  LED_PIN; // Enable output for LED
  LED_PORT &= ~LED_PIN; // LED off

  // Cargo cult programming from someone else's example -- not really sure what this does
  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);
  SPI.setClockDivider(SPI_CLOCK_DIV16); // 1 MHz max, else flicker

  // This is just to add a little more randomness during testing so repeated
  // compiles don't do exactly the same thing.  This has no noticable effect
  // during normal operation.
  tmp_time = 0;
  for (i = 0; compile_time[i] != '\0'; i++)
    tmp_time += (int)compile_time[i];

  // This isn't random at all but it's the best we have without a RTC
  randomSeed(analogRead(0) + tmp_time);

  last_color_choice = 0;

  reinit();

  return;
  }

void flicker(byte index)
  {
  byte i;

  for (i = (random(FLICKER_MAX - FLICKER_MIN) + FLICKER_MIN) + 1; i > 0; i--)
    {
    redraw();

    delay(FLICKER_DELAY_MSECS);

    COPY_COLOR(permode.xmas.saved_color, color[index]);
    SET_BLACK(color[index]);

    redraw();

    delay(FLICKER_DELAY_MSECS);

    COPY_COLOR(color[index], permode.xmas.saved_color);
    }

  redraw();

  return;
  }

void move_xmas_fade()
  {
  byte i;
  byte j;
  byte dest_index;
  byte source_index;
  unsigned long down_start_time;
  unsigned long up_start_time;
  unsigned long current_time;
  unsigned long duration;
  byte target_color[3];
  byte num_moves;

  duration = random(FADEMOVE_MAX_MSECS - FADEMOVE_MIN_MSECS) + FADEMOVE_MIN_MSECS;

  i = 0;
  do
    {
    source_index = random(NUM_LED);
    i++;
    }
  while ((i < 255) &&
         IS_BLACK(color[source_index]));

  COPY_COLOR(target_color, color[source_index]);

  for (j = (random(FADEMOVE_MAX - FADEMOVE_MIN) + FADEMOVE_MIN); j > 0; j--)
    {
    i = 0;
    do
      {
      dest_index = random(NUM_LED);
      i++;
      }
    while ((i < 255) &&
           !IS_BLACK(color[dest_index]));

    if (IS_BLACK(color[dest_index]))
      {
      down_start_time = millis();
      up_start_time = down_start_time + (duration * 0.33);
      current_time = down_start_time;

      i = 0;
      while ((i < 255) &&
             (current_time < (down_start_time + duration)))
        {
        if (current_time < (down_start_time + (duration * 0.66)))
          FADE_BLACK(color[source_index], target_color, duration * 0.66, current_time - down_start_time);
        else
          SET_BLACK(color[source_index]);

        if (current_time >= up_start_time)
          FADE_UP(color[dest_index], target_color, duration * 0.66, current_time - up_start_time);

        redraw();
        delay(LOOP_DELAY_MSECS);
        current_time = millis();
        i++;
        }

      SET_BLACK(color[source_index]);
      COPY_COLOR(color[dest_index], target_color);
      }
    else
      select_color(dest_index);

    source_index = dest_index;
    }

  return;
  }

void move_xmas_change()
  {
  byte i;
  byte index;
  byte start_color[3];
  byte new_color[3];
  unsigned long duration;
  unsigned long start_time;
  unsigned long current_time;

  i = 0;
  do
    {
    index = random(NUM_LED);
    i++;
    }
  while ((i < 255) &&
         IS_BLACK(color[index]));

  select_color(new_color);
  COPY_COLOR(start_color, color[index]);

  current_time = millis();
  start_time = current_time;
  duration = random(FADESAME_MAX_MSECS - FADESAME_MIN_MSECS) + FADESAME_MIN_MSECS;

  i = 0;
  while ((i < 255) &&
         (current_time < (start_time + duration)))
    {
    FADE_COLOR(color[index], start_color, new_color, duration, current_time - start_time);

    redraw();
    delay(LOOP_DELAY_MSECS);
    current_time = millis();
    i++;
    }

  COPY_COLOR(color[index], new_color);

  return;
  }

boolean move_xmas(unsigned long current_time)
  {
  boolean return_value;
  byte i;
  byte j;
  byte tmp_count;
  byte tmp_num;
  byte tmp_index;

  return_value = 0;

  // Count the number of lit LEDs
  tmp_count = 0;
  tmp_num = 0;
  for (i = 0; i < NUM_LED; i++)
    {
    if (mode[i] == MODE_XMAS)
      tmp_num++;

    if (!IS_BLACK(color[i]))
      tmp_count++;
    }

  if (tmp_num > 0)
    {
    if ((mode_field == MODE_XMAS) &&
        (random(100) < XMAS_CHANGE_PERCENT))
      {
      if (random(100) < XMAS_FADEMOVE_PERCENT)
        move_xmas_fade();
      else
        move_xmas_change();

      return_value = 1;
      }
    else if (random(100) < (((XMAS_ON_MAX - tmp_count) * 100.0) / XMAS_ON_MAX))
      {
      // Turn on a dark LED
      i = 0;
      do
        {
        tmp_index = random(NUM_LED);
        if ((mode[tmp_index] == MODE_XMAS) &&
            IS_BLACK(color[tmp_index]))
          {
          if ((mode_field == MODE_XMAS) &&
              (random(100) < XMAS_FLICKER_ON_PERCENT))
            {
            select_color(tmp_index);
            flicker(tmp_index);
            }
          else
            select_color(tmp_index);

          return_value = 1;
          }

        i++;
        }
      while ((i < 255) &&
             !return_value);
      }
    else
      {
      // Turn off a lit LED
      i = 0;
      do
        {
        tmp_index = random(NUM_LED);
        if ((mode[tmp_index] == MODE_XMAS) &&
            !IS_BLACK(color[tmp_index]))
          {
          if ((mode_field == MODE_XMAS) &&
              (random(100) < XMAS_FLICKER_OFF_PERCENT))
            flicker(tmp_index);

          SET_BLACK(color[tmp_index]);

          return_value = 1;
          }

        i++;
        }
      while ((i < 255) &&
             !return_value);
      }

    next_time[TIME_MINOR] = current_time + random(TIME_MINOR_MAX_MSECS - TIME_MINOR_MIN_MSECS) + TIME_MINOR_MIN_MSECS;
    }

  return(return_value);
  }

void reset_next_time(unsigned long current_time)
  {
  next_time[TIME_MINOR] = current_time + random(TIME_MINOR_MAX_MSECS - TIME_MINOR_MIN_MSECS) + TIME_MINOR_MIN_MSECS;
  next_time[TIME_MAJOR] = current_time + random(TIME_MAJOR_MAX_MSECS - TIME_MAJOR_MIN_MSECS) + TIME_MAJOR_MIN_MSECS;

  return;
  }

void start_life(unsigned long current_time)
  {
  byte i;
  byte j;
  byte tmp_count;

  tmp_count = 0;
  for (i = 0; i < NUM_LED; i++)
    if (!IS_BLACK(color[i]))
      tmp_count++;

  if (tmp_count >= LIFE_LED_MIN)
    {
    mode_field = MODE_LIFE;
    for (i = 0; i <= LIFE_LIFESPAN; i++)
      {
      j = 0;
      do
        {
        select_color(permode.life.color[i]);
        j++;
        }
      while ((j < 255) &&
             (i > 0) &&
             (abs(permode.life.color[i][0] - permode.life.color[i - 1][0]) < 63) &&
             (abs(permode.life.color[i][1] - permode.life.color[i - 1][1]) < 63) &&
             (abs(permode.life.color[i][2] - permode.life.color[i - 1][2]) < 63));
      }

    for (i = 0; i < NUM_LED; i++)
      {
      if (!IS_BLACK(color[i]))
        {
        COPY_COLOR(permode.life.saved_color[i], color[i]);
        mode[i] = LIFE_LEDMODE_BIRTHING;
        }
      else
        mode[i] = LIFE_LEDMODE_DEAD;
      }

    reset_next_time(current_time);

    permode.life.next_time = current_time + LIFE_GENERATION_MSECS;
    }
  else
    next_time[TIME_MAJOR] = current_time + random(LIFE_TIME_POSTPONE_MAX_MSECS - LIFE_TIME_POSTPONE_MIN_MSECS) + LIFE_TIME_POSTPONE_MIN_MSECS;

  return;
  }

void end_game(unsigned long current_time)
  {
  byte i;

  mode_field = MODE_XMAS;
  for (i = 0; i < NUM_LED; i++)
    mode[i] = MODE_XMAS;

  reset_next_time(current_time);

  return;
  }

boolean move_life(unsigned long current_time)
  {
  byte i;
  byte j;
  byte tmp_count;
  byte tmp_index;
  boolean return_value;
  boolean continue_game;

  return_value = 0;
  continue_game = 0;

  for (i = 0; i < NUM_LED; i++)
    if (mode[i] == LIFE_LEDMODE_BIRTHING)
      {
      FADE_COLOR(color[i], permode.life.saved_color[i], permode.life.color[0], LIFE_GENERATION_MSECS, LIFE_GENERATION_MSECS - (permode.life.next_time - current_time));
      return_value = 1;
      }
    else if (mode[i] == LIFE_LEDMODE_DYING)
      {
      FADE_BLACK(color[i], permode.life.saved_color[i], LIFE_GENERATION_MSECS, LIFE_GENERATION_MSECS - (permode.life.next_time - current_time));
      return_value = 1;
      }
    else if ((mode[i] >= LIFE_LEDMODE_ALIVE) &&
             (mode[i] < (LIFE_LEDMODE_ALIVE + LIFE_LIFESPAN)))
      {
      FADE_COLOR(color[i], permode.life.saved_color[i], permode.life.color[mode[i] - LIFE_LEDMODE_ALIVE], LIFE_GENERATION_MSECS, LIFE_GENERATION_MSECS - (permode.life.next_time - current_time));

      return_value = 1;
      }

  if (permode.life.next_time < current_time)
    {
    for (i = 0; i < NUM_LED; i++)
      {
      if (mode[i] != LIFE_LEDMODE_DEAD)
        continue_game = 1;

      tmp_count = 0;
      for (j = 0; j < NUM_NEIGHBORS; j++)
        {
        if (((tmp_index = get_neighbor(i, j)) != INVALID_LED) &&
            (mode[tmp_index] >= LIFE_LEDMODE_ALIVE) &&
            (mode[tmp_index] < (LIFE_LEDMODE_ALIVE + LIFE_LIFESPAN)))
          tmp_count++;
        }

      if ((mode[i] >= LIFE_LEDMODE_ALIVE) &&
          (mode[i] < (LIFE_LEDMODE_ALIVE + LIFE_LIFESPAN)))
        {
        COPY_COLOR(color[i], permode.life.color[mode[i] - LIFE_LEDMODE_ALIVE]);

        // LED is alive, check for starvation or overcrowding
        if ((tmp_count >= LIFE_STARVATION_MIN) &&
            (tmp_count <= LIFE_STARVATION_MAX))
          mode[i]++;
        else if ((tmp_count >= LIFE_OVERCROWDING_MIN) &&
                 (tmp_count <= LIFE_OVERCROWDING_MAX))
          mode[i] = LIFE_LEDMODE_DYING;
        else
          mode[i] = LIFE_LEDMODE_ALIVE;

        COPY_COLOR(permode.life.saved_color[i], color[i]);

        return_value = 1;
        }
      else if (mode[i] == LIFE_LEDMODE_DEAD)
        {
        // LED is dead
        if ((tmp_count >= LIFE_BIRTH_MIN) &&
            (tmp_count <= LIFE_BIRTH_MAX) &&
            (random(100) < LIFE_BIRTH_CHANCE))
          {
          mode[i] = LIFE_LEDMODE_BIRTHING;
          COPY_COLOR(permode.life.saved_color[i], color[i]);
          }
        }
      else if (mode[i] == LIFE_LEDMODE_BIRTHING)
        {
        mode[i] = LIFE_LEDMODE_ALIVE;
        COPY_COLOR(color[i], permode.life.color[0]);
        COPY_COLOR(permode.life.saved_color[i], permode.life.color[0]);
        return_value = 1;
        }
      else if (mode[i] == LIFE_LEDMODE_DYING)
        {
        mode[i] = LIFE_LEDMODE_DEAD;
        SET_BLACK(color[i]);
        return_value = 1;
        }
      }

    if ((next_time[TIME_MAJOR] < current_time) ||
        !continue_game)
      end_game(current_time);
    else
      permode.life.next_time = current_time + LIFE_GENERATION_MSECS;
    }

  return(return_value);
  }

void move_scroll(unsigned long current_time)
  {
  byte i;
  byte j;
  byte k;
  byte num_lit;
  byte scroll_direction;
  byte next_num;

  scroll_direction = INVALID_DIRECTION;

  for (k = (random(SCROLL_DIRECTION_MAX - SCROLL_DIRECTION_MIN) + SCROLL_DIRECTION_MIN); k > 0; k--)
    {
    j = 0;
    do
      {
      next_num = random(NUM_NEIGHBORS);
      j++;
      }
    while ((j < 255) &&
           (next_num == scroll_direction));

    scroll_direction = next_num;

    for (i = (random(SCROLL_DISTANCE_MAX - SCROLL_DISTANCE_MIN) + SCROLL_DISTANCE_MIN); i > 0; i--)
      {
      num_lit = 0;

      for (j = 0; j < NUM_LED; j++)
        {
        COPY_COLOR(permode.scroll.saved_color[j], color[j]);

        if (!IS_BLACK(color[j]))
          num_lit++;
        }

      for (j = 0; j < NUM_LED; j++)
        {
        if ((next_num = get_neighbor(j, scroll_direction)) != INVALID_LED)
          COPY_COLOR(color[j], permode.scroll.saved_color[next_num]);
        else
          {
          if (random(XMAS_ON_MAX / 2) < ((XMAS_ON_MAX / 2) - num_lit))
            select_color(j);
          else
            SET_BLACK(color[j]);
          }
        }

      redraw();
      delay(SCROLL_MOVEMENT_DELAY_MSECS / k);
      }
    }

  reset_next_time(current_time);

  return;
  }

void start_tracer(unsigned long current_time)
  {
  byte i;
  byte start_led;
  byte tracer_num;
  byte tmp_direction;

  tracer_num = INVALID_TRACER;

  if (mode_field != MODE_TRACER)
    {
    mode_field = MODE_TRACER;

    for (i = 0; i < TRACER_MAX_ACTIVE; i++)
      permode.tracer.start_time[i] = 0;
    }

  for (i = 0; i < TRACER_MAX_ACTIVE; i++)
    if (permode.tracer.start_time[i] == 0)
      {
      tracer_num = i;
      break;
      }

  if (tracer_num != INVALID_TRACER)
    {
    i = 0;
    do
      {
      start_led = random(NUM_LED);
      i++;
      }
    while ((i < 255) &&
           ((mode[start_led] != MODE_XMAS) ||
            !is_edge(start_led)));

    if (i < 255)
      {
      i = 0;
      do
        {
        tmp_direction = random(NUM_NEIGHBORS);
        i++;
        }
      while ((i < 255) &&
             ((get_neighbor(start_led, tmp_direction) == INVALID_LED) ||
              (get_neighbor(start_led, OPPOSITE_DIRECTION(tmp_direction)) != INVALID_LED)));

      if (i < 255)
        {
        permode.tracer.count_direction[tracer_num] = tmp_direction;
        permode.tracer.tail_bounces[tracer_num] = (random(TRACER_LENGTH_MAX - TRACER_LENGTH_MIN) << 4) | (random(TRACER_BOUNCE_MAX - TRACER_BOUNCE_MIN) & 0x0F);
        permode.tracer.bright_msecs[tracer_num] = random(TRACER_BRIGHTEN_MSECS_MAX - TRACER_BRIGHTEN_MSECS_MIN) + TRACER_BRIGHTEN_MSECS_MIN;

        mode[start_led] = MODE_TRACER;
        permode.tracer.location[tracer_num][0] = start_led;
        for (i = 1; i < TRACER_LENGTH_MAX; i++)
          permode.tracer.location[tracer_num][i] = INVALID_LED;

        permode.tracer.start_time[tracer_num] = current_time;

        COPY_COLOR(permode.tracer.start_color[tracer_num], color[start_led]);

        select_color(permode.tracer.color[tracer_num]);

        permode.tracer.start_time[tracer_num] = current_time;
        }
      }
    }

  next_time[TIME_MINOR] = current_time + random(TIME_MINOR_MAX_MSECS - TIME_MINOR_MIN_MSECS) + TIME_MINOR_MIN_MSECS;

  return;
  }

boolean move_tracer(unsigned long current_time)
  {
  boolean return_value;
  boolean found_match;
  byte i;
  byte j;
  byte tmp_len;
  byte tmp_location;
  byte tmp_direction;
  byte start_color[3];
  byte end_color[3];

  return_value = 0;

  for (i = 0; i < TRACER_MAX_ACTIVE; i++)
    if (permode.tracer.start_time[i] != 0)
      {
      tmp_len = (permode.tracer.tail_bounces[i] >> 4) + TRACER_LENGTH_MIN;

      if ((current_time - permode.tracer.start_time[i]) > permode.tracer.bright_msecs[i])
        {
        found_match = 0;
        for (j = 0; j < tmp_len; j++)
          if (permode.tracer.location[i][j] != INVALID_LED)
            {
            found_match = 1;
            break;
            }

        if (found_match)
          {
          permode.tracer.start_time[i] = current_time;

          if (permode.tracer.location[i][tmp_len - 1] != INVALID_LED)
            {
            mode[permode.tracer.location[i][tmp_len - 1]] = MODE_XMAS;
            SET_BLACK(color[permode.tracer.location[i][tmp_len - 1]]);
            return_value = 1;
            }

          for (j = (tmp_len - 1); j > 0; j--)
            permode.tracer.location[i][j] = permode.tracer.location[i][j - 1];

          if (permode.tracer.location[i][0] != INVALID_LED)
            {
            if (((tmp_location = get_neighbor(permode.tracer.location[i][0], (permode.tracer.count_direction[i] & 0x0F))) == INVALID_LED) ||
                (mode[tmp_location] != MODE_XMAS) ||
                (!IS_BLACK(color[tmp_location]) &&
                 (random(100) < TRACER_REFLECT_PERCENT) &&
                 is_edge(tmp_location) &&
                 is_edge(permode.tracer.location[i][0])))
              {
              if (tmp_location == INVALID_LED)
                found_match = 0;

              j = 0;
              do
                {
                tmp_direction = random(NUM_NEIGHBORS);

                if (((tmp_location = get_neighbor(permode.tracer.location[i][0], tmp_direction)) != INVALID_LED) &&
                    (mode[tmp_location] != MODE_XMAS))
                  tmp_location = INVALID_LED;

                j++;
                }
              while (((tmp_location == INVALID_LED) ||
                      (tmp_location == permode.tracer.location[i][2])) &&
                     (j < 255));

              if (tmp_location != INVALID_LED)
                permode.tracer.count_direction[i] = (((permode.tracer.count_direction[i] >> 4) + (found_match ? 0 : 1)) << 4) | (((byte)tmp_direction) & 0x0F);
              else
                permode.tracer.location[i][0] = INVALID_LED;
              }

            if ((permode.tracer.location[i][0] != INVALID_LED) &&
                ((permode.tracer.count_direction[i] >> 4) < ((permode.tracer.tail_bounces[i] & 0x0F) + TRACER_BOUNCE_MIN)))
              {
              permode.tracer.location[i][0] = tmp_location;
              COPY_COLOR(permode.tracer.start_color[i], color[tmp_location]);
              mode[tmp_location] = MODE_TRACER;
              }
            else
              permode.tracer.location[i][0] = INVALID_LED;
            }
          }
        else
          permode.tracer.start_time[i] = 0;
        }

      if (permode.tracer.start_time[i] != 0)
        {
        for (j = 1; j < tmp_len; j++)
          if (permode.tracer.location[i][j] != INVALID_LED)
            {
            start_color[0] = (permode.tracer.color[i][0] * (tmp_len - j)) / tmp_len;
            start_color[1] = (permode.tracer.color[i][1] * (tmp_len - j)) / tmp_len;
            start_color[2] = (permode.tracer.color[i][2] * (tmp_len - j)) / tmp_len;
            end_color[0] = (permode.tracer.color[i][0] * (tmp_len - (j + 1))) / tmp_len;
            end_color[1] = (permode.tracer.color[i][1] * (tmp_len - (j + 1))) / tmp_len;
            end_color[2] = (permode.tracer.color[i][2] * (tmp_len - (j + 1))) / tmp_len;

            FADE_COLOR(color[permode.tracer.location[i][j]], start_color, end_color, permode.tracer.bright_msecs[i], current_time - permode.tracer.start_time[i]);

            return_value = 1;
            }

        if (permode.tracer.location[i][0] != INVALID_LED)
          {
          FADE_COLOR(color[permode.tracer.location[i][0]], permode.tracer.start_color[i], permode.tracer.color[i], permode.tracer.bright_msecs[i], current_time - permode.tracer.start_time[i]);
          return_value = 1;
          }
        }
      }

  found_match = 0;
  for (i = 0; i < TRACER_MAX_ACTIVE; i++)
    if (permode.tracer.start_time[i] != 0)
      {
      found_match = 1;
      break;
      }

  if (!found_match)
    {
    mode_field = MODE_XMAS;
    next_time[TIME_MINOR] = current_time + random(TIME_MINOR_MAX_MSECS - TIME_MINOR_MIN_MSECS) + TIME_MINOR_MIN_MSECS;
    }

  return(return_value);
  }

void end_starlietz(unsigned long start_time)
  {
  byte i;
  unsigned long current_time;
  int fade_time;
  byte max_num;
  double tmp_mult;

  for (i = 0; i < NUM_LED; i++)
    if (mode_field == MODE_BLINKENLIETZ)
      COPY_COLOR(permode.starlietz.saved_color[i], color[i]);
    else if (random(NUM_LED) < STARFIELD_END_LED_MAX)
      {
      max_num = random(STARFIELD_END_RGB_MAX - STARFIELD_END_RGB_MIN) + (STARFIELD_END_RGB_MIN - MAXVAL(color[i][0], MAXVAL(color[i][1], color[i][2])));
      permode.starlietz.saved_color[i][0] = (color[i][0] > 0) ? (color[i][0] + max_num) : 0;
      permode.starlietz.saved_color[i][1] = (color[i][1] > 0) ? (color[i][1] + max_num) : 0;
      permode.starlietz.saved_color[i][2] = (color[i][2] > 0) ? (color[i][2] + max_num) : 0;
      }
    else
      SET_BLACK(permode.starlietz.saved_color[i]);

  current_time = millis();
  fade_time = (mode_field == MODE_STARFIELD) ? STARFIELD_TIME_FIELD_FADE_MSECS : BLINKENLIETZ_TIME_FIELD_FADE_MSECS;
  do
    {
    for (i = 0; i < NUM_LED; i++)
      if (mode_field == MODE_STARFIELD)
        FADE_COLOR(color[i], permode.starlietz.base_color, permode.starlietz.saved_color[i], fade_time, current_time - start_time);
      else
        FADE_BLACK(color[i], permode.starlietz.saved_color[i], fade_time, current_time - start_time);

    redraw();
    delay(LOOP_DELAY_MSECS);
    current_time = millis();
    }
  while (current_time < (start_time + fade_time));

  for (i = 0; i < NUM_LED; i++)
    {
    if (mode_field == MODE_STARFIELD)
      COPY_COLOR(color[i], permode.starlietz.saved_color[i]);
    else
      SET_BLACK(color[i]);

    mode[i] = MODE_XMAS;
    }

  redraw();

  reset_next_time(current_time);

  mode_field = MODE_XMAS;

  return;
  }

void start_starfield(unsigned long start_time)
  {
  mode_field = MODE_STARFIELD;
  start_starlietz(start_time);

  return;
  }

void start_blinkenlietz(unsigned long start_time)
  {
  mode_field = MODE_BLINKENLIETZ;
  start_starlietz(start_time);

  return;
  }

void start_starlietz(unsigned long start_time)
  {
  byte i;
  unsigned long current_time;
  int fade_time;
  byte max_num;

  for (i = 0; i < NUM_LED; i++)
    COPY_COLOR(permode.starlietz.saved_color[i], color[i]);

  if (mode_field == MODE_STARFIELD)
    {
    // This is only necessary because a magenta background isn't very pretty
    i = 0;
    do
      {
      select_color(permode.starlietz.base_color);
      if ((max_num = MAXVAL(permode.starlietz.base_color[0], MAXVAL(permode.starlietz.base_color[1], permode.starlietz.base_color[2]))) > STARFIELD_COLOR_MAX)
        {
        max_num -= STARFIELD_COLOR_MAX;
        permode.starlietz.base_color[0] = (permode.starlietz.base_color[0] > max_num) ? (permode.starlietz.base_color[0] - max_num) : 0;
        permode.starlietz.base_color[1] = (permode.starlietz.base_color[1] > max_num) ? (permode.starlietz.base_color[1] - max_num) : 0;
        permode.starlietz.base_color[2] = (permode.starlietz.base_color[2] > max_num) ? (permode.starlietz.base_color[2] - max_num) : 0;
        }
      }
    while ((i < 255) &&
           (permode.starlietz.base_color[0] > 0) &&
           (permode.starlietz.base_color[1] == 0) &&
           (permode.starlietz.base_color[2] > 0));
    }
  else
    SET_BLACK(permode.starlietz.base_color);

  current_time = millis();
  fade_time = (mode_field == MODE_STARFIELD) ? STARFIELD_TIME_FIELD_FADE_MSECS : BLINKENLIETZ_TIME_FIELD_FADE_MSECS;
  do
    {
    for (i = 0; i < NUM_LED; i++)
      FADE_COLOR(color[i], permode.starlietz.saved_color[i], permode.starlietz.base_color, fade_time, current_time - start_time);

    redraw();
    delay(LOOP_DELAY_MSECS);
    current_time = millis();
    }
  while (current_time < (start_time + fade_time));

  for (i = 0; i < NUM_LED; i++)
    {
    COPY_COLOR(permode.starlietz.saved_color[i], permode.starlietz.base_color);
    COPY_COLOR(color[i], permode.starlietz.base_color);

    // move_starfield() uses XMAS/STARFIELD to track which LEDs are actually stars and which are neighbors
    mode[i] = MODE_XMAS;
    }

  redraw();

  if (mode_field == MODE_STARFIELD)
    {
    permode.starlietz.end_time = random(STARFIELD_TIME_END_MAX_MSECS - STARFIELD_TIME_END_MIN_MSECS) + STARFIELD_TIME_END_MIN_MSECS + current_time;
    permode.starlietz.next_time = random(STARFIELD_TIME_STAR_MAX_MSECS - STARFIELD_TIME_STAR_MIN_MSECS) + STARFIELD_TIME_STAR_MIN_MSECS + current_time;
    }
  else
    {
    permode.starlietz.end_time = random(BLINKENLIETZ_TIME_END_MAX_MSECS - BLINKENLIETZ_TIME_END_MIN_MSECS) + BLINKENLIETZ_TIME_END_MIN_MSECS + current_time;
    permode.starlietz.next_time = random(BLINKENLIETZ_TIME_STAR_MAX_MSECS - BLINKENLIETZ_TIME_STAR_MIN_MSECS) + BLINKENLIETZ_TIME_STAR_MIN_MSECS + current_time;
    }

  permode.starlietz.num_active = 0;

  return;
  }

boolean move_starlietz(unsigned long current_time)
  {
  boolean return_value;
  byte i;
  byte j;
  byte tmp_star;

  return_value = false;

  if (current_time > permode.starlietz.end_time)
    end_starlietz(current_time);
  else
    {
    if ((current_time >= permode.starlietz.next_time) &&
        (permode.starlietz.num_active < STARLIETZ_MAX_STARS))
      {
      j = 0;
      do
        {
        tmp_star = random(NUM_LED);
        for (i = 0; i < permode.starlietz.num_active; i++)
          if (tmp_star == permode.starlietz.location[i])
            {
            tmp_star = INVALID_LED;
            break;
            }

        j++;
        }
      while ((j < 255) &&
             (tmp_star == INVALID_LED));

      if (tmp_star != INVALID_LED)
        {
        if (mode_field == MODE_STARFIELD)
          SET_COLOR(permode.starlietz.color[permode.starlietz.num_active], 255, 255, 255);
        else
          select_color(permode.starlietz.color[permode.starlietz.num_active]);

        permode.starlietz.location[permode.starlietz.num_active] = tmp_star;
        permode.starlietz.start_time[permode.starlietz.num_active] = current_time;
        permode.starlietz.change_time[permode.starlietz.num_active] = (mode_field == MODE_STARFIELD) ? (random(STARFIELD_TIME_BRIGHT_MAX_MSECS - STARFIELD_TIME_BRIGHT_MIN_MSECS) + STARFIELD_TIME_BRIGHT_MIN_MSECS) : (random(BLINKENLIETZ_TIME_BRIGHT_MAX_MSECS - BLINKENLIETZ_TIME_BRIGHT_MIN_MSECS) + BLINKENLIETZ_TIME_BRIGHT_MIN_MSECS);

        permode.starlietz.updown[permode.starlietz.num_active] = 1;
        mode[tmp_star] = MODE_STARFIELD;

        permode.starlietz.num_active++;
        permode.starlietz.next_time = current_time + ((mode_field == MODE_STARFIELD) ? (random(STARFIELD_TIME_STAR_MAX_MSECS - STARFIELD_TIME_STAR_MIN_MSECS) + STARFIELD_TIME_STAR_MIN_MSECS) : (random(BLINKENLIETZ_TIME_STAR_MAX_MSECS - BLINKENLIETZ_TIME_STAR_MIN_MSECS) + BLINKENLIETZ_TIME_STAR_MIN_MSECS));
        }
      }

    for (i = 0; i < permode.starlietz.num_active; i++)
      {
      if (permode.starlietz.updown[i])
        {
        if ((current_time - permode.starlietz.start_time[i]) < permode.starlietz.change_time[i])
          FADE_COLOR(color[permode.starlietz.location[i]], permode.starlietz.base_color, permode.starlietz.color[i], permode.starlietz.change_time[i], current_time - permode.starlietz.start_time[i]);
        else
          {
          COPY_COLOR(color[permode.starlietz.location[i]], permode.starlietz.color[i]);
          permode.starlietz.change_time[i] = (mode_field == MODE_STARFIELD) ? (random(STARFIELD_TIME_DIM_MAX_MSECS - STARFIELD_TIME_DIM_MIN_MSECS) + STARFIELD_TIME_DIM_MIN_MSECS) : (random(BLINKENLIETZ_TIME_DIM_MAX_MSECS - BLINKENLIETZ_TIME_DIM_MIN_MSECS) + BLINKENLIETZ_TIME_DIM_MIN_MSECS);
          permode.starlietz.start_time[i] = current_time;
          permode.starlietz.updown[i] = 0;
          }
        }
      else
        {
        if ((current_time - permode.starlietz.start_time[i]) < permode.starlietz.change_time[i])
          FADE_COLOR(color[permode.starlietz.location[i]], permode.starlietz.color[i], permode.starlietz.base_color, permode.starlietz.change_time[i], current_time - permode.starlietz.start_time[i]);
        else
          {
          COPY_COLOR(color[permode.starlietz.location[i]], permode.starlietz.base_color);
          mode[permode.starlietz.location[i]] = MODE_XMAS;
          permode.starlietz.location[i] = INVALID_LED;
          }
        }

      return_value = true;
      }

    i = 0;
    while (i < permode.starlietz.num_active)
      {
      if (permode.starlietz.location[i] == INVALID_LED)
        {
        permode.starlietz.num_active--;
        if (i < permode.starlietz.num_active)
          {
          permode.starlietz.location[i] = permode.starlietz.location[permode.starlietz.num_active];
          permode.starlietz.start_time[i] = permode.starlietz.start_time[permode.starlietz.num_active];
          permode.starlietz.change_time[i] = permode.starlietz.change_time[permode.starlietz.num_active];
          permode.starlietz.updown[i] = permode.starlietz.updown[permode.starlietz.num_active];
          COPY_COLOR(permode.starlietz.color[i], permode.starlietz.color[permode.starlietz.num_active]);
          }
        }
      else
        i++;
      }
    }

  return(return_value);
  }

byte twenty48_choose_led()
  {
  byte return_value;
  int i;

  i = 0;
  do
    {
    return_value = random(NUM_LED);
    if (!IS_BLACK(color[return_value]) ||
        ((get_row(return_value) % 2) == 1))
      return_value = INVALID_LED;

    i++;
    }
  while ((return_value == INVALID_LED) &&
         (i < 1000));

  return(return_value);
  }

void start_twenty48(unsigned long start_time)
  {
  byte i;
  byte j;
  byte k;
  boolean found_match;
  byte tmp_led;
  unsigned long current_time;

  for (i = 0; i < NUM_LED; i++)
    COPY_COLOR(permode.twenty48.saved_color[i], color[i]);

  for (i = 0; i < TWENTY48_MAX_COLORS; i++)
    {
    j = 0;
    do
      {
      found_match = 0;

      select_color(permode.twenty48.color[i]);

      for (k = 1; k < i; k++)
        if ((abs(permode.twenty48.color[i][0] - permode.twenty48.color[k][0]) < 63) &&
            (abs(permode.twenty48.color[i][1] - permode.twenty48.color[k][1]) < 63) &&
            (abs(permode.twenty48.color[i][2] - permode.twenty48.color[k][2]) < 63))
          {
          found_match = 1;
          break;
          }
        
      j++;
      }
    while ((j < 255) &&
           (i > 0) &&
           found_match);
    }

  current_time = millis();
  do
    {
    for (i = 0; i < NUM_LED; i++)
      FADE_BLACK(color[i], permode.twenty48.saved_color[i], TWENTY48_TIME_FIELD_FADE_MSECS, current_time - start_time);

    redraw();
    delay(LOOP_DELAY_MSECS);
    current_time = millis();
    }
  while (current_time < (start_time + TWENTY48_TIME_FIELD_FADE_MSECS));

  for (i = 0; i < NUM_LED; i++)
    {
    SET_BLACK(color[i]);
    permode.twenty48.value[i] = 0;
    }

  //FIXME: Fade into the start colors
  for (i = 0; i < TWENTY48_START_LEDS; i++)
    if ((tmp_led = twenty48_choose_led()) != INVALID_LED)
      {
      permode.twenty48.value[tmp_led] = (random(100) < TWENTY48_NEW_TILE_DOUBLE_PERCENT) ? 2 : 1;
      COPY_COLOR(color[tmp_led], permode.twenty48.color[permode.twenty48.value[tmp_led]]);
      }

  redraw();

  current_time = millis();
  permode.twenty48.end_time = random(TWENTY48_TIME_END_MAX_MSECS - TWENTY48_TIME_END_MIN_MSECS) + TWENTY48_TIME_END_MIN_MSECS + current_time;
  permode.twenty48.next_time = current_time + TWENTY48_TIME_MOVE_MSECS;
  permode.twenty48.mode = TWENTY48_MODE_IDLE;
  mode_field = MODE_TWENTY48;

  return;
  }

boolean twenty48_is_legal(byte direction)
  {
  boolean return_value;
  byte i;
  byte tmp_led;

  return_value = 0;

  for (i = 0; i < NUM_LED; i++)
    if ((permode.twenty48.value[i] > 0) &&
        ((tmp_led = get_neighbor(i, direction)) != INVALID_LED) &&
        ((permode.twenty48.value[tmp_led] == 0) ||
         (permode.twenty48.value[tmp_led] == permode.twenty48.value[i])))
      {
      return_value = 1;
      break;
      }

  return(return_value);
  }

boolean move_twenty48(unsigned long current_time)
  {
  boolean return_value;
  byte i;
  byte j;
  byte sideways;
  byte edge_led;
  byte current_led;
  byte prev_led;
  boolean changed;

  return_value = 0;
  changed = 0;

  if (permode.twenty48.end_time < current_time)
    end_game(current_time);
  else if (permode.twenty48.next_time < current_time)
    {
    if (permode.twenty48.mode == TWENTY48_MODE_IDLE)
      {
      /* Choose a direction to slide randomly. The move must be legal. */
      prev_led = 0;
      for (j = 0; j < 4; j++)
        {
        i = 0;
        do
          {
          permode.twenty48.direction = random(4) * 2;
          if (prev_led & (0x01 << permode.twenty48.direction))
            permode.twenty48.direction = INVALID_DIRECTION;

          i++;
          }
        while ((permode.twenty48.direction == INVALID_DIRECTION) &&
               (i < 200));

        if (permode.twenty48.direction != INVALID_DIRECTION)
          {
          if (twenty48_is_legal(permode.twenty48.direction))
            break;
          else
            prev_led |= 0x01 << permode.twenty48.direction;
          }
        }

      for (i = 0; i < NUM_LED; i++)
        permode.twenty48.combined[i] = 0;
      }

    if (permode.twenty48.direction != INVALID_DIRECTION)
      {
      /*
       * Find an LED in the corner on the edge the slide moves towards,
       * then find the "sideways" direction that leads along that edge.
       */
      edge_led = 0;
      if (get_neighbor(edge_led, permode.twenty48.direction) != INVALID_LED)
        edge_led = NUM_LED - 1;

      sideways = permode.twenty48.direction + (NUM_NEIGHBORS / 4);
      if (sideways >= NUM_NEIGHBORS)
        sideways -= NUM_NEIGHBORS;

      if (get_neighbor(edge_led, sideways) == INVALID_LED)
        sideways = OPPOSITE_DIRECTION(sideways);

      /*
       * Proceed along the edge, then along each line to move/add the values
       */
      while (edge_led != INVALID_LED)
        {
        if ((current_led = get_neighbor(edge_led, OPPOSITE_DIRECTION(permode.twenty48.direction))) != INVALID_LED)
          {
          prev_led = edge_led;

          do
            {
            if ((permode.twenty48.value[prev_led] == 0) &&
                (permode.twenty48.value[current_led] > 0))
              {
              permode.twenty48.value[prev_led] = permode.twenty48.value[current_led];
              permode.twenty48.combined[prev_led] = permode.twenty48.combined[current_led];
              permode.twenty48.value[current_led] = 0;
              permode.twenty48.combined[current_led] = 0;

              COPY_COLOR(color[prev_led], permode.twenty48.color[permode.twenty48.value[prev_led]]);
              SET_BLACK(color[current_led]);

              changed = 1;
              }
            else if ((permode.twenty48.value[prev_led] > 0) &&
                     (permode.twenty48.value[prev_led] == permode.twenty48.value[current_led]) &&
                     !permode.twenty48.combined[prev_led] &&
                     !permode.twenty48.combined[current_led])
              {
              permode.twenty48.value[prev_led]++;
              permode.twenty48.value[current_led] = 0;
              permode.twenty48.combined[prev_led] = 1;

              COPY_COLOR(color[prev_led], permode.twenty48.color[permode.twenty48.value[prev_led]]);
              SET_BLACK(color[current_led]);

              changed = 1;
              }

            prev_led = current_led;
            current_led = get_neighbor(current_led, OPPOSITE_DIRECTION(permode.twenty48.direction));
            }
          while (current_led != INVALID_LED);
          }

        edge_led = get_neighbor(edge_led, sideways);
        }

      if (changed)
        {
        permode.twenty48.next_time = current_time + TWENTY48_TIME_STEP_MSECS;
        permode.twenty48.mode = TWENTY48_MODE_MOVING;
        }
      else
        {
        for (i = 0; i < TWENTY48_TURN_LEDS; i++)
          if ((current_led = twenty48_choose_led()) != INVALID_LED)
            {
            permode.twenty48.value[current_led] = (random(100) < TWENTY48_NEW_TILE_DOUBLE_PERCENT) ? 2 : 1;
            COPY_COLOR(color[current_led], permode.twenty48.color[permode.twenty48.value[current_led]]);
            }

        permode.twenty48.next_time = current_time + TWENTY48_TIME_MOVE_MSECS;
        permode.twenty48.mode = TWENTY48_MODE_IDLE;
        }

      return_value = 1;
      }
    else
      end_game(current_time);
    }

  return(return_value);
  }

void start_wave(unsigned long start_time)
  {
  byte i;

  permode.wave.bright_time = random(WAVE_MAX_BRIGHT_MSECS - WAVE_MIN_BRIGHT_MSECS) + WAVE_MIN_BRIGHT_MSECS;
  permode.wave.wave_time = permode.wave.bright_time + random(WAVE_MAX_HOLD_MSECS - WAVE_MIN_HOLD_MSECS) + WAVE_MIN_HOLD_MSECS;
  permode.wave.num_colors = random(WAVE_MAX_COLORS - WAVE_MIN_COLORS) + WAVE_MIN_COLORS;

  for (i = 0; i < permode.wave.num_colors; i++)
    if ((i == 0) ||
        (i == (permode.wave.num_colors - 1)) ||
        (random(100) > WAVE_BLACK_PERCENT) ||
        ((i > 0) &&
         IS_BLACK(permode.wave.colors[i - 1])))
      select_color(permode.wave.colors[i]);
    else
      SET_BLACK(permode.wave.colors[i]);

  for (i = 0; i < NUM_LED; i++)
    {
    permode.wave.color_num[i] = WAVE_INVALID;
    COPY_COLOR(permode.wave.saved_color[i], color[i]);
    }

  i = random(NUM_LED);
  permode.wave.color_num[i] = 0;
  permode.wave.start_time[i] = start_time;

  mode_field = MODE_WAVE;

  return;
  }

boolean move_wave(unsigned long current_time)
  {
  byte i;
  byte j;
  byte tmp_neighbor;
  boolean found_match;

  found_match = 0;

  for (i = 0; i < NUM_LED; i++)
    {
    if ((permode.wave.color_num[i] != WAVE_INVALID) &&
        (permode.wave.color_num[i] < permode.wave.num_colors))
      {
      if (current_time > (permode.wave.start_time[i] + permode.wave.wave_time))
        {
        if (permode.wave.color_num[i] < permode.wave.num_colors)
          {
          permode.wave.color_num[i]++;
          permode.wave.start_time[i] = current_time;

          for (j = 0; j < NUM_NEIGHBORS; j++)
            if (((tmp_neighbor = get_neighbor(i, j)) != INVALID_LED) &&
                (permode.wave.color_num[tmp_neighbor] == WAVE_INVALID))
              {
              permode.wave.color_num[tmp_neighbor] = 0;
              permode.wave.start_time[tmp_neighbor] = current_time + (random(WAVE_MAX_DELAY_MSECS - WAVE_MIN_DELAY_MSECS) + WAVE_MIN_DELAY_MSECS);
              }
          }
        }
      else if (permode.wave.start_time[i] < current_time)
        {
        if (current_time > (permode.wave.start_time[i] + permode.wave.bright_time))
          for (j = 0; j < NUM_NEIGHBORS; j++)
            if (((tmp_neighbor = get_neighbor(i, j)) != INVALID_LED) &&
                (permode.wave.color_num[tmp_neighbor] == WAVE_INVALID))
              {
              permode.wave.color_num[tmp_neighbor] = 0;
              permode.wave.start_time[tmp_neighbor] = current_time + (random(WAVE_MAX_DELAY_MSECS - WAVE_MIN_DELAY_MSECS) + WAVE_MIN_DELAY_MSECS);
              }

        if (permode.wave.color_num[i] == 0)
          FADE_COLOR(color[i], permode.wave.saved_color[i], permode.wave.colors[permode.wave.color_num[i]], permode.wave.bright_time, current_time - permode.wave.start_time[i]);
        else
          FADE_COLOR(color[i], permode.wave.colors[permode.wave.color_num[i] - 1], permode.wave.colors[permode.wave.color_num[i]], permode.wave.bright_time, current_time - permode.wave.start_time[i]);
        }

      found_match = 1;
      }
    else if (permode.wave.color_num[i] == permode.wave.num_colors)
      {
      if (current_time > (permode.wave.start_time[i] + permode.wave.wave_time))
        {
        permode.wave.color_num[i]++;

        if (IS_BLACK(permode.wave.saved_color[i]))
          SET_BLACK(color[i]);
        }
      else if (IS_BLACK(permode.wave.saved_color[i]))
        FADE_BLACK(color[i], permode.wave.colors[permode.wave.num_colors - 1], permode.wave.wave_time, current_time - permode.wave.start_time[i]);

      found_match = 1;
      }
    }

  if (!found_match)
    end_wave_undulate(current_time);

  return(1);
  }

void end_wave_undulate(unsigned long current_time)
  {
  byte i;

  for (i = 0; i < NUM_LED; i++)
    mode[i] = MODE_XMAS;

  mode_field = MODE_XMAS;

  reset_next_time(current_time);

  return;
  }

void start_undulate(unsigned long start_time)
  {
  byte i;

  permode.undulate.num_colors = random(UNDULATE_MAX_COLORS - UNDULATE_MIN_COLORS) + UNDULATE_MIN_COLORS;
  permode.undulate.up_time = random(UNDULATE_MAX_UP_MSECS - UNDULATE_MIN_UP_MSECS) + UNDULATE_MIN_UP_MSECS;
  permode.undulate.down_time = random(UNDULATE_MAX_DOWN_MSECS - UNDULATE_MIN_DOWN_MSECS) + UNDULATE_MIN_DOWN_MSECS;
  permode.undulate.num_updown = (random(UNDULATE_MAX_UPDOWN - UNDULATE_MIN_UPDOWN) + UNDULATE_MIN_UPDOWN) * 2;

  for (i = 0; i < permode.undulate.num_colors; i++)
    select_color(permode.undulate.color[i]);

  for (i = 0; i < NUM_LED; i++)
    {
    COPY_COLOR(permode.undulate.saved_color[i], color[i]);
    permode.undulate.updown[i] = 0;
    permode.undulate.color_num[i] = UNDULATE_INVALID_COLOR;
    }

  i = random(NUM_LED);
  permode.undulate.color_num[i] = 0;
  permode.undulate.start_time[i] = start_time;

  mode_field = MODE_UNDULATE;

  return;
  }

boolean move_undulate(unsigned long current_time)
  {
  byte i;
  byte j;
  byte tmp_neighbor;
  boolean found_match;

  found_match = 0;
  for (i = 0; i < NUM_LED; i++)
    if ((permode.undulate.color_num[i] != UNDULATE_INVALID_COLOR) &&
        (permode.undulate.color_num[i] < permode.undulate.num_colors))
      {
      if (permode.undulate.start_time[i] < current_time)
        {
        if ((permode.undulate.updown[i] % 2) == 0)
          {
          if ((permode.undulate.up_time + permode.undulate.start_time[i]) > current_time)
            {
            if ((permode.undulate.color_num[i] == 0) &&
                (permode.undulate.updown[i] == 0))
              FADE_COLOR(color[i], permode.undulate.saved_color[i], permode.undulate.color[permode.undulate.color_num[i]], permode.undulate.up_time, current_time - permode.undulate.start_time[i]);
            else
              FADE_UP(color[i], permode.undulate.color[permode.undulate.color_num[i]], permode.undulate.up_time, current_time - permode.undulate.start_time[i]);
            }
          else
            {
            COPY_COLOR(color[i], permode.undulate.color[permode.undulate.color_num[i]]);

            permode.undulate.updown[i]++;
            permode.undulate.start_time[i] = current_time;

            if ((permode.undulate.updown[i] == (permode.undulate.num_updown - 1)) &&
                (permode.undulate.color_num[i] == (permode.undulate.num_colors - 1)) &&
                !IS_BLACK(permode.undulate.saved_color[i]))
              permode.undulate.color_num[i] = permode.undulate.num_colors;
            else if (permode.undulate.color_num[i] == 0)
              for (j = 0; j < NUM_NEIGHBORS; j++)
                if (((tmp_neighbor = get_neighbor(i, j)) != INVALID_LED) &&
                    (permode.undulate.color_num[tmp_neighbor] == UNDULATE_INVALID_COLOR))
                  {
                  permode.undulate.color_num[tmp_neighbor] = 0;
                  permode.undulate.start_time[tmp_neighbor] = random(UNDULATE_MAX_DELAY_MSECS - UNDULATE_MIN_DELAY_MSECS) + UNDULATE_MIN_DELAY_MSECS + current_time;
                  }
            }
          }
        else
          if ((permode.undulate.down_time + permode.undulate.start_time[i]) > current_time)
            FADE_BLACK(color[i], permode.undulate.color[permode.undulate.color_num[i]], permode.undulate.down_time, current_time - permode.undulate.start_time[i]);
          else
            {
            SET_BLACK(color[i]);

            permode.undulate.updown[i]++;
            permode.undulate.start_time[i] = random(UNDULATE_MAX_BLACK_DELAY_MSECS - UNDULATE_MIN_BLACK_DELAY_MSECS) + UNDULATE_MIN_BLACK_DELAY_MSECS + current_time;

            if (permode.undulate.updown[i] == permode.undulate.num_updown)
              {
              permode.undulate.color_num[i]++;

              if (permode.undulate.color_num[i] < permode.undulate.num_colors)
                permode.undulate.updown[i] = 0;
              }
            }
        }

      found_match = 1;
      }

  if (!found_match)
    end_wave_undulate(current_time);

  return(1);
  }

void loop()
  {
  unsigned long current_time;
  boolean changed;
  char chance;

  current_time = millis();
  changed = 0;

  if (current_time > last_time)
    {
    if (mode_field == MODE_LIFE)
      {
      if (move_life(current_time))
        changed = 1;
      }
    else if ((mode_field == MODE_STARFIELD) ||
             (mode_field == MODE_BLINKENLIETZ))
      {
      if (move_starlietz(current_time))
        changed = 1;
      }
    else if (mode_field == MODE_TWENTY48)
      {
      if (move_twenty48(current_time))
        changed = 1;
      }
    else if (mode_field == MODE_WAVE)
      {
      if (move_wave(current_time))
        changed = 1;
      }
    else if (mode_field == MODE_UNDULATE)
      {
      if (move_undulate(current_time))
        changed = 1;
      }
    else if ((next_time[TIME_MAJOR] < current_time) &&
             (mode_field == MODE_XMAS))
      {
      chance = random(100);
      if (chance < LIFE_CHANCE_PERCENT)
        start_life(current_time);
      else
        {
        chance -= LIFE_CHANCE_PERCENT;
        if (chance < STARFIELD_CHANCE_PERCENT)
          start_starfield(current_time);
        else
          {
          chance -= STARFIELD_CHANCE_PERCENT;
          if (chance < BLINKENLIETZ_CHANCE_PERCENT)
            start_blinkenlietz(current_time);
          else
            {
            chance -= BLINKENLIETZ_CHANCE_PERCENT;
            if (chance < TWENTY48_CHANCE_PERCENT)
              start_twenty48(current_time);
            else
              {
              chance -= TWENTY48_CHANCE_PERCENT;
              if (chance < SCROLL_CHANCE_PERCENT)
                move_scroll(current_time);
              else
                {
                chance -= SCROLL_CHANCE_PERCENT;
                if (chance < WAVE_CHANCE_PERCENT)
                  start_wave(current_time);
                else
                  {
                  chance -= WAVE_CHANCE_PERCENT;
                  if (chance < UNDULATE_CHANCE_PERCENT)
                    start_undulate(current_time);
                  }
                }
              }
            }
          }
        }
      }
    else if ((mode_field == MODE_TRACER) ||
             (mode_field == MODE_XMAS))
      {
      if (next_time[TIME_MINOR] < current_time)
        {
        if (random(100) < TRACER_CHANCE_PERCENT)
          start_tracer(current_time);
        else if (move_xmas(current_time))
          changed = 1;
        }

      if ((mode_field == MODE_TRACER) &&
          move_tracer(current_time))
        changed = 1;
      }

    if (changed)
      redraw();

    last_time = current_time;
    }
  else
    reinit();

  delay(LOOP_DELAY_MSECS);

  return;
  }
