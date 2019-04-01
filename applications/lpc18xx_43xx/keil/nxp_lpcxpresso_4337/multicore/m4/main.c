#include "chip.h"
#include "board.h"

#define LPC_UART LPC_USART0
#define UARTx_IRQn  USART0_IRQn
#define UARTx_IRQHandler UART0_IRQHandler
#define _GPDMA_CONN_UART_Tx GPDMA_CONN_UART0_Tx
#define _GPDMA_CONN_UART_Rx GPDMA_CONN_UART0_Rx

STATIC RINGBUFF_T txring, rxring;

static uint8_t uartABStart[] = "Starting UART Auto-Baud - Press 'A' or 'a'! \n\r";
static uint8_t uartABComplete[] = "UART Auto-Baudrate synchronized! \n\r";
static uint8_t welcomeMessage[] = "Welcome to an AES and dual core embedded demo using UART protocol. Press 'c' to continue, ESC to exit.\r\n";
static uint8_t loopMessage[] = "Press 'c' to continue, ESC to exit.\r\n";
static uint8_t endMessage[] = "Demo ended.\r\n";
static uint8_t uartDMAMessage[] = "Insert string: \r\n";

static uint8_t dmaChannelNumTx, dmaChannelNumRx;

static volatile uint32_t channelTC;	/* Terminal Counter flag for Channel */
static volatile uint32_t channelTCErr;
static FunctionalState  isDMATx = ENABLE;

static void App_DMA_Init(void)
{
	/* Initialize GPDMA controller */
	Chip_GPDMA_Init(LPC_GPDMA);
	/* Setting GPDMA interrupt */
	NVIC_DisableIRQ(DMA_IRQn);
	NVIC_SetPriority(DMA_IRQn, ((0x01 << 3) | 0x01));
	NVIC_EnableIRQ(DMA_IRQn);
}

static void App_DMA_DeInit(void)
{
	Chip_GPDMA_Stop(LPC_GPDMA, dmaChannelNumTx);
	Chip_GPDMA_Stop(LPC_GPDMA, dmaChannelNumRx);
	NVIC_DisableIRQ(DMA_IRQn);
}

static void App_DMA(void)
{
	uint8_t receiveBuffer[16];
	int i;
	uint8_t temp;

	App_DMA_Init();
	dmaChannelNumTx = Chip_GPDMA_GetFreeChannel(LPC_GPDMA, _GPDMA_CONN_UART_Tx);

	isDMATx = ENABLE;
	channelTC = channelTCErr = 0;
	Chip_GPDMA_Transfer(LPC_GPDMA, dmaChannelNumTx,
					  (uint32_t) &uartDMAMessage[0],
					  _GPDMA_CONN_UART_Tx,
					  GPDMA_TRANSFERTYPE_M2P_CONTROLLER_DMA,
					  sizeof(uartDMAMessage));
	while (!channelTC) {}

	dmaChannelNumRx = Chip_GPDMA_GetFreeChannel(LPC_GPDMA, _GPDMA_CONN_UART_Rx);
	isDMATx = DISABLE;
	channelTC = channelTCErr = 0;
	Chip_GPDMA_Transfer(LPC_GPDMA, dmaChannelNumRx,
					  _GPDMA_CONN_UART_Rx,
					  (uint32_t) &receiveBuffer[0],
					  GPDMA_TRANSFERTYPE_P2M_CONTROLLER_DMA,
					  13);
	while (!channelTC) {}
	
	for (i = 0; i < 13; i++) {
		temp = receiveBuffer[i + 1];
		receiveBuffer[i] = temp;
	}
	
	receiveBuffer[12] = 'M';
	receiveBuffer[13] = '4';
	receiveBuffer[14] = 'M';
	receiveBuffer[15] = '0';

	isDMATx = ENABLE;
	channelTC = channelTCErr = 0;
	Chip_GPDMA_Transfer(LPC_GPDMA, dmaChannelNumTx,
					  (uint32_t) &receiveBuffer[0],
					  _GPDMA_CONN_UART_Tx,
					  GPDMA_TRANSFERTYPE_M2P_CONTROLLER_DMA,
					  16);
	while (!channelTC) {}

	App_DMA_DeInit();
}

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

int main(void)
{
	FlagStatus exitflag;
	uint8_t buffer[10];
	int ret = 0;
	int len;

	SystemCoreClockUpdate();
	Board_Init();
	Board_UART_Init(LPC_UART);

	Chip_UART_SetupFIFOS(LPC_UART, (UART_FCR_FIFO_EN | UART_FCR_RX_RS |
							UART_FCR_TX_RS | UART_FCR_DMAMODE_SEL | UART_FCR_TRG_LEV0));
	Chip_UART_IntEnable(LPC_UART, (UART_IER_ABEOINT | UART_IER_ABTOINT));
	
	NVIC_SetPriority(UARTx_IRQn, 1);
	NVIC_EnableIRQ(UARTx_IRQn);
	Chip_UART_SendBlocking(LPC_UART, uartABStart, sizeof(uartABStart));
	Chip_UART_ABCmd(LPC_UART, UART_ACR_MODE0, true, ENABLE);
	while (Chip_UART_GetABEOStatus(LPC_UART) == RESET) {}
	Chip_UART_SendBlocking(LPC_UART, uartABComplete, sizeof(uartABComplete));
  NVIC_DisableIRQ(UARTx_IRQn);

	Chip_UART_SendBlocking(LPC_UART, welcomeMessage, sizeof(welcomeMessage));

	exitflag = RESET;
	while (exitflag == RESET) {
		len = 0;
		while (len == 0) {
			len = Chip_UART_Read(LPC_UART, buffer, 1);
		}
		if (buffer[0] == 27) {
			/* ESC key, set exit flag */
			Chip_UART_SendBlocking(LPC_UART, endMessage, sizeof(endMessage));
			ret = -1;
			exitflag = SET;
		}
		else if (buffer[0] == 'c') {
			App_DMA();
			Chip_UART_SendBlocking(LPC_UART, loopMessage, sizeof(loopMessage));
		}
	}

	while (Chip_UART_CheckBusy(LPC_UART) == SET) {}

	Chip_UART_DeInit(LPC_UART);

	return ret;
}
