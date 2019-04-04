#include <string.h>
#include "chip.h"
#include "board.h"
#include "app_multicore_cfg.h"
#include "ipc_example.h"
#include "ipc_msg.h"

#define SHARED_MEM_IPC      0x10088000
#define IPC_IRQ_Priority    7
#define IPC_IRQn       M4_IRQn
static volatile int notify = 0;

void M4_IRQHandler(void)
{
	int i;
	LPC_CREG->M4TXEVENT = 0;
	Board_LED_Set(0, 1);
	for (i = 0; i < 500000; i++) {}
	notify = 1;
}

int main(void)
{
	uint8_t *ipcBuffer = (uint8_t *) SHARED_MEM_IPC;
	uint8_t receiveBuffer[16];
	
	SystemCoreClockUpdate();
	NVIC_SetPriority(IPC_IRQn, IPC_IRQ_Priority);
	NVIC_EnableIRQ(IPC_IRQn);
	
	while(1) {
			while(!notify) {}
			notify = 0;
		
			memcpy(receiveBuffer, ipcBuffer, 16);

			receiveBuffer[14] = 'M';
			receiveBuffer[15] = '0';
		
			memcpy(ipcBuffer, receiveBuffer, 16);
		
			__DSB();
			__SEV();
	}
}
