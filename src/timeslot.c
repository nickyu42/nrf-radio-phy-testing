#include <mpsl_timeslot.h>
#include <mpsl.h>
#include <hal/nrf_timer.h>
#include <string.h>
#include <zephyr/sys/printk.h>

#include "radio.h"

#define TIMESLOT_LENGTH_US (MPSL_TIMESLOT_LENGTH_MAX_US)
#define TIMER_EXPIRY_US (TIMESLOT_LENGTH_US - 50)
#define TIMESLOT_REQUEST_DISTANCE_US (1000000)

static mpsl_timeslot_session_id_t session_id = 0xFFu;
static mpsl_timeslot_signal_return_param_t signal_callback_return_param;

static mpsl_timeslot_request_t timeslot_request_earliest = {
    .request_type = MPSL_TIMESLOT_REQ_TYPE_EARLIEST,
    .params.earliest.hfclk = MPSL_TIMESLOT_HFCLK_CFG_NO_GUARANTEE,
    .params.earliest.priority = MPSL_TIMESLOT_PRIORITY_NORMAL,
    .params.earliest.length_us = TIMESLOT_LENGTH_US,
    .params.earliest.timeout_us = 1000000};

static mpsl_timeslot_request_t timeslot_request_normal = {
    .request_type = MPSL_TIMESLOT_REQ_TYPE_NORMAL,
    .params.normal.hfclk = MPSL_TIMESLOT_HFCLK_CFG_NO_GUARANTEE,
    .params.normal.priority = MPSL_TIMESLOT_PRIORITY_NORMAL,
    .params.normal.distance_us = TIMESLOT_REQUEST_DISTANCE_US,
    .params.normal.length_us = TIMESLOT_LENGTH_US};

static mpsl_timeslot_signal_return_param_t *timeslot_callback(
    mpsl_timeslot_session_id_t session_id,
    uint32_t signal_type)
{
    mpsl_timeslot_signal_return_param_t *p_ret_val = NULL;

    switch (signal_type)
    {
    case MPSL_TIMESLOT_SIGNAL_START:

        printk("TIMESLOT_START\n");

        // No return action
        signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
        p_ret_val = &signal_callback_return_param;

        // Setup timer to trigger an interrupt (and thus the TIMER0
        // signal) before timeslot end. At the start of the timeslot TIMER0
        // is initialized to zero and set to run at 1MHz.
        nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0, TIMER_EXPIRY_US);
        nrf_timer_int_enable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);

        struct radio_test_config test_config;
        memset(&test_config, 0, sizeof(test_config));
        test_config.type = MODULATED_TX;
        test_config.mode = NRF_RADIO_MODE_BLE_LR125KBIT;
        test_config.params.modulated_tx.txpower = NRF_RADIO_TXPOWER_POS8DBM;
        test_config.params.modulated_tx.channel = 0;
        test_config.params.modulated_tx.pattern = TRANSMIT_PATTERN_11110000;
        radio_test_start(&test_config);

        break;

    case MPSL_TIMESLOT_SIGNAL_TIMER0:
        printk("TIMESLOT_TIMER0\n");
        nrf_timer_int_disable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);
        nrf_timer_event_clear(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE0);

        signal_callback_return_param.params.request.p_next =
            &timeslot_request_normal;
        signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_REQUEST;
        // signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_END;
        p_ret_val = &signal_callback_return_param;

        radio_test_cancel();

        break;

    case MPSL_TIMESLOT_SIGNAL_SESSION_IDLE:
        break;
    case MPSL_TIMESLOT_SIGNAL_SESSION_CLOSED:
        printk("TIMESLOT_CLOSED\n");
        break;

    case MPSL_TIMESLOT_SIGNAL_CANCELLED:
        // Start a new request on cancel
        int32_t err = mpsl_timeslot_request(
            session_id,
            &timeslot_request_earliest);
        if (err)
        {
            printk("Timeslot request error: %d", err);
        }

        break;

    default:
        printk("unexpected signal: %u", signal_type);
        // k_oops();
        break;
    }

    return p_ret_val;
}

int start_radio_timeslot(bool is_tx)
{
    int err;
    printk("Timeslot start\n");

    __disable_irq();

    err = mpsl_timeslot_session_open(
        timeslot_callback,
        &session_id);
    if (err)
    {
        printk("Timeslot session open error: %d", err);
        // k_oops();
    }

    err = mpsl_timeslot_request(
        session_id,
        &timeslot_request_earliest);
    if (err)
    {
        printk("Timeslot request error: %d", err);
    }

    __enable_irq();
}