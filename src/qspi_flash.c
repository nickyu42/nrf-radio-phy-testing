// Adapted from https://wiki.seeedstudio.com/xiao-ble-qspi-flash-usage/

#include "qspi_flash.h"

#include <stdbool.h>
#include <nrfx_qspi.h>
#include <stdint.h>

#include <zephyr/sys/printk.h>

// sdk_config definitions
#define NRFX_QSPI_CONFIG_XIP_OFFSET 0
#define NRFX_QSPI_CONFIG_IRQ_PRIORITY 7
#define NRFX_QSPI_CONFIG_ADDRMODE 0
#define NRFX_QSPI_CONFIG_MODE 0

#define QSPI_STD_CMD_WRSR 0x01
#define QSPI_STD_CMD_RSTEN 0x66
#define QSPI_STD_CMD_RST 0x99
#define QSPI_DPM_ENTER 0x0003 // 3 x 256 x 62.5ns = 48ms
#define QSPI_DPM_EXIT 0x0003

static uint32_t *QSPI_Status_Ptr = (uint32_t *)0x40029604; // Setup for the SEEED XIAO BLE - nRF52840
static nrfx_qspi_config_t QSPIConfig;
static nrf_qspi_cinstr_conf_t QSPICinstr_cfg;
// static const uint32_t MemToUse = 64 * 1024; // Alter this to create larger read writes, 64Kb is the size of the Erase
static bool Debug_On = true;
// static uint16_t pBuf[32768] = {0}; // 16bit used as that is what this memory is going to be used for
// static uint32_t *BufMem = (uint32_t *)&pBuf;
static bool QSPIWait = false;

void QSPI_Status(char ASender[])
{ // Prints the QSPI Status
    printk("(");
    // printk("", ASender);
    printk(") QSPI is busy/idle ... Result = ");
    printk("%x\n", nrfx_qspi_mem_busy_check() & 8);
    printk("(");
    // printk(ASender);
    printk(") QSPI Status flag = 0x");
    printk("%x", NRF_QSPI->STATUS);
    printk(" (from NRF_QSPI) or 0x");
    printk("%x", *QSPI_Status_Ptr);
    printk(" (from *QSPI_Status_Ptr)\n");
}

static void QSPI_PrintData(uint16_t *AnAddress, uint32_t AnAmount)
{
    uint32_t i;

    printk("Data :");
    for (i = 0; i < AnAmount; i++)
    {
        printk(" 0x%x", *(AnAddress + i));
    }
    printk("\n");
}

nrfx_err_t QSPI_IsReady()
{
    if (((*QSPI_Status_Ptr & 8) == 8) && (*QSPI_Status_Ptr & 0x01000000) == 0)
    {
        return NRFX_SUCCESS;
    }
    else
    {
        return NRFX_ERROR_BUSY;
    }
}

nrfx_err_t QSPI_WaitForReady()
{
    while (QSPI_IsReady() == NRFX_ERROR_BUSY)
    {
        printk("busy\n");
        // if (Debug_On)
        // {
        //     printk("*QSPI_Status_Ptr & 8 = ");
        //     printk("%x", *QSPI_Status_Ptr & 8);
        //     printk(", *QSPI_Status_Ptr & 0x01000000 = 0x");
        //     printk("%x", *QSPI_Status_Ptr & 0x01000000);
        //     QSPI_Status("QSPI_WaitForReady");
        // }
    }
    return NRFX_SUCCESS;
}

