#include <string.h>

#include <mpsl.h>
#include <mpsl_timeslot.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_radio.h>
#include <hal/nrf_gpio.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "radio.h"
#include "timer.h"

#define TIMESLOT_LENGTH_US 200UL
#define TIMER_EXPIRY_US (TIMESLOT_LENGTH_US - 50)

#define TIMESLOT_LENGTH_US_RX 500UL
#define TIMER_EXPIRY_US_RX (TIMESLOT_LENGTH_US_RX - 50)

#define TIMESLOT_REQUEST_DISTANCE_US (1000)
#define TIMESLOT_REQUEST_DISTANCE_US_RX (1200)

static mpsl_timeslot_session_id_t session_id = 0xFFu;
static mpsl_timeslot_signal_return_param_t signal_callback_return_param;

static bool timeslot_is_tx = true;

static enum ts_state_t {
    DESYNCED = 0,
    RECEIVED = 1,
    SYNCED = 2,
};

static enum ts_state_t ts_state = DESYNCED;

static enum mpsl_timeslot_call {
    START_TIMESLOT,
    STOP_TIMESLOT,
};

K_MSGQ_DEFINE(mpsl_api_msgq, sizeof(enum mpsl_timeslot_call), 3, 4);

static mpsl_timeslot_request_t timeslot_request_earliest = {
    .request_type = MPSL_TIMESLOT_REQ_TYPE_EARLIEST,
    .params.earliest.hfclk = MPSL_TIMESLOT_HFCLK_CFG_XTAL_GUARANTEED,
    .params.earliest.priority = MPSL_TIMESLOT_PRIORITY_HIGH,
    .params.earliest.length_us = TIMESLOT_LENGTH_US_RX,
    .params.earliest.timeout_us = 1000000};

static mpsl_timeslot_request_t timeslot_request_normal = {
    .request_type = MPSL_TIMESLOT_REQ_TYPE_NORMAL,
    .params.normal.hfclk = MPSL_TIMESLOT_HFCLK_CFG_XTAL_GUARANTEED,
    .params.normal.priority = MPSL_TIMESLOT_PRIORITY_HIGH,
    .params.normal.distance_us = TIMESLOT_REQUEST_DISTANCE_US,
    .params.normal.length_us = TIMESLOT_LENGTH_US};

typedef struct
{
    int32_t timer_val;
} sync_pkt_t;

static sync_pkt_t sync_pkt;

static void timeslot_radio_config()
{
    // RF PHY
    NRF_RADIO->MODE = RADIO_MODE_MODE_Ble_1Mbit << RADIO_MODE_MODE_Pos;

    // Fast startup mode
    NRF_RADIO->MODECNF0 =
        (RADIO_MODECNF0_RU_Fast << RADIO_MODECNF0_RU_Pos) |
        (RADIO_MODECNF0_DTX_Center << RADIO_MODECNF0_DTX_Pos);

    // CRC configuration
    NRF_RADIO->CRCCNF =
        (RADIO_CRCCNF_LEN_Disabled << RADIO_CRCCNF_LEN_Pos) |
        (RADIO_CRCCNF_SKIPADDR_Include << RADIO_CRCCNF_SKIPADDR_Pos);

    // Packet format
    NRF_RADIO->PCNF0 = (0 << RADIO_PCNF0_S0LEN_Pos) | (0 << RADIO_PCNF0_LFLEN_Pos) | (0 << RADIO_PCNF0_S1LEN_Pos);
    NRF_RADIO->PCNF1 = (RADIO_PCNF1_WHITEEN_Disabled << RADIO_PCNF1_WHITEEN_Pos) |
                       (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos) |
                       (4 << RADIO_PCNF1_BALEN_Pos) |
                       (sizeof(sync_pkt_t) << RADIO_PCNF1_STATLEN_Pos) |
                       (sizeof(sync_pkt_t) << RADIO_PCNF1_MAXLEN_Pos);
    NRF_RADIO->PACKETPTR = (uint32_t)&sync_pkt;

    // Radio address config
    NRF_RADIO->PREFIX0 = 0x6A;
    NRF_RADIO->BASE0 = 0x58FE811B;

    NRF_RADIO->TXADDRESS = 0;
    NRF_RADIO->RXADDRESSES = (1 << 0);

    NRF_RADIO->FREQUENCY = 2450;
    NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_Pos4dBm << RADIO_TXPOWER_TXPOWER_Pos;

    NRF_RADIO->EVENTS_END = 0;

    NRF_RADIO->INTENCLR = 0xFFFFFFFF;
    NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;

    // Only send a single packet
    NRF_RADIO->SHORTS = RADIO_SHORTS_END_DISABLE_Msk;

    NVIC_EnableIRQ(RADIO_IRQn);
}

