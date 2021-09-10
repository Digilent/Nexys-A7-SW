/******************************************************************************
* Copyright (C) 2010 - 2021 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

/*****************************************************************************/
/**
*
* @file helloworld.c
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "xil_printf.h"
#include "xaxidma.h"
#include "xparameters.h"
#include "xstatus.h"
#include "sleep.h"
#include "xgpio.h"
#include "xil_cache.h"
#include "xuartlite.h"
#include "math.h"
#include "stdio.h"
#define VERBOSE 0

/************************** Constant Definitions *****************************/
#define DDR_BASE_ADDR		XPAR_MIG7SERIES_0_BASEADDR
#define MEM_BASE_ADDR		(DDR_BASE_ADDR + 0x1000000)

/**************************** Type Definitions *******************************/


/**
 * The functioning modes of the demo
 *
 * DEMO_MODE_PAUSED - the previous mode is stopped and now the application is waiting for other command
 * DEMO_MODE_HW_TONE_GEN - was not implemented
 * DEMO_MODE_SW_TONE_GEN - a sin wave created in software is send to the audio port through DMA
 * DEMO_MODE_RECV_WAV_FILE - the application is waiting for a Wav file to be send through the UART port. After it is interpreted the sound
 * 								is played.
 * DEMO_MODE_PLAY_WAV_FILE - plays the Wav file stored in the memory. ( play_wav_file mode should be used only after the application
 * 								has entered DEMO_MODE_RECV_WAV_FILE at least once).
 */
typedef enum DemoMode {
	DEMO_MODE_PAUSED = 0,
	DEMO_MODE_HW_TONE_GEN,
	DEMO_MODE_SW_TONE_GEN,
	DEMO_MODE_RECV_WAV_FILE,
	DEMO_MODE_PLAY_WAV_FILE
} DemoMode;


/**
 * Definition of the I/O ports and DMA used in this demo
 *
 * 1 DMA
 * 1 GPIO input port
 * 1 GPIO output port
 * 1 UART
 * 1 the mode that is running
 *
*/
typedef struct Demo {
	XAxiDma dma_inst;
	XGpio gpio_out_inst;
	XGpio gpio_in_inst;
	XUartLite uart_inst;
	DemoMode mode;
} Demo;

/**
 * Definition of the Input ports
 *
 * buttons - (having definition for both positive and negative edge)
 * switches - (having definition for both positive and negative edge)
 *
 *
*/
typedef struct GpioIn_Data {
	u8 buttons;
	u16 switches;
	u8 button_pe;
	u8 button_ne;
	u16 switch_pe;
	u16 switch_ne;
} GpioIn_Data;
/**
 * Definition of the Header Wav file interpreted as 3 separate struct data structures.
 *
 * 1 for the main header of Wav file
 * 1 for the following Format part
 * 1 for the data header
 *
*/

typedef struct {
	u8 riff[4];
	u8 overall_size[4]; // u32
	u8 wave[4];
} Wav_HeaderRaw;
typedef struct {
	u8 fmt_chunk_marker[4];
	u8 fmt_chunk_size[4]; // u32
	u8 format_type[2]; // u16
	u8 channels[2]; // u16
	u8 sample_rate[4]; // u32
	u8 byte_rate[4]; // u32
	u8 block_align[2]; // u16
	u8 bits_per_sample[2]; // u16
} Wav_FormatRaw;
typedef struct {
	u8 data_chunk_header[4];
	u8 data_chunk_size[4]; // u32
} Wav_DataRaw;

/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/

const u32 max_file_size = 0x7FFFFF; // 16.777 MB
u8 *file;

/*****************************************************************************/
/**
 * This function initializes a DMA engine.
 *
 * @param	p_dma_inst is a pointer to the device instance to be
 *		worked on.
 * @param	dma_device_id is a integer that stores the ID of the DMA, and is the unique device ID of the device to lookup for.
 *
 *	It uses the ID to look up for DMA and configure it and then it will initialize the DMA driver. After initialization
 *	the driver is tested for Scatter Father mode and then all interrupts would be disabled for both channels.
 *
 * @return
 * 		- XST_SUCCESS for successful initialization
 * 		- XST_FAILURE otherwise
 *
 *****************************************************************************/
