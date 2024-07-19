#include <stdio.h>
#include <stdlib.h>
#include "string.h"
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "pico/multicore.h"
#include "hardware/spi.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "i2s_32_441.pio.h"
#include "ws2812b.pio.h"
#include "rand.h"

//  #define DEBUG

#define ALARM_1_NUM 0
#define ALARM_1_IRQ TIMER_IRQ_0

#define ALARM_2_NUM 1
#define ALARM_2_IRQ TIMER_IRQ_1

#define PICO_CLK_KHZ 133000UL // ((SPI_FREQ * SPI_CLK_DIVISOR) / 1000)
#define AUDIO_BUFF_COUNT 2
#define AUDIO_BUFF_SIZE 1024UL // Size of max Ocarina
#define MAX_VOL 208
#define MAX_VOL_SCALE ((float)MAX_VOL/256.0F)
#define VOL_AVG 32
#define AUDIO_STATEMACHINE 0
#define AUDIO_DMA_DREQ DREQ_PIO0_TX0

#define ADC_VOL_GPIO 26

#define I2S_DATA_GPIO 17
#define I2S_CLK_GPIO 18
#define I2S_FS_GPIO 19

#define PWM_LED_0_GPIO 2
#define PWM_LED_1_GPIO 3
#define PWM_LED_2_GPIO 4

#define NOTE_COUNT 5

#define NOTE_D_GPIO 5
#define NOTE_F_GPIO 6
#define NOTE_A_GPIO 7
#define NOTE_B_GPIO 8
#define NOTE_D2_GPIO 9

#define MODE_GPIO 10

#define MODE_LED_0_GPIO 11
#define MODE_LED_1_GPIO 12

#define RGB_LED_GPIO 13
#define RGB_LED_COUNT 32
#define RGB_LED_BOTTOM_COUNT 5
#define RGB_LED_STATEMACHINE 1
#define RGB_LED_UPDATE_DELAY_MS 50
#define RGB_LED_DMA_DREQ DREQ_PIO0_TX1

#define DEBOUNCE_US 50000ULL
#define MODE_HOLD_US 3000000ULL

#define FIRST_NYAN_NOTE 0
#define SECOND_NYAN_NOTE 1

#define FIRST_PUZZLE_NOTE 2
#define SECOND_PUZZLE_NOTE 3
#define THIRD_PUZZLE_NOTE 4

#define FIRST_PUZZLE_NOTE_COUNT 3
#define SECOND_PUZZLE_NOTE_COUNT 8

#define MIN_PUZZLE_VOL (200 * MAX_VOL_SCALE)

#define PWM_INITIAL_DELAY_US 4000ULL
#define PWM_FULL_DUTY_DELAY_US 2000000ULL
#define SINE_TABLE_SIZE 256

#define PIO pio0

#define SONG_COUNT 13

#define STANDBY_WHITE 0x00102010
#define STANDBY_COLOR_FLASH_THRESH_LO 0x10000000
#define STANDBY_COLOR_FLASH_THRESH_HI 0x40000000

#define ERROR_COLOR 0x00004000
#define ERROR_DELAY_CYCLES 2

#define NYAN_COLOR_COUNT 24
#define NYAN_DELAY_CYCLES 1
#define NYAN_BOTTOM_COLOR 0x00000020
#define NYAN_STAR_COLOR 0x00202020
#define NYAN_STAR_RAND_THRESH 50

#define SONG_ARRAY_COLOR_STEP 8
#define SONG_OVERALL_COLOR_STEP 1
#define SONG_OVERALL_MAX_STEPS (30 * SONG_OVERALL_COLOR_STEP)
#define SONG_ARRAY_DELAY_CYCLES 2
#define SONG_OVERALL_DELAY_CYCLES 1

extern uint8_t song_of_storms[];
extern uint8_t bolero_of_fire[];
extern uint8_t eponas_theme[];
extern uint8_t minuet_of_forest[];
extern uint8_t nocturne_of_shadow[];
extern uint8_t prelude_of_light[];
extern uint8_t requiem_of_spirit[];
extern uint8_t sarias_song[];
extern uint8_t serenade_of_water[];
extern uint8_t song_of_time[];
extern uint8_t suns_song[];
extern uint8_t zeldas_lullaby[];
extern uint8_t nyan_cat[];
extern uint8_t low_battery_chirp[];
extern uint8_t game_hint[];
extern uint8_t konami_hint[];
extern uint8_t rick_roll_hint[];
extern uint8_t dfiu[];

extern uint8_t a_loop[];
extern uint8_t b_loop[];
extern uint8_t d_loop[];
extern uint8_t d2_loop[];
extern uint8_t f_loop[];
extern uint8_t error[];

const uint8_t *note_samples[] = {d_loop, f_loop, a_loop, b_loop, d2_loop};
const uint16_t chaseLookupTable[] = {0, 0, 0xFFFF};