static mpsl_timeslot_signal_return_param_t *timeslot_callback(
    mpsl_timeslot_session_id_t session_id,
    uint32_t signal_type)
{
    mpsl_timeslot_signal_return_param_t *ts_ret_val = NULL;

    switch (signal_type)
    {
    case MPSL_TIMESLOT_SIGNAL_START:
        nrf_gpio_pin_set(3);

        signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
        ts_ret_val = &signal_callback_return_param;

        // Setup timer to trigger an interrupt (and thus the TIMER0
        // signal) before timeslot end. At the start of the timeslot TIMER0
        // is initialized to zero and set to run at 1MHz.
        if (timeslot_is_tx)
        {
            nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0, TIMER_EXPIRY_US);
        }
        else
        {
            nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0, 400UL);
        }

        nrf_timer_int_enable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);

        timeslot_radio_config();

        if (timeslot_is_tx)
        {
            // Sequence of events
            // timer3 ensures that timer4 ("clock") is captured at the 40us mark
            // at the 50us mark the radio is instructed to start transmitting
            timer_prime_radio_startup();
            NRF_RADIO->TASKS_TXEN = 1;
            NRF_TIMER3->TASKS_START = 1;

            // Wait for timer to trigger
            while (NRF_TIMER3->EVENTS_COMPARE[0] == 0)
            {
                __NOP();
            }

            // Capture "clock" value
            sync_pkt.timer_val = NRF_TIMER4->CC[1];
        }
        else
        {
            // In the rx case, keep listening for packets
            NRF_RADIO->SHORTS =
                RADIO_SHORTS_READY_START_Msk |
                RADIO_SHORTS_END_DISABLE_Msk;

            // Start listening
            NRF_RADIO->TASKS_RXEN = 1;
        }

        break;

    case MPSL_TIMESLOT_SIGNAL_RADIO:
        signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
        ts_ret_val = &signal_callback_return_param;

        // Timing critical part

        if (!timeslot_is_tx)
        {
            // printk("RADIO: %u\n", sync_pkt.timer_val);
        }

        // HACK: even though radio is DISABLED after the initial packet
        //  this timeslot callback keeps getting called, implying an interrupt
        //  is pending. But no RADIO interrupt should be pending, since RADIO
        //  should only interrupt on END event, not sure why this happens.
        //  Disable RADIO interrupt entirely, since it won't be needed after
        //  this anyway.
        NRF_RADIO->INTENCLR = 0xFFFFFFFF;
        NRF_RADIO->TASKS_DISABLE = 1;

        NVIC_ClearPendingIRQ(RADIO_IRQn);
        NVIC_DisableIRQ(RADIO_IRQn);

        break;

    case MPSL_TIMESLOT_SIGNAL_TIMER0:
        NRF_RADIO->INTENCLR = 0xFFFFFFFF;
        NRF_RADIO->TASKS_DISABLE = 1;

        NVIC_ClearPendingIRQ(RADIO_IRQn);
        NVIC_DisableIRQ(RADIO_IRQn);

        nrf_timer_int_disable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);
        nrf_timer_event_clear(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE0);

        if (timeslot_is_tx)
        {
            timeslot_request_normal.params.normal.length_us = TIMESLOT_LENGTH_US;
        }
        else
        {
            timeslot_request_normal.params.normal.length_us = TIMESLOT_LENGTH_US_RX;
        }

        signal_callback_return_param.params.request.p_next = &timeslot_request_normal;
        signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_REQUEST;
        ts_ret_val = &signal_callback_return_param;

        nrf_gpio_pin_clear(3);

        break;

    case MPSL_TIMESLOT_SIGNAL_SESSION_IDLE:
        break;

    case MPSL_TIMESLOT_SIGNAL_SESSION_CLOSED:
        printk("TIMESLOT_CLOSED\n");
        break;

    case MPSL_TIMESLOT_SIGNAL_CANCELLED:
        printk("TIMESLOT_CANCELLED\n");
        break;

    default:
        printk("unexpected signal: %u", signal_type);
        break;
    }

    return ts_ret_val;
}

int start_radio_timeslot()
{
    int err;

    // TEMP: Use device ID to identify TX and RX
    timeslot_is_tx = NRF_FICR->DEVICEID[0] == 0x6e7418a8;

    if (timeslot_is_tx)
    {
        printk("I am TX\n");
    }
    else
    {
        printk("I am RX\n");
    }

    // Set p0.03 as output
    nrf_gpio_cfg_output(3);

    enum mpsl_timeslot_call api_call = START_TIMESLOT;
    err = k_msgq_put(&mpsl_api_msgq, &api_call, K_FOREVER);
    if (err)
    {
        printk("Message sent error: %d", err);
        return 1;
    }

    return 0;
}

static void mpsl_nonpreemptible_thread(void)
{
    int err;
    enum mpsl_timeslot_call api_call = 0;

    while (1)
    {
        if (k_msgq_get(&mpsl_api_msgq, &api_call, K_FOREVER) == 0)
        {
            switch (api_call)
            {
            case START_TIMESLOT:
                err = mpsl_timeslot_session_open(timeslot_callback, &session_id);
                if (err)
                {
                    printk("Timeslot session open error: %d", err);
                }

                err = mpsl_timeslot_request(session_id, &timeslot_request_earliest);
                if (err)
                {
                    printk("Timeslot request error: %d", err);
                }
                break;

            case STOP_TIMESLOT:
                err = mpsl_timeslot_session_close(session_id);
                if (err)
                {
                    printk("Timeslot request error: %d", err);
                }
                break;

            default:
                break;
            }
        }
    }
}

K_THREAD_DEFINE(mpsl_nonpreemptible_thread_id, CONFIG_MAIN_STACK_SIZE,
                mpsl_nonpreemptible_thread, NULL, NULL, NULL,
                K_PRIO_COOP(CONFIG_MPSL_THREAD_COOP_PRIO), 0, 0);
