#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/cm3/nvic.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "font8x8.h"


#define VGA_GPIO GPIOB
#define GPIO_VSYNC GPIO0
#define GPIO_HSYNC GPIO1
#define CH_WIDTH 50
#define CH_HEIGHT 38



static uint64_t frame = 0;
static uint8_t hsync = 0;
static uint8_t vsync = 0;
static uint8_t char_buff[CH_WIDTH*CH_HEIGHT];
static uint8_t pixel_buffer[2*CH_WIDTH];

const static volatile uint32_t pixels_buffs[2] = {(uint32_t)pixel_buffer, (uint32_t)pixel_buffer + CH_WIDTH};


void tim2_3_init() {

	rcc_periph_clock_enable(RCC_TIM2);
    nvic_set_priority(NVIC_TIM2_IRQ, 1 << 4);
	nvic_enable_irq(NVIC_TIM2_IRQ);
	rcc_periph_reset_pulse(RST_TIM2);
	timer_set_mode(TIM2, TIM_CR1_CKD_CK_INT,
			TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);

	timer_set_prescaler(TIM2, 1);

	timer_disable_preload(TIM2);
	timer_continuous_mode(TIM2);
	timer_set_period(TIM2, 1023);
	timer_set_oc_value(TIM2, TIM_OC1, 800+24);
	timer_enable_irq(TIM2, TIM_DIER_UIE | TIM_DIER_CC1IE);


	rcc_periph_clock_enable(RCC_TIM3);
    nvic_set_priority(NVIC_TIM3_IRQ, 2 << 4);
	nvic_enable_irq(NVIC_TIM3_IRQ);
	rcc_periph_reset_pulse(RST_TIM3);
	timer_set_mode(TIM3, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);

	timer_set_prescaler(TIM3, 0);
	timer_disable_preload(TIM3);
	timer_continuous_mode(TIM3);
	timer_set_period(TIM3, 624);
	timer_set_oc_value(TIM3, TIM_OC1, 600+1);
	timer_enable_irq(TIM3, TIM_DIER_UIE | TIM_DIER_CC1IE);


	timer_set_master_mode(TIM2, TIM_CR2_MMS_UPDATE);
	timer_slave_set_mode(TIM3, TIM_SMCR_SMS_ECM1);
	timer_slave_set_trigger(TIM3, TIM_SMCR_TS_ITR1);

	timer_enable_counter(TIM3);
	timer_enable_counter(TIM2);


}

void dma1_init() {
    rcc_periph_clock_enable(RCC_DMA1);
    dma_channel_reset(DMA1, DMA_CHANNEL3);
    dma_set_peripheral_address(DMA1, DMA_CHANNEL3, (uint32_t)&SPI1_DR);
    dma_set_read_from_memory(DMA1, DMA_CHANNEL3);
    dma_enable_memory_increment_mode(DMA1, DMA_CHANNEL3);
    dma_disable_peripheral_increment_mode(DMA1, DMA_CHANNEL3);
    dma_set_peripheral_size(DMA1, DMA_CHANNEL3, DMA_CCR_PSIZE_8BIT);
    dma_set_memory_size(DMA1, DMA_CHANNEL3, DMA_CCR_MSIZE_8BIT);
    dma_set_priority(DMA1, DMA_CHANNEL3, DMA_CCR_PL_LOW);
    dma_enable_transfer_complete_interrupt(DMA1, DMA_CHANNEL3);
    nvic_set_priority(NVIC_DMA1_CHANNEL3_IRQ, 3 << 4);
    nvic_enable_irq(NVIC_DMA1_CHANNEL3_IRQ);
}

