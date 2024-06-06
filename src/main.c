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

#define DEBUG

#define ALARM_1_NUM 0
#define ALARM_1_IRQ TIMER_IRQ_0

#define ALARM_2_NUM 1
#define ALARM_2_IRQ TIMER_IRQ_1

#define SAMPLE_RATE 44100UL
#define BITS_PER_SAMPLE 32
#define SPI_CLK_DIVISOR 90
#define SPI_FREQ (SAMPLE_RATE * BITS_PER_SAMPLE)
#define PICO_CLK_KHZ 133000UL // ((SPI_FREQ * SPI_CLK_DIVISOR) / 1000)
#define AUDIO_BUFF_COUNT 2
#define AUDIO_BUFF_SIZE 1024UL // Size of max Ocarina
#define MAX_VOL 254
#define VOL_AVG 32

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

#define RGB_LEDS_GPIO 13

// TODO: remove, not in use
#define PWM_CYCLES_LONG 10000UL
#define PWM_CYCLES_MED 5000UL
#define PWM_CYCLES_SHORT 1000UL

#define DEBOUNCE_US 50000ULL
#define MODE_HOLD_US 4000000ULL

#define FIRST_NYAN_NOTE 0
#define SECOND_NYAN_NOTE 4

#define PWM_INITIAL_DELAY_US 4000ULL
#define PWM_FULL_DUTY_DELAY_US 2000000ULL
#define SINE_TABLE_SIZE 256

extern uint8_t song_of_storms[];
extern uint8_t a_loop[];
extern uint8_t b_loop[];
extern uint8_t d_loop[];
extern uint8_t d2_loop[];
extern uint8_t f_loop[];

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

typedef struct audio_buff_t
{
    uint32_t buffer[AUDIO_BUFF_SIZE * 2];
    uint32_t length;
} audio_buff_t;

typedef enum badge_mode
{
    freeplay,
    song,
} badge_mode;

typedef enum badge_output_state
{
    none,
    play_nyan,
    battery_low,
    play_note,
    play_error,
    play_song,
    extra_song
} badge_output_state;

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

badge_mode mode = freeplay;
badge_output_state output_state = none;
bool active_note_buttons[] = {false, false, false, false, false};

bool nayn_playing = false;
bool battery_low_playing = false;

audio_buff_t audio_buffs[AUDIO_BUFF_COUNT];

// Maybe create a struct
// TODO: Remove unused
uint8_t *current_sample;
uint32_t current_sample_len;
uint32_t current_sample_offset = 0;
uint8_t audio_dma_channel;
uint8_t current_volume = 0;
uint16_t volume_accum = 0;
uint8_t current_buff_index = 0;
uint16_t vol_running_avg[VOL_AVG];
uint8_t vol_avg_count = 0;

dma_channel_config audio_dma_config;

alarm_id_t alarm_id;

uint8_t led_pwm_lookup_index = 0;

alarm_pool_t *core_0_alarm;

uint64_t mode_button_start_time;
uint64_t note_button_starts[] = {0, 0, 0, 0, 0};
notes current_note = no_note;

uint32_t pwm_delay = PWM_INITIAL_DELAY_US;

uint8_t pwm_mode_index = 1;

int64_t pwm_mode_0()
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
            printf("Ending mode 0\n");
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
            printf("Ending mode 1\n");
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
            printf("Ending mode 2\n");
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
    alarm_id = alarm_pool_add_alarm_in_ms(core_0_alarm, pwm_delay, led_pwm_cb, 0, true);
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

    // init led data pins as output

    // init i2s pins as output
}

void led_pwm_init()
{
    core_0_alarm = alarm_pool_create_with_unused_hardware_alarm(5);

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

void check_volume()
{
    //Use averaging
    uint8_t volume;
    volume_accum += (uint16_t)(adc_read() >> 4);
    vol_avg_count++;

    if (vol_avg_count == VOL_AVG)
    {
        volume = (uint8_t)(volume_accum / VOL_AVG);
        vol_avg_count = 0;
        volume_accum = 0;

        //and also filter to 2 steps
        volume -= (volume % 2);

        if (volume != current_volume)
        {
            current_volume = volume;
#ifdef DEBUG
            printf("New Volume: %d\n", current_volume);
#endif
        }
    }
}

void change_mode()
{

    // currently only two modes so just switch between them
    mode = (mode + 1) % 2;
#ifdef DEBUG
    printf("Changed mode. %d\n", mode);
#endif
}

// Update audio and RGB LED output based on current output_state
void update_oot_output()
{
#ifdef DEBUG
    printf("Update output for state: %d\n", output_state);
#endif
}

// Only valid for Song mode
void record_note_played(notes note)
{
    if (mode != song)
    {
        return;
    }

#ifdef DEBUG
    printf("record note played: %d\n", note);
#endif
}

void begin_play_note(notes note)
{
    // don't need to do anything if we are already playing the note
    if (note == current_note)
    {
        return;
    }

    current_note = note;

#ifdef DEBUG
    printf("start note %d\n", note);
#endif
    output_state = play_note;
    update_oot_output();
    record_note_played(note);
}

// We only play one note or sample at a time so this can end any active audio
void stop_sample()
{

    // housekeeping. clear the note
    current_note = no_note;

#ifdef DEBUG
    printf("stop sample\n");
#endif
    output_state = none;
    update_oot_output();
}

void note_buttons_state_changed()
{

    printf("Button State: D: %s, F: %s, A: %s, B: %s, D2: %s\n",
           (active_note_buttons[0] ? "t" : "f"),
           (active_note_buttons[1] ? "t" : "f"),
           (active_note_buttons[2] ? "t" : "f"),
           (active_note_buttons[3] ? "t" : "f"),
           (active_note_buttons[4] ? "t" : "f"));

    // Here, we can do something else based on a combo of keys pressed.
    // Right now, only nyan mode is available

    if (active_note_buttons[FIRST_NYAN_NOTE] && active_note_buttons[SECOND_NYAN_NOTE])
    {
        output_state = play_nyan;
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

    // if we reached here, no note was active
    stop_sample();
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
            if (active_note_buttons[i] && output_state != play_nyan)
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

            change_mode();

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
    stdio_init_all();
    oot_gpio_init();
    vol_adc_init();
    led_pwm_init();

#ifdef DEBUG
    sleep_ms(1000);
#endif

    printf("OOT Pico Badge Started\n");

    while (1)
    {
        check_mode_button();
        check_note_buttons();
        check_volume();
    }
}