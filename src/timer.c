#include <zephyr/sys/printk.h>

#include <hal/nrf_ppi.h>
#include <hal/nrf_gpiote.h>
#include <hal/nrf_timer.h>

// Allocated PPI channels
// Some channels numbers are specific to timing master and node.
#define GPIOTE_PPI_CH 0
#define TIMER_CAPTURE_PPI_CH 1       // TX
#define TIMER_CLEAR_PPI_CH 1         // RX
#define RADIO_START_PPI_CH 2         // TX
#define DISABLE_TIMER_CLEAR_PPI_CH 2 // RX

#define TIMER_GPIOTE_PORT 0U
#define TIMER_GPIOTE_PIN_SELECT 2U

#define MAX_TIMER_VALUE 1000UL

// Magic value
// Is the delay in TIMER ticks from
// TX timer capture to RX receive
// From measurements should be equal to approximately 96.4 us
#define TX_CHAIN_DELAY 103UL

void timer_init()
{
    printk("timer: Enabling timer\n");

    // Reset
    NRF_TIMER4->TASKS_STOP = 1;
    NRF_TIMER4->TASKS_CLEAR = 1;

    // Prescaler = 4 => Timer frequency is equal to HFCLK / 2^4
    NRF_TIMER4->PRESCALER = 4;
    NRF_TIMER4->BITMODE = (TIMER_BITMODE_BITMODE_32Bit << TIMER_BITMODE_BITMODE_Pos);
    NRF_TIMER4->MODE = TIMER_MODE_MODE_Timer;

    NRF_TIMER4->CC[0] = MAX_TIMER_VALUE;
    NRF_TIMER4->SHORTS = TIMER_SHORTS_COMPARE0_CLEAR_Msk;

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

void timer_compensate_offset(uint32_t peer_timer)
{
    uint32_t local_timer;
    uint32_t timer_offset;

    // Tuneable value
    peer_timer += TX_CHAIN_DELAY;

    NRF_TIMER4->TASKS_CAPTURE[1] = 1;
    local_timer = NRF_TIMER4->CC[1];

    if (local_timer > peer_timer)
    {
        timer_offset = MAX_TIMER_VALUE - local_timer + peer_timer;
    }
    else
    {
        timer_offset = peer_timer - local_timer;
    }

    if (timer_offset == 0 || timer_offset == MAX_TIMER_VALUE)
    {
        // Already in sync
        return;
    }

    // PPI channel 0: clear timer when offset value is reached
    NRF_PPI->CHENCLR = (1 << TIMER_CLEAR_PPI_CH);
    NRF_PPI->CH[TIMER_CLEAR_PPI_CH].EEP = (uint32_t)&NRF_TIMER4->EVENTS_COMPARE[2];
    NRF_PPI->CH[TIMER_CLEAR_PPI_CH].TEP = (uint32_t)&NRF_TIMER4->TASKS_CLEAR;

    // PPI channel 1: disable PPI channel 0 such that the timer is only reset once.
    NRF_PPI->CHENCLR = (1 << DISABLE_TIMER_CLEAR_PPI_CH);
    NRF_PPI->CH[DISABLE_TIMER_CLEAR_PPI_CH].EEP = (uint32_t)&NRF_TIMER4->EVENTS_COMPARE[2];
    NRF_PPI->CH[DISABLE_TIMER_CLEAR_PPI_CH].TEP = (uint32_t)&NRF_PPI->TASKS_CHG[0].DIS;

    // Use PPI group for PPI channel 0 disabling
    NRF_PPI->TASKS_CHG[0].DIS = 1;
    NRF_PPI->CHG[0] = (1 << TIMER_CLEAR_PPI_CH);

    // Write offset to timer compare register
    NRF_TIMER4->CC[2] = (MAX_TIMER_VALUE - timer_offset);

    // Enable PPI channels
    NRF_PPI->CHENSET = (1 << TIMER_CLEAR_PPI_CH) | (1 << DISABLE_TIMER_CLEAR_PPI_CH);
}