const uint16_t sineLookupTable[] = {
    0x8000, 0x8327, 0x864e, 0x8974, 0x8c98, 0x8fbb, 0x92db, 0x95f8,
    0x9911, 0x9c27, 0x9f39, 0xa245, 0xa54c, 0xa84e, 0xab49, 0xae3d,
    0xb12a, 0xb410, 0xb6ed, 0xb9c2, 0xbc8e, 0xbf51, 0xc20a, 0xc4b8,
    0xc75c, 0xc9f4, 0xcc82, 0xcf03, 0xd178, 0xd3e0, 0xd63c, 0xd88a,
    0xdaca, 0xdcfc, 0xdf1f, 0xe134, 0xe33a, 0xe530, 0xe717, 0xe8ed,
    0xeab3, 0xec69, 0xee0e, 0xefa2, 0xf124, 0xf295, 0xf3f4, 0xf541,
    0xf67b, 0xf7a4, 0xf8b9, 0xf9bc, 0xfaac, 0xfb89, 0xfc53, 0xfd09,
    0xfdac, 0xfe3c, 0xfeb8, 0xff20, 0xff74, 0xffb5, 0xffe2, 0xfffa,
    0xffff, 0xfff0, 0xffce, 0xff97, 0xff4c, 0xfeee, 0xfe7c, 0xfdf7,
    0xfd5d, 0xfcb1, 0xfbf0, 0xfb1d, 0xfa37, 0xf93d, 0xf831, 0xf712,
    0xf5e0, 0xf49c, 0xf346, 0xf1df, 0xf065, 0xeeda, 0xed3e, 0xeb90,
    0xe9d2, 0xe804, 0xe625, 0xe437, 0xe239, 0xe02c, 0xde0f, 0xdbe5,
    0xd9ab, 0xd764, 0xd510, 0xd2ae, 0xd03f, 0xcdc4, 0xcb3d, 0xc8aa,
    0xc60b, 0xc362, 0xc0ae, 0xbdf1, 0xbb29, 0xb859, 0xb580, 0xb29e,
    0xafb5, 0xacc4, 0xa9cc, 0xa6ce, 0xa3c9, 0xa0bf, 0x9db0, 0x9a9d,
    0x9785, 0x946a, 0x914b, 0x8e2a, 0x8b06, 0x87e1, 0x84bb, 0x8194,
    0x7e6c, 0x7b45, 0x781f, 0x74fa, 0x71d6, 0x6eb5, 0x6b96, 0x687b,
    0x6563, 0x6250, 0x5f41, 0x5c37, 0x5932, 0x5634, 0x533c, 0x504b,
    0x4d62, 0x4a80, 0x47a7, 0x44d7, 0x420f, 0x3f52, 0x3c9e, 0x39f5,
    0x3756, 0x34c3, 0x323c, 0x2fc1, 0x2d52, 0x2af0, 0x289c, 0x2655,
    0x241b, 0x21f1, 0x1fd4, 0x1dc7, 0x1bc9, 0x19db, 0x17fc, 0x162e,
    0x1470, 0x12c2, 0x1126, 0xf9b, 0xe21, 0xcba, 0xb64, 0xa20,
    0x8ee, 0x7cf, 0x6c3, 0x5c9, 0x4e3, 0x410, 0x34f, 0x2a3,
    0x209, 0x184, 0x112, 0xb4, 0x69, 0x32, 0x10, 0x01,
    0x06, 0x1e, 0x4b, 0x8c, 0xe0, 0x148, 0x1c4, 0x254,
    0x2f7, 0x3ad, 0x477, 0x554, 0x644, 0x747, 0x85c, 0x985,
    0xabf, 0xc0c, 0xd6b, 0xedc, 0x105e, 0x11f2, 0x1397, 0x154d,
    0x1713, 0x18e9, 0x1ad0, 0x1cc6, 0x1ecc, 0x20e1, 0x2304, 0x2536,
    0x2776, 0x29c4, 0x2c20, 0x2e88, 0x30fd, 0x337e, 0x360c, 0x38a4,
    0x3b48, 0x3df6, 0x40af, 0x4372, 0x463e, 0x4913, 0x4bf0, 0x4ed6,
    0x51c3, 0x54b7, 0x57b2, 0x5ab4, 0x5dbb, 0x60c7, 0x63d9, 0x66ef,
    0x6a08, 0x6d25, 0x7045, 0x7368, 0x768c, 0x79b2, 0x7cd9};

const uint32_t rgb_colors[] = {
    0x00800000, // green
    0x00000080, // blue
    0x00008000, // red
    0x00308000, // yellow
    0x00505050, // white
    0x00004040, // purple
    0x00040040, // cyan
};

const uint32_t nyan_colors[] = {
    0x007f0000,
    0x007f2000,
    0x007f4200,
    0x007f6200,
    0x007b7f00,
    0x005b7f00,
    0x00397f00,
    0x00197f00,
    0x00007f08,
    0x00007f28,
    0x00007f4a,
    0x00007f6a,
    0x0000737f,
    0x0000537f,
    0x0000317f,
    0x0000117f,
    0x0011007f,
    0x0031007f,
    0x0053007f,
    0x0073007f,
    0x007f006a,
    0x007f004a,
    0x007f0028,
    0x007f0008};

typedef struct audio_buff_t
{
    uint32_t buffer[AUDIO_BUFF_SIZE * 2];
    uint32_t length;
} audio_buff_t;

typedef enum badge_mode
{
    freeplay,
    song,
    puzzle,
} badge_mode;

typedef enum badge_output_state
{
    none,
    play_nyan,
    battery_low,
    play_note,
    play_error,
    play_song,
    play_puzzle,
} badge_output_state;

typedef enum badge_puzzle_mode
{
    puzzle_none,
    puzzle_start,
    puzzle_mid,
    puzzle_end
} badge_puzzle_mode;

// Leave no_note at the end so the notes are 0-based
// and can be used as indexes
typedef enum notes
{
    note_d,
    note_f,
    note_a,
    note_b,
    note_d2,
    no_note
} notes;

typedef struct note_song_map
{
    notes note_sequence[8];
    uint8_t sequence_count;
    uint8_t *song;
    uint32_t base_color;
    bool is_match; // used while testing note sequence
} note_song_map;

badge_mode mode = freeplay;
badge_output_state output_state = none;
badge_puzzle_mode puzzle_mode = none;
bool active_note_buttons[] = {false, false, false, false, false};

audio_buff_t audio_buffs[AUDIO_BUFF_COUNT];

// Maybe create a struct
// TODO: Remove unused
uint8_t *current_sample;
uint32_t current_sample_len;
uint32_t current_sample_offset = 0;
uint32_t current_sample_loop_offset = 0;
uint8_t current_volume = 0;
uint16_t volume_accum = 0;
uint8_t current_buff_index = 0;
uint16_t vol_running_avg[VOL_AVG];
uint8_t vol_avg_count = 0;

dma_channel_config audio_dma_config;
int audio_dma_channel_number;

alarm_id_t alarm_id;

uint8_t led_pwm_lookup_index = 0;

alarm_pool_t *core_0_alarm;

uint64_t mode_button_start_time;
uint64_t note_button_starts[] = {0, 0, 0, 0, 0};
notes current_note = no_note;

uint32_t pwm_delay = PWM_INITIAL_DELAY_US;

uint8_t pwm_mode_index = 1;