XStatus init_dma (XAxiDma *p_dma_inst, int dma_device_id) {
// Local variables
	XStatus status = 0;
	XAxiDma_Config* cfg_ptr;

	// Look up hardware configuration for device
	cfg_ptr = XAxiDma_LookupConfig(dma_device_id);
	if (!cfg_ptr)
	{
		xil_printf("ERROR! No hardware configuration found for AXI DMA with device id %d.\r\n", dma_device_id);
		return XST_FAILURE;
	}

	// Initialize driver
	status = XAxiDma_CfgInitialize(p_dma_inst, cfg_ptr);
	if (status != XST_SUCCESS)
	{
		xil_printf("ERROR! Initialization of AXI DMA failed with %d\r\n", status);
		return XST_FAILURE;
	}

	// Test for Scatter Gather
	if (XAxiDma_HasSg(p_dma_inst))
	{
		xil_printf("ERROR! Device configured as SG mode.\r\n");
		return XST_FAILURE;
	}

	// Disable all interrupts for both channels
	XAxiDma_IntrDisable(p_dma_inst, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
	XAxiDma_IntrDisable(p_dma_inst, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

	// Reset DMA
	XAxiDma_Reset(p_dma_inst);
	while (!XAxiDma_ResetIsDone(p_dma_inst));

	xil_printf("Note: MaxTransferLen=%d\r\n", p_dma_inst->TxBdRing.MaxTransferLen);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
 * This function initializes the entire Device used in demo.
 *
 * @param	p_dma_inst is a pointer to the device instance to be
 *		worked on.
 *
 *
 *	It will initialize the I/O ports and then call the init_dma function defined previously.
 *
 *	After GPIO in/out drivers are initialized, the function sets the input/output direction of
 *	all discrete signals for the specified GPIO channel. (bitmask for input is 1 and for output is 0)
 *	The status variable is used to check if all the driver are correctly initialized.
 *
 *	After that UART and DMA will be initialized and the Interrupt will be disabled for the UART driver.
 *
 * @return
 * 		- XST_SUCCESS for successful initialization
 * 		- XST_FAILURE otherwise
 *
 *****************************************************************************/
XStatus init(Demo *p_demo_inst) {
	XStatus status;

	status = XGpio_Initialize(&(p_demo_inst->gpio_out_inst), XPAR_GPIO_OUT_DEVICE_ID);
	if (status != XST_SUCCESS) return XST_FAILURE;
	XGpio_SetDataDirection(&(p_demo_inst->gpio_out_inst), 1, 0xFF); // RGB LED
	XGpio_SetDataDirection(&(p_demo_inst->gpio_out_inst), 2, 0x0000); // LED
	xil_printf("%08x %08x\r\n",
			XGpio_GetDataDirection(&(p_demo_inst->gpio_out_inst), 1),
			XGpio_GetDataDirection(&(p_demo_inst->gpio_out_inst), 2)
	);

	status = XGpio_Initialize(&(p_demo_inst->gpio_in_inst), XPAR_GPIO_IN_DEVICE_ID);
	if (status != XST_SUCCESS) return XST_FAILURE;
	XGpio_SetDataDirection(&(p_demo_inst->gpio_in_inst), 1, 0x00); // BUTTON
	XGpio_SetDataDirection(&(p_demo_inst->gpio_in_inst), 2, 0xFFFF); // SWITCH
	xil_printf("%08x %08x\r\n",
			XGpio_GetDataDirection(&(p_demo_inst->gpio_in_inst), 1),
			XGpio_GetDataDirection(&(p_demo_inst->gpio_in_inst), 2)
	);

	XUartLite_Initialize(&(p_demo_inst->uart_inst), XPAR_AXI_UARTLITE_0_DEVICE_ID);
	XUartLite_DisableInterrupt(&(p_demo_inst->uart_inst));

	status = init_dma(&(p_demo_inst->dma_inst), XPAR_AXI_DMA_0_DEVICE_ID);
	if (status != XST_SUCCESS) return XST_FAILURE;
	return XST_SUCCESS;
}

/*****************************************************************************/
/**
 * This function checks for any change in behavior of data found on GPIO
 *
 *
 * @param	p_dma_inst is a pointer to the device instance to be
 *		worked on.
 *
 *
 *	Checks for any press of button or switch change comparing the new data present on button with the
 *	data interpreted in the previous clock cycle.
 *
 *
 * @return
 * 		- GpioIn_Data variable that stores all the data found in the GPIO driver
 *
 *****************************************************************************/
GpioIn_Data get_gpio_data(Demo *p_demo_inst) {
	static GpioIn_Data last = {0,0,0,0,0,0};
	GpioIn_Data data;
	data.buttons = XGpio_DiscreteRead(&(p_demo_inst->gpio_in_inst), 1);
	data.switches = XGpio_DiscreteRead(&(p_demo_inst->gpio_in_inst), 2);
	data.button_pe = (data.buttons) & (~last.buttons);
	data.button_ne = (~data.buttons) & (last.buttons);
	data.switch_pe = (data.switches) & (~last.switches);
	data.switch_ne = (~data.switches) & (last.switches);
	last = data;
	return data;
}

/*****************************************************************************/
/**
 *Function used to send data found on device to DMA controller that will transfer it to the main memory.
 *
 * @param	p_dma_inst is a pointer to the device instance to be
 *		worked on.
 *
 * @param  buffer that will be received by the DMA. The buffer is interpreted as an UINTPTR ( an integer whose size is platform-specific )
 *
 * @param  length the size of the buffer
 *
 *
 *	Before sending data to DMA, it needs to be flushed first to avoid interpreting data that was not updated correctly.
 *	Data from the device is sent to DMA, through the XAXIDMA_DEVICE_TO_DMA channel.
 *
 * @return
 * 		- XST_SUCCESS for successful initialization
 * 		- XST_FAILURE otherwise
 *
 *****************************************************************************/
XStatus dma_receive(Demo *p_demo_inst, UINTPTR buffer, u32 length) {
	XStatus status;

	Xil_DCacheFlushRange(buffer, length);

	status = XAxiDma_SimpleTransfer(&(p_demo_inst->dma_inst), buffer, length, XAXIDMA_DEVICE_TO_DMA);
	if (status != XST_SUCCESS) {
		xil_printf("ERROR: failed to kick off S2MM transfer\r\n");
		return XST_FAILURE;
	}

	u32 busy = 0;
	do {
		busy = XAxiDma_Busy(&(p_demo_inst->dma_inst), XAXIDMA_DEVICE_TO_DMA);
	} while (busy);

	if ((XAxiDma_ReadReg(p_demo_inst->dma_inst.RegBase, XAXIDMA_RX_OFFSET+XAXIDMA_SR_OFFSET) & XAXIDMA_IRQ_ERROR_MASK) != 0) {
		xil_printf("ERROR: AXI DMA returned an error during the S2MM transfer\r\n");
		return XST_FAILURE;
	}

	Xil_DCacheFlushRange(buffer, length);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
 *	Function used to send data stored in memory through DMA back to the device.
 *
 * @param	p_dma_inst is a pointer to the device instance to be
 *		worked on.
 *
 * @param  buffer that will be received by the DMA. The buffer is interpreted as an UINTPTR ( an integer whose size is platform-specific )
 *
 * @param  length the size of the buffer
 *
 *
 *	Before sending data to DMA, it needs to be flushed first to avoid interpreting data that was not updated correctly.
 *	Data stored in memory will be sent to DMA, which will transfer it back to the I/O ports.
 *	For this operation the XAXIDMA_DMA_TO_DEVICE channel is used .
 *
 * @return
 * 		- XST_SUCCESS for successful initialization
 * 		- XST_FAILURE otherwise
 *
 *****************************************************************************/
XStatus dma_send(Demo *p_demo_inst, UINTPTR buffer, u32 length) {
	XStatus status;

	Xil_DCacheFlushRange(buffer, length);

	status = XAxiDma_SimpleTransfer(&(p_demo_inst->dma_inst), buffer, length, XAXIDMA_DMA_TO_DEVICE);

	if (status != XST_SUCCESS)
		xil_printf("ERROR: failed to kick off MM2S transfer\r\n");

	while (XAxiDma_Busy(&(p_demo_inst->dma_inst), XAXIDMA_DMA_TO_DEVICE));

	if ((XAxiDma_ReadReg(p_demo_inst->dma_inst.RegBase, XAXIDMA_TX_OFFSET+XAXIDMA_SR_OFFSET) & XAXIDMA_IRQ_ERROR_MASK) != 0) {
		xil_printf("ERROR: AXI DMA returned an error during the MM2S transfer\r\n");
		return XST_FAILURE;
	}

	Xil_DCacheFlushRange((UINTPTR)buffer, length);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
 *	Function used to reset the DMA controller
 *
 * @param	p_dma_inst is a pointer to the device instance to be
 *		worked on.
 *
 *	Resets the DMA controller
 *
 *****************************************************************************/
void dma_reset(Demo *p_demo_inst) {
	XAxiDma_Reset(&(p_demo_inst->dma_inst));
	while (!XAxiDma_ResetIsDone(&(p_demo_inst->dma_inst)));
}

/*****************************************************************************/
/**
 *	Function used to send data created using a user defined IP using the help of DMA to the device
 *
 * @param	p_dma_inst is a pointer to the device instance to be
 *		worked on.
 *
 *	Function is not yet implemented for this demo
 *
 *
 * @return
 * 		- XST_SUCCESS for successful initialization
 * 		- XST_FAILURE otherwise
 *
 *****************************************************************************/
void dma_forward(Demo *p_demo_inst) {
	// TODO: modify tone_generator.v to support 8 bit audio...
	static const u32 BUFFER_SIZE_WORDS = 256;
	static const u32 BUFFER_SIZE_BYTES = 1024; //BUFFER_SIZE_WORDS * sizeof(u32));
	xil_printf("entered dma_forward\r\n");
	u8 *buffer = malloc(BUFFER_SIZE_BYTES);
	memset(buffer, 0, BUFFER_SIZE_BYTES);
	xil_printf("  1.\r\n");

	for (int i=0; i<BUFFER_SIZE_WORDS; i++) {
		dma_receive(p_demo_inst, (UINTPTR)((u32)buffer + sizeof(u32) * i), 4);
	}

	xil_printf("  2.\r\n");

	if (VERBOSE) {
		xil_printf("data received:\r\n");
		for (u32 word = 0; word < BUFFER_SIZE_WORDS; word++) {
			xil_printf("    %08x\r\n", ((u32*)buffer)[word]);
		}
	}

	dma_send(p_demo_inst, (UINTPTR)buffer, BUFFER_SIZE_BYTES);

	xil_printf("  3.\r\n");

	free(buffer);
}

/*****************************************************************************/
/**
 *	Function used for tone(sin wave) generation using software and later played on audio port
 *
 * @param	p_dma_inst is a pointer to the device instance to be
 *		worked on.
 *
 *	Memory is allocated for the buffer.
 *	In order to create the sin wave the number of samples and also a specific period was considered.
 *	t_period = sampleFrequency / waveFrequency.
 *
 *	The data is stored in a buffer in a specific order to respect the little endianes.
 *	After the buffer is filled we send it to the DMA and wait for another press of button in order to exit this mode.
 *	Otherwise it will play the sound again and again until the mode is changed.
 *
 * 	1.a - all the variables or expressions used for sin wave creation
 *	1.b - all the variables or expressions used for square wave creation
 *
 * @return
 * 		- XST_SUCCESS for successful initialization
 * 		- XST_FAILURE otherwise
 *
 *****************************************************************************/
void dma_sw_tone_gen(Demo *p_demo_inst) {
	static const u32 buffer_size = 128;

	UINTPTR buffer = (UINTPTR)malloc(buffer_size * sizeof(u32));
	memset((u32*)buffer, 0, buffer_size * sizeof(u32));

	XStatus status;
	//accum = 0x00B22D0E;
	usleep(10000);

	u8 temp[5];				//use a temporary variable to store my words.
							//I noticed that the PWM will take the LSB first and
							//put it in duty signal. Having this in mind I must
	int i = 0;
	float A = 2.0;					//1.a //pack my data in two words and then send it to the buffer
	float f = 2000,time=0;			//1.a //the buffer is on 32 bits (enters in axis2fifo module).But
	double Y=0.0;					//1.a //the data that I send to the fifo generator is on 16 bits.
	//u8 val=0x00;						  //so I need to wait until the two buffers[i] and [i+1]
							     	 	  //are filled with data, that will be later read by AUDPWM
	float time_step;					  //from fifo on 31 bits. The fifo_read_data is represents the
										  //least significant 16 bits from the two buffers concatenated.

		for (u16 sample=0; sample < 256 && time < 48; sample++) //time constraint is used to represent time < 48
	{															//a correct period of the sin wave.
																//This value is given by the following relationship
																// sampleFrequency/waveFrequency = 96000/2000 = 48 //1.a

		time_step = time/96000;							//1.a
	 Y = (A * sin(2*3.14159*f*time_step) + A)*(127/A);  //1.a  SIN FORMULA

	 // *************************** 1.b settings for creation of a square signal **************************************
	 //	 if(sample % 16 == 0 )
	 //		{if(sample == 16 || sample == 48 || sample ==80 || sample == 112 || sample == 144 || sample == 176
	 //				|| sample == 208 || sample == 240 )
	 //			val = 0xFF;
	 //		else val = 0x00;
	 //		}
	 //				if(sample %4 == 0)
	 //				 ((u8*)temp)[0] = val;
	 //
	 //				if(sample % 4 == 1)
	 //			((u8*)temp)[1] = val;
	 //
	 //				if(sample % 4 == 2)
	 //				  ((u8*)temp)[2] = val;
	 //
	 //			if(sample % 4 == 3)
	 //				{((u8*)temp)[3] = val;
	 //
	 //			    ((u32*)buffer)[i] =  (((u8*)temp)[3]<<8) +  ((u8*)temp)[2];
	 //				((u32*)buffer)[i+1] =  (((u8*)temp)[1]<<8) +  ((u8*)temp)[0];
	 //
	 //					i+=2;}
	 //****************************************************************************************************************





	if (sample % 4 == 0)     			//we put each sample in a temporary array on the
		((u8*)temp)[0] = Y;				//index that corresponds to the duty_reg that will
	else if (sample % 4 == 1)			//interpret it
		((u8*)temp)[1] = Y;
	else if (sample % 4 == 2)
		((u8*)temp)[2] = Y;
	else if (sample % 4 == 3)
	 {
		((u8*)temp)[3] = Y;
										//after 4 samples are observed I can put them on the
		 	 	 	 	 	 	 	 	//two buffers. The buffer will send their data to the
		 	 	 	 	 	 	 	 	//axis2fifo and fifoGenerator, where the data will be
		 	 	 	 	 	 	 	 	//concatenated.
		 	 	 	 	 	 	 	 	//For example for the samples: 0x00 0x01 0x02 0x03
		 	 	 	 	 	 	 	 	//The buffer[0] will store: 0x00000302
		 	 	 	 	 	 	 	 	//The buffer[1] will store: 0x00000100
		 	 	 	 	 	 	 	 	//And the fifo_read_data will send it later to AUDPPWm as:
		 	 	 	 	 	 	 	 	// 03020100. Pwm will start with LSB first

		 ((u32*)buffer)[i] =  (((u8*)temp)[3]<<8) +  ((u8*)temp)[2];
		 ((u32*)buffer)[i+1] =  (((u8*)temp)[1]<<8) +  ((u8*)temp)[0];

		 i+=2;


	 }
	 time++; //1.a   needed only for Sin wave creation


	}

	while (1) {

	status = dma_send(p_demo_inst, buffer, i*4);

	GpioIn_Data gpio = get_gpio_data(p_demo_inst);
     if (gpio.button_pe != 0) break;
	}

	p_demo_inst->mode = DEMO_MODE_PAUSED;

	xil_printf("Exiting SW tone gen mode\r\n");

	memset((u32*)buffer, 0, buffer_size * sizeof(u32));

	status = dma_send(p_demo_inst, buffer, buffer_size*4);

	free((u32*)buffer);
	dma_reset(p_demo_inst);
}

/*****************************************************************************/
/**
 *	Function used to receive data from UART byte by byte
 *
 *
 *	@param p_demo_inst is a pointer to the device instance to be
 *		worked on.
 *
 *	@param buffer buffer that will receive data from the UART
 *
 *
 *	@param length
 *
 * @return
 * 		- number of received bytes
 *
 *****************************************************************************/
u32 uart_recv (Demo *p_demo_inst, u8 *buffer, unsigned int length) {
	u32 received_count = 0;
	unsigned int one_byte = 1;
	while (received_count < length) {
		received_count += XUartLite_Recv(&(p_demo_inst->uart_inst), buffer + received_count, one_byte);
	}
	return received_count;
}

/*****************************************************************************/
/**
 *	Function used to convert a u8 variable into a string
 *
 *	@param buffer the buffer that will be converted
 *
 *	@param str the string
 *
 *	@param length the size of buffer
 *
 * @return
 * 		-returns the string
 *
 *****************************************************************************/
u8 *buf2str(u8 buffer[], u8 str[], int length) {
	memcpy(str, buffer, length);
	str[length] = 0;
	return str;
}

/*****************************************************************************/
/**
 *	Function used to convert a u8 variable into a u32 by concatenating all the elements stored in the array buffer
 *
 *
 *	@param buffer the buffer that will be converted
 *
 * @return
 * 		-returns a u32 buffer
 *
 *****************************************************************************/
u32 buf2u32(u8 buffer[4]) {
	return ((u32)buffer[3] << 24) |((u32)buffer[2] << 16) | ((u32)buffer[1] << 8) | ((u32)buffer[0]);
}

/*****************************************************************************/
/**
 *	Function used to convert a u16 variable into a u32 by concatenating all the elements stored in the array buffer
 *
 *
 *	@param buffer the buffer that will be converted
 *
 * @return
 * 		-returns a u32 buffer
 *
 *****************************************************************************/
u16 buf2u16(u8 buffer[2]) {
	return ((u16)buffer[1] << 8) | ((u16)buffer[0]);
}

/*****************************************************************************/
/**
 *	Function used for playing the wav file stored in memory
 *
 * @param	p_dma_inst is a pointer to the device instance to be
 *		worked on.
 *
 * @param	file is a pointer to the Wav data stored in the main memory
 *
 *	Size of data is downscaled to support a 8bit audio depth
 *	After stroing the data in an dynamically allocated array it is send to DMA.
 *	And then later play on audio port thorugh DMA
 *
 *
 *****************************************************************************/
void play_wav(Demo *p_demo_inst, u8* file ) {

	u8 *ptr = file;
	ptr += 12;
	Wav_FormatRaw *p_format = (Wav_FormatRaw*)(ptr);
	ptr += 8 + buf2u32(p_format->fmt_chunk_size);
	Wav_DataRaw *p_data = (Wav_DataRaw*)(ptr);
	ptr += 8;
	u16 *wav_data = (u16*)ptr;

	xil_printf("preparing for playback\r\n");

	if(*file == 0)
	{
		xil_printf("Must receive a WAV file to be able to play\r\n");
		p_demo_inst->mode = DEMO_MODE_PAUSED;
		return;
	}
	// create dma buffer, downscale audio depth to 8bit.
	u32 dma_data_length = buf2u32(p_data->data_chunk_size) * sizeof(u8) / sizeof(u16);
	u8 *dma_data = (u8*)malloc(dma_data_length);
	for (int i=0; i<dma_data_length; i++) {
//		dma_data[i] = wav_data[i] >> 8;

		dma_data[i] = (u8)((u16)(wav_data[i] + 32768) >> 8);
	}
	dma_send(p_demo_inst, (UINTPTR)dma_data, dma_data_length);
	// wait for dma transfer to complete...

	xil_printf("sleep for a while\r\n");
	sleep(2);

	free(dma_data);
	dma_reset(p_demo_inst);
	p_demo_inst->mode = DEMO_MODE_PAUSED;
	xil_printf("Exiting play mode\r\n");

}

/*****************************************************************************/
/**
 *	Function used for playing the wav file stored in memory
 *
 * @param	p_dma_inst is a pointer to the device instance to be
 *		worked on.
 *
 * @param	file is a pointer to the Wav data stored in the main memory
 *
 *	UART FIFO's are reset in order to prevent FIFO overrun error and avoid having corrupted data.
 *	Data received on UART is interpreted using the data structures defined for the Wav File Header.
 *
 *	After sending data to DMA, the Wav file will be played.
 *
 *
 *****************************************************************************/
void recv_wav(Demo *p_demo_inst,u8* file) {

	u8  str[5];


	if(file == NULL)
	{
		xil_printf("Memory fault");
		p_demo_inst->mode = DEMO_MODE_PAUSED;
		return;
	}
	XUartLite_ResetFifos(&(p_demo_inst->uart_inst)); //flushing the FIFO before each UART transmission is important,
													//in order to avoid unwanted data on buffer

	xil_printf("Demo waiting for a WAV file...\r\n");

	uart_recv(p_demo_inst, file, sizeof(Wav_HeaderRaw));

	xil_printf("header received\r\n");

	u8 *ptr = file;
	Wav_HeaderRaw *p_header = (Wav_HeaderRaw*)(ptr);



	unsigned int var ;
	var = buf2u32(p_header->overall_size)-4;  // minus 4 because overall_size = File_size-8 (header_riff + header_overall_size
	uart_recv(p_demo_inst, file+sizeof(Wav_HeaderRaw), var); //these two are not included. Minus 4 because you don't want ot include
															 //header_wave, when you read the remaining bytes.
	ptr += 12;
	Wav_FormatRaw *p_format = (Wav_FormatRaw*)(ptr);
	ptr += 8 + buf2u32(p_format->fmt_chunk_size);
	Wav_DataRaw *p_data = (Wav_DataRaw*)(ptr);


	xil_printf("file info: \r\n");
	xil_printf("  header:\r\n");
	xil_printf("    riff: '%s'\r\n", buf2str(p_header->riff, str, 4));
	xil_printf("    overall_size: %d\r\n", buf2u32(p_header->overall_size));
	xil_printf("    wave: '%s'\r\n", buf2str(p_header->wave, str, 4));
	xil_printf("  format:\r\n");
	xil_printf("    fmt_chunk_marker: '%s'\r\n", buf2str(p_format->fmt_chunk_marker, str, 4));
	xil_printf("    fmt_chunk_size: %d\r\n", buf2u32(p_format->fmt_chunk_size));
	xil_printf("    format_type: %d\r\n", buf2u16(p_format->format_type));
	xil_printf("    channels: %d\r\n", buf2u16(p_format->channels));
	xil_printf("    sample_rate: %d\r\n", buf2u32(p_format->sample_rate));
	xil_printf("    byte_rate: %d\r\n", buf2u32(p_format->byte_rate));
	xil_printf("    block_align: %d\r\n", buf2u16(p_format->block_align));
	xil_printf("    bits_per_sample: %d\r\n", buf2u16(p_format->bits_per_sample));
	xil_printf("  data:\r\n");
	xil_printf("    fmt_chunk_marker: '%s'\r\n", buf2str(p_data->data_chunk_header, str, 4));
	xil_printf("    data_chunk_size: %d\r\n", buf2u32(p_data->data_chunk_size));


	play_wav(p_demo_inst,file);



	xil_printf("Exiting receive mode\r\n");


}

int main() {
	Demo device;
	XStatus status;
	file = malloc(max_file_size); // this will take most of the heap haha
	memset(file,0,max_file_size);

#ifdef __MICROBLAZE__
#ifdef XPAR_MICROBLAZE_0_USE_ICACHE
	Xil_ICacheEnable();
#endif
#ifdef XPAR_MICROBLAZE_0_USE_DCACHE
	Xil_DCacheEnable();
#endif
#endif

	xil_printf("----------------------------------------\r\n");
	xil_printf("entering main\r\n");
	status = init(&device);
	if (status != XST_SUCCESS) {
		xil_printf("ERROR: Demo not initialized correctly\r\n");
		sleep(1);
	} else
		xil_printf("Demo started\r\n");

	device.mode = DEMO_MODE_SW_TONE_GEN;


	while (1) {

		GpioIn_Data data = get_gpio_data(&device);

		switch (data.button_pe) {
		case 0x01: device.mode = DEMO_MODE_PAUSED;        xil_printf("\nDemo paused\r\n");                       break; // BUTTON C
		//case 0x02: device.mode = DEMO_MODE_HW_TONE_GEN;   xil_printf("Demo generating 261 Hz tone in HW\r\n"); break; // BUTTON U
		case 0x04: device.mode = DEMO_MODE_RECV_WAV_FILE; xil_printf("\nDemo prepared to receive wav file\r\n"); sleep(1);  break; // BUTTON L
		case 0x08: device.mode = DEMO_MODE_PLAY_WAV_FILE; xil_printf("\nDemo playing back wav file\r\n");        break; // BUTTON R
		case 0x10: device.mode = DEMO_MODE_SW_TONE_GEN;   xil_printf("\nDemo generating 261 Hz tone in SW\r\n"); break; // BUTTON D
		}
		switch (device.mode) {
		case DEMO_MODE_PAUSED:         break;
		case DEMO_MODE_HW_TONE_GEN:   break;//dma_forward(&device); break; // not implemented
		case DEMO_MODE_RECV_WAV_FILE:  recv_wav(&device,file);  break;
		case DEMO_MODE_PLAY_WAV_FILE: play_wav(&device,file); break;
		case DEMO_MODE_SW_TONE_GEN:   dma_sw_tone_gen(&device); break;
		}
	}

	xil_printf("exiting main\r\n");
	return 0;
}
