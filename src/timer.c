#include <zephyr/sys/printk.h>

#include <hal/nrf_ppi.h>
#include <hal/nrf_gpiote.h>
#include <hal/nrf_timer.h>

// Allocated PPI channels
#define GPIOTE_PPI_CH 0
#define TIMER_CAPTURE_PPI_CH 1
#define RADIO_START_PPI_CH 2

#define TIMER_GPIOTE_PORT 0U
#define TIMER_GPIOTE_PIN_SELECT 2U

void timer_init()
{
    printk("timer: Enabling timer\n");

    // Reset
    NRF_TIMER4->TASKS_STOP = 1;
    NRF_TIMER4->TASKS_CLEAR = 1;

    // Prescaler = 0 => Timer frequency is equal to HFCLK
    NRF_TIMER4->PRESCALER = 4;
    NRF_TIMER4->BITMODE = (TIMER_BITMODE_BITMODE_32Bit << TIMER_BITMODE_BITMODE_Pos);
    NRF_TIMER4->MODE = TIMER_MODE_MODE_Timer;

    NRF_TIMER4->CC[0] = 0;

    NRF_TIMER4->TASKS_START = TIMER_TASKS_START_TASKS_START_Trigger;
}

void timer_connect_to_gpiote()
{
    // Configure p0.02 as toggle task
    NRF_GPIOTE->CONFIG[0] =
        (GPIOTE_CONFIG_POLARITY_Toggle << GPIOTE_CONFIG_POLARITY_Pos) |
        (GPIOTE_CONFIG_MODE_Task << GPIOTE_CONFIG_MODE_Pos) |
        (TIMER_GPIOTE_PORT << GPIOTE_CONFIG_PORT_Pos) |
        (TIMER_GPIOTE_PIN_SELECT << GPIOTE_CONFIG_PSEL_Pos) |
        (GPIOTE_CONFIG_OUTINIT_High << GPIOTE_CONFIG_OUTINIT_Pos);

    // Set PPI channel 0 to trigger on TIMER0 compare event
    // GPIO p0.02 will then be toggled
    NRF_PPI->CH[GPIOTE_PPI_CH].EEP = (uint32_t)&NRF_TIMER4->EVENTS_COMPARE[0];
    NRF_PPI->CH[GPIOTE_PPI_CH].TEP = (uint32_t)&NRF_GPIOTE->TASKS_OUT[0];

    // Enable channel 0
    NRF_PPI->CHENSET = (1 << GPIOTE_PPI_CH);
}

void timer_prime_radio_startup()
{
    // TIMER3 is used to signal capturing the clock and starting the radio
    // TIMER4 is the clock source

    NRF_PPI->CH[TIMER_CAPTURE_PPI_CH].EEP = (uint32_t)&NRF_TIMER3->EVENTS_COMPARE[0];
    NRF_PPI->CH[TIMER_CAPTURE_PPI_CH].TEP = (uint32_t)&NRF_TIMER4->TASKS_CAPTURE[1];
    NRF_PPI->CHENSET = (1 << TIMER_CAPTURE_PPI_CH);

    NRF_PPI->CH[RADIO_START_PPI_CH].EEP = (uint32_t)&NRF_TIMER3->EVENTS_COMPARE[1];
    NRF_PPI->CH[RADIO_START_PPI_CH].TEP = (uint32_t)&NRF_RADIO->TASKS_START;
    NRF_PPI->CHENSET = (1 << RADIO_START_PPI_CH);

    // 16MHz / 2^4 = 1MHz = 1us per tick
    NRF_TIMER3->PRESCALER = 4; // 1us resolution
    NRF_TIMER3->MODE = TIMER_MODE_MODE_Timer << TIMER_MODE_MODE_Pos;
    NRF_TIMER3->SHORTS = TIMER_SHORTS_COMPARE1_STOP_Msk | TIMER_SHORTS_COMPARE1_CLEAR_Msk;
    NRF_TIMER3->TASKS_STOP = 1;
    NRF_TIMER3->TASKS_CLEAR = 1;
    NRF_TIMER3->CC[0] = 40; // Matches 40 us radio rampup time
    NRF_TIMER3->CC[1] = 50; // Margin for timer readout

    NRF_TIMER3->EVENTS_COMPARE[0] = 0;
    NRF_TIMER3->EVENTS_COMPARE[1] = 0;
}