uint32_t rgb_leds[RGB_LED_COUNT];
repeating_timer_t rgb_led_timer;
dma_channel_config rgb_led_dma_config;
int rgb_led_dma_channel_number;

uint32_t standby_color_flash_ms = 0;
bool standby_color_flash_active = false;
uint32_t standby_current_flash_color = 0;
uint8_t standby_color_flash_led_index = 0;

uint32_t error_cycle_count = 0;
bool error_show_color = false;

uint32_t nyan_cycle_count = 0;
uint8_t nyan_color_index = 0;

uint32_t song_array_cycle_count = 0;
uint32_t song_overall_cycle_count = 0;
uint32_t song_overall_step_count = 0;
uint8_t song_array_index_offset = 0;
bool song_array_index_offset_dec = false;
bool song_overall_step_dec = false;

uint8_t first_puzzle_note_map[] = {note_d2, note_f, note_d, no_note, no_note, no_note, no_note, no_note};
uint8_t second_puzzle_note_map[] = {note_d2, note_d2, note_f, note_f, note_b, note_a, note_b, note_a};

//colors are GRB
//Notes are up = d2, left = b, right = a, down = f, A = d
note_song_map map[] = {
    {.note_sequence = {note_b, note_d2, note_a, note_b, note_d2, note_a, no_note, no_note},
     .sequence_count = 6,
     .song = zeldas_lullaby,
     .base_color = 0x0000317f,
     .is_match = true},
    {.note_sequence = {note_d2, note_b, note_a, note_d2, note_b, note_a, no_note, no_note},
     .sequence_count = 6,
     .song = eponas_theme,
     .base_color = 0x006f7b00,
     .is_match = true},
    {.note_sequence = {note_f, note_a, note_b, note_f, note_a, note_b, no_note, no_note},
     .sequence_count = 6,
     .song = sarias_song,
     .base_color = 0x00800000,
     .is_match = true},
    {.note_sequence = {note_a, note_f, note_d2, note_a, note_f, note_d2, no_note, no_note},
     .sequence_count = 6,
     .song = suns_song,
     .base_color = 0x00427f00,
     .is_match = true},
    {.note_sequence = {note_a, note_d, note_f, note_a, note_d, note_f, no_note, no_note},
     .sequence_count = 6,
     .song = song_of_time,
     .base_color = 0x00007f6a,
     .is_match = true},
    {.note_sequence = {note_d, note_f, note_d2, note_d, note_f, note_d2, no_note, no_note},
     .sequence_count = 6,
     .song = song_of_storms,
     .base_color = 0x007f006a,
     .is_match = true},
    {.note_sequence = {note_d, note_d2, note_b, note_a, note_b, note_a, no_note, no_note},
     .sequence_count = 6,
     .song = minuet_of_forest,
     .base_color = 0x007f5b00,
     .is_match = true},
    {.note_sequence = {note_f, note_d, note_f, note_d, note_a, note_f, note_a, note_f},
     .sequence_count = 8,
     .song = bolero_of_fire,
     .base_color = 0x00008000,
     .is_match = true},
    {.note_sequence = {note_d, note_f, note_a, note_a, note_b},
     .sequence_count = 5,
     .song = serenade_of_water,
     .base_color = 0x00000080,
     .is_match = true},
    {.note_sequence = {note_b, note_a, note_a, note_d, note_b, note_a, note_f, no_note},
     .sequence_count = 7,
     .song = nocturne_of_shadow,
     .base_color = 0x0031006f,
     .is_match = true},
    {.note_sequence = {note_d, note_f, note_d, note_a, note_f, note_d, no_note, no_note},
     .sequence_count = 6,
     .song = requiem_of_spirit,
     .base_color = 0x00002f7f,
     .is_match = true},
    {.note_sequence = {note_d2, note_a, note_d2, note_a, note_b, note_d2, no_note, no_note},
     .sequence_count = 6,
     .song = prelude_of_light,
     .base_color = 0x00202020,
     .is_match = true},
    {.note_sequence = {note_d2, note_d2, note_d2, note_d2, no_note, no_note, no_note, no_note},
     .sequence_count = 4,
     .song = dfiu,
     .base_color = 0x00320120,
     .is_match = true},     
};

uint8_t note_sequence_index = 0;
uint8_t *current_song = 0;
uint32_t current_song_base_color;

void update_oot_output();

uint64_t pwm_mode_0()
{
    pwm_set_gpio_level(PWM_LED_0_GPIO, sineLookupTable[(led_pwm_lookup_index + 0x00) % SINE_TABLE_SIZE]);
    pwm_set_gpio_level(PWM_LED_1_GPIO, sineLookupTable[(led_pwm_lookup_index + 0x55) % SINE_TABLE_SIZE]);
    pwm_set_gpio_level(PWM_LED_2_GPIO, sineLookupTable[(led_pwm_lookup_index + 0xAA) % SINE_TABLE_SIZE]);
    led_pwm_lookup_index++;

    if (!led_pwm_lookup_index)
    {
        if (pwm_delay > 250)
        {
            pwm_delay -= 50;
        }
        else
        {
#ifdef DEBUG
            printf("Ending pwm mode 0\n");
#endif
            pwm_set_gpio_level(PWM_LED_0_GPIO, 0xFFFF);
            pwm_set_gpio_level(PWM_LED_1_GPIO, 0xFFFF);
            pwm_set_gpio_level(PWM_LED_2_GPIO, 0xFFFF);
            led_pwm_lookup_index = 0;
            pwm_mode_index = (pwm_mode_index + 1) % 3;
            pwm_delay = PWM_INITIAL_DELAY_US;
            return PWM_FULL_DUTY_DELAY_US;
        }
    }

    return pwm_delay;
}

