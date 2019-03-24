#include <stdio.h>
#include <string.h>
#include "chip.h"
#include "board.h"
#include "app_dualcore_cfg.h"

#define IPC_IRQ_Priority    IRQ_PRIO_IPC
#define IPC_IRQHandler M4_IRQHandler
#define ClearTXEvent   Chip_CREG_ClearM4Event
#define IPC_IRQn       M4_IRQn

static volatile uint32_t notifyEvent;

void IPC_IRQHandler(void)
{
	ClearTXEvent();
	notifyEvent = 1;
}

int main(void)
{
	uint8_t buffer[16];
	uint8_t *M4SharedBuffer;
	uint8_t *M0SharedBuffer;
	
	notifyEvent = 0;
	M4SharedBuffer = (uint8_t *) SHARED_MEM_M4;
	M0SharedBuffer = (uint8_t *) SHARED_MEM_M0;
	memset(buffer, 0, sizeof(buffer));
	
	extern void prvSetupTimerInterrupt(void);
	SystemCoreClockUpdate();
	
		// Enable IPC interrupt
	NVIC_SetPriority(IPC_IRQn, IPC_IRQ_Priority);
	NVIC_EnableIRQ(IPC_IRQn);
	
	DEBUGSTR("Starting M0 Tasks...\r\n");
	
	while (1) {
		// Guard
		while (!notifyEvent) {}
		notifyEvent = 0;
		memcpy(buffer, M4SharedBuffer, 16);
		buffer[14] = 'M';
		buffer[15] = '0';
		memcpy(M0SharedBuffer, buffer, 16);
		// Sending notify to M4
		__DSB();
		__SEV();
	}
	
	return 0;
}
