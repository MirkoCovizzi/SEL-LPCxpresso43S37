#include <stdio.h>
#include <string.h>
#include "chip.h"
#include "board.h"
#include "app_dualcore_cfg.h"

// UART 0
#define LPC_UART LPC_USART0
#define UARTx_IRQn  USART0_IRQn
#define UARTx_IRQHandler UART0_IRQHandler
STATIC RINGBUFF_T txring, rxring;

// UART DMA
#define _GPDMA_CONN_UART_Tx GPDMA_CONN_UART0_Tx
#define _GPDMA_CONN_UART_Rx GPDMA_CONN_UART0_Rx

#define IPC_IRQ_Priority    IRQ_PRIO_IPC
#define IPC_IRQHandler M0APP_IRQHandler
#define ClearTXEvent   Chip_CREG_ClearM0AppEvent
#define IPC_IRQn       M0APP_IRQn

static volatile uint32_t channelTC;
static volatile uint32_t channelTCErr;
static volatile uint32_t notifyEvent;
static FunctionalState  isDMATx = ENABLE;
static uint8_t dmaChannelNumTx, dmaChannelNumRx;

static uint8_t uartABStart[] = "Starting UART Auto-Baud - Press any key! \n\r";
static uint8_t uartABComplete[] = "UART Auto-Baudrate synchronized! \n\r";

static uint8_t welcomeMessage[] =	"Welcome to an AES and dual core embedded demo using"
									"UART protocol. Press any key to continue, ESC to exit.\r\n";
static uint8_t loopMessage[] = "Press any key to continue, ESC to exit.\r\n";
static uint8_t endMessage[] = "Demo ended.\r\n";
static uint8_t uartDMAMessage[] = "Insert string: \r\n";

// DMA interrupt handler
void DMA_IRQHandler(void)
{
	uint8_t dmaChannelNum;
	if (isDMATx) {
		dmaChannelNum = dmaChannelNumTx;
	}
	else {
		dmaChannelNum = dmaChannelNumRx;
	}
	if (Chip_GPDMA_Interrupt(LPC_GPDMA, dmaChannelNum) == SUCCESS) {
		channelTC++;
	}
	else {
		channelTCErr++;
	}
}

void UARTx_IRQHandler(void)
{
	Chip_UART_IRQRBHandler(LPC_UART, &rxring, &txring);
}

void IPC_IRQHandler(void)
{
	ClearTXEvent();
	notifyEvent = 1;
}

void MSleep(int32_t msecs)
{
	int32_t curr = (int32_t) Chip_RIT_GetCounter(LPC_RITIMER);
	int32_t final = curr + ((SystemCoreClock / 1000) * msecs);

	if (!msecs || (msecs < 0)) {
		return;
	}

	if ((final < 0) && (curr > 0)) {
		while (Chip_RIT_GetCounter(LPC_RITIMER) < (uint32_t) final) {}
	}
	else {
		while ((int32_t) Chip_RIT_GetCounter(LPC_RITIMER) < final) {}
	}
}