int64_t pwm_mode_1()
{
    pwm_set_gpio_level(PWM_LED_0_GPIO, chaseLookupTable[(led_pwm_lookup_index + 0x00) % 3]);
    pwm_set_gpio_level(PWM_LED_1_GPIO, chaseLookupTable[(led_pwm_lookup_index + 0x01) % 3]);
    pwm_set_gpio_level(PWM_LED_2_GPIO, chaseLookupTable[(led_pwm_lookup_index + 0x02) % 3]);
    led_pwm_lookup_index++;

    if (!(led_pwm_lookup_index % 3))
    {
        if (pwm_delay > 250)
        {
            pwm_delay -= 50;
        }
        else
        {
#ifdef DEBUG
            printf("Ending pwm mode 1\n");
#endif
            pwm_set_gpio_level(PWM_LED_0_GPIO, 0xFFFF);
            pwm_set_gpio_level(PWM_LED_1_GPIO, 0xFFFF);
            pwm_set_gpio_level(PWM_LED_2_GPIO, 0xFFFF);
            led_pwm_lookup_index = 0;
            pwm_mode_index = (pwm_mode_index + 1) % 3;
            pwm_delay = PWM_INITIAL_DELAY_US;
            return PWM_FULL_DUTY_DELAY_US;
        }
    }

    return pwm_delay * 50;
}

int64_t pwm_mode_2()
{
    pwm_set_gpio_level(PWM_LED_0_GPIO, sineLookupTable[(led_pwm_lookup_index)]);
    pwm_set_gpio_level(PWM_LED_1_GPIO, sineLookupTable[(led_pwm_lookup_index)]);
    pwm_set_gpio_level(PWM_LED_2_GPIO, sineLookupTable[(led_pwm_lookup_index)]);
    led_pwm_lookup_index++;

    if (!led_pwm_lookup_index)
    {
        if (pwm_delay > 250)
        {
            pwm_delay -= 50;
        }
        else
        {
#ifdef DEBUG
            printf("Ending pwm mode 2\n");
#endif
            pwm_set_gpio_level(PWM_LED_0_GPIO, 0xFFFF);
            pwm_set_gpio_level(PWM_LED_1_GPIO, 0xFFFF);
            pwm_set_gpio_level(PWM_LED_2_GPIO, 0xFFFF);
            pwm_mode_index = (pwm_mode_index + 1) % 3;
            pwm_delay = PWM_INITIAL_DELAY_US;
            return PWM_FULL_DUTY_DELAY_US;
        }
    }

    return pwm_delay;
}

int64_t led_pwm_cb(alarm_id_t id, void *user_data)
{
    switch (pwm_mode_index)
    {
    case 0:
        return pwm_mode_0();
    case 1:
        return pwm_mode_1();
    case 2:
        return pwm_mode_2();
    }
}

void pwm_update_timer_init()
{
    alarm_id = alarm_pool_add_alarm_in_us(core_0_alarm, pwm_delay, led_pwm_cb, 0, true);
}

uint32_t darken_color(uint32_t color, uint8_t amt)
{
    uint8_t r = (color >> 8) & 0xFF;
    uint8_t g = (color >> 16) & 0xFF;
    ;
    uint8_t b = (color >> 0) & 0xFF;
    ;

    r = (r < amt ? 0 : r - amt);
    g = (g < amt ? 0 : g - amt);
    b = (b < amt ? 0 : b - amt);

    return (uint32_t)(g << 16) + (uint32_t)(r << 8) + (uint32_t)(b << 0);
}

void rgb_led_song_update()
{
    // generate led colors from current_song_base_color

    if (song_array_cycle_count >= SONG_ARRAY_DELAY_CYCLES)
    {
        song_array_cycle_count = 0;
        for (uint8_t i = 0; i < RGB_LED_COUNT - RGB_LED_BOTTOM_COUNT; i++)
        {
            rgb_leds[i] = darken_color(current_song_base_color, (SONG_ARRAY_COLOR_STEP * (i < song_array_index_offset ? song_array_index_offset - i : i - song_array_index_offset)));
        }

        song_array_index_offset = (song_array_index_offset_dec ? song_array_index_offset - 1 : song_array_index_offset + 1);

        if (song_array_index_offset == (RGB_LED_COUNT - RGB_LED_BOTTOM_COUNT - 1))
        {
            song_array_index_offset_dec = true;
        }
        if (song_array_index_offset == 0)
        {
            song_array_index_offset_dec = false;
        }
    }

    if (song_overall_cycle_count >= SONG_OVERALL_DELAY_CYCLES)
    {
        song_overall_cycle_count = 0;
        for (uint8_t i = (RGB_LED_COUNT - RGB_LED_BOTTOM_COUNT); i < RGB_LED_COUNT; i++)
        {
            rgb_leds[i] = darken_color(current_song_base_color, song_overall_step_count);
        }

        song_overall_step_count = (song_overall_step_dec ? song_overall_step_count - SONG_OVERALL_COLOR_STEP : song_overall_step_count + SONG_OVERALL_COLOR_STEP);

        if (song_overall_step_count >= SONG_OVERALL_MAX_STEPS)
        {
            song_overall_step_dec = true;
        }

        if (song_overall_step_count == 0)
        {
            song_overall_step_dec = false;
        }
    }

    song_array_cycle_count++;
    song_overall_cycle_count++;
}

void rgb_led_nyan_update()
{
    if (nyan_cycle_count >= NYAN_DELAY_CYCLES)
    {

        uint8_t star = (get_rand_32() % NYAN_STAR_RAND_THRESH);
        for (uint8_t i = 0; i < RGB_LED_COUNT; i++)
        {
            if (i < RGB_LED_COUNT - RGB_LED_BOTTOM_COUNT)
            {
                rgb_leds[i] = nyan_colors[(i + nyan_color_index) % (NYAN_COLOR_COUNT)];
            }
            else
            {
                if (i == star)
                {
                    rgb_leds[i] = NYAN_STAR_COLOR;
                }
                else
                {
                    rgb_leds[i] = NYAN_BOTTOM_COLOR;
                }
            }
        }
        nyan_color_index++;
        nyan_cycle_count = 0;
    }

    nyan_cycle_count++;
}

void rgb_leds_error_update()
{
    if (error_show_color)
    {
        for (uint8_t i = 0; i < RGB_LED_COUNT; i++)
        {
            rgb_leds[i] = ERROR_COLOR;
        }
    }
    else
    {
        for (uint8_t i = 0; i < RGB_LED_COUNT; i++)
        {
            rgb_leds[i] = 0;
        }
    }

    if (error_cycle_count >= ERROR_DELAY_CYCLES)
    {
        error_cycle_count = 0;
        error_show_color = !error_show_color;
    }

    error_cycle_count++;
}

