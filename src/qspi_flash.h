#ifndef _QSPI_FLASH_H_
#define _QSPI_FLASH_H_

#include "nrfx_qspi.h"

void QSPI_Configure_Memory(void);

nrfx_err_t QSPI_IsReady();

nrfx_err_t QSPI_WaitForReady();

nrfx_err_t QSPI_Initialise();

void QSPI_Status(char ASender[]);

void QSPI_Erase(uint32_t AStartAddress);

void QSPI_Configure_Memory(void);

#endif