int main(void)
{
	FlagStatus exitflag;
	uint8_t buffer[10];
	uint8_t receiveBuffer[16];
	uint8_t M4Buffer[16];
	uint8_t M0Buffer[16];
	uint8_t *M4SharedBuffer;
	uint8_t *M0SharedBuffer;
	int len;

	SystemCoreClockUpdate();
	Board_Init();

	if (M0Image_Boot((uint32_t) M0_IMAGE_ADDR) < 0) {
		while (1) {
			__WFI();
		}
	}
	MSleep(100);
	
	// Enable IPC interrupt
	NVIC_SetPriority(IPC_IRQn, IPC_IRQ_Priority);
	NVIC_EnableIRQ(IPC_IRQn);
	
	DEBUGSTR("Starting M4 Tasks...\r\n");
	
	notifyEvent = 0;
	M4SharedBuffer = (uint8_t *) SHARED_MEM_M4;
	M0SharedBuffer = (uint8_t *) SHARED_MEM_M0;
	
	// Init GPDMA controller
	Chip_GPDMA_Init(LPC_GPDMA);
	// Setting GPDMA interrupt
	NVIC_DisableIRQ(DMA_IRQn);
	NVIC_SetPriority(DMA_IRQn, ((0x01 << 3) | 0x01));
	NVIC_EnableIRQ(DMA_IRQn);
	// Getting free DMA channels for UART communication
	dmaChannelNumTx = Chip_GPDMA_GetFreeChannel(LPC_GPDMA, _GPDMA_CONN_UART_Tx);
	dmaChannelNumRx = Chip_GPDMA_GetFreeChannel(LPC_GPDMA, _GPDMA_CONN_UART_Rx);

	// Page 1119 UM10503.pdf
	// See "uart" project example
	// Reset FIFOs, Enable FIFOs and DMA mode in UART
	Chip_UART_SetupFIFOS(LPC_UART, (UART_FCR_FIFO_EN | UART_FCR_RX_RS |
							UART_FCR_TX_RS | UART_FCR_DMAMODE_SEL | UART_FCR_TRG_LEV0));
	Chip_UART_IntEnable(LPC_UART, (UART_IER_ABEOINT | UART_IER_ABTOINT));

	// Setup UART baudrate automatically
	NVIC_SetPriority(UARTx_IRQn, 1);
	NVIC_EnableIRQ(UARTx_IRQn);
	Chip_UART_SendBlocking(LPC_UART, uartABStart, sizeof(uartABStart));
	Chip_UART_ABCmd(LPC_UART, UART_ACR_MODE0, true, ENABLE);
	while (Chip_UART_GetABEOStatus(LPC_UART) == RESET) {}
	Chip_UART_SendBlocking(LPC_UART, uartABComplete, sizeof(uartABComplete));
  NVIC_DisableIRQ(UARTx_IRQn);

	// Send welcome message to user
	Chip_UART_SendBlocking(LPC_UART, welcomeMessage, sizeof(welcomeMessage));

	exitflag = RESET;
	while (exitflag == RESET) {
		len = 0;
		while (len == 0) {
			len = Chip_UART_Read(LPC_UART, buffer, 1);
		}

		if (buffer[0] == 27) {
			// Pressed ESC key
			Chip_UART_SendBlocking(LPC_UART, endMessage, sizeof(endMessage));
			exitflag = SET;
		} else {
			isDMATx = ENABLE;
			channelTC = channelTCErr = 0;
			Chip_GPDMA_Transfer(LPC_GPDMA, dmaChannelNumTx,
								(uint32_t) &uartDMAMessage[0],
								_GPDMA_CONN_UART_Tx,
								GPDMA_TRANSFERTYPE_M2P_CONTROLLER_DMA,
								sizeof(uartDMAMessage));
			while (!channelTC) {}

			// Initialize M4Buffer with zeros
			memset(M4Buffer, 0, sizeof(M4Buffer));
				
			// Receive max 12 chars
			
			isDMATx = DISABLE;
			channelTC = channelTCErr = 0;
			Chip_GPDMA_Transfer(LPC_GPDMA, dmaChannelNumRx,
								(uint32_t) &receiveBuffer[0],
								_GPDMA_CONN_UART_Rx,
								GPDMA_TRANSFERTYPE_P2M_CONTROLLER_DMA,
								12);
			while (!channelTC) {}

			// Send receiveBuffer + "M4" to M0 through IPC
			memcpy(M4Buffer, receiveBuffer, 12);
			M4Buffer[12] = 'M';
			M4Buffer[13] = '4';
				
			memcpy(M4SharedBuffer, M4Buffer, 16);
			// Sending notify to M0
			__DSB();
			__SEV();
			
			// Guard
			while (!notifyEvent) {}
			notifyEvent = 0;
			memcpy(M0Buffer, M0SharedBuffer, 16);

			// Call AES and get encrypted message
			
			// Printing to user
			isDMATx = ENABLE;
			channelTC = channelTCErr = 0;
			Chip_GPDMA_Transfer(LPC_GPDMA, dmaChannelNumTx,
								(uint32_t) &M0Buffer[0],
								_GPDMA_CONN_UART_Tx,
								GPDMA_TRANSFERTYPE_M2P_CONTROLLER_DMA,
								sizeof(M0Buffer));
			while (!channelTC) {}
			
			Chip_UART_SendBlocking(LPC_UART, loopMessage, sizeof(loopMessage));
		}
	}
	
	// Stop GPDMA controller
	Chip_GPDMA_Stop(LPC_GPDMA, dmaChannelNumTx);
	Chip_GPDMA_Stop(LPC_GPDMA, dmaChannelNumRx);
	NVIC_DisableIRQ(DMA_IRQn);
	
	while (Chip_UART_CheckBusy(LPC_UART) == SET) {}
	Chip_UART_DeInit(LPC_UART);
	return 0;
}