nrfx_err_t QSPI_Initialise()
{
    // Initialises the QSPI and NRF LOG
    uint32_t Error_Code;

    // QSPI Config
    QSPIConfig.xip_offset = NRFX_QSPI_CONFIG_XIP_OFFSET;
    QSPIConfig.pins = (nrf_qspi_pins_t){
        // Setup for the SEEED XIAO BLE - nRF52840
        .sck_pin = 21,
        .csn_pin = 25,
        .io0_pin = 20,
        .io1_pin = 24,
        .io2_pin = 22,
        .io3_pin = 23,
    };
    QSPIConfig.irq_priority = (uint8_t)NRFX_QSPI_CONFIG_IRQ_PRIORITY;
    QSPIConfig.prot_if = (nrf_qspi_prot_conf_t){
        // .readoc     = (nrf_qspi_readoc_t)NRFX_QSPI_CONFIG_READOC,
        .readoc = (nrf_qspi_readoc_t)NRF_QSPI_READOC_READ4O,
        // .writeoc    = (nrf_qspi_writeoc_t)NRFX_QSPI_CONFIG_WRITEOC,
        .writeoc = (nrf_qspi_writeoc_t)NRF_QSPI_WRITEOC_PP4O,
        .addrmode = (nrf_qspi_addrmode_t)NRFX_QSPI_CONFIG_ADDRMODE,
        .dpmconfig = false,
    };
    QSPIConfig.phy_if.sck_freq = (nrf_qspi_frequency_t)NRF_QSPI_FREQ_32MDIV1; // I had to do it this way as it complained about nrf_qspi_phy_conf_t not being visible
    // QSPIConfig.phy_if.sck_freq   = (nrf_qspi_frequency_t)NRFX_QSPI_CONFIG_FREQUENCY;
    QSPIConfig.phy_if.spi_mode = (nrf_qspi_spi_mode_t)NRFX_QSPI_CONFIG_MODE;
    QSPIConfig.phy_if.dpmen = false;
    // QSPI Config Complete
    // Setup QSPI to allow for DPM but with it turned off
    QSPIConfig.prot_if.dpmconfig = true;
    // NRF_QSPI->DPMDUR = (QSPI_DPM_ENTER << 16) | QSPI_DPM_EXIT; // Found this on the Nordic Q&A pages, Sets the Deep power-down mode timer

    printk("Starting\n");

    Error_Code = 1;
    while (Error_Code != 0)
    {
        Error_Code = nrfx_qspi_init(&QSPIConfig, NULL, NULL);
        if (Error_Code != NRFX_SUCCESS)
        {
            if (Debug_On)
            {
                printk("(QSPI_Initialise) nrfx_qspi_init returned : %u\n", Error_Code);
            }
        }
        else
        {
            if (Debug_On)
            {
                printk("(QSPI_Initialise) nrfx_qspi_init successful\n");
            }
        }
    }
    QSPI_Status("QSPI_Initialise (Before QSPI_Configure_Memory)");
    QSPI_Configure_Memory();
    if (Debug_On)
    {
        printk("(QSPI_Initialise) Wait for QSPI to be ready ...\n");
    }
    NRF_QSPI->TASKS_ACTIVATE = 1;
    QSPI_WaitForReady();
    if (Debug_On)
    {
        printk("(QSPI_Initialise) QSPI is ready\n");
    }
    return QSPI_IsReady();
}

void QSPI_Erase(uint32_t AStartAddress)
{
    bool QSPIReady = false;
    bool AlreadyPrinted = false;

    if (Debug_On)
    {
        printk("(QSPI_Erase) Erasing memory\n");
    }
    while (!QSPIReady)
    {
        if (QSPI_IsReady() != NRFX_SUCCESS)
        {
            if (!AlreadyPrinted)
            {
                QSPI_Status("QSPI_Erase (Waiting)");
                AlreadyPrinted = true;
            }
        }
        else
        {
            QSPIReady = true;
            QSPI_Status("QSPI_Erase (Waiting Loop Breakout)");
        }
    }
    if (Debug_On)
    {
        QSPI_Status("QSPI_Erase (Finished Waiting)");
        // TimeTaken = millis();
    }
    if (nrfx_qspi_erase(NRF_QSPI_ERASE_LEN_64KB, AStartAddress) != NRFX_SUCCESS)
    {
        if (Debug_On)
        {
            printk("(QSPI_Initialise_Page) QSPI Address 0x");
            printk("%x", AStartAddress);
            printk(" failed to erase!\n");
        }
    }
    else
    {
        // if (Debug_On)
        // {
        // TimeTaken = millis() - TimeTaken;
        // printk("(QSPI_Initialise_Page) QSPI took ");
        // printk(TimeTaken);
        // printkln("ms to erase a 64Kb page");
        // }
    }
}

void QSPI_Configure_Memory(void)
{
    // uint8_t  temporary = 0x40;
    uint8_t temporary[] = {0x00, 0x02};
    // uint32_t Error_Code;

    QSPICinstr_cfg = (nrf_qspi_cinstr_conf_t){
        .opcode = QSPI_STD_CMD_RSTEN,
        .length = NRF_QSPI_CINSTR_LEN_1B,
        .io2_level = true,
        .io3_level = true,
        .wipwait = QSPIWait,
        .wren = true};
    QSPI_WaitForReady();
    if (nrfx_qspi_cinstr_xfer(&QSPICinstr_cfg, NULL, NULL) != NRFX_SUCCESS)
    { // Send reset enable
        if (Debug_On)
        {
            printk("(QSPI_Configure_Memory) QSPI 'Send reset enable' failed!\n");
        }
    }
    else
    {
        QSPICinstr_cfg.opcode = QSPI_STD_CMD_RST;
        QSPI_WaitForReady();
        if (nrfx_qspi_cinstr_xfer(&QSPICinstr_cfg, NULL, NULL) != NRFX_SUCCESS)
        { // Send reset command
            if (Debug_On)
            {
                printk("(QSPI_Configure_Memory) QSPI Reset failed!\n");
            }
        }
        else
        {
            QSPICinstr_cfg.opcode = QSPI_STD_CMD_WRSR;
            QSPICinstr_cfg.length = NRF_QSPI_CINSTR_LEN_3B;
            QSPI_WaitForReady();
            if (nrfx_qspi_cinstr_xfer(&QSPICinstr_cfg, &temporary, NULL) != NRFX_SUCCESS)
            { // Switch to qspi mode
                if (Debug_On)
                {
                    printk("(QSPI_Configure_Memory) QSPI failed to switch to QSPI mode!\n");
                }
            }
            else
            {
                QSPI_Status("QSPI_Configure_Memory");
            }
        }
    }
}