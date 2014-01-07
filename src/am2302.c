#include <stdbool.h>
#include "interrupts.h"
#include "usart.h"
#include "timer.h"
#include "platform.h"
#include "hardware.h"

#define HI_BIT_THRESHOLD_TIME 100
#define VALID_DATA_START_TIME 250

static bool acquiring = false;
static int bit_position;
static int last_timestamp;

struct am2302_sensor_data
{
    uint16_t humidity;
    int16_t temperature;
};


uint64_t raw_data;

static void delay(int count)
{
    while(count--){ };
}

static void interrupt_handler()
{
    if(!acquiring)
        return;
    int timestamp = timer2_get_current_counter();
    int bit_value = (timestamp - last_timestamp) > HI_BIT_THRESHOLD_TIME;
    last_timestamp = timestamp;
    
    if (timestamp > VALID_DATA_START_TIME)
        raw_data |= (uint64_t) bit_value << bit_position++;
}

static void reset()
{
    bit_position = 0;
    last_timestamp = 0;
    raw_data = 0;
    acquiring = false;
}

static int16_t get_2complement_from_signed_magnitude(uint16_t x) 
{
    int16_t sign_mask = 0x8FFF;
    int16_t positive_part = x & sign_mask;
    bool is_negative = (~sign_mask & x);
    return is_negative ? -positive_part : positive_part;
}

static int get_int_from_bits(uint64_t bits, int offset, int size)
{
    int i;
    int j = size * 8;
    int result = 0;
    for (i = 0; i < size * 8; i++)
    {
        uint64_t bit_mask = ((uint64_t) 1 << offset + i);
        int bit = (bits & bit_mask) > 0;
        result |= bit << --j;
    }
    return result;
}

static struct am2302_sensor_data get_converted_sensor_data()
{
    struct am2302_sensor_data sdata;
    sdata.humidity = get_int_from_bits(raw_data, 0,  sizeof(int16_t));
    sdata.temperature = get_int_from_bits(raw_data, 16, sizeof(int16_t));
    sdata.temperature = get_2complement_from_signed_magnitude(sdata.temperature);

    return sdata;
}

static int calculate_checksum()
{
    int i; uint8_t checksum = 0;
    for (i = 0; i < 4; i++)
        checksum += get_int_from_bits(raw_data, i * 8, sizeof(uint8_t));
    return checksum;
}

static bool has_parity_errors()
{
    uint8_t parity = get_int_from_bits(raw_data, 32, sizeof(uint8_t));
    return parity != calculate_checksum() ;
}

void am2302_acquire()
{
    reset();
    gpio_set_pin_mode(AM2302_PIN, GPIO_MODE_OUT_PUSH_PULL);
    gpio_set_pin_low(AM2302_PIN);
    delay(35200);
    gpio_set_pin_high(AM2302_PIN);
    gpio_set_pin_mode(AM2302_PIN, GPIO_MODE_IN_FLOATING);
    
    timer2_start();
    acquiring = true;
    while(!timer2_has_finished() && bit_position < 40) { }
    acquiring = false;
    timer2_stop();
    
    if(bit_position < 40)
        usart_puts("Timeout\n");
    
    if(has_parity_errors())
        usart_puts("Invalid data\n");
    
    struct am2302_sensor_data a = get_converted_sensor_data();
    
    usart_putc('h');
    printint(a.humidity);
    usart_putc('t');
    printint(a.temperature);
    
    usart_putc('\n');
}

void am2302_init()
{
    timer2_init();
    gpio_set_interrupt_on_rising(AM2302_PIN, interrupt_handler);
}