void rgb_leds_standby_update()
{
    if (!standby_color_flash_active)
    {
        standby_color_flash_ms = ((get_rand_32() % 10) + 1) * 1000; //((0 to 9) + 1) * 1000
        standby_current_flash_color = rgb_colors[(get_rand_32() % 6)];
        standby_color_flash_active = true;
        standby_color_flash_led_index = 0;
    }

    for (uint8_t i = 0; i < RGB_LED_COUNT; i++)
    {
        rgb_leds[i] = STANDBY_WHITE;
    }

    if (standby_color_flash_ms <= RGB_LED_UPDATE_DELAY_MS)
    {
        if (standby_color_flash_led_index == RGB_LED_COUNT)
        {
            // put the last one back to white
            rgb_leds[RGB_LED_COUNT - 1] = STANDBY_WHITE;
            standby_color_flash_active = false;
            return;
        }
        // need to also sequence the color through the LED array before getting the next timing.
        rgb_leds[standby_color_flash_led_index++] = standby_current_flash_color;
    }
    else
    {
        standby_color_flash_ms -= RGB_LED_UPDATE_DELAY_MS;
    }
}

void rgb_leds_all_off_update()
{
    for (uint8_t i = 0; i < RGB_LED_COUNT; i++)
    {
        rgb_leds[i] = 0;
    }
}

void rgb_leds_puzzle_pattern_update()
{
    rgb_leds_all_off_update();

    rgb_leds[0]  = STANDBY_WHITE;
    rgb_leds[4]  = STANDBY_WHITE;
    rgb_leds[5]  = STANDBY_WHITE;
    rgb_leds[7]  = rgb_colors[0];
    rgb_leds[9]  = rgb_colors[1];
    rgb_leds[11]  = rgb_colors[2];
    rgb_leds[13]  = rgb_colors[2];
    rgb_leds[15]  = rgb_colors[1];
    rgb_leds[17]  = rgb_colors[0];
    rgb_leds[19]  = rgb_colors[1];        
    rgb_leds[21]  = rgb_colors[0];
    rgb_leds[23]  = rgb_colors[2];
    rgb_leds[25]  = STANDBY_WHITE;

}

bool rgb_leds_update_cb(repeating_timer_t *rt)
{

    if (mode == puzzle)
    {
        if (puzzle_mode == puzzle_end)
        {
            rgb_leds_puzzle_pattern_update();
        }
        else
        {
            rgb_leds_all_off_update();
        }
    }
    else
    {
        switch (output_state)
        {
        case none:
        case play_note:
        case battery_low:
            rgb_leds_standby_update();
            break;
        case play_nyan:
            rgb_led_nyan_update();
            break;
        case play_song:
            rgb_led_song_update();
            break;
        case play_error:
            rgb_leds_error_update();
            break;
        default:
            break;
        }
    }

    if (output_state == play_note && current_note != no_note)
    {
        // clear the last n LEDs
        for (uint8_t i = 0; i < NOTE_COUNT; i++)
        {
            rgb_leds[RGB_LED_COUNT - i - 1] = 0;
        }

        // then set one for the specific note

        switch (current_note)
        {
        case note_d:
            rgb_leds[RGB_LED_COUNT - 1] = 0x00008000; // red
            break;
        case note_f:
            rgb_leds[RGB_LED_COUNT - 5] = 0x00308000; // yellow
            break;
        case note_a:
            rgb_leds[RGB_LED_COUNT - 4] = 0x00800000; // green
            break;
        case note_b:
            rgb_leds[RGB_LED_COUNT - 3] = 0x00000080; // blue
            break;
        case note_d2:
            rgb_leds[RGB_LED_COUNT - 2] = 0x00008080; // purple
            break;
        }
    }

    // TODO: Update RGB LED array and init DMA to transfer to  ws2812b PIO
    // Will need to check for mode, sample playing, and note playing
    dma_channel_configure(
        rgb_led_dma_channel_number,
        &rgb_led_dma_config,
        &pio0_hw->txf[RGB_LED_STATEMACHINE], // Write address (only need to set this once)
        rgb_leds,
        RGB_LED_COUNT,
        true);

    return true;
}

// Loads the sample data, upconverts from 22.05K to 44.1K
// Only loads the left channel with data. Leave right at 0
// Returns true if a buffer was loaded
void load_audio_buffer(uint8_t *src, uint32_t src_len, uint32_t *src_offset, audio_buff_t *buff, uint8_t volume)
{

    if (!(output_state == play_note || output_state == play_nyan || output_state == battery_low || output_state == play_error || output_state == play_song || output_state == play_puzzle))
    {
        buff->length = 0;
        return;
    }

    buff->length = (*src_offset + AUDIO_BUFF_SIZE) >= src_len ? (src_len - *src_offset) + 4 : AUDIO_BUFF_SIZE;
    uint32_t data;

    for (uint32_t i = 0; i < buff->length; i++)
    {
        int16_t s = (int16_t)(src[*src_offset + i] - 0x7F);
        data = (uint32_t)(s * volume) << 16;

        // This double load upconverts from 22.05k to 44.1K
        buff->buffer[i * 2] = data;
        buff->buffer[(i * 2) + 1] = data;
    }

    (*src_offset) += buff->length;
}

void audio_dma_start(int dma_channel, audio_buff_t *buff)
{

    dma_channel_configure(
        dma_channel,
        &audio_dma_config,
        &pio0_hw->txf[AUDIO_STATEMACHINE], // Write address (only need to set this once)
        buff->buffer,                      // Don't provide a read address yet
        buff->length * 2,                  // len is doubled because we upconverted
        true                               // Don't start yet
    );
}