void spi1_init() {
    rcc_periph_clock_enable(RCC_SPI1);

    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO7);

    spi_reset(SPI1);
    spi_init_master(
        SPI1,
        SPI_CR1_BAUDRATE_FPCLK_DIV_4,
        SPI_CR1_CPOL_CLK_TO_0_WHEN_IDLE,
        SPI_CR1_CPHA_CLK_TRANSITION_1,
		SPI_CR1_DFF_8BIT,
        SPI_CR1_MSBFIRST
    );

    spi_enable_software_slave_management(SPI1);
    spi_set_nss_high(SPI1);
    spi_set_unidirectional_mode(SPI1);

	SPI1_CR2 |= SPI_CR2_TXDMAEN;
    spi_enable(SPI1);
}



static volatile  bool buff_num = true;
#define offsetT 10
void tim2_isr() {
	if (TIM_SR(TIM2) & TIM_SR_CC1IF) {
		if(hsync){
			gpio_set(VGA_GPIO, GPIO_HSYNC);
			timer_set_oc_value(TIM2, TIM_OC1, 800+24+offsetT);
		}
		else {
			gpio_clear(VGA_GPIO, GPIO_HSYNC);
			timer_set_oc_value(TIM2, TIM_OC1, 800+24+72+offsetT);

		}
		hsync = !hsync;
		timer_clear_flag(TIM2, TIM_SR_CC1IF);

	}
	else { //if (TIM_SR(TIM2) & TIM_SR_UIF) { //  for speed reasons

		DMA1_CNDTR3 = CH_WIDTH;
		DMA1_CMAR3 = pixels_buffs[buff_num];
		DMA1_CCR3 |= DMA_CCR_EN;


		const uint16_t line = ((TIM3_CNT + 1) % 600) >> 1;
		const uint16_t line_in_ch = line & 0b111;

		const unsigned char* f = font8x8 + (line_in_ch * FONT8X8_CHARS);


		buff_num = 1-buff_num;

		register unsigned char* ch = char_buff + ((line >> 3) * CH_WIDTH);
		register uint8_t* buff = (uint8_t*)pixels_buffs[buff_num];

		register uint8_t i = 50;
		while(i--) *(buff++) = f[*(ch++)];

		timer_clear_flag(TIM2, TIM_SR_UIF);



	}
}

static volatile uint8_t new_frame_flag = 0;

void tim3_isr() {
	if (TIM_SR(TIM3) & TIM_SR_CC1IF) {
			if(vsync){
				gpio_set(VGA_GPIO, GPIO_VSYNC);
				timer_set_oc_value(TIM3, TIM_OC1, 600+1);
			}
			else {
				gpio_clear(VGA_GPIO, GPIO_VSYNC);
				timer_set_oc_value(TIM3, TIM_OC1, 600+1+2);
			}
			vsync = !vsync;
			timer_clear_flag(TIM3, TIM_SR_CC1IF);
		}
		else { //if (TIM_SR(TIM3) & TIM_SR_UIF) { //  for speed reasons
			frame++;
			new_frame_flag = 1;
			memset(pixel_buffer, 0, sizeof(pixel_buffer));
			timer_clear_flag(TIM3, TIM_SR_UIF);
		}
}


void dma1_channel3_isr() {
	DMA_IFCR(DMA1) = (DMA_TCIF << DMA_FLAG_OFFSET(DMA_CHANNEL3));
	DMA1_CCR3 &= ~DMA_CCR_EN;
}


int main() {

	rcc_clock_setup_in_hse_8mhz_out_72mhz();
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);

	gpio_set_mode(VGA_GPIO, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO_VSYNC | GPIO_HSYNC);

	spi1_init();
	dma1_init();

	tim2_3_init();



	memset(char_buff, ' ', sizeof(char_buff));
	while(1){

		if(new_frame_flag){
			new_frame_flag = 0;

			sprintf(char_buff+50, "Frame: %d", frame);
			sprintf(char_buff+100, "Time: %d sec", frame/56);
			sprintf(char_buff+150, "Resolution: 800x600");
			sprintf(char_buff+200, "Resolution characters: 50x37");
		}
	}
}