void audio_dma_isr()
{

    // Clear the interrupt request.
    dma_hw->ints0 = 1u << audio_dma_channel_number;

    if (!(output_state == play_note || output_state == play_nyan || output_state == battery_low || output_state == play_error || output_state == play_song || output_state == play_puzzle))
    {
        current_song = 0; // clear the pointer
        return;
    }

    // if the buffer to be output next has a len of 0, we are done
    if (audio_buffs[current_buff_index % 2].length > 0)
    {
        // start the next output and load the next buffer
        audio_dma_start(audio_dma_channel_number, audio_buffs + (current_buff_index % 2));
    }

    current_buff_index++; // ok to wrap back to 0
    load_audio_buffer(current_sample, current_sample_len, &current_sample_offset, audio_buffs + (current_buff_index % 2), current_volume);

    // only repeat notes, nyan, and battery low
    if (audio_buffs[current_buff_index % 2].length == 0)
    {
        // repeats indef and something else stops it
        if (output_state == play_note || output_state == play_nyan || output_state == battery_low)
        { // Auto restart
            //  printf("Auto Restart\n");
            current_sample_offset = current_sample_loop_offset + 4;
            load_audio_buffer(current_sample, current_sample_len, &current_sample_offset, audio_buffs + (current_buff_index % 2), current_volume);
        }

        // Does not repeat and needs to signal end
        if (output_state == play_song || output_state == play_error || output_state == play_puzzle)
        {
            output_state = none;
            update_oot_output();
        }
    }
}

void oot_gpio_init()
{
    // init buttons as input
    uint32_t mask = (1 << NOTE_D_GPIO) | (1 << NOTE_F_GPIO) | (1 << NOTE_A_GPIO) | (1 << NOTE_B_GPIO) | (1 << NOTE_D2_GPIO) | (1 << MODE_GPIO);

    gpio_set_dir_in_masked(mask);

    gpio_set_pulls(NOTE_D_GPIO, false, true);
    gpio_set_pulls(NOTE_F_GPIO, false, true);
    gpio_set_pulls(NOTE_A_GPIO, false, true);
    gpio_set_pulls(NOTE_B_GPIO, false, true);
    gpio_set_pulls(NOTE_D2_GPIO, false, true);
    gpio_set_pulls(MODE_GPIO, false, true);

    // init pwm leds
    gpio_set_function(PWM_LED_0_GPIO, GPIO_FUNC_PWM);
    gpio_set_function(PWM_LED_1_GPIO, GPIO_FUNC_PWM);
    gpio_set_function(PWM_LED_2_GPIO, GPIO_FUNC_PWM);

    // init led mode pins as output
    gpio_init(MODE_LED_0_GPIO);
    gpio_init(MODE_LED_1_GPIO);
    gpio_set_dir(MODE_LED_0_GPIO, true);
    gpio_set_dir(MODE_LED_1_GPIO, true);
}

void alarm_pool_init()
{
    core_0_alarm = alarm_pool_create_with_unused_hardware_alarm(5);
}

void led_pwm_init()
{
    uint slice0 = pwm_gpio_to_slice_num(PWM_LED_0_GPIO);
    uint slice1 = pwm_gpio_to_slice_num(PWM_LED_1_GPIO);
    uint slice2 = pwm_gpio_to_slice_num(PWM_LED_2_GPIO);

    pwm_set_gpio_level(PWM_LED_0_GPIO, 0);
    pwm_set_gpio_level(PWM_LED_1_GPIO, 0);
    pwm_set_gpio_level(PWM_LED_2_GPIO, 0);
    pwm_set_enabled(slice0, true);
    pwm_set_enabled(slice1, true);
    pwm_set_enabled(slice2, true);

    // Trigger the timers for the pwm
    pwm_update_timer_init();
}

void vol_adc_init()
{
    adc_init();
    adc_gpio_init(ADC_VOL_GPIO);
    adc_select_input(0);
}

void rgb_leds_init()
{
    uint ws2812b_offset = pio_add_program(PIO, &ws2812b_program);
    ws2812b_program_init(PIO, RGB_LED_STATEMACHINE, RGB_LED_GPIO, ws2812b_offset);

    rgb_led_dma_channel_number = dma_claim_unused_channel(true);
    rgb_led_dma_config = dma_channel_get_default_config(rgb_led_dma_channel_number);
    channel_config_set_transfer_data_size(&rgb_led_dma_config, DMA_SIZE_32);
    channel_config_set_read_increment(&rgb_led_dma_config, true);
    channel_config_set_write_increment(&rgb_led_dma_config, false);
    channel_config_set_dreq(&rgb_led_dma_config, DREQ_PIO0_TX1);

    alarm_pool_add_repeating_timer_ms(core_0_alarm, RGB_LED_UPDATE_DELAY_MS, rgb_leds_update_cb, 0, &rgb_led_timer);
}

void audio_irq_init(int channel)
{
    dma_channel_set_irq0_enabled(channel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, audio_dma_isr);
    irq_set_enabled(DMA_IRQ_0, true);
}

void audio_init()
{
    uint i2s_offset = pio_add_program(PIO, &i2s_32_441_program);

    audio_dma_channel_number = dma_claim_unused_channel(true);
    audio_dma_config = dma_channel_get_default_config(audio_dma_channel_number);
    channel_config_set_transfer_data_size(&audio_dma_config, DMA_SIZE_32);
    channel_config_set_read_increment(&audio_dma_config, true);
    channel_config_set_write_increment(&audio_dma_config, false);
    channel_config_set_dreq(&audio_dma_config, DREQ_PIO0_TX0);

    dma_channel_set_irq0_enabled(audio_dma_channel_number, true);
    irq_set_exclusive_handler(DMA_IRQ_0, audio_dma_isr);
    i2s_32_441_program_init(PIO, AUDIO_STATEMACHINE, I2S_CLK_GPIO, I2S_DATA_GPIO, I2S_FS_GPIO, i2s_offset);

    audio_irq_init(audio_dma_channel_number);
}

void set_current_sample(uint8_t *sample)
{
    current_sample = sample;
    current_sample_len = (uint32_t)(current_sample[3] << 24) + (uint32_t)(current_sample[2] << 16) + (uint32_t)(current_sample[1] << 8) + (uint32_t)(current_sample[0] << 0);
    current_sample_loop_offset = (uint32_t)(current_sample[current_sample_len + 7] << 24) + (uint32_t)(current_sample[current_sample_len + 6] << 16) + (uint32_t)(current_sample[current_sample_len + 5] << 8) + (uint32_t)(current_sample[current_sample_len + 4] << 0);
    current_sample_offset = 4;
    current_buff_index = 0;
#ifdef DEBUG
    printf("Curr Samp len: %lu (0x%08x). Loop offset: 0x%08x\n", current_sample_len, current_sample_len, current_sample_loop_offset);
#endif
}

void check_volume()
{
    // Use averaging
    uint8_t volume;
    volume_accum += (uint16_t)(adc_read() >> 4);
    vol_avg_count++;

    if (vol_avg_count == VOL_AVG)
    {
        volume = (uint8_t)(volume_accum / VOL_AVG);
        vol_avg_count = 0;
        volume_accum = 0;

        // and also filter to 2 steps
        volume -= (volume % 2);
        volume *= MAX_VOL_SCALE;
        
        if (volume != current_volume)
        {            
            current_volume = volume;
#ifdef DEBUG
            printf("New Volume: %d. Scale: %f\n", current_volume, MAX_VOL_SCALE);
#endif
        }
    }
}

void start_sample(uint8_t *sample)
{
    set_current_sample(sample);
    load_audio_buffer(current_sample, current_sample_len, &current_sample_offset, audio_buffs, current_volume);
    audio_dma_isr();
}

void reset_sequence_matches()
{
#ifdef DEBUG
    printf("Reseting maps\n");
#endif

    note_sequence_index = 0;
    for (uint8_t i = 0; i < SONG_COUNT; i++)
    {
        map[i].is_match = true;
    }
}

void change_mode(badge_mode new_mode)
{
    reset_sequence_matches();
    // currently only two modes so just switch between them
    mode = new_mode;
#ifdef DEBUG
    printf("Changed mode. %d\n", mode);
#endif

    switch (mode)
    {
    case freeplay:
#ifdef DEBUG
        printf("freeplay\n", mode);
#endif
        gpio_put(MODE_LED_0_GPIO, true);
        gpio_put(MODE_LED_1_GPIO, false);
        break;
    case song:
#ifdef DEBUG
        printf("song\n", mode);
#endif
        gpio_put(MODE_LED_1_GPIO, true);
        gpio_put(MODE_LED_0_GPIO, false);
        break;
    case puzzle:
#ifdef DEBUG
        printf("puzzle\n", mode);
#endif
        gpio_put(MODE_LED_1_GPIO, true);
        gpio_put(MODE_LED_0_GPIO, true);
        break;
    }
}

// Update audio and RGB LED output based on current output_state
void update_oot_output()
{
#ifdef DEBUG
    printf("Update output for state: %d\n", output_state);
#endif

    switch (output_state)
    {
    case play_puzzle:
        reset_sequence_matches();
        start_sample(current_song);
        break;
    case play_nyan:
        reset_sequence_matches();
        start_sample(nyan_cat);
        break;
    case battery_low:
        reset_sequence_matches();
        start_sample(low_battery_chirp);
        break;
    case play_note:
        start_sample((uint8_t *)(note_samples[current_note]));
        break;
    case play_song:
        if (current_song)
        {
            start_sample(current_song);
        }
        break;
    case play_error:
        start_sample(error);
    default:
        break;
    }
}

// We only play one note or sample at a time so this can end any active audio
void stop_sample()
{

#ifdef DEBUG
    printf("stop sample\n");
#endif
    output_state = none;
    update_oot_output();

    dma_channel_abort(audio_dma_channel_number);
}

// Only valid for Song mode
void check_note_sequence(notes note)
{
    bool has_match = false;

    if (mode == puzzle)
    {
#ifdef DEBUG
        printf("Checking note %d for mode %d, puzzle mode %d. Index: %d\n", note, mode, puzzle_mode, note_sequence_index);
#endif

        switch (puzzle_mode)
        {
        case puzzle_start:
            has_match = note == first_puzzle_note_map[note_sequence_index++];
            if (has_match && note_sequence_index == FIRST_PUZZLE_NOTE_COUNT)
            {

                reset_sequence_matches();
                output_state = play_puzzle;
                current_note = 0;
                puzzle_mode = puzzle_mid;
                current_song = konami_hint;
                update_oot_output();
            }
            break;
        case puzzle_mid:
            has_match = note == second_puzzle_note_map[note_sequence_index++];
            if (has_match && note_sequence_index == SECOND_PUZZLE_NOTE_COUNT)
            {
                reset_sequence_matches();
                output_state = play_puzzle;
                current_note = 0;
                puzzle_mode = puzzle_end;
                current_song = game_hint;
                update_oot_output();
            }
            break;
        case puzzle_end:
            reset_sequence_matches();
            current_note = 0;
            output_state = none;
            // always default back to freeplay
            change_mode(freeplay);
            update_oot_output();
            return;
        }

        if (!has_match)
        {
#ifdef DEBUG
            printf("No match found for puzzle. Playing error\n");
#endif
            reset_sequence_matches();
            current_note = 0;
            output_state = play_error;
            // always default back to freeplay
            change_mode(freeplay);
            update_oot_output();
        }
        return;
    }

    if (mode != song)
    {
#ifdef DEBUG
        printf("sequence not checked\n");
#endif
        return;
    }

#ifdef DEBUG
    printf("sequence note played: %d\n", note);
#endif

    for (uint8_t i = 0; i < SONG_COUNT; i++)
    {
        if (map[i].note_sequence[note_sequence_index] != note)
        {

            // clear the true flag if we find a note that doesn't match
            map[i].is_match = false;
        }

        if (map[i].sequence_count - 1 == note_sequence_index && map[i].is_match)
        {
#ifdef DEBUG
            printf("Matched for map %d\n", i);
#endif
            // we found the match so reset and play the song
            reset_sequence_matches();
            current_note = 0;
            output_state = play_song;
            current_song = map[i].song;
            current_song_base_color = map[i].base_color;
            update_oot_output();
            return;
        }

        if (map[i].is_match)
        {
#ifdef DEBUG
            printf("has matched for map %d. seq index: %d\n", i, note_sequence_index);
#endif
            has_match = true;
        }
    }

    note_sequence_index++;

    if (!has_match)
    {
#ifdef DEBUG
        printf("No match found. Playing error\n");
#endif
        reset_sequence_matches();
        current_note = 0;
        output_state = play_error;
        update_oot_output();
    }
}

void begin_play_note(notes note)
{
    // don't need to do anything if we are already playing the note
    if (note == current_note)
    {
        return;
    }

    // make sure the current sample is stopped

    current_note = note;

#ifdef DEBUG
    printf("start note %d\n", note);
#endif
    output_state = play_note;
    update_oot_output();
    check_note_sequence(note);
}

void note_buttons_state_changed()
{

#ifdef DEBUG
    printf("Button State: D: %s, F: %s, A: %s, B: %s, D2: %s\n",
           (active_note_buttons[0] ? "t" : "f"),
           (active_note_buttons[1] ? "t" : "f"),
           (active_note_buttons[2] ? "t" : "f"),
           (active_note_buttons[3] ? "t" : "f"),
           (active_note_buttons[4] ? "t" : "f"));
#endif
    // Here, we can do something else based on a combo of keys pressed.
    // Right now, only nyan mode is available

    if (active_note_buttons[FIRST_NYAN_NOTE] && active_note_buttons[SECOND_NYAN_NOTE])
    {

#ifdef DEBUG
        printf("Starting Nyan\n");
#endif
        output_state = play_nyan;
        current_note = no_note;
        update_oot_output();
        return;
    }

    // This combo starts the puzzle
    if (active_note_buttons[FIRST_PUZZLE_NOTE] && active_note_buttons[SECOND_PUZZLE_NOTE] && active_note_buttons[THIRD_PUZZLE_NOTE] && current_volume > MIN_PUZZLE_VOL)
    {

#ifdef DEBUG
        printf("Starting puzzle\n");
#endif
        puzzle_mode = puzzle_start;
        change_mode(puzzle);
        output_state = play_puzzle;
        current_note = no_note;
        current_song = rick_roll_hint;
        update_oot_output();
        return;
    }

    // Select which note to play based on note priority (0 to 4)
    for (uint8_t i = 0; i < NOTE_COUNT; i++)
    {
        if (active_note_buttons[i])
        {
            begin_play_note(i);
            return;
        }
    }

    // housekeeping. clear the note
    current_note = no_note;
    // if we reached here, no note or song was active
    if (!(output_state == play_error || output_state == play_song))
    {
        stop_sample();
    }
}

// converts the pressed button GPIOs into an 8-bit mask
// that starts at bit 0 for note 0 (based on the notes enum)
uint8_t get_note_buttons()
{
    uint32_t highs = gpio_get_all();
    uint8_t buttons = 0;

    // bitwise AND the highs with a 1 shifted left by the number of the GPIO
    // This will return a single bit set if the GPIO is high. Then move that
    // bit back to pos 0 with a right-shift by the GPIO number - the note number.

    buttons |= ((highs & (1 << NOTE_D_GPIO)) >> (NOTE_D_GPIO - note_d)) | ((highs & (1 << NOTE_F_GPIO)) >> (NOTE_F_GPIO - note_f)) | ((highs & (1 << NOTE_A_GPIO)) >> (NOTE_A_GPIO - note_a)) | ((highs & (1 << NOTE_B_GPIO)) >> (NOTE_B_GPIO - note_b)) | ((highs & (1 << NOTE_D2_GPIO)) >> (NOTE_D2_GPIO - note_d2));

    return buttons;
}

void check_note_buttons()
{
    uint8_t buttons = get_note_buttons();
    uint8_t i = NOTE_COUNT;

    while (i--)
    {
        if (buttons & (1 << i))
        {
            if (!note_button_starts[i])
            {
                note_button_starts[i] = time_us_64();
            }

            if (!active_note_buttons[i] && (time_us_64() - note_button_starts[i]) > DEBOUNCE_US)
            {
                active_note_buttons[i] = true;
                note_buttons_state_changed();
            }
        }
        else
        {
            // For special modes, like nyan mode, we don't want to update
            // call button state changed until a button is pressed again
            if (active_note_buttons[i] && output_state != play_nyan && output_state != play_puzzle)
            {
                // button was active but now is not so update status
                active_note_buttons[i] = false;
                note_buttons_state_changed();
            }

            active_note_buttons[i] = false;
            note_button_starts[i] = 0;
        }
    }
}

void check_mode_button()
{
    if (gpio_get(MODE_GPIO))
    {
        if (!mode_button_start_time)
        {
            mode_button_start_time = time_us_64();
        }

        if ((time_us_64() - mode_button_start_time) > MODE_HOLD_US && output_state != battery_low)
        {

            // button has been held for hold time. Start Motorola Battery Low sample playing
            output_state = battery_low;
            update_oot_output();
        }
    }
    else // Button is not down
    {
        // a time start of 0 indicates no button press so do nothing
        if (!mode_button_start_time)
        {
            return;
        }

        // If the battery_low state is playing, turn it off
        if (output_state == battery_low)
        {
#ifdef DEBUG
            printf("mode button long hold released\n");
#endif
            output_state = none;
            update_oot_output();
        }
        else if ((time_us_64() - mode_button_start_time) > DEBOUNCE_US)
        {
            // if we get here, we were not playing the battery low sound
            // and we passed the debounce, so change modes

#ifdef DEBUG
            printf("mode button debounced\n");
#endif

            change_mode((mode + 1) % 2);

            // also stop any output because we changed mode
            output_state = none;
            update_oot_output();
        }

        // Always reset the time start
        mode_button_start_time = 0;
    }
}

int main()
{
    set_sys_clock_khz(PICO_CLK_KHZ, true);
#ifdef DEBUG
    stdio_init_all();
#endif
    alarm_pool_init();
    oot_gpio_init();
    vol_adc_init();
    led_pwm_init();
    rgb_leds_init();
    audio_init(); // make second core
    change_mode(freeplay);

#ifdef DEBUG
    sleep_ms(5000);
    printf("OOT Pico Badge Started\n");
#endif

    while (1)
    {
        check_mode_button();
        check_note_buttons();
        check_volume();
    